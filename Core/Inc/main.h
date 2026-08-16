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
#include "stm32l4xx_hal.h"
#include "stm32l4xx_ll_crs.h"
#include "stm32l4xx_ll_rcc.h"
#include "stm32l4xx_ll_bus.h"
#include "stm32l4xx_ll_system.h"
#include "stm32l4xx_ll_exti.h"
#include "stm32l4xx_ll_cortex.h"
#include "stm32l4xx_ll_utils.h"
#include "stm32l4xx_ll_pwr.h"
#include "stm32l4xx_ll_dma.h"
#include "stm32l4xx_ll_tim.h"
#include "stm32l4xx_ll_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "stm32l4xx_ll_tim.h"

void Tus1(uint32_t status);
void Tus2(uint32_t status);
void Tus3(uint32_t status);
void Tus4(uint32_t status);
void Tus5(uint32_t status);

void Capture_Callback(TIM_TypeDef *TIMx);
void Timeout_Callback(TIM_TypeDef *TIMx);
void Adr_Reset(void);


/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

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

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define IN5_Pin LL_GPIO_PIN_14
#define IN5_GPIO_Port GPIOC
#define UART_DE_Pin LL_GPIO_PIN_1
#define UART_DE_GPIO_Port GPIOA
#define UART_TX_Pin LL_GPIO_PIN_2
#define UART_TX_GPIO_Port GPIOA
#define UART_RX_Pin LL_GPIO_PIN_3
#define UART_RX_GPIO_Port GPIOA
#define ROLE2_Pin LL_GPIO_PIN_4
#define ROLE2_GPIO_Port GPIOA
#define ROLE1_Pin LL_GPIO_PIN_5
#define ROLE1_GPIO_Port GPIOA
#define IN1_Pin LL_GPIO_PIN_6
#define IN1_GPIO_Port GPIOA
#define IN2_Pin LL_GPIO_PIN_7
#define IN2_GPIO_Port GPIOA
#define IN3_Pin LL_GPIO_PIN_0
#define IN3_GPIO_Port GPIOB
#define IN4_Pin LL_GPIO_PIN_1
#define IN4_GPIO_Port GPIOB
#define DALI_RX_Pin LL_GPIO_PIN_8
#define DALI_RX_GPIO_Port GPIOA
#define DALI_TX_Pin LL_GPIO_PIN_9
#define DALI_TX_GPIO_Port GPIOA
#define ROLE5_Pin LL_GPIO_PIN_10
#define ROLE5_GPIO_Port GPIOA
#define ROLE4_Pin LL_GPIO_PIN_11
#define ROLE4_GPIO_Port GPIOA
#define ROLE3_Pin LL_GPIO_PIN_12
#define ROLE3_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
