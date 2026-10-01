////////////////////////////////////////////////////////////////////////////////
/// @file    iox_proto.c
/// @brief   Protocol layer (spec 5.5 - 9): stop-and-wait command handling,
///          duplicate-seq response cache, events, heartbeat.
/// @note    TX is blocking and whole-frame, so frames never interleave and a
///          response always goes out before the next queued event.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_PROTO_C_

#include <string.h>
#include "hal_conf.h"
#include "iox_config.h"
#include "iox_sys.h"
#include "uart.h"
#include "iox_env.h"
#include "iox_pins.h"
#include "iox_scan.h"
#include "iox_adc.h"
#include "iox_evt.h"
#include "iox_lwdt.h"
#include "iox_proto.h"

#define POST_NONE   0
#define POST_RESET  1

static const env_ops_t* s_env = &env_ku_ops;

static iox_frame_t s_rx;
static iox_frame_t s_tx;
static u8  s_wire[IOX_WIRE_MAX];                                                // last response, encoded (dup cache)
static u8  s_wire_len;
static u8  s_cache_valid;
static u8  s_last_seq;
static u8  s_last_id;

static u8  s_evt_seq;
static u8  s_hb_mask = IOX_HB_DEFAULT_MASK;

static u16 s_uart_err_seen;
static u16 s_uart_err_reported;
static u32 s_uart_fault_ms;

////////////////////////////////////////////////////////////////////////////////
// little-endian helpers
////////////////////////////////////////////////////////////////////////////////
static u8 put16(u8* p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    return 2;
}

static u8 put32(u8* p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
    return 4;
}

