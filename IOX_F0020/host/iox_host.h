/*
 * iox_host.h - MM32F0020 UART I/O expander: host-side helpers.
 *
 * Header-only, C99 (also valid C++), no malloc, no I/O. You own the UART:
 *   - build a request with iox_req_*()  -> write the returned bytes to the UART
 *   - push every received byte into iox_rx_feed() -> it hands you whole frames
 *   - decode payloads with iox_dec_*()
 * Link policy (seq, retries, timeouts) is described in IOX_HOST_GUIDE.md.
 *
 * All iox_req_*() write into 'wire', which must hold IOX_WIRE_MAX (74) bytes,
 * and return the number of bytes to send (0 = invalid argument).
 */
#ifndef IOX_HOST_H
#define IOX_HOST_H

#include <stdint.h>
#include <string.h>
#include "iox_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Little-endian field helpers                                               */
/* ------------------------------------------------------------------------ */
static inline void iox_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void iox_put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint16_t iox_get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t iox_get32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint8_t iox_sum8(const uint8_t* p, uint16_t n)
{
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *p++);
    return s;
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                     */
/* ------------------------------------------------------------------------ */
typedef struct {
    uint8_t type;                       /* IOX_FT_RESPONSE or IOX_FT_EVENT */
    uint8_t seq;
    uint8_t id;                         /* command id (response) or event id */
    uint8_t len;                        /* payload bytes; responses: payload[0] = status */
    uint8_t payload[IOX_MAX_PAYLOAD];
} iox_frame_t;

/* Encode one host->MM32 command. Returns wire length, 0 if len > 64. */
static inline uint8_t iox_encode(uint8_t* wire, uint8_t seq, uint8_t id, const uint8_t* payload, uint8_t len)
{
    uint8_t n = (uint8_t)(IOX_INNER_HDR_LEN + len);
    if (len > IOX_MAX_PAYLOAD) return 0;
    wire[0] = IOX_KU_H0;
    wire[1] = IOX_KU_H1;
    wire[2] = IOX_KU_VER_HOST;
    wire[3] = IOX_KU_CMD;
    wire[4] = 0;
    wire[5] = n;
    wire[6] = IOX_FT_COMMAND;
    wire[7] = seq;
    wire[8] = id;
    if (len) memcpy(&wire[IOX_OFS_PAYLOAD], payload, len);
    wire[IOX_KU_HDR_LEN + n] = iox_sum8(wire, (uint16_t)(IOX_KU_HDR_LEN + n));
    return (uint8_t)(IOX_KU_HDR_LEN + n + 1);
}

/* True if f is the response to the command you sent with (seq, id). */
static inline int iox_is_response_to(const iox_frame_t* f, uint8_t seq, uint8_t id)
{
    return f->type == IOX_FT_RESPONSE && f->seq == seq && f->id == id && f->len >= 1;
}

/* ------------------------------------------------------------------------ */
/* Receiver: byte-at-a-time KU parser for MM32 -> host frames.              */
/* Same rules as the MM32's own parser: on any mismatch it drops one byte    */
/* and rescans, so a 0x55 inside a payload never breaks framing. Valid KU    */
/* frames for other KU commands are skipped whole (see other_ku below).      */
/* If you already run a KU parser, skip this and call iox_decode_ku() on     */
/* each complete frame whose command byte is 0x40.                           */
/* ------------------------------------------------------------------------ */
typedef struct {
    uint8_t  win[IOX_WIRE_MAX];
    uint8_t  n;
    uint16_t bad;                       /* bytes discarded while resyncing (diagnostics) */
    uint16_t other_ku;                  /* valid KU frames with a command other than 0x40 */
} iox_rx_t;

static inline void iox_rx_init(iox_rx_t* rx) { memset(rx, 0, sizeof(*rx)); }

/* Drop a half-received frame, e.g. after 20 ms of silence mid-frame. */
static inline void iox_rx_reset(iox_rx_t* rx) { rx->n = 0; }

static inline void iox_rx_drop_(iox_rx_t* rx, uint8_t k)
{
    rx->n = (uint8_t)(rx->n - k);
    memmove(rx->win, rx->win + k, rx->n);
}

