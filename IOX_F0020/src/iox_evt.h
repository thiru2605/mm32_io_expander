////////////////////////////////////////////////////////////////////////////////
/// @file    iox_evt.h
/// @brief   Event queue (fire-and-forget events) and latched status flags.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_EVT_H
#define __IOX_EVT_H

#include "mm32_device.h"

// Event ids (spec 8)
#define EV_BOOTED       0x80
#define EV_PIN_CHANGE   0x81
#define EV_FAULT        0x82

// FAULT codes
#define FAULT_EVT_OVF   1
#define FAULT_UART      2
#define FAULT_LINK_WDT  3

// Heartbeat FLAGS / GET_STATUS flags bits
#define FLAG_EVT_OVF    0x01
#define FLAG_LINK_WDT   0x02
#define FLAG_UART_ERR   0x04

#define EVT_MAX_DATA    8

typedef struct {
    u8 id;
    u8 len;
    u8 data[EVT_MAX_DATA];
} evt_t;

void evt_push(u8 id, const u8* data, u8 len);                                   // ISR- and main-safe
void evt_pin_change(u16 changed, u16 levels, u32 t_ms);
void evt_fault(u8 code, u16 detail);
u8   evt_peek(evt_t* e);                                                        // 1 = e holds the oldest event
void evt_drop_head(void);
u16  evt_take_pending_drops(void);                                              // drops since last FAULT(1)
u16  evt_drops_total(void);

void flags_set(u8 bits);
u8   flags_peek(void);
u8   flags_take(void);                                                          // read and clear

#endif
