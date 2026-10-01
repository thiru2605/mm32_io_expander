////////////////////////////////////////////////////////////////////////////////
/// @file    iox_scan.c
/// @brief   Debounce / edge / CHANGED latch, run from the 1 ms SysTick.
/// @note    A level is accepted once it has differed from the debounced level
///          for debounce_ms consecutive scans (1 scan when debounce_ms = 0).
///          A channel that just became INPUT adopts its level on the next
///          scan without reporting a change.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_SCAN_C_

#include "iox_config.h"
#include "iox_sys.h"
#include "iox_pins.h"
#include "iox_scan.h"
#include "iox_evt.h"

static volatile u16 s_in_mask;                                                  // channels of type INPUT
static volatile u16 s_stable;                                                   // debounced input levels
static volatile u16 s_settle;                                                   // adopt level on next scan
static volatile u16 s_changed;                                                  // CHANGED latch
static u8 s_cnt[IOX_CH_COUNT];

void scan_init(void)
{
    CRIT_ENTER();
    s_in_mask = pins_type_mask(CH_INPUT);
    s_stable = 0;
    s_settle = s_in_mask;
    s_changed = 0;
    CRIT_EXIT();
}

void scan_mode_changed(u8 ch)
{
    u16 bit = (u16)(1U << ch);

    s_cnt[ch] = 0;
    if (g_cfg[ch].type == CH_INPUT) {
        s_in_mask |= bit;
        s_settle |= bit;
    }
    else {
        s_in_mask &= (u16)~bit;
        s_stable &= (u16)~bit;
        s_settle &= (u16)~bit;
    }
}

void scan_tick(u32 now)
{
    u16 raw = pins_read_raw();
    u16 in = s_in_mask;
    u16 stable = s_stable;
    u16 batch = 0;
    u8 ch;

    if (s_settle) {
        stable = (u16)((stable & ~s_settle) | (raw & s_settle));
        s_settle = 0;
    }

    for (ch = 0; ch < IOX_CH_COUNT; ch++) {
        u16 bit = (u16)(1U << ch);
        u8 thr, edge;

        if (!(in & bit)) continue;
        if (!((raw ^ stable) & bit)) {
            s_cnt[ch] = 0;
            continue;
        }
        thr = g_cfg[ch].debounce_ms ? g_cfg[ch].debounce_ms : 1;
        if (++s_cnt[ch] < thr) continue;

        s_cnt[ch] = 0;
        stable ^= bit;
        s_changed |= bit;
        edge = g_cfg[ch].edge;
        if (edge == EDGE_BOTH
            || (edge == EDGE_RISING && (raw & bit))
            || (edge == EDGE_FALL && !(raw & bit)))
            batch |= bit;
    }
    s_stable = stable;

    if (batch)
        evt_pin_change(batch, scan_levels(), now);
}

u16 scan_levels(void)
{
    return (u16)((s_stable & s_in_mask) | pins_out_levels());
}

u16 scan_take_changed(void)
{
    u16 v;
    CRIT_ENTER();
    v = s_changed;
    s_changed = 0;
    CRIT_EXIT();
    return v;
}
