/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdbool.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef enum{
  ME_EEPROM_READ = 0,
  ME_EEPROM_WRITE,
  ME_EEPROM_ERASE,

  ME_MAX
}E_ERROR_CODES;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void vM_SetError(E_ERROR_CODES eError);
void vM_ClearError(E_ERROR_CODES eError);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RELAY_Pin GPIO_PIN_7
#define RELAY_GPIO_Port GPIOA
#define LED_ERR_Pin GPIO_PIN_7
#define LED_ERR_GPIO_Port GPIOE
#define SW2_Pin GPIO_PIN_9
#define SW2_GPIO_Port GPIOE
#define LED2_Pin GPIO_PIN_11
#define LED2_GPIO_Port GPIOE
#define SW1_Pin GPIO_PIN_13
#define SW1_GPIO_Port GPIOE
#define LED1_Pin GPIO_PIN_15
#define LED1_GPIO_Port GPIOE
#define SW0_Pin GPIO_PIN_11
#define SW0_GPIO_Port GPIOB
#define LED0_Pin GPIO_PIN_13
#define LED0_GPIO_Port GPIOB
#define BTN_Pin GPIO_PIN_15
#define BTN_GPIO_Port GPIOB
#define W1_OUT_Pin GPIO_PIN_0
#define W1_OUT_GPIO_Port GPIOE
#define W1_IN_Pin GPIO_PIN_1
#define W1_IN_GPIO_Port GPIOE
#define W1_IN_EXTI_IRQn EXTI1_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