/* Decode an already-delimited KU frame (whole wire bytes). 1 = IOX frame in f. */
static inline int iox_decode_ku(const uint8_t* w, uint16_t wlen, iox_frame_t* f)
{
    uint8_t len;
    if (wlen < IOX_KU_HDR_LEN + IOX_KU_LEN_MIN + 1) return 0;
    len = w[IOX_OFS_LENL];
    if (w[0] != IOX_KU_H0 || w[1] != IOX_KU_H1 || w[IOX_OFS_VER] != IOX_KU_VER_MCU
        || w[IOX_OFS_KUCMD] != IOX_KU_CMD || w[IOX_OFS_LENH] != 0
        || len < IOX_KU_LEN_MIN || len > IOX_KU_LEN_MAX
        || wlen != (uint16_t)(IOX_KU_HDR_LEN + len + 1)
        || iox_sum8(w, (uint16_t)(wlen - 1)) != w[wlen - 1]) return 0;
    f->type = w[IOX_OFS_TYPE];
    f->seq = w[IOX_OFS_SEQ];
    f->id = w[IOX_OFS_ID];
    f->len = (uint8_t)(len - IOX_INNER_HDR_LEN);
    memcpy(f->payload, &w[IOX_OFS_PAYLOAD], f->len);
    return 1;
}

/* Feed one received byte. Returns 1 when a complete IOX frame is in *f.
 * Keep calling for every byte; leftover bytes stay buffered. */
