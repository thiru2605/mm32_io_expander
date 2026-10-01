////////////////////////////////////////////////////////////////////////////////
/// @file    iox_sys.h
/// @brief   1 ms SysTick, uptime, reset cause, IWDG, critical sections.
////////////////////////////////////////////////////////////////////////////////
#ifndef __IOX_SYS_H
#define __IOX_SYS_H

#include "mm32_device.h"

// reset_cause bits (spec 7.4)
#define RST_POR     0x01
#define RST_PIN     0x02
#define RST_IWDG    0x04
#define RST_WWDG    0x08
#define RST_SW      0x10

extern volatile u32 g_ms;

void sys_init(void);
u8   sys_reset_cause(void);
void sys_iwdg_start(void);
void sys_iwdg_feed(void);

// PRIMASK save/restore critical section
#define CRIT_ENTER()    u32 _pm = __get_PRIMASK(); __disable_irq()
#define CRIT_EXIT()     __set_PRIMASK(_pm)

#endif
