/*
 * iox_host_example.c - reference host driver for the MM32F0020 I/O expander.
 *
 * Shows the link discipline from IOX_HOST_GUIDE.md: stop-and-wait, retry with
 * the same seq, accept events while waiting, re-apply config after BOOTED.
 * Port it by implementing the three platform hooks below (ESP-IDF, Arduino,
 * STM32 HAL, ...). Everything else is portable C99.
 */
#include <stdio.h>
#include "iox_host.h"

/* ---- platform hooks: implement these for your MCU ---------------------- */
extern void     plat_uart_write(const uint8_t* buf, uint16_t len);     /* blocking, whole buffer */
extern int      plat_uart_read(uint8_t* c);                            /* 1 = byte available, 0 = none (non-blocking) */
extern uint32_t plat_millis(void);                                     /* free-running ms counter */

/* ---- driver state ------------------------------------------------------- */
static iox_rx_t    s_rx;
static iox_frame_t s_frame;
static uint8_t     s_seq;
static uint8_t     s_need_config = 1;       /* set by BOOTED: MM32 lost its config */

/* Example application config: CH9..CH12 outputs (low), CH6 input with pull-up,
 * falling-edge events, 20 ms debounce. */
#define APP_OUTPUTS     (IOX_CH_BIT(IOX_CH9_PA5) | IOX_CH_BIT(IOX_CH10_PA6) | \
                         IOX_CH_BIT(IOX_CH11_PA8) | IOX_CH_BIT(IOX_CH12_PA9))
#define APP_BUTTON      IOX_CH6_PA0

static void on_event(const iox_frame_t* f)
{
    iox_booted_t b;
    iox_pin_change_t pc;
    iox_fault_t ft;

    if (iox_dec_booted(f, &b) == 0) {
        printf("IOX booted, cause 0x%02X, fw %u.%u\n",
               (unsigned)b.reset_cause, (unsigned)b.fw_major, (unsigned)b.fw_minor);
        s_need_config = 1;                  /* every channel is back to floating input */
    }
    else if (iox_dec_pin_change(f, &pc) == 0) {
        if (pc.changed & IOX_CH_BIT(APP_BUTTON))
            printf("button %s at %lu ms\n", (pc.levels & IOX_CH_BIT(APP_BUTTON)) ? "released" : "pressed",
                   (unsigned long)pc.t_ms);
    }
    else if (iox_dec_fault(f, &ft) == 0) {
        printf("IOX fault %u detail 0x%04X\n", (unsigned)ft.code, (unsigned)ft.detail);
        /* IOX_FAULT_EVT_OVF: events were lost -> resync with READ_ALL / heartbeat */
    }
}

/* Drain received bytes; dispatch events. Returns 1 if the response to
 * (seq, id) arrived (it is then in s_frame). Call it from your main loop too,
 * so events are handled between commands. */
static int iox_service(uint8_t seq, uint8_t id)
{
    uint8_t c;
    while (plat_uart_read(&c)) {
        if (!iox_rx_feed(&s_rx, c, &s_frame))
            continue;
        if (s_frame.type == IOX_FT_EVENT)
            on_event(&s_frame);
        else if (id && iox_is_response_to(&s_frame, seq, id))
            return 1;
        /* anything else: stale response to an earlier attempt, ignore */
    }
    return 0;
}

/* Send one command, wait for its response. wire/wlen from an iox_req_*() call
 * built with seq. Returns the response status (IOX_ST_*) or -1 = no response. */
static int iox_transact(const uint8_t* wire, uint8_t wlen, uint8_t seq, uint8_t id)
{
    int attempt;
    for (attempt = 0; attempt <= IOX_RETRIES; attempt++) {
        uint32_t t0 = plat_millis();
        plat_uart_write(wire, wlen);        /* retries resend the SAME bytes (same seq) */
        while ((uint32_t)(plat_millis() - t0) < IOX_RSP_TIMEOUT_MS)
            if (iox_service(seq, id))
                return iox_rsp_status(&s_frame);
    }
    return -1;                              /* link down: check wiring/power, then PING */
}

/* Convenience: build (with the current s_seq) + send + wait, then advance
 * s_seq, so every NEW command gets a new seq and retries inside
 * iox_transact reuse it. */
#define IOX_DO(req_call, id) \
    (wlen = (req_call), iox_transact(wire, wlen, s_seq++, (id)))

static int iox_apply_config(void)
{
    uint8_t wire[IOX_WIRE_MAX], wlen;
    int st;

    st = IOX_DO(iox_req_set_mode_mask(wire, s_seq, APP_OUTPUTS, IOX_TYPE_OUTPUT, IOX_OUT_LOW), IOX_CMD_SET_MODE_MASK);
    if (st != IOX_ST_OK) return st;
    st = IOX_DO(iox_req_set_mode(wire, s_seq, APP_BUTTON, IOX_TYPE_INPUT, IOX_IN_PULLUP), IOX_CMD_SET_MODE);
    if (st != IOX_ST_OK) return st;
    st = IOX_DO(iox_req_set_dig_event(wire, s_seq, APP_BUTTON, IOX_EDGE_BOTH, 20), IOX_CMD_SET_DIG_EVENT);
    if (st != IOX_ST_OK) return st;
    s_need_config = 0;
    return IOX_ST_OK;
}

int iox_init(void)
{
    uint8_t wire[IOX_WIRE_MAX], wlen;
    iox_info_t info;
    int st;

    iox_rx_init(&s_rx);
    st = IOX_DO(iox_req_get_info(wire, s_seq), IOX_CMD_GET_INFO);
    if (st != IOX_ST_OK || iox_dec_info(&s_frame, &info) != IOX_ST_OK)
        return -1;
    if (info.proto_major != IOX_PROTO_MAJOR)
        return -2;                          /* incompatible expander firmware */
    return iox_apply_config();
}

/* Set outputs: bits in mask get the matching bit of values. */
int iox_set_outputs(uint16_t mask, uint16_t values)
{
    uint8_t wire[IOX_WIRE_MAX], wlen;
    return IOX_DO(iox_req_write_mask(wire, s_seq, mask, values), IOX_CMD_WRITE_MASK);
}

/* Call from the main loop, e.g. every 10 ms. */
void iox_poll(void)
{
    static uint32_t last_hb;
    uint8_t wire[IOX_WIRE_MAX], wlen;
    iox_heartbeat_t hb;

    iox_service(0, 0);                      /* events between commands */
    if (s_need_config)
        iox_apply_config();

    if ((uint32_t)(plat_millis() - last_hb) >= 100) {   /* 100 ms heartbeat */
        last_hb = plat_millis();
        if (IOX_DO(iox_req_heartbeat(wire, s_seq), IOX_CMD_HEARTBEAT) == IOX_ST_OK
            && iox_dec_heartbeat(&s_frame, &hb) == IOX_ST_OK) {
            if (hb.changed)                 /* inputs that changed since the last heartbeat,
                                               even if the PIN_CHANGE event was lost */
                printf("changed 0x%04X levels 0x%04X\n", (unsigned)hb.changed, (unsigned)hb.levels);
            if ((hb.mask & IOX_HB_OUT_MASK) && (hb.out_mask & APP_OUTPUTS) != APP_OUTPUTS)
                s_need_config = 1;          /* backstop: config lost without a BOOTED seen */
        }
    }
}
