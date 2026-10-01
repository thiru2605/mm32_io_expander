////////////////////////////////////////////////////////////////////////////////
/// @file    iox_adc.h
/// @brief   Background round-robin ADC sampling, cache, averaged reads, VDD.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_ADC_H
#define __IOX_ADC_H

#include "mm32_device.h"

void iox_adc_init(void);
void iox_adc_poll(u32 now);                                                     // main loop, non-blocking
u16  iox_adc_cached(u8 ch);                                                     // ch = expander channel 0..5
u16  iox_adc_read_avg(u8 ch, u8 n);                                             // blocking fresh conversion(s)
u16  iox_adc_vdd_mv(void);
u16  iox_adc_to_mv(u16 raw);

#endif
