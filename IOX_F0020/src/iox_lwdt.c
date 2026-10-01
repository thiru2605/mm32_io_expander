////////////////////////////////////////////////////////////////////////////////
/// @file    iox_lwdt.c
/// @brief   Link watchdog (spec 7.16). Off by default. On timeout, OUTPUT
///          channels in force_mask go to their force_levels bit, the others
///          hold; FAULT(3) is queued and flag bit 1 set. Trips once, re-arms
///          on the next valid frame.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_LWDT_C_

#include "iox_pins.h"
#include "iox_evt.h"
#include "iox_lwdt.h"

static u16 s_timeout;
static u16 s_mask;
static u16 s_levels;
static u32 s_last;
static u8  s_tripped;

void lwdt_config(u16 timeout_ms, u16 force_mask, u16 force_levels, u32 now)
{
    s_timeout = timeout_ms;
    s_mask = force_mask;
    s_levels = force_levels;
    s_last = now;
    s_tripped = 0;
}

void lwdt_kick(u32 now)
{
    s_last = now;
    s_tripped = 0;
}

void lwdt_poll(u32 now)
{
    u16 m;

    if (!s_timeout || s_tripped) return;
    if ((u32)(now - s_last) < s_timeout) return;

    s_tripped = 1;
    m = s_mask & pins_type_mask(CH_OUTPUT);
    if (m)
        pins_write_mask(m, s_levels);
    flags_set(FLAG_LINK_WDT);
    evt_fault(FAULT_LINK_WDT, m);
}