static inline int iox_rx_feed(iox_rx_t* rx, uint8_t c, iox_frame_t* f)
{
    rx->win[rx->n++] = c;
    while (rx->n) {
        uint8_t len, total;
        if (rx->win[0] != IOX_KU_H0) { iox_rx_drop_(rx, 1); rx->bad++; continue; }
        if (rx->n < 2) return 0;
        if (rx->win[1] != IOX_KU_H1) { iox_rx_drop_(rx, 1); rx->bad++; continue; }
        if (rx->n < IOX_KU_HDR_LEN) return 0;
        len = rx->win[IOX_OFS_LENL];
        if (rx->win[IOX_OFS_VER] != IOX_KU_VER_MCU || rx->win[IOX_OFS_LENH] != 0
            || len < IOX_KU_LEN_MIN || len > IOX_KU_LEN_MAX) { iox_rx_drop_(rx, 1); rx->bad++; continue; }
        total = (uint8_t)(IOX_KU_HDR_LEN + len + 1);
        if (rx->n < total) return 0;
        if (iox_sum8(rx->win, (uint16_t)(total - 1)) != rx->win[total - 1]) { iox_rx_drop_(rx, 1); rx->bad++; continue; }
        if (rx->win[IOX_OFS_KUCMD] != IOX_KU_CMD) { iox_rx_drop_(rx, total); rx->other_ku++; continue; }
        iox_decode_ku(rx->win, total, f);
        iox_rx_drop_(rx, total);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Request builders (host -> MM32). seq: your counter, +1 per NEW command;   */
/* reuse the same seq only when retrying the same command.                   */
/* ------------------------------------------------------------------------ */
static inline uint8_t iox_req_ping(uint8_t* w, uint8_t seq, const uint8_t* data, uint8_t n)
{
    return n > IOX_PING_MAX_DATA ? 0 : iox_encode(w, seq, IOX_CMD_PING, data, n);
}
static inline uint8_t iox_req_get_info(uint8_t* w, uint8_t seq)   { return iox_encode(w, seq, IOX_CMD_GET_INFO, 0, 0); }
static inline uint8_t iox_req_get_status(uint8_t* w, uint8_t seq) { return iox_encode(w, seq, IOX_CMD_GET_STATUS, 0, 0); }
static inline uint8_t iox_req_heartbeat(uint8_t* w, uint8_t seq)  { return iox_encode(w, seq, IOX_CMD_HEARTBEAT, 0, 0); }
static inline uint8_t iox_req_read_all(uint8_t* w, uint8_t seq)   { return iox_encode(w, seq, IOX_CMD_READ_ALL, 0, 0); }

static inline uint8_t iox_req_reset(uint8_t* w, uint8_t seq)
{
    uint8_t p[4];
    iox_put32(p, IOX_MAGIC_RESET);
    return iox_encode(w, seq, IOX_CMD_RESET, p, 4);
}
static inline uint8_t iox_req_set_hb_content(uint8_t* w, uint8_t seq, uint8_t mask)
{
    return iox_encode(w, seq, IOX_CMD_SET_HB_CONTENT, &mask, 1);
}
static inline uint8_t iox_req_set_mode(uint8_t* w, uint8_t seq, uint8_t ch, uint8_t type, uint8_t param)
{
    uint8_t p[3] = { ch, type, param };
    return iox_encode(w, seq, IOX_CMD_SET_MODE, p, 3);
}
static inline uint8_t iox_req_set_mode_mask(uint8_t* w, uint8_t seq, uint16_t mask, uint8_t type, uint8_t param)
{
    uint8_t p[4];
    iox_put16(p, mask); p[2] = type; p[3] = param;
    return iox_encode(w, seq, IOX_CMD_SET_MODE_MASK, p, 4);
}
static inline uint8_t iox_req_get_config(uint8_t* w, uint8_t seq, uint8_t ch)
{
    return iox_encode(w, seq, IOX_CMD_GET_CONFIG, &ch, 1);
}
static inline uint8_t iox_req_set_dig_event(uint8_t* w, uint8_t seq, uint8_t ch, uint8_t edge, uint8_t debounce_ms)
{
    uint8_t p[3] = { ch, edge, debounce_ms };
    return iox_encode(w, seq, IOX_CMD_SET_DIG_EVENT, p, 3);
}
static inline uint8_t iox_req_write(uint8_t* w, uint8_t seq, uint8_t ch, uint8_t level)
{
    uint8_t p[2] = { ch, (uint8_t)(level ? 1 : 0) };
    return iox_encode(w, seq, IOX_CMD_WRITE, p, 2);
}
/* Bits set in mask are written; each gets its bit from values. Unmasked outputs are untouched. */
static inline uint8_t iox_req_write_mask(uint8_t* w, uint8_t seq, uint16_t mask, uint16_t values)
{
    uint8_t p[4];
    iox_put16(p, mask); iox_put16(p + 2, values);
    return iox_encode(w, seq, IOX_CMD_WRITE_MASK, p, 4);
}
static inline uint8_t iox_req_read_adc(uint8_t* w, uint8_t seq, uint8_t ch, uint8_t avg)
{
    uint8_t p[2] = { ch, avg };
    return iox_encode(w, seq, IOX_CMD_READ_ADC, p, 2);
}
static inline uint8_t iox_req_read_adc_all(uint8_t* w, uint8_t seq, uint8_t avg)
{
    return iox_encode(w, seq, IOX_CMD_READ_ADC_ALL, &avg, 1);
}
/* timeout_ms 0 = off. On timeout, OUTPUT channels in force_mask go to their bit in force_levels. */
static inline uint8_t iox_req_set_link_wdt(uint8_t* w, uint8_t seq, uint16_t timeout_ms, uint16_t force_mask, uint16_t force_levels)
{
    uint8_t p[6];
    iox_put16(p, timeout_ms); iox_put16(p + 2, force_mask); iox_put16(p + 4, force_levels);
    return iox_encode(w, seq, IOX_CMD_SET_LINK_WDT, p, 6);
}

/* ------------------------------------------------------------------------ */
/* Response decoders. Pass the response frame. Each returns the status byte */
/* (IOX_ST_*), or -1 if the payload is malformed. Output is only filled on   */
/* IOX_ST_OK.                                                                */
/* ------------------------------------------------------------------------ */
static inline int iox_rsp_status(const iox_frame_t* f) { return f->len >= 1 ? f->payload[0] : -1; }

typedef struct {
    uint8_t  fw_major, fw_minor, proto_major, proto_minor;
    uint8_t  uid[12];                   /* MM32 96-bit unique id */
    uint8_t  ch_count;                  /* 13 */
    uint16_t adc_mask;                  /* 0x003F */
    uint8_t  framing;                   /* IOX_FRAMING_KU */
} iox_info_t;

static inline int iox_dec_info(const iox_frame_t* f, iox_info_t* o)
{
    const uint8_t* p = f->payload + 1;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len != IOX_RSP_LEN_GET_INFO) return -1;
    o->fw_major = p[0]; o->fw_minor = p[1]; o->proto_major = p[2]; o->proto_minor = p[3];
    memcpy(o->uid, p + 4, 12);
    o->ch_count = p[16];
    o->adc_mask = iox_get16(p + 17);
    o->framing = p[19];
    return IOX_ST_OK;
}

typedef struct {
    uint32_t uptime_ms;
    uint16_t vdd_mv;                    /* from internal 1.2 V ref, +-few %; 3300 until first measurement */
    uint8_t  reset_cause;               /* IOX_RST_* */
    uint8_t  flags;                     /* IOX_FLAG_*, NOT cleared by GET_STATUS */
    uint16_t uart_errors;               /* running count */
    uint16_t event_drops;               /* running count */
} iox_status_t;

static inline int iox_dec_status(const iox_frame_t* f, iox_status_t* o)
{
    const uint8_t* p = f->payload + 1;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len != IOX_RSP_LEN_GET_STATUS) return -1;
    o->uptime_ms = iox_get32(p);
    o->vdd_mv = iox_get16(p + 4);
    o->reset_cause = p[6];
    o->flags = p[7];
    o->uart_errors = iox_get16(p + 8);
    o->event_drops = iox_get16(p + 10);
    return IOX_ST_OK;
}

