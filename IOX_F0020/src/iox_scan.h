////////////////////////////////////////////////////////////////////////////////
/// @file    iox_scan.h
/// @brief   1 ms input scan: debounce, edge detection, latched CHANGED mask.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_SCAN_H
#define __IOX_SCAN_H

#include "mm32_device.h"

void scan_init(void);
void scan_tick(u32 now);                                                        // SysTick context
void scan_mode_changed(u8 ch);                                                  // call with IRQs masked
u16  scan_levels(void);                                                         // debounced inputs | output levels
u16  scan_take_changed(void);                                                   // read and clear CHANGED latch

#endif
