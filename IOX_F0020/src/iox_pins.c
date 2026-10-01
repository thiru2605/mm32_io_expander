////////////////////////////////////////////////////////////////////////////////
/// @file    iox_pins.c
/// @brief   Channel map (spec 2.3) and runtime mode switching.
/// @note    CRL/CRH nibble per pin: 0x0 analog, 0x4 floating input,
///          0x8 pull input (ODR selects up/down), 0x1 push-pull output 10 MHz.
///          Runtime changes run with IRQs masked so the SysTick scan never
///          sees a half-applied mode.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_PINS_C_

#include "hal_conf.h"
#include "gpios.h"
#include "iox_sys.h"
#include "iox_pins.h"
#include "iox_scan.h"

#define NIB_ANALOG  0x0
#define NIB_FLOAT   0x4
#define NIB_PULL    0x8
#define NIB_OUT_PP  0x1                                                         // MODE=01 (slowest edges)

const ch_hw_t g_hw[IOX_CH_COUNT] = {
    {GPIOB,  1, 0},                                                             // CH0  PB1  ADC0
    {GPIOB,  0, 1},                                                             // CH1  PB0  ADC1
    {GPIOA, 11, 4},                                                             // CH2  PA11 ADC4
    {GPIOA,  2, 5},                                                             // CH3  PA2  ADC5
    {GPIOA, 15, 6},                                                             // CH4  PA15 ADC6
    {GPIOA,  7, 7},                                                             // CH5  PA7  ADC7
    {GPIOA,  0, ADC_NONE},                                                      // CH6  PA0
    {GPIOA,  1, ADC_NONE},                                                      // CH7  PA1
    {GPIOA,  4, ADC_NONE},                                                      // CH8  PA4
    {GPIOA,  5, ADC_NONE},                                                      // CH9  PA5
    {GPIOA,  6, ADC_NONE},                                                      // CH10 PA6
    {GPIOA,  8, ADC_NONE},                                                      // CH11 PA8
    {GPIOA,  9, ADC_NONE},                                                      // CH12 PA9
};

ch_cfg_t g_cfg[IOX_CH_COUNT];

static void set_nibble(GPIO_TypeDef* p, u8 pin, u32 v)
{
    __IO u32* r = (pin < 8) ? &p->CRL : &p->CRH;
    u32 sh = (u32)(pin & 7) * 4;
    *r = (*r & ~(0xFUL << sh)) | (v << sh);
}

static void set_odr(GPIO_TypeDef* p, u8 pin, u8 level)
{
    if (level) p->BSRR = 1UL << pin;
    else       p->BRR  = 1UL << pin;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  Boot default: every channel INPUT / floating, edge none.
////////////////////////////////////////////////////////////////////////////////
void pins_init(void)
{
    u8 ch;
    u16 pa = 0, pb = 0;

    for (ch = 0; ch < IOX_CH_COUNT; ch++) {
        if (g_hw[ch].port == GPIOA) pa |= (u16)(1U << g_hw[ch].pin);
        else                        pb |= (u16)(1U << g_hw[ch].pin);
        g_cfg[ch].type = CH_INPUT;
        g_cfg[ch].param = IN_FLOAT;
        g_cfg[ch].edge = EDGE_NONE;
        g_cfg[ch].debounce_ms = 0;
    }
    gpio_init(GPIOA, pa, GPIO_Mode_FLOATING, GPIO_Speed_10MHz);
    gpio_init(GPIOB, pb, GPIO_Mode_FLOATING, GPIO_Speed_10MHz);
}

u8 pins_check_mode(u8 ch, u8 type, u8 param)
{
    if (ch >= IOX_CH_COUNT) return 3;
    switch (type) {
        case CH_INPUT:  return (param <= IN_PULLDOWN) ? 0 : 5;
        case CH_OUTPUT: return (param <= 1) ? 0 : 5;
        case CH_ADC:
            if (g_hw[ch].adc_ch == ADC_NONE) return 4;
            return (param == 0) ? 0 : 5;
        default:        return 5;
    }
}

void pins_set_mode(u8 ch, u8 type, u8 param)
{
    const ch_hw_t* h = &g_hw[ch];
    CRIT_ENTER();

    switch (type) {
        case CH_INPUT:
            if (param == IN_FLOAT) {
                set_nibble(h->port, h->pin, NIB_FLOAT);
            }
            else {
                set_odr(h->port, h->pin, param == IN_PULLUP);
                set_nibble(h->port, h->pin, NIB_PULL);
            }
            break;
        case CH_OUTPUT:
            set_odr(h->port, h->pin, param);                                    // level first: no glitch
            set_nibble(h->port, h->pin, NIB_OUT_PP);
            break;
        default:
            set_nibble(h->port, h->pin, NIB_ANALOG);
            break;
    }

    if (type != CH_INPUT)
        g_cfg[ch].edge = EDGE_NONE;                                             // spec 7.8
    g_cfg[ch].type = type;
    g_cfg[ch].param = param;
    scan_mode_changed(ch);

    CRIT_EXIT();
}

void pins_write_mask(u16 mask, u16 values)
{
    u32 a = 0, b = 0;
    u8 ch;

    for (ch = 0; ch < IOX_CH_COUNT; ch++) {
        u32 bit;
        if (!(mask & (1U << ch))) continue;
        bit = (values & (1U << ch)) ? (1UL << g_hw[ch].pin) : (1UL << (g_hw[ch].pin + 16));
        if (g_hw[ch].port == GPIOA) a |= bit;
        else                        b |= bit;
    }
    // one BSRR write per port (set in low half, reset in high half)
    if (a) GPIOA->BSRR = a;
    if (b) GPIOB->BSRR = b;
}

u16 pins_read_raw(void)
{
    u32 ia = GPIOA->IDR, ib = GPIOB->IDR;
    u16 m = 0;
    u8 ch;

    for (ch = 0; ch < IOX_CH_COUNT; ch++) {
        u32 idr = (g_hw[ch].port == GPIOA) ? ia : ib;
        if (idr & (1UL << g_hw[ch].pin))
            m |= (u16)(1U << ch);
    }
    return m;
}

u16 pins_out_levels(void)
{
    u32 oa = GPIOA->ODR, ob = GPIOB->ODR;
    u16 m = 0;
    u8 ch;

    for (ch = 0; ch < IOX_CH_COUNT; ch++) {
        u32 odr;
        if (g_cfg[ch].type != CH_OUTPUT) continue;
        odr = (g_hw[ch].port == GPIOA) ? oa : ob;
        if (odr & (1UL << g_hw[ch].pin))
            m |= (u16)(1U << ch);
    }
    return m;
}

u16 pins_type_mask(u8 type)
{
    u16 m = 0;
    u8 ch;

    for (ch = 0; ch < IOX_CH_COUNT; ch++)
        if (g_cfg[ch].type == type)
            m |= (u16)(1U << ch);
    return m;
}
