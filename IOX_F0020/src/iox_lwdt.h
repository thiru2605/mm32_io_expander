////////////////////////////////////////////////////////////////////////////////
/// @file    iox_lwdt.h
/// @brief   Link watchdog: force selected outputs when the ESP32 goes quiet.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_LWDT_H
#define __IOX_LWDT_H

#include "mm32_device.h"

void lwdt_config(u16 timeout_ms, u16 force_mask, u16 force_levels, u32 now);
void lwdt_kick(u32 now);                                                        // any valid frame
void lwdt_poll(u32 now);

#endif
