#include "mm32_device.h"
#include "hal_conf.h"
#include "device_config.h"

//prototypes
void gpio_init(GPIO_TypeDef *GPIOx, uint16_t pin, GPIOMode_TypeDef mode, GPIOSpeed_TypeDef speed);
void gpio_af_init(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, uint16_t GPIO_Mode, uint16_t GPIO_Speed, uint16_t GPIO_AF);

