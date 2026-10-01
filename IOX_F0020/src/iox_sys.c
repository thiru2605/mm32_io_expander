////////////////////////////////////////////////////////////////////////////////
/// @file    iox_sys.c
/// @brief   1 ms SysTick (drives the input scan), reset cause, IWDG.
////////////////////////////////////////////////////////////////////////////////
#define _IOX_SYS_C_

#include "hal_conf.h"
#include "wdg.h"
#include "iox_config.h"
#include "iox_sys.h"
#include "iox_scan.h"

extern u32 SystemCoreClock;                                                     // system_mm32f0020.c

volatile u32 g_ms;
static u8 s_reset_cause;

////////////////////////////////////////////////////////////////////////////////
/// @brief  Capture and clear the reset flags, start the 1 ms SysTick.
/// @note   Clock (48 MHz from HSI) is already set by SystemInit().
////////////////////////////////////////////////////////////////////////////////
void sys_init(void)
{
    u32 csr = RCC->CSR;

    s_reset_cause = 0;
    if (csr & RCC_CSR_PORRSTF)  s_reset_cause |= RST_POR;
    if (csr & RCC_CSR_PINRSTF)  s_reset_cause |= RST_PIN;
    if (csr & RCC_CSR_IWDGRSTF) s_reset_cause |= RST_IWDG;
    if (csr & RCC_CSR_WWDGRSTF) s_reset_cause |= RST_WWDG;
    if (csr & RCC_CSR_SFTRSTF)  s_reset_cause |= RST_SW;
    RCC->CSR |= RCC_CSR_RMVF;

    SysTick_Config(SystemCoreClock / 1000);
    NVIC_SetPriority(SysTick_IRQn, 1);                                          // below UART1 (0)
}

u8 sys_reset_cause(void)
{
    return s_reset_cause;
}

void sys_iwdg_start(void)
{
    Write_Iwdg_ON(IOX_IWDG_PRESCALER, IOX_IWDG_RELOAD);
}

void sys_iwdg_feed(void)
{
    IWDG_ReloadCounter();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  1 ms tick: uptime + input scan / debounce / edge detection.
////////////////////////////////////////////////////////////////////////////////
void SysTick_Handler(void)
{
    g_ms++;
    scan_tick(g_ms);
}
