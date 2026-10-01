////////////////////////////////////////////////////////////////////////////////
/// @file    device_config.h
/// @brief   IOX build of the shared feature macros consumed by the reused
///          SYSTEM/UART, SYSTEM/WDG and HARDWARE/GPIO drivers.
/// @note    Shadows USER/HARDWARE_F0020xx/device_config.h (src is first on
///          the include path). UART1 runs uart.c's binary-safe ring mode
///          (UART1_RX_RING_ENABLE) instead of its '\n' line mode.
////////////////////////////////////////////////////////////////////////////////
#ifndef __DEVICE_CONFIG_H
#define __DEVICE_CONFIG_H

#define MM32F0010xx 1
#define MM32F0020xx 2
#define MM32F0140xx 3
#define SELECT_IC MM32F0020xx

#define WATCHDOG_ENABLE

/////////[UART]/////////////////
#define UART_ENABLE
#define UART1_RX_RING_ENABLE
#define UART1_RX_RING_SIZE 128
#define UART1_INTERRUPT_PRIORITY 0                                              // above SysTick (1)
#define UART1_Port GPIOA
#define UART1_TxPin GPIO_Pin_12
#define UART1_TxPin_AF GPIO_AF_1
#define UART1_RxPin GPIO_Pin_3
#define UART1_RxPin_AF GPIO_AF_1
#define UART1_TxPinSource GPIO_PinSource12
#define UART1_RxPinSource GPIO_PinSource3
#define UART1_BaudRate 115200

#endif
