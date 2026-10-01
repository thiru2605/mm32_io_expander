#include "gpios.h"

void gpio_init(GPIO_TypeDef *GPIOx, uint16_t pin, GPIOMode_TypeDef mode, GPIOSpeed_TypeDef speed){
    GPIO_InitTypeDef GPIO_InitStructure;
    if(GPIOx==GPIOA) RCC_AHBPeriphClockCmd(RCC_AHBENR_GPIOA, ENABLE);
    else if(GPIOx==GPIOB) RCC_AHBPeriphClockCmd(RCC_AHBENR_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = pin;
    GPIO_InitStructure.GPIO_Speed = speed;
    GPIO_InitStructure.GPIO_Mode = mode;
    GPIO_Init(GPIOx, &GPIO_InitStructure);
}
