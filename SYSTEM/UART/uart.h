
// Define to prevent recursive inclusion
#ifndef __UART_H
#define __UART_H
// Files includes
#include "mm32_device.h"
#include "device_config.h"
#include "stdio.h"
#include "defaults.h"

#ifdef UART_ENABLE
#define UART1_RX_BUF_SIZE  100
#define UART2_RX_BUF_SIZE  100
#define UART3_RX_BUF_SIZE  100

#if defined(UART1_RX_INTERRUPT_ENABLE) && defined(UART1_RX_RING_ENABLE)
#error "UART1: choose line mode (UART1_RX_INTERRUPT_ENABLE) or ring mode (UART1_RX_RING_ENABLE), not both"
#endif

#if defined(UART_ENABLE) && defined(UART1_RX_INTERRUPT_ENABLE)
extern char uart1_receive_buf[UART1_RX_BUF_SIZE];
extern u8 uart1_receive_len;
extern u8 uart1_received_flag;
#endif

// UART1 ring mode: binary-safe RX. The ISR stores every byte in a ring buffer,
// the application reads bytes with uart1_rx_read() and does its own framing.
#if defined(UART_ENABLE) && defined(UART1_RX_RING_ENABLE)
#ifndef UART1_RX_RING_SIZE
#define UART1_RX_RING_SIZE 128                                                  // power of 2, <= 256
#endif
u8  uart1_rx_read(u8* c);                                                       // 1 = byte returned
u16 uart1_rx_errors(void);                                                      // overrun/framing/ring-full count
#endif

#if defined(UART_ENABLE) && defined(UART2_RX_INTERRUPT_ENABLE)

extern char uart2_receive_buf[UART2_RX_BUF_SIZE];
extern u8 uart2_receive_len;
extern u8 uart2_received_flag;
#endif

#if defined(UART_ENABLE) && defined(UART3_RX_INTERRUPT_ENABLE)

extern char uart3_receive_buf[UART3_RX_BUF_SIZE];
extern u8 uart3_receive_len;
extern u8 uart3_received_flag;
#endif

#ifdef DEBUG_ENABLE
extern char debug_buff[100];
#endif

//function prototype
void CONSOLE_Init(void);
void uartSendString(char *str, UART_TypeDef *UARTx);
void uartSendByte(u8 ch, UART_TypeDef *UARTx);
void NVIC_UART_ENABLE(void);
void NVIC_UART_DISABLE(void);
uint8_t uartReceiveByte(UART_TypeDef *UARTx);
uint8_t uartDataAvailable(UART_TypeDef *UARTx);
void uartSendGroup(u8 *buf, u8 len, UART_TypeDef *UARTx);
void uartWaitTxDone(UART_TypeDef *UARTx);

////////////////////////////////////////////////////////////////////////////////
#endif
#endif
////////////////////////////////////////////////////////////////////////////////


