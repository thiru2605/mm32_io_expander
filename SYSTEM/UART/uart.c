
// Define to prevent recursive inclusion
#define _UART_C_

// Files includes
#include "uart.h"
#include "main.h"
#include "hal_conf.h"

#ifdef UART_ENABLE

#if defined(UART_ENABLE) && defined(UART1_RX_INTERRUPT_ENABLE)

char uart1_receive_buf[UART1_RX_BUF_SIZE];
u8 uart1_receive_len;
u8 uart1_received_flag = 0;
#endif

#if defined(UART_ENABLE) && defined(UART1_RX_RING_ENABLE)
#define UART1_RX_RING_MASK  (UART1_RX_RING_SIZE - 1)
#define UART1_RX_ERR_BITS   (UART_ISR_RXOERR | UART_ISR_RXFERR | UART_ISR_RXPERR | UART_ISR_RXBRK)

static u8 uart1_rx_ring[UART1_RX_RING_SIZE];
static volatile u8 uart1_rx_head;                                               // written by ISR
static volatile u8 uart1_rx_tail;                                               // written by application
static volatile u16 uart1_rx_err;
#endif

#if defined(UART_ENABLE) && defined(UART2_RX_INTERRUPT_ENABLE)

char uart2_receive_buf[UART2_RX_BUF_SIZE];
u8 uart2_receive_len;
u8 uart2_received_flag = 0;
#endif

#if defined(UART_ENABLE) && defined(UART3_RX_INTERRUPT_ENABLE)

char uart3_receive_buf[UART3_RX_BUF_SIZE];
u8 uart3_receive_len;
u8 uart3_received_flag = 0;
#endif

// #ifdef DEBUG_ENABLE
char debug_buff[100];
// #endif

void _sys_exit(s32 x)
{
    x = x;
}
#if ( defined( ENABLE_DEBUG ) || defined( DEBUG_ENABLE ) ) && defined(DEBUG_UART) && defined(UART_ENABLE)
//redefine fputcfunction
s32 fputc(s32 ch, FILE* f)
{
    while((DEBUG_UART->CSR & UART_IT_TXIEN) == 0); //The loop is sent until it is finished
    DEBUG_UART->TDR = (ch & (u16)0x00FF);
    return ch;
}
#endif

void NVIC_UART_ENABLE(void){
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = UART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void NVIC_UART_DISABLE(void){
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = UART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&NVIC_InitStructure);
}