static u16 get16(const u8* p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 get32(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u8 avg_valid(u8 avg)
{
    return avg == 0 || avg == 1 || avg == 4 || avg == 16;
}

static u8 adc_entry(u8* p, u8 ch, u8 avg)
{
    u16 raw = avg ? iox_adc_read_avg(ch, avg) : iox_adc_cached(ch);
    p[0] = ch;
    put16(p + 1, raw);
    put16(p + 3, iox_adc_to_mv(raw));
    return 5;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  HEARTBEAT response: [status][mask][blocks in bit order] (spec 9.2)
////////////////////////////////////////////////////////////////////////////////
static u8 build_heartbeat(u8* p)
{
    u8 m = s_hb_mask;
    u8 k = 0;

    p[k++] = ST_OK;
    p[k++] = m;
    if (m & 0x01) p[k++] = flags_take();
    if (m & 0x02) k += put32(p + k, g_ms);
    if (m & 0x04) k += put16(p + k, scan_levels());
    if (m & 0x08) k += put16(p + k, scan_take_changed());
    if (m & 0x10) k += put16(p + k, pins_type_mask(CH_OUTPUT));
    if (m & 0x20) {
        u16 am = pins_type_mask(CH_ADC);
        u8 ch;
        p[k++] = (u8)am;
        for (ch = 0; ch < 6; ch++)
            if (am & (1U << ch))
                k += put16(p + k, iox_adc_cached(ch));
    }
    return k;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  Execute one command.
/// @param  f: received command. p: response payload (p[0] = status).
/// @retval response payload length
////////////////////////////////////////////////////////////////////////////////
#define NEED_LEN(n)     do { if (rl != (n)) { p[0] = ST_BAD_LEN; return 1; } } while (0)
#define FAIL(st)        do { p[0] = (st); return 1; } while (0)

static u8 dispatch(const iox_frame_t* f, u8* p, u8* post, u32 now)
{
    const u8* r = f->payload;
    u8 rl = f->len;
    u8 k = 1;
    u8 ch, st;
    u16 mask;

    p[0] = ST_OK;

    switch (f->id) {
        case OP_PING:
            if (rl > IOX_MAX_PAYLOAD - 4) FAIL(ST_BAD_LEN);
            memcpy(p + 1, r, rl);
            return (u8)(1 + rl);

        case OP_GET_INFO:
            NEED_LEN(0);
            p[k++] = FW_MAJOR;
            p[k++] = FW_MINOR;
            p[k++] = PROTO_MAJOR;
            p[k++] = PROTO_MINOR;
            memcpy(p + k, (const u8*)UID_BASE, 12);
            k += 12;
            p[k++] = IOX_CH_COUNT;
            k += put16(p + k, IOX_ADC_CAPABLE);
            p[k++] = s_env->framing;
            return k;

        case OP_GET_STATUS:
            NEED_LEN(0);
            k += put32(p + k, g_ms);
            k += put16(p + k, iox_adc_vdd_mv());
            p[k++] = sys_reset_cause();
            p[k++] = flags_peek();
            k += put16(p + k, uart1_rx_errors());
            k += put16(p + k, evt_drops_total());
            return k;

        case OP_RESET:
            NEED_LEN(4);
            if (get32(r) != IOX_MAGIC_RESET) FAIL(ST_BAD_PARAM);
            *post = POST_RESET;
            return 1;

        case OP_SET_FRAMING:
            NEED_LEN(1);
            if (r[0] != s_env->framing) FAIL(ST_BAD_PARAM);                     // only KU is built
            return 1;

        case OP_HEARTBEAT:
            NEED_LEN(0);
            return build_heartbeat(p);

        case OP_SET_HB_CONTENT:
            NEED_LEN(1);
            if (r[0] & 0xC0) FAIL(ST_BAD_PARAM);                                // bits 6-7 reserved
            s_hb_mask = r[0];
            return 1;

        case OP_SET_MODE:
            NEED_LEN(3);
            st = pins_check_mode(r[0], r[1], r[2]);
            if (st) FAIL(st);
            pins_set_mode(r[0], r[1], r[2]);
            return 1;

        case OP_SET_MODE_MASK:
            NEED_LEN(4);
            mask = get16(r);
            if (mask & ~IOX_CH_ALL) FAIL(ST_BAD_CH);
            for (ch = 0; ch < IOX_CH_COUNT; ch++)                               // all-or-nothing
                if (mask & (1U << ch)) {
                    st = pins_check_mode(ch, r[2], r[3]);
                    if (st) FAIL(st);
                }
            for (ch = 0; ch < IOX_CH_COUNT; ch++)
                if (mask & (1U << ch))
                    pins_set_mode(ch, r[2], r[3]);
            return 1;

        case OP_GET_CONFIG:
            NEED_LEN(1);
            ch = r[0];
            if (ch >= IOX_CH_COUNT) FAIL(ST_BAD_CH);
            p[k++] = ch;
            p[k++] = g_cfg[ch].type;
            p[k++] = g_cfg[ch].param;
            p[k++] = g_cfg[ch].edge;
            p[k++] = g_cfg[ch].debounce_ms;
            return k;

        case OP_SET_DIG_EVENT:
            NEED_LEN(3);
            ch = r[0];
            if (ch >= IOX_CH_COUNT) FAIL(ST_BAD_CH);
            if (r[1] > EDGE_BOTH) FAIL(ST_BAD_PARAM);
            if (g_cfg[ch].type != CH_INPUT) FAIL(ST_BAD_TYPE);
            {
                CRIT_ENTER();
                g_cfg[ch].edge = r[1];
                g_cfg[ch].debounce_ms = r[2];
                CRIT_EXIT();
            }
            return 1;

        case OP_WRITE:
            NEED_LEN(2);
            ch = r[0];
            if (ch >= IOX_CH_COUNT) FAIL(ST_BAD_CH);
            if (g_cfg[ch].type != CH_OUTPUT) FAIL(ST_BAD_TYPE);
            if (r[1] > 1) FAIL(ST_BAD_PARAM);
            pins_write_mask((u16)(1U << ch), r[1] ? (u16)(1U << ch) : 0);
            return 1;

        case OP_WRITE_MASK:
            NEED_LEN(4);
            mask = get16(r);
            if (mask & ~IOX_CH_ALL) FAIL(ST_BAD_CH);
            if (mask & ~pins_type_mask(CH_OUTPUT)) FAIL(ST_BAD_TYPE);           // writes nothing
            pins_write_mask(mask, get16(r + 2));
            return 1;

        case OP_READ_ALL:
            NEED_LEN(0);
            k += put16(p + k, scan_levels());
            k += put16(p + k, pins_read_raw());
            k += put16(p + k, pins_type_mask(CH_OUTPUT));
            return k;

        case OP_READ_ADC:
            NEED_LEN(2);
            ch = r[0];
            if (ch >= IOX_CH_COUNT) FAIL(ST_BAD_CH);
            if (g_cfg[ch].type != CH_ADC) FAIL(ST_BAD_TYPE);
            if (!avg_valid(r[1])) FAIL(ST_BAD_PARAM);
            k += adc_entry(p + k, ch, r[1]);
            return k;

        case OP_READ_ADC_ALL:
            NEED_LEN(1);
            if (!avg_valid(r[0])) FAIL(ST_BAD_PARAM);
            mask = pins_type_mask(CH_ADC);
            p[k++] = 0;                                                         // count, filled below
            for (ch = 0; ch < 6; ch++)
                if (mask & (1U << ch)) {
                    k += adc_entry(p + k, ch, r[0]);
                    p[1]++;
                }
            return k;

        case OP_SET_LINK_WDT:
            NEED_LEN(6);
            if ((get16(r + 2) | get16(r + 4)) & ~IOX_CH_ALL) FAIL(ST_BAD_CH);
            lwdt_config(get16(r), get16(r + 2), get16(r + 4), now);
            return 1;

        default:
            FAIL(ST_UNKNOWN_CMD);
    }
}

static void handle(const iox_frame_t* f, u32 now)
{
    u8 post = POST_NONE;

    lwdt_kick(now);                                                             // any valid frame
    if (f->type != FT_COMMAND)
        return;

    // duplicate (retry): resend the cached response without re-executing
    if (s_cache_valid && f->seq == s_last_seq && f->id == s_last_id) {
        uartSendGroup(s_wire, s_wire_len, UART1);
        return;
    }

    s_tx.type = FT_RESPONSE;
    s_tx.seq = f->seq;
    s_tx.id = f->id;
    s_tx.len = dispatch(f, s_tx.payload, &post, now);

    s_wire_len = s_env->encode(&s_tx, s_wire);
    s_last_seq = f->seq;
    s_last_id = f->id;
    s_cache_valid = 1;
    uartSendGroup(s_wire, s_wire_len, UART1);

    if (post == POST_RESET) {
        uartWaitTxDone(UART1);
        NVIC_SystemReset();
    }
}

void proto_init(void)
{
    u8 d[6];

    s_env->reset();
    d[0] = sys_reset_cause();
    d[1] = FW_MAJOR;
    d[2] = FW_MINOR;
    d[3] = PROTO_MAJOR;
    d[4] = PROTO_MINOR;
    d[5] = s_env->framing;
    evt_push(EV_BOOTED, d, 6);
}

void proto_poll(u32 now)
{
    if (s_env->poll(&s_rx, now))
        handle(&s_rx, now);
}

void proto_flush_event(void)
{
    evt_t e;
    iox_frame_t* f = &s_rx;                                                     // free between commands
    u8 wire[6 + IOX_KU_INNER_HDR + EVT_MAX_DATA + 1];
    u16 drops;

    if (!evt_peek(&e))
        return;

    f->type = FT_EVENT;
    f->seq = s_evt_seq++;
    f->id = e.id;
    f->len = e.len;
    memcpy(f->payload, e.data, e.len);
    uartSendGroup(wire, s_env->encode(f, wire), UART1);
    evt_drop_head();

    drops = evt_take_pending_drops();                                           // a slot is free now
    if (drops)
        evt_fault(FAULT_EVT_OVF, drops);
}

void proto_check_faults(u32 now)
{
    u16 e = uart1_rx_errors();

    if (e != s_uart_err_seen) {
        s_uart_err_seen = e;
        flags_set(FLAG_UART_ERR);
    }
    if (e != s_uart_err_reported && (u32)(now - s_uart_fault_ms) >= IOX_FAULT_MIN_GAP_MS) {
        s_uart_err_reported = e;
        s_uart_fault_ms = now;
        evt_fault(FAULT_UART, e);
    }
}
