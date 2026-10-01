
#include "led.h"
#include "flash.h"
#include "uart.h"
#include "stdio.h"
#include "gpios.h"
#include "delay.h"

#ifdef LED_ENABLE

leds_t leds;

#if (defined(ENABLE_SW_NODE_4) || defined(ENABLE_SW_NODE_1)) || defined(MODULAR_TOUCH_FAN) || defined(SW_1_NODE) || defined(ENERGY_METER_BUTTON)
extern u8 relaystatusbuf[10];
char printBuff[50];
extern bool save_state;
extern int save_lastTime;
#endif
////////////////////////////////////////////////////////////////////////////////
/// @brief  initialize LED GPIO pin
/// @note   if use jtag/swd interface GPIO PIN as LED, need to be careful,
///         can not debug or program.
/// @param  None.
/// @retval None.
////////////////////////////////////////////////////////////////////////////////
void LED_Init(void)
{
    leds.size = LED_COUNT;
    if(leds.size <=0) return;
    #ifdef LED1_Pin
    leds.port[0] = LED1_Port;
    leds.pin[0] = LED1_Pin;
    #endif
    #ifdef LED2_Pin
    leds.port[1] = LED2_Port;
    leds.pin[1] = LED2_Pin;
    #endif
    #ifdef LED3_Pin
    leds.port[2] = LED3_Port;
    leds.pin[2] = LED3_Pin;
    #endif
    #ifdef LED4_Pin
    leds.port[3] = LED4_Port;
    leds.pin[3] = LED4_Pin;
    #endif
    #ifdef LED5_Pin
    leds.port[4] = LED5_Port;
    leds.pin[4] = LED5_Pin;
    #endif
    #ifdef LED6_Pin
    leds.port[5] = LED6_Port;
    leds.pin[5] = LED6_Pin;
    #endif
    #ifdef LED7_Pin
    leds.port[6] = LED7_Port;
    leds.pin[6] = LED7_Pin;
    #endif
    #ifdef LED8_Pin
    leds.port[7] = LED8_Port;
    leds.pin[7] = LED8_Pin;
    #endif
    #ifdef LED9_Pin
    leds.port[8] = LED9_Port;
    leds.pin[8] = LED9_Pin;
    #endif
    #ifdef LED10_Pin
    leds.port[9] = LED10_Port;
    leds.pin[9] = LED10_Pin;
    #endif
    for(u8 i = 0; i < leds.size; i++){
        gpio_init(leds.port[i], leds.pin[i], GPIO_Mode_Out_PP, GPIO_Speed_50MHz);
    }
}


void LED_SetStatusAll(u8 status){
    for(u8 i = 0; i < leds.size; i++){
        if(status == 0){
            leds.state[i] = 0;
            // GPIO_ResetBits(leds.port[i], leds.pin[i]);
            leds.port[i]->BRR = leds.pin[i];
        }else{
            leds.state[i] = 1;
            // GPIO_SetBits(leds.port[i], leds.pin[i]);
            leds.port[i]->BSRR = leds.pin[i];
        }
        #if defined(FLASH_EEPROM_ENABLE) && !defined(MODULAR_TOUCH_FAN) && (defined(ENABLE_SW_NODE_4) || defined(ENABLE_SW_NODE_1) || defined(ENERGY_METER_BUTTON))
        relaystatusbuf[i] = leds.state[i];
        // Write_Settings();
        // EEPROM_WriteRelayStatus();
        #endif         
    }
    #ifdef FLASH_EEPROM_ENABLE
    Write_Settings();
    #endif
}


void LED_SetStatusById(u8 ledid, u8 status, u8 save){
    #if BCK_DESIGN
    if(ledid == LED_COUNT - 2){
        if(status == 0){
            leds.state[ledid] = 0;
            // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BRR = leds.pin[ledid];
            leds.port[ledid+1]->BRR = leds.pin[ledid+1];
        }else{
            leds.state[ledid] = 1;
            // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BSRR = leds.pin[ledid];
            leds.port[ledid+1]->BSRR = leds.pin[ledid+1];
        }
    }
    else{
        if(status == 0){
            leds.state[ledid] = 0;
            // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BRR = leds.pin[ledid];
        }else{
            leds.state[ledid] = 1;
            // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BSRR = leds.pin[ledid];
        }
    }
    #else
    if(status == 0){
        leds.state[ledid] = 0;
        // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[ledid]->BRR = leds.pin[ledid];
    }else{
        leds.state[ledid] = 1;
        // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[ledid]->BSRR = leds.pin[ledid];
    }
    #endif

    #ifdef FLASH_EEPROM_ENABLE
    if(save){
        #if (defined(ENABLE_SW_NODE_4) || defined(ENABLE_SW_NODE_1))
        relaystatusbuf[ledid] = leds.state[ledid];
        #endif
        Write_Settings();
        // EEPROM_WriteRelayStatus();
    }
    #endif
}


void LED_ToggleById(u8 ledid, bool save){
    if(ledid > leds.size) return;
    #if BCK_DESIGN
    if((LED_COUNT - 2) == ledid){
        if(leds.state[ledid] == 0){
            leds.state[ledid] = 1;
            // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BSRR = leds.pin[ledid];
            leds.port[ledid+1]->BSRR = leds.pin[ledid+1];
        }else{
            leds.state[ledid] = 0;
            // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BRR = leds.pin[ledid];
            leds.port[ledid+1]->BRR = leds.pin[ledid+1];
        }
    } else {
        if(leds.state[ledid] == 0){
            leds.state[ledid] = 1;
            // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BSRR = leds.pin[ledid];
        }else{
            leds.state[ledid] = 0;
            // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
            leds.port[ledid]->BRR = leds.pin[ledid];
        }
    }
    #else
    if(leds.state[ledid] == 0){
        leds.state[ledid] = 1;
        // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[ledid]->BSRR = leds.pin[ledid];
    }else{
        leds.state[ledid] = 0;
        // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[ledid]->BRR = leds.pin[ledid];
    }
    #endif

    #ifdef FLASH_EEPROM_ENABLE
    #if (defined(ENABLE_SW_NODE_4) || defined(ENABLE_SW_NODE_1))
    relaystatusbuf[ledid] = leds.state[ledid];
    #endif
    if(save){
        Write_Settings();
    }
    // EEPROM_WriteRelayStatus();
    #endif    
}


#if ENABLE_SW_NODE
void setRelayStatusById(u8 relayid, u8 status, u8 save){
    if(status){
        leds.state[relayid-1] = 1;
        // GPIO_SetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[relayid-1]->BSRR = leds.pin[relayid-1];
    }else{
        leds.state[relayid-1] = 0;
        // GPIO_ResetBits(leds.port[ledid-1], leds.pin[ledid-1]);
        leds.port[relayid-1]->BRR = leds.pin[relayid-1];
    }

	#ifdef UART_ENABLE
    printf("relay %d %d\r\n", relayid, status);
    // uartSendString(printBuff);
	#endif
    if(save == 0) return;

    #ifdef FLASH_EEPROM_ENABLE
    relaystatusbuf[relayid-1] = leds.state[relayid-1];
    save_state = 1;
    save_lastTime = millis();
    #endif
}

u8 getRelayStatusById(u8 relayid){

    return leds.state[relayid-1];
}

#endif
/// @}

/// @}

/// @}

#endif

