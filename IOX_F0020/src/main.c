////////////////////////////////////////////////////////////////////////////////
/// @file    main.c
/// @brief   MM32F0020 UART I/O expander - init and super-loop (spec 4.1).
////////////////////////////////////////////////////////////////////////////////
#define _MAIN_C_

#include "main.h"
#include "iox_sys.h"
#include "uart.h"
#include "iox_pins.h"
#include "iox_scan.h"
#include "iox_adc.h"
#include "iox_lwdt.h"
#include "iox_proto.h"

s32 main(void)
{
    sys_init();                                                                 // reset cause, 1 ms SysTick
    pins_init();                                                                // all channels INPUT floating
    scan_init();
    iox_adc_init();
    CONSOLE_Init();                                                             // uart.c: UART1 ring mode
    proto_init();                                                               // queues BOOTED
    sys_iwdg_start();

    while (1) {
        u32 now = g_ms;

        proto_poll(now);                                                        // parse, execute, respond
        iox_adc_poll(now);                                                      // background sampling
        proto_flush_event();                                                    // events after responses
        lwdt_poll(now);
        proto_check_faults(now);
        sys_iwdg_feed();
    }
}