void CONSOLE_Init(void)
{
    //GPIO port set
    GPIO_InitTypeDef GPIO_InitStruct;
    UART_InitTypeDef UART_InitStruct;
    #if defined(UART1_RX_INTERRUPT_ENABLE) || defined(UART1_RX_RING_ENABLE) || defined(UART2_RX_INTERRUPT_ENABLE) || defined(UART3_RX_INTERRUPT_ENABLE)
    NVIC_InitTypeDef NVIC_InitStructure;
    #endif

    #if defined(UART1_RX_INTERRUPT_ENABLE) || defined(UART1_RX_RING_ENABLE)
    //NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = UART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = UART1_INTERRUPT_PRIORITY;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    #endif

    #ifdef UART2_RX_INTERRUPT_ENABLE
    //NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = UART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = UART2_INTERRUPT_PRIORITY;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    #endif

    #ifdef UART3_RX_INTERRUPT_ENABLE
    //NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = UART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = UART3_INTERRUPT_PRIORITY;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    #endif

    #if SELECT_IC == MM32F0140xx
    RCC_APB2PeriphClockCmd(RCC_APB2ENR_UART1, ENABLE);   //enableUART1,GPIOAclock
    RCC_APB1PeriphClockCmd(RCC_APB1ENR_UART2, ENABLE);   //enableUART2,GPIOAclock
    RCC_APB1PeriphClockCmd(RCC_APB1ENR_UART3, ENABLE);   //enableUART2,GPIOAclock
    RCC_AHBPeriphClockCmd(RCC_AHBENR_GPIOA, ENABLE);  //
    RCC_AHBPeriphClockCmd(RCC_AHBENR_GPIOD, ENABLE);  //
    #else
    RCC_APB1PeriphClockCmd(RCC_APB1ENR_UART1, ENABLE);   //enableUART1,GPIOAclock
    RCC_APB1PeriphClockCmd(RCC_APB1ENR_UART2, ENABLE);   //enableUART2,GPIOAclock
    RCC_AHBPeriphClockCmd(RCC_AHBENR_GPIOA, ENABLE);  //
    #endif

    //UART initialset
    #ifdef UART1_TxPinSource
    GPIO_PinAFConfig(UART1_Port, UART1_TxPinSource, UART1_TxPin_AF);
    #endif
    #ifdef UART2_TxPinSource
    GPIO_PinAFConfig(UART2_Port, UART2_TxPinSource, UART2_TxPin_AF);
    #endif
    #ifdef UART3_TxPinSource
    GPIO_PinAFConfig(UART3_Port, UART3_TxPinSource, UART3_TxPin_AF);
    #endif

    #ifdef UART1_RxPinSource
    GPIO_PinAFConfig(UART1_Port, UART1_RxPinSource, UART1_RxPin_AF);
    #endif
    #ifdef UART2_RxPinSource
    GPIO_PinAFConfig(UART2_Port, UART2_RxPinSource, UART2_RxPin_AF);
    #endif
    #ifdef UART3_RxPinSource
    GPIO_PinAFConfig(UART3_Port, UART3_RxPinSource, UART3_RxPin_AF);
    #endif

    UART_StructInit(&UART_InitStruct);
    UART_InitStruct.UART_WordLength = UART_WordLength_8b;
    UART_InitStruct.UART_StopBits = UART_StopBits_1;//one stopbit
    UART_InitStruct.UART_Parity = UART_Parity_No;//none odd-even  verify bit
    UART_InitStruct.UART_HardwareFlowControl = UART_HardwareFlowControl_None;//No hardware flow control
    UART_InitStruct.UART_Mode = UART_Mode_Rx | UART_Mode_Tx; // receive and sent  mode
		
    #ifdef UART1_RX_INTERRUPT_ENABLE
    UART_ITConfig(UART1, UART_IT_RXIEN, ENABLE);
    #endif

    #ifdef UART1_RX_RING_ENABLE
    UART1->ICR = 0xFFFFFFFF;
    UART_ITConfig(UART1, UART_IT_RXIEN, ENABLE);
    UART1->IER |= UART_IER_RXOERR | UART_IER_RXFERR;                           // count line errors
    #endif

    #ifdef UART2_RX_INTERRUPT_ENABLE
    UART_ITConfig(UART2, UART_IT_RXIEN, ENABLE);
    #endif

    #ifdef UART3_RX_INTERRUPT_ENABLE
    UART_ITConfig(UART3, UART_IT_RXIEN, ENABLE);
    #endif

    #ifdef UART1_TxPin
    UART_InitStruct.UART_BaudRate = UART1_BaudRate;
    UART_Init(UART1, &UART_InitStruct); //initial uart 1
    UART_Cmd(UART1, ENABLE);                    //enable uart 1
    #endif

    #ifdef UART2_TxPin
    UART_InitStruct.UART_BaudRate = UART2_BaudRate;
    UART_Init(UART2, &UART_InitStruct); //initial uart 2
    UART_Cmd(UART2, ENABLE);                    //enable uart 2
    #endif

    #ifdef UART3_TxPin
    UART_InitStruct.UART_BaudRate = UART3_BaudRate;
    UART_Init(UART3, &UART_InitStruct); //initial uart 3
    UART_Cmd(UART3, ENABLE);                    //enable uart 3
    #endif

    #ifdef UART1_TxPin
    //UART1_TX   GPIOA.12
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin = UART1_TxPin;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(UART1_Port, &GPIO_InitStruct);
    #endif

    #ifdef UART1_RxPin
//    UART1_RX    GPIOA.3
    GPIO_InitStruct.GPIO_Pin = UART1_RxPin;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(UART1_Port, &GPIO_InitStruct);
    #endif

    #ifdef UART2_TxPin
    //UART2_TX   GPIOA.1
    GPIO_InitStruct.GPIO_Pin = UART2_TxPin;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(UART2_Port, &GPIO_InitStruct);
    #endif

    #ifdef UART2_RxPin
    //UART2_RX    GPIOA.13
    GPIO_InitStruct.GPIO_Pin = UART2_RxPin;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(UART2_Port, &GPIO_InitStruct);
    #endif

    #ifdef UART3_TxPin
    //UART3_TX   GPIOA.1
    GPIO_InitStruct.GPIO_Pin = UART3_TxPin;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(UART3_Port, &GPIO_InitStruct);
    #endif

    #ifdef UART3_RxPin
    //UART3_RX    GPIOA.13
    GPIO_InitStruct.GPIO_Pin = UART3_RxPin;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(UART3_Port, &GPIO_InitStruct);
    #endif

}

#ifdef UART1_RX_INTERRUPT_ENABLE
////////////////////////////////////////////////////////////////////////////////
/// @brief  UART1_IRQHandler function
/// @note   The received data is terminated by a carriage return sign.
/// @param  None.
/// @retval None.
////////////////////////////////////////////////////////////////////////////////
void UART1_IRQHandler(void)
{
    u8 Res;
    if(UART_GetITStatus(UART1, UART_IT_RXIEN)  != RESET) {
        //Receiving interrupts (data received must end at 0x0D 0x0a)
        UART_ClearITPendingBit(UART1, UART_IT_RXIEN);
        // UART1->ICR = UART_IT_RXIEN;
        //read receive data.
        Res = (u16)(UART1->RDR & 0xFFU);
        

        if(Res != '\n'){
            uart1_receive_buf[uart1_receive_len] = (char)Res;
            uart1_receive_len++;
        }else{
            uart1_receive_buf[uart1_receive_len] = '\0';
            uart1_receive_len = 0;
            uart1_received_flag = 1;
        }
	}
}
#endif