typedef struct {
    uint8_t ch, type, param, edge, debounce_ms;
} iox_config_t;

static inline int iox_dec_config(const iox_frame_t* f, iox_config_t* o)
{
    const uint8_t* p = f->payload + 1;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len != IOX_RSP_LEN_GET_CONFIG) return -1;
    o->ch = p[0]; o->type = p[1]; o->param = p[2]; o->edge = p[3]; o->debounce_ms = p[4];
    return IOX_ST_OK;
}

typedef struct {
    uint16_t levels;                    /* debounced inputs | commanded output levels */
    uint16_t raw;                       /* instantaneous pin read, every channel */
    uint16_t out_mask;                  /* channels configured as OUTPUT */
} iox_read_all_t;

static inline int iox_dec_read_all(const iox_frame_t* f, iox_read_all_t* o)
{
    const uint8_t* p = f->payload + 1;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len != IOX_RSP_LEN_READ_ALL) return -1;
    o->levels = iox_get16(p);
    o->raw = iox_get16(p + 2);
    o->out_mask = iox_get16(p + 4);
    return IOX_ST_OK;
}

typedef struct {
    uint8_t  ch;
    uint16_t raw;                       /* 0..4095 */
    uint16_t mv;                        /* raw scaled by measured VDD, approximate */
} iox_adc_t;

static inline int iox_dec_read_adc(const iox_frame_t* f, iox_adc_t* o)
{
    const uint8_t* p = f->payload + 1;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len != IOX_RSP_LEN_READ_ADC) return -1;
    o->ch = p[0]; o->raw = iox_get16(p + 1); o->mv = iox_get16(p + 3);
    return IOX_ST_OK;
}

/* out must hold 6 entries. *count = entries written (one per ADC-type channel, ascending). */
static inline int iox_dec_read_adc_all(const iox_frame_t* f, iox_adc_t out[6], uint8_t* count)
{
    const uint8_t* p = f->payload + 1;
    uint8_t i, n;
    if (f->len < 1) return -1;
    if (p[-1] != IOX_ST_OK) return p[-1];
    if (f->len < 2) return -1;
    n = p[0];
    if (n > 6 || f->len != (uint8_t)(2 + n * IOX_RSP_LEN_ADC_ENTRY)) return -1;
    for (i = 0; i < n; i++) {
        const uint8_t* e = p + 1 + i * IOX_RSP_LEN_ADC_ENTRY;
        out[i].ch = e[0]; out[i].raw = iox_get16(e + 1); out[i].mv = iox_get16(e + 3);
    }
    *count = n;
    return IOX_ST_OK;
}

