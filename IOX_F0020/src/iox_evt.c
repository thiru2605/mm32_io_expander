////////////////////////////////////////////////////////////////////////////////
/// @file    iox_evt.c
/// @brief   16-entry event queue. Producers: SysTick (PIN_CHANGE) and main
///          (BOOTED, FAULT), so pushes run in a short critical section.
///          Consumer: main loop only.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_EVT_C_

#include <string.h>
#include "iox_config.h"
#include "iox_sys.h"
#include "iox_evt.h"

#define Q_MASK (IOX_EVT_QUEUE_LEN - 1)

static evt_t s_q[IOX_EVT_QUEUE_LEN];
static volatile u8 s_head;
static volatile u8 s_tail;
static volatile u16 s_pending_drops;
static volatile u16 s_total_drops;
static volatile u8 s_flags;

void evt_push(u8 id, const u8* data, u8 len)
{
    u8 next;
    CRIT_ENTER();
    next = (u8)((s_head + 1) & Q_MASK);
    if (next == s_tail) {
        s_pending_drops++;
        s_total_drops++;
        s_flags |= FLAG_EVT_OVF;
    }
    else {
        s_q[s_head].id = id;
        s_q[s_head].len = len;
        memcpy(s_q[s_head].data, data, len);
        s_head = next;
    }
    CRIT_EXIT();
}

void evt_pin_change(u16 changed, u16 levels, u32 t_ms)
{
    u8 d[8];
    d[0] = (u8)changed;  d[1] = (u8)(changed >> 8);
    d[2] = (u8)levels;   d[3] = (u8)(levels >> 8);
    d[4] = (u8)t_ms;     d[5] = (u8)(t_ms >> 8);
    d[6] = (u8)(t_ms >> 16); d[7] = (u8)(t_ms >> 24);
    evt_push(EV_PIN_CHANGE, d, 8);
}

void evt_fault(u8 code, u16 detail)
{
    u8 d[3];
    d[0] = code;
    d[1] = (u8)detail;
    d[2] = (u8)(detail >> 8);
    evt_push(EV_FAULT, d, 3);
}

u8 evt_peek(evt_t* e)
{
    if (s_tail == s_head)
        return 0;
    *e = s_q[s_tail];
    return 1;
}

void evt_drop_head(void)
{
    if (s_tail != s_head)
        s_tail = (u8)((s_tail + 1) & Q_MASK);
}

u16 evt_take_pending_drops(void)
{
    u16 v;
    CRIT_ENTER();
    v = s_pending_drops;
    s_pending_drops = 0;
    CRIT_EXIT();
    return v;
}

u16 evt_drops_total(void)
{
    return s_total_drops;
}

void flags_set(u8 bits)
{
    CRIT_ENTER();
    s_flags |= bits;
    CRIT_EXIT();
}

u8 flags_peek(void)
{
    return s_flags;
}

u8 flags_take(void)
{
    u8 v;
    CRIT_ENTER();
    v = s_flags;
    s_flags = 0;
    CRIT_EXIT();
    return v;
}
