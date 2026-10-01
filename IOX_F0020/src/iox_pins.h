////////////////////////////////////////////////////////////////////////////////
/// @file    iox_pins.h
/// @brief   Channel table, mode application, reads and writes.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_PINS_H
#define __IOX_PINS_H

#include "mm32_device.h"
#include "iox_config.h"

#define CH_INPUT    0
#define CH_OUTPUT   1
#define CH_ADC      2

#define IN_FLOAT    0
#define IN_PULLUP   1
#define IN_PULLDOWN 2

#define EDGE_NONE   0
#define EDGE_RISING 1
#define EDGE_FALL   2
#define EDGE_BOTH   3

#define ADC_NONE    0xFF

typedef struct {
    GPIO_TypeDef* port;
    u8 pin;
    u8 adc_ch;                                                                  // ADC input or ADC_NONE
} ch_hw_t;

typedef struct {
    u8 type;
    u8 param;
    u8 edge;
    u8 debounce_ms;
} ch_cfg_t;

extern const ch_hw_t g_hw[IOX_CH_COUNT];
extern ch_cfg_t g_cfg[IOX_CH_COUNT];

void pins_init(void);
u8   pins_check_mode(u8 ch, u8 type, u8 param);                                 // status code, 0 = OK
void pins_set_mode(u8 ch, u8 type, u8 param);
void pins_write_mask(u16 mask, u16 values);                                     // outputs only, caller validated
u16  pins_read_raw(void);                                                       // instantaneous IDR, all channels
u16  pins_out_levels(void);                                                     // ODR of OUTPUT channels
u16  pins_type_mask(u8 type);

#endif