/* Heartbeat: fields are valid only if their bit is set in 'mask'. */
typedef struct {
    uint8_t  mask;                      /* IOX_HB_* echoed by the MM32 */
    uint8_t  flags;                     /* IOX_HB_FLAGS */
    uint32_t uptime_ms;                 /* IOX_HB_UPTIME */
    uint16_t levels;                    /* IOX_HB_LEVELS */
    uint16_t changed;                   /* IOX_HB_CHANGED */
    uint16_t out_mask;                  /* IOX_HB_OUT_MASK */
    uint8_t  adc_mask;                  /* IOX_HB_ADC: which CH0..5 are ADC */
    uint16_t adc_raw[6];                /* indexed by channel; only bits in adc_mask valid */
} iox_heartbeat_t;

static inline int iox_dec_heartbeat(const iox_frame_t* f, iox_heartbeat_t* o)
{
    const uint8_t* p = f->payload;
    uint8_t k = 2, ch;
    if (f->len < 1) return -1;
    if (p[0] != IOX_ST_OK) return p[0];
    if (f->len < 2) return -1;
    memset(o, 0, sizeof(*o));
    o->mask = p[1];
#define IOX_NEED_(n) do { if ((uint8_t)(k + (n)) > f->len) return -1; } while (0)
    if (o->mask & IOX_HB_FLAGS)    { IOX_NEED_(1); o->flags = p[k]; k += 1; }
    if (o->mask & IOX_HB_UPTIME)   { IOX_NEED_(4); o->uptime_ms = iox_get32(p + k); k += 4; }
    if (o->mask & IOX_HB_LEVELS)   { IOX_NEED_(2); o->levels = iox_get16(p + k); k += 2; }
    if (o->mask & IOX_HB_CHANGED)  { IOX_NEED_(2); o->changed = iox_get16(p + k); k += 2; }
    if (o->mask & IOX_HB_OUT_MASK) { IOX_NEED_(2); o->out_mask = iox_get16(p + k); k += 2; }
    if (o->mask & IOX_HB_ADC) {
        IOX_NEED_(1); o->adc_mask = p[k]; k += 1;
        for (ch = 0; ch < 6; ch++)
            if (o->adc_mask & (1u << ch)) { IOX_NEED_(2); o->adc_raw[ch] = iox_get16(p + k); k += 2; }
    }
#undef IOX_NEED_
    return k == f->len ? IOX_ST_OK : -1;
}

/* ------------------------------------------------------------------------ */
/* Event decoders. Pass an IOX_FT_EVENT frame. Return 0 = ok, -1 = wrong id  */
/* or length.                                                                */
/* ------------------------------------------------------------------------ */
typedef struct {
    uint8_t reset_cause;                /* IOX_RST_* */
    uint8_t fw_major, fw_minor, proto_major, proto_minor, framing;
} iox_booted_t;

static inline int iox_dec_booted(const iox_frame_t* f, iox_booted_t* o)
{
    if (f->id != IOX_EV_BOOTED || f->len != IOX_EV_LEN_BOOTED) return -1;
    o->reset_cause = f->payload[0];
    o->fw_major = f->payload[1]; o->fw_minor = f->payload[2];
    o->proto_major = f->payload[3]; o->proto_minor = f->payload[4];
    o->framing = f->payload[5];
    return 0;
}

typedef struct {
    uint16_t changed;                   /* channels whose edge matched, all from one 1 ms tick */
    uint16_t levels;                    /* debounced inputs | output levels, after the change */
    uint32_t t_ms;                      /* MM32 uptime at the change */
} iox_pin_change_t;

static inline int iox_dec_pin_change(const iox_frame_t* f, iox_pin_change_t* o)
{
    if (f->id != IOX_EV_PIN_CHANGE || f->len != IOX_EV_LEN_PIN_CHANGE) return -1;
    o->changed = iox_get16(f->payload);
    o->levels = iox_get16(f->payload + 2);
    o->t_ms = iox_get32(f->payload + 4);
    return 0;
}

typedef struct {
    uint8_t  code;                      /* IOX_FAULT_* */
    uint16_t detail;
} iox_fault_t;

static inline int iox_dec_fault(const iox_frame_t* f, iox_fault_t* o)
{
    if (f->id != IOX_EV_FAULT || f->len != IOX_EV_LEN_FAULT) return -1;
    o->code = f->payload[0];
    o->detail = iox_get16(f->payload + 1);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* IOX_HOST_H */
