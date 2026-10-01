
// Define to prevent recursive inclusion
#ifndef __LED_H
#define __LED_H

// Files includes
#include <string.h>

#include "mm32_device.h"
#include "hal_conf.h"
#include "device_config.h"

#ifdef LED_ENABLE

//led structure
typedef struct {
    u8 size;
    GPIO_TypeDef* port[LED_COUNT+1];
    u16 pin[LED_COUNT+1];
    u8 state[LED_COUNT+1];
} leds_t;


#define LED1_OFF()  (LED1_Port->BRR = LED1_Pin)
#define LED1_ON()  (LED1_Port->BSRR = LED1_Pin)
#define LED1_TOGGLE()  (LED1_Port->ODR ^= LED1_Pin)

#ifdef LED2_Pin
#define LED2_OFF() (LED2_Port->BRR = LED2_Pin)
#define LED2_ON() (LED2_Port->BSRR = LED2_Pin)
#define LED2_TOGGLE() (LED2_Port->ODR ^= LED2_Pin)
#endif

#ifdef LED3_Pin
#define LED3_OFF() (LED3_Port->BRR = LED3_Pin)
#define LED3_ON() (LED3_Port->BSRR = LED3_Pin)
#define LED3_TOGGLE() (LED3_Port->ODR ^= LED3_Pin)
#endif

#ifdef LED4_Pin
#define LED4_OFF() (LED4_Port->BRR = LED4_Pin)
#define LED4_ON() (LED4_Port->BSRR = LED4_Pin)
#define LED4_TOGGLE() (LED4_Port->ODR ^= LED4_Pin)
#endif

#ifdef LED5_Pin
#define LED5_OFF() (LED5_Port->BRR = LED5_Pin)
#define LED5_ON() (LED5_Port->BSRR = LED5_Pin)
#define LED5_TOGGLE() (LED5_Port->ODR ^= LED5_Pin)
#endif

#ifdef LED6_Pin
#define LED6_OFF() (LED6_Port->BRR = LED6_Pin)
#define LED6_ON() (LED6_Port->BSRR = LED6_Pin)
#define LED6_TOGGLE() (LED6_Port->ODR ^= LED6_Pin)
#endif

#ifdef LED7_Pin
#define LED7_OFF() (LED7_Port->BRR = LED7_Pin)
#define LED7_ON() (LED7_Port->BSRR = LED7_Pin)
#define LED7_TOGGLE() (LED7_Port->ODR ^= LED7_Pin)
#endif

#ifdef LED8_Pin
#define LED8_OFF() (LED8_Port->BRR = LED8_Pin)
#define LED8_ON() (LED8_Port->BSRR = LED8_Pin)
#define LED8_TOGGLE() (LED8_Port->ODR ^= LED8_Pin
#endif

#ifdef LED9_Pin
#define LED9_OFF() (LED9_Port->BRR = LED9_Pin)
#define LED9_ON() (LED9_Port->BSRR = LED9_Pin)
#define LED9_TOGGLE() (LED9_Port->ODR ^= LED9_Pin)
#endif



#define LED1_GET() (LED1_Port->ODR & LED1_Pin)

#ifdef LED2_Pin
#define LED2_GET() (LED2_Port->ODR & LED2_Pin)
#endif

#ifdef LED3_Pin
#define LED3_GET() (LED3_Port->ODR & LED3_Pin)
#endif

#ifdef LED4_Pin
#define LED4_GET() (LED4_Port->ODR & LED4_Pin)
#endif
#define LED1_GET() (LED1_Port->ODR & LED1_Pin)

#ifdef LED2_Pin
#define LED2_GET() (LED2_Port->ODR & LED2_Pin)
#endif

#ifdef LED3_Pin
#define LED3_GET() (LED3_Port->ODR & LED3_Pin)
#endif

#ifdef LED4_Pin
#define LED4_GET() (LED4_Port->ODR & LED4_Pin)
#endif

#ifdef LED5_Pin
#define LED5_GET() (LED5_Port->ODR & LED5_Pin)
#endif

#ifdef LED6_Pin
#define LED6_GET() (LED6_Port->ODR & LED6_Pin)
#endif

#ifdef LED7_Pin
#define LED7_GET() (LED7_Port->ODR & LED7_Pin)
#endif

#ifdef LED8_Pin
#define LED8_GET() (LED8_Port->ODR & LED8_Pin)
#endif

#ifdef LED9_Pin
#define LED9_GET() (LED9_Port->ODR & LED9_Pin)
#endif
/// @}

////////////////////////////////////////////////////////////////////////////////
/// @defgroup MM32_Exported_Enumeration
/// @{

////////////////////////////////////////////////////////////////////////////////
/// @brief XXXX enumerate definition.
/// @anchor XXXX
////////////////////////////////////////////////////////////////////////////////
typedef enum {
    LED1,
    LED2,
    LED3,
    LED4
} Led_TypeDef;


/// @}

////////////////////////////////////////////////////////////////////////////////
/// @defgroup MM32_Exported_Variables
/// @{
#ifdef _LED_C_
#define GLOBAL

#else
#define GLOBAL extern

#endif

#undef GLOBAL

/// @}


////////////////////////////////////////////////////////////////////////////////
/// @defgroup MM32_Exported_Functions
/// @{

void LED_Init(void);
void setRelayStatusById(u8 relayid, u8 status, u8 save);
u8 getRelayStatusById(u8 relayid);
void LED_SetStatusAll(u8 status);
void LED_SetStatusById(u8 ledid, u8 status, u8 save);
void LED_ToggleById(u8 ledid, bool save);

/// @}


/// @}

/// @}


////////////////////////////////////////////////////////////////////////////////
#endif
#endif
////////////////////////////////////////////////////////////////////////////////
