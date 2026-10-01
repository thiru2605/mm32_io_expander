////////////////////////////////////////////////////////////////////////////////
/// @file    iox_adc.c
/// @brief   ADC1, single software-triggered conversions.
/// @note    fADC <= 16 MHz (DS 5.3.16): PCLK1/3 at 48 MHz, PCLK1/2 at <= 32 MHz.
///          External inputs sample 42.5 cycles (source <= ~27 kohm, DS table 5-29).
///          VREFINT (ADC ch 8, 1.2 V typ, no calibration) samples 240.5 cycles
///          (>= 11.8 us required). One round-robin step per ms: each ADC-type
///          channel in turn, then VREFINT once per cycle.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_ADC_C_

#include "hal_conf.h"
#include "iox_config.h"
#include "iox_pins.h"
#include "iox_adc.h"

#define VREF_CH         8
#define VREFINT_MV      1200UL
#define SAMCTL_EXT      4U                                                      // 42.5 cycles
#define SAMCTL_VREF     7U                                                      // 240.5 cycles
#define RR_VREF_SLOT    6                                                       // after CH0..CH5

static u16 s_cache[6];
static u16 s_vref_raw;
static u8  s_slot;                                                              // next round-robin slot
static s8  s_busy_slot = -1;                                                    // slot being converted
static u32 s_last_ms;

static u8 slot_hw(u8 slot)
{
    return (slot == RR_VREF_SLOT) ? VREF_CH : g_hw[slot].adc_ch;
}

static void start(u8 hw_ch)
{
    ADC1->ADCHS = 1UL << hw_ch;
    ADC1->ADSTA = ADC_SR_ADIF;                                                  // clear stale EOC
    ADC1->ADCR |= ADC_CR_ADST;
}

static u8 done(void)
{
    return (ADC1->ADSTA & ADC_SR_ADIF) != 0;
}

static u16 result(void)
{
    u16 v = (u16)(ADC1->ADDATA & 0xFFF);
    ADC1->ADSTA = ADC_SR_ADIF;
    return v;
}

static void store(u8 slot, u16 v)
{
    if (slot == RR_VREF_SLOT) s_vref_raw = v;
    else                      s_cache[slot] = v;
}

void iox_adc_init(void)
{
    ADC_InitTypeDef init;

    RCC_APB1PeriphClockCmd(RCC_APB1ENR_ADC1, ENABLE);

    ADC_StructInit(&init);
    init.ADC_Resolution = ADC_Resolution_12b;
    init.ADC_PRESCARE = (RCC_GetPCLK1Freq() > 32000000) ? ADC_PCLK2_PRESCARE_3 : ADC_PCLK2_PRESCARE_2;
    init.ADC_Mode = ADC_Mode_Imm;
    init.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_Init(ADC1, &init);

    // per-channel sample time: SMPR1 = ch0..7, SMPR2[3:0] = ch8
    ADC1->SMPR1 = SAMCTL_EXT * 0x11111111UL;
    ADC1->SMPR2 = (ADC1->SMPR2 & ~0xFUL) | SAMCTL_VREF;

    ADC_VrefintCmd(ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    s_vref_raw = 0;
}

void iox_adc_poll(u32 now)
{
    u8 i;

    if (s_busy_slot >= 0) {
        if (!done()) return;
        store((u8)s_busy_slot, result());
        s_busy_slot = -1;
    }
    if (now == s_last_ms) return;                                               // one conversion per ms
    s_last_ms = now;

    for (i = 0; i <= RR_VREF_SLOT; i++) {
        u8 slot = s_slot;
        s_slot = (u8)((s_slot + 1) % (RR_VREF_SLOT + 1));
        if (slot == RR_VREF_SLOT || g_cfg[slot].type == CH_ADC) {
            s_busy_slot = (s8)slot;
            start(slot_hw(slot));
            return;
        }
    }
}

u16 iox_adc_cached(u8 ch)
{
    return (ch < 6) ? s_cache[ch] : 0;
}

u16 iox_adc_read_avg(u8 ch, u8 n)
{
    u32 sum = 0;
    u8 i;

    if (s_busy_slot >= 0) {                                                     // finish background conversion
        while (!done());
        store((u8)s_busy_slot, result());
        s_busy_slot = -1;
    }
    for (i = 0; i < n; i++) {
        start(g_hw[ch].adc_ch);
        while (!done());
        sum += result();
    }
    s_cache[ch] = (u16)(sum / n);
    return s_cache[ch];
}

u16 iox_adc_vdd_mv(void)
{
    if (s_vref_raw == 0)
        return 3300;                                                            // not measured yet
    return (u16)((VREFINT_MV * 4095UL) / s_vref_raw);
}

u16 iox_adc_to_mv(u16 raw)
{
    return (u16)(((u32)raw * iox_adc_vdd_mv()) / 4095UL);
}
