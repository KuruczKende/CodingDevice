/*
 * UI.c
 *
 *  Created on: Jun 28, 2026
 *      Author: kuruc
 */

#include "UI.h"
#include "OneWireCD.h"

#define TIMEOUT         (1000)

static uint8_t u8Cnt;
static uint32_t u32LastPress;

void vUI_Init(void){
  u8Cnt = 1;
  u32LastPress = 0;
}

void vUI_Process(void){
  u8Cnt--;
  if (u8Cnt!=0)
    return;

  // Check button
  if (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin) == GPIO_PIN_RESET){
    if(HAL_GetTick() - u32LastPress > TIMEOUT){
      u32LastPress = HAL_GetTick();
      // Set active to switch value
      uint8_t u8Switch;
      vUI_ReadSwitch(&u8Switch);
      boOWCD_WriteActive(u8Switch);
    }
  }
}

void vUI_ReadSwitch(uint8_t* pu8Switch){
  *pu8Switch = 0;
  if (HAL_GPIO_ReadPin(SW0_GPIO_Port, SW0_Pin) == GPIO_PIN_SET) *pu8Switch |= 0x04;
  if (HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin) == GPIO_PIN_SET) *pu8Switch |= 0x02;
  if (HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin) == GPIO_PIN_SET) *pu8Switch |= 0x01;
}

void vWriteLed(const uint8_t u8Leds){
  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, ((u8Leds & 0x04) == 0)?GPIO_PIN_RESET:GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, ((u8Leds & 0x02) == 0)?GPIO_PIN_RESET:GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, ((u8Leds & 0x01) == 0)?GPIO_PIN_RESET:GPIO_PIN_SET);

  HAL_GPIO_WritePin(LED_ERR_GPIO_Port, LED_ERR_Pin, ((u8Leds & 0x08) == 0)?GPIO_PIN_RESET:GPIO_PIN_SET);
}

void vUI_WriteRelay(const bool boOn){
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, boOn?GPIO_PIN_SET:GPIO_PIN_RESET);
}

bool boUI_ReadRelay(void){
  return (HAL_GPIO_ReadPin(RELAY_GPIO_Port, RELAY_Pin) == GPIO_PIN_SET);
}