#ifdef UART1_RX_RING_ENABLE
////////////////////////////////////////////////////////////////////////////////
/// @brief  UART1_IRQHandler function (ring mode)
/// @note   Binary-safe: every received byte goes into the ring, no delimiter.
///         Line errors and ring overflow are counted, not fatal.
/// @param  None.
/// @retval None.
////////////////////////////////////////////////////////////////////////////////
void UART1_IRQHandler(void)
{
    u32 isr = UART1->ISR;
    u8 next;

    if (isr & UART1_RX_ERR_BITS) {
        UART1->ICR = isr & UART1_RX_ERR_BITS;
        uart1_rx_err++;
    }
    if (isr & UART_ISR_RX) {
        UART1->ICR = UART_ICR_RX;
    }
    while (UART1->CSR & UART_CSR_RXAVL) {
        u8 d = (u8)(UART1->RDR & 0xFFU);
        next = (u8)((uart1_rx_head + 1) & UART1_RX_RING_MASK);
        if (next != uart1_rx_tail) {
            uart1_rx_ring[uart1_rx_head] = d;
            uart1_rx_head = next;
        }
        else {
            uart1_rx_err++;                                                     // ring full, byte dropped
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief  Read one byte from the UART1 RX ring.
/// @param  c: destination.
/// @retval 1 if a byte was returned, 0 if the ring is empty.
////////////////////////////////////////////////////////////////////////////////
u8 uart1_rx_read(u8* c)
{
    u8 t = uart1_rx_tail;
    if (t == uart1_rx_head)
        return 0;
    *c = uart1_rx_ring[t];
    uart1_rx_tail = (u8)((t + 1) & UART1_RX_RING_MASK);
    return 1;
}

u16 uart1_rx_errors(void)
{
    return uart1_rx_err;
}
#endif

#ifdef UART2_RX_INTERRUPT_ENABLE
////////////////////////////////////////////////////////////////////////////////
/// @brief  UART1_IRQHandler function
/// @note   The received data is terminated by a carriage return sign.
/// @param  None.
/// @retval None.
////////////////////////////////////////////////////////////////////////////////
void UART2_IRQHandler(void)
{
    u8 Res;
    if(UART_GetITStatus(UART2, UART_IT_RXIEN)  != RESET) {
        //Receiving interrupts (data received must end at 0x0D 0x0a)
        UART_ClearITPendingBit(UART2, UART_IT_RXIEN);
        // UART1->ICR = UART_IT_RXIEN;
        //read receive data.
        Res = (u16)(UART2->RDR & 0xFFU);
        

        if(Res != '\n'){
            uart2_receive_buf[uart2_receive_len] = (char)Res;
            uart2_receive_len++;
        }else{
            uart2_receive_buf[uart2_receive_len] = '\0';
            uart2_receive_len = 0;
            uart2_received_flag = 1;
        }
	}
}
#endif

#ifdef UART3_RX_INTERRUPT_ENABLE
////////////////////////////////////////////////////////////////////////////////
/// @brief  UART1_IRQHandler function
/// @note   The received data is terminated by a carriage return sign.
/// @param  None.
/// @retval None.
////////////////////////////////////////////////////////////////////////////////
void UART3_IRQHandler(void)
{
    u8 Res;
    if(UART_GetITStatus(UART3, UART_IT_RXIEN)  != RESET) {
        //Receiving interrupts (data received must end at 0x0D 0x0a)
        UART_ClearITPendingBit(UART3, UART_IT_RXIEN);
        // UART1->ICR = UART_IT_RXIEN;
        //read receive data.
        Res = (u16)(UART3->RDR & 0xFFU);        

        if(Res != '\n'){
            uart3_receive_buf[uart3_receive_len++] = (char)Res;
        }else{
            uart3_receive_buf[uart3_receive_len] = '\0';
            uart3_receive_len = 0;
            uart3_received_flag = 1;
        }
	}
}
#endif

void uartSendString(char *str, UART_TypeDef *UARTx)
{
    while(*str) {
        while((UARTx->CSR & UART_IT_TXIEN) == 0); //The loop is sent until it is finished
        UARTx->TDR = (*str & (u16)0x00FF);
        str++;
    }
}

void uartSendByte(u8 ch, UART_TypeDef *UARTx)
{
    while((UARTx->CSR & UART_IT_TXIEN) == 0); //The loop is sent until it is finished
    UARTx->TDR = (ch & (u16)0x00FF);
}

uint8_t uartReceiveByte(UART_TypeDef *UARTx)
{
    while((UARTx->CSR & UART_IT_RXIEN) == 0); //The loop is sent until it is finished
    return (uint8_t)(UARTx->RDR & 0xFFU);
}

uint8_t uartDataAvailable(UART_TypeDef *UARTx)
{
    return (UARTx->CSR & UART_IT_RXIEN);
}

void uartSendGroup(u8 *buf, u8 len, UART_TypeDef *UARTx)
{
    for(u8 i = 0; i < len; i++) {
        uartSendByte(buf[i], UARTx);
    }
}

// Wait until the last byte has fully left the shift register (e.g. before a reset)
void uartWaitTxDone(UART_TypeDef *UARTx)
{
    while((UARTx->CSR & UART_CSR_TXC) == 0);
}


#endif // UART_ENABLE
