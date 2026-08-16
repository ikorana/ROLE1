/* USER CODE BEGIN Header */

    #include <sys/types.h>
    #include <time.h>

    /*
      5 adet  lamba adresler. 
      
      5. Tuş Lamba Aç Kapat
         **** Uzun Basma Adresleri SIL
            ---------
      RS485 destegi var. Komutlar:
        {"com":"adres","dev":xx,"adr":yy}
           dev : 1 Lamba
                 2 Lamba 
                 3 Lamba
                 4 Lamba
                 5 Lamba
           adr : 0-63 veya 255 (adresi sil)
       
        {"com":"status","dev":xx,"durum":yy}  
           dev : 1-5
           durum :   0 Close 
                     1 Open 
                     2 Toggle                                 
            --------
    Created Version 2.0
      Tarih : 16.05.2025

    -----------------------------------

    TIM1 DaliRX Timer 1us
    TIM16 Dali Delay Timer 1us
    TIM15 Dali Twice timer (one pulse) 100ms 
    Tim6 Sleep Timer 100ms

    Tim2  Global Timer 10ms taşma
    */

/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/*.   ------------- ONEMLI ---------------
     İşlemci normalde ramden çalışmak için programlanmış. Boot0 pinini (31 nolu pin)
     10k ile GND ye çekmediğimizden start ramden yapılıyor. BOOT0 pinini fiziksel 
     GND ye çekmemek için OPTION BYTE üzerinden SWBOOT0 ı uncheck yapıyoruz. 
     Bunun için PRG programı OB sekmesi User configde nSWBOOT0 ın checkini kaldırıp
     kaydediyoruz. 
*/

#include "string.h"
#include "dali_cln03.h"
#include "eprom.h"
#include "stdio.h"
#include "dali_tool.h"
#include "relay.h"
#include "stm32l4xx_ll_gpio.h"
#include "jsmn.h"
#include "jsmn_tool.c"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RX_BUFFER_SIZE 512
uint8_t uart2_rx_buffer[RX_BUFFER_SIZE];

DALI_t dali;
volatile DALI_Address_t AdresList[DALI_ADDRESS_COUNT] = {0};
Relay_t relays[5];

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;

/* USER CODE BEGIN PV */
volatile bool tus1_active = false;
volatile bool tus1_manual = false;

volatile bool tus2_active = false;
volatile bool tus2_manual = false;

volatile bool tus3_active = false;
volatile bool tus3_manual = false;

volatile bool tus4_active = false;
volatile bool tus4_manual = false;

volatile uint16_t tus1_counter = 0, tus2_counter = 0, tus3_counter = 0, tus4_counter = 0, tus5_counter = 0;

static uint16_t btn_debounce_timers[5] = {50, 50, 50, 50, 50};
static uint8_t  btn_last_raw_states[5] = {0};
volatile bool tus5_active = false;
volatile bool tus5_reset_triggered = false;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM15_Init(void);
static void MX_TIM16_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif
PUTCHAR_PROTOTYPE {
    uint8_t temp = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &temp, 1, HAL_MAX_DELAY);
    return ch;
}

void Capture_Callback(TIM_TypeDef *TIMx)
{
   dali.capture_handle(&dali,TIMx);
}

void Button_Filter_Update(void) {
    uint8_t raw[5];
    // Pin okumalarını diziye al (Logic inverse: !LL_GPIO...)
    raw[0] = !LL_GPIO_IsInputPinSet(IN4_GPIO_Port, IN4_Pin); // Tus1
    raw[1] = !LL_GPIO_IsInputPinSet(IN3_GPIO_Port, IN3_Pin); // Tus2
    raw[2] = !LL_GPIO_IsInputPinSet(IN2_GPIO_Port, IN2_Pin); // Tus3
    raw[3] = !LL_GPIO_IsInputPinSet(IN1_GPIO_Port, IN1_Pin); // Tus4
    raw[4] = !LL_GPIO_IsInputPinSet(IN5_GPIO_Port, IN5_Pin); // Tus5

    for (int i = 0; i < 5; i++) {
        if (raw[i] != btn_last_raw_states[i]) {
            btn_debounce_timers[i] = 0;
            btn_last_raw_states[i] = raw[i];
        } else {
            if (btn_debounce_timers[i] < 50) { // 500ms filtre (50 * 10ms)
                if (++btn_debounce_timers[i] == 50) {
                    // Kararlı duruma ulaşıldı, ilgili fonksiyonu tetikle
                    switch(i) {
                        case 0: Tus1(raw[i]); break;
                        case 1: Tus2(raw[i]); break;
                        case 2: Tus3(raw[i]); break;
                        case 3: Tus4(raw[i]); break;
                        case 4: Tus5(raw[i]); break;
                    }
                }
            }
        }
    }
}

void Timeout_Callback(TIM_TypeDef *TIMx)
{
   if (TIMx!= TIM2) {
    dali.timeout_handle(&dali,TIMx); 
  } else {
    // TIM2 10ms Interrupt
    for (int i=0;i<5;i++) {
      Relay_HandleTIMER(&relays[i]);
    }

    Button_Filter_Update();

  
    // Tus 5 Uzun Basım (Adres Reset) Kontrolü (5 saniye)
    if (tus5_active) {
      if (++tus5_counter >= 500) { // 5 saniye (500 * 10ms)
        if (!tus5_reset_triggered) {
          tus5_reset_triggered = true;
          Adr_Reset();
          // Opsiyonel: Reset başarılı bildirimi için röleyi toggle yapabilirsiniz
          // Relay_Toggle(&relay); 
        }
      }
    }
  } 
}

void Adr_Reset(void)
{
    LL_GPIO_SetOutputPin(ROLE1_GPIO_Port, ROLE1_Pin);    
    for (int i = 0; i < DALI_ADDRESS_COUNT; i++) {
      AdresList[i].short_address = 0xff; 
    }
    write_eeprom(AdresList);
    for (int i = 0; i < 10; i++) {
      LL_GPIO_TogglePin(ROLE1_GPIO_Port, ROLE1_Pin);
      LL_mDelay(200);     
    }
  NVIC_SystemReset(); //

}


void DaliDataCallback(uint32_t rxdata, uint8_t bit)
{
    if (bit == 16) {
        // 16 bitlik veri: [Adres Byte (8 bit)][Veri Byte (8 bit)]
        uint8_t addr_byte = (uint8_t)((rxdata >> 8) & 0xFF);
        uint8_t data_byte = (uint8_t)(rxdata & 0xFF);
        
        DALI_DecodedAddr_t addr = DALI_Decode_Address(addr_byte);

        if (addr.type==DALI_ADDR_SPECIAL) {
           if (addr.value==0xA7) { //RANDOMISE
              //uint32_t radr = (my_rand() & 0xFFFFFF);
              uint32_t radr = (my_rand() & 0xFFFFFF);
              for (int i = 0; i < 5; i++) {
                if (relays[i].is_init) {
                    relays[i].config->random_address = radr;
                    radr++;
                }             
              }
              return;
          }
        }

        // Tüm Blindlere gelen paketi kontrol etmesi için gönder
        for (int i = 0; i < 5; i++) {
            uint8_t kk=Relay_HandleDALI(&relays[i], addr, data_byte);
            if (kk==1) break;
        }
    } else if (bit == 8) {
        // Genellikle cihazdan gelen cevap (Backward Frame)
        printf("Geri Bildirim (Backward Frame): 0x%02lX\n", rxdata);
    }
}

void DaliErrorCallback(uint8_t err)
{
  if (err==0 || err==1) {
    if (err==1) {
        LL_GPIO_SetOutputPin(GPIOA, ROLE5_Pin);
    } else {  
        LL_GPIO_ResetOutputPin(GPIOA, ROLE5_Pin);
    }
  }

}

void DaliSaveCallback(void)
{
  write_eeprom(AdresList);
}

#define MAX_TOKEN 32

void command_process(char * data)
{
    jsmn_parser p;
    jsmn_init(&p);
    jsmntok_t tokens[MAX_TOKEN];
    jsmn_init(&p);
    int r = jsmn_parse(&p, data, strlen(data), tokens, MAX_TOKEN);    
    if (r>0) {
        char command[25];
        if (json_get_value(data, tokens, r, "com", command, sizeof(command)) == 0) {
              if (strcmp(command,"adres")==0) {
                uint8_t dev=0xFF, adr=0xFF;
                json_get_int(data, tokens, r,"dev",&dev);
                json_get_int(data, tokens, r,"adr",&adr);
                if (dev>0 && dev<4) {
                  if (adr<64 || adr==0xFF) {
                    AdresList[dev-1].short_address = adr;
                    write_eeprom(AdresList);
                  }
                }
              }

              if (strcmp(command,"status")==0) 
              {
                uint8_t dev=0xFF, dur=0xFF;
                json_get_int(data, tokens, r,"dev",&dev);
                json_get_int(data, tokens, r,"durum",&dur);
                if (dev>0 && dev<6) {
                  if (dur==0) Relay_Off(&relays[dev-1]);
                  if (dur==1) Relay_On(&relays[dev-1]);
                  if (dur==2) Relay_Toggle(&relays[dev-1]);
                }
              }
        }        
    }
      
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_TIM6_Init();
  MX_TIM15_Init();
  MX_TIM16_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

  my_srand(HAL_GetTick());
  DALI_Timer_t d1t = {TIM1, LL_TIM_CHANNEL_CH1};
  DALI_Pin_t d1Rx = {DALI_RX_GPIO_Port,DALI_RX_Pin};
  DALI_Pin_t d1Tx = {DALI_TX_GPIO_Port,DALI_TX_Pin};

  //HAL_IWDG_Refresh(&hiwdg);

  DALI_Init(&dali,
      d1t,
      TIM16,
      TIM15,
      TIM6,
      d1Rx,d1Tx,
      USART2_IRQn
    );
  
  read_eeprom(AdresList);
  for (uint8_t i = 0; i < DALI_ADDRESS_COUNT; i++) {   
    // AdresList[i].short_address = 20+i; 
      AdresList[i].device_type = 0x07;
      AdresList[i].next_device_type = 0x07; 
  }

  // Adres Listesi ile eşleştirerek başlat
  Relay_Init(&relays[0], ROLE1_GPIO_Port, ROLE1_Pin,&AdresList[0], &dali, DaliSaveCallback);
  Relay_Init(&relays[1], ROLE2_GPIO_Port, ROLE2_Pin,&AdresList[1], &dali, DaliSaveCallback);
  Relay_Init(&relays[2], ROLE3_GPIO_Port, ROLE3_Pin,&AdresList[2], &dali, DaliSaveCallback);
  Relay_Init(&relays[3], ROLE4_GPIO_Port, ROLE4_Pin,&AdresList[3], &dali, DaliSaveCallback);
  Relay_Init(&relays[4], ROLE5_GPIO_Port, ROLE5_Pin,&AdresList[4], &dali, DaliSaveCallback);


  DALI_Set_DataCallback(&dali,DaliDataCallback);
  DALI_Set_ErrorCallback(&dali,DaliErrorCallback);

  uint16_t flag = dali.flags.hat_error_flag;
  DaliErrorCallback((uint8_t)flag);

  // DMA üzerinden veri alımını başlat
  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, uart2_rx_buffer, sizeof(uart2_rx_buffer));

  Relay_On(&relays[0]);
  HAL_Delay(500);
  Relay_Off(&relays[0]);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  
  LL_TIM_EnableIT_UPDATE(TIM2);
  LL_TIM_EnableCounter(TIM2);

  while (1)
  {
 //   HAL_IWDG_Refresh(&hiwdg);
    dali.loop(&dali);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_2)
  {
  }
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  while (LL_PWR_IsActiveFlag_VOS() != 0)
  {
  }
  LL_RCC_MSI_Enable();

   /* Wait till MSI is ready */
  while(LL_RCC_MSI_IsReady() != 1)
  {

  }
  LL_RCC_MSI_EnableRangeSelection();
  LL_RCC_MSI_SetRange(LL_RCC_MSIRANGE_11);
  LL_RCC_MSI_SetCalibTrimming(0);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_MSI);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_MSI)
  {

  }
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_SetSystemCoreClock(48000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);

  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
  /**TIM1 GPIO Configuration
  PA8   ------> TIM1_CH1
  */
  GPIO_InitStruct.Pin = DALI_RX_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_1;
  LL_GPIO_Init(DALI_RX_GPIO_Port, &GPIO_InitStruct);

  /* TIM1 interrupt Init */
  NVIC_SetPriority(TIM1_BRK_TIM15_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM1_BRK_TIM15_IRQn);
  NVIC_SetPriority(TIM1_UP_TIM16_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
  NVIC_SetPriority(TIM1_CC_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM1_CC_IRQn);

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  TIM_InitStruct.Prescaler = 47;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 5000;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  TIM_InitStruct.RepetitionCounter = 0;
  LL_TIM_Init(TIM1, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM1);
  LL_TIM_SetClockSource(TIM1, LL_TIM_CLOCKSOURCE_INTERNAL);
  LL_TIM_SetTriggerOutput(TIM1, LL_TIM_TRGO_RESET);
  LL_TIM_SetTriggerOutput2(TIM1, LL_TIM_TRGO2_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM1);
  LL_TIM_IC_SetActiveInput(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_ACTIVEINPUT_DIRECTTI);
  LL_TIM_IC_SetPrescaler(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_ICPSC_DIV1);
  LL_TIM_IC_SetFilter(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_IC_FILTER_FDIV16_N5);
  LL_TIM_IC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_BOTHEDGE);
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

  /* TIM2 interrupt Init */
  NVIC_SetPriority(TIM2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(TIM2_IRQn);

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  TIM_InitStruct.Prescaler = 47;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 9999;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  LL_TIM_Init(TIM2, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM2);
  LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
  LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM2);
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM6);

  /* TIM6 interrupt Init */
  NVIC_SetPriority(TIM6_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM6_IRQn);

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  TIM_InitStruct.Prescaler = 479;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 9999;
  LL_TIM_Init(TIM6, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM6);
  LL_TIM_SetTriggerOutput(TIM6, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM6);
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM15 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM15_Init(void)
{

  /* USER CODE BEGIN TIM15_Init 0 */

  /* USER CODE END TIM15_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM15);

  /* TIM15 interrupt Init */
  NVIC_SetPriority(TIM1_BRK_TIM15_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM1_BRK_TIM15_IRQn);

  /* USER CODE BEGIN TIM15_Init 1 */

  /* USER CODE END TIM15_Init 1 */
  TIM_InitStruct.Prescaler = 479;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 9999;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  TIM_InitStruct.RepetitionCounter = 0;
  LL_TIM_Init(TIM15, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM15);
  LL_TIM_SetClockSource(TIM15, LL_TIM_CLOCKSOURCE_INTERNAL);
  LL_TIM_SetOnePulseMode(TIM15, LL_TIM_ONEPULSEMODE_SINGLE);
  LL_TIM_SetTriggerOutput(TIM15, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM15);
  /* USER CODE BEGIN TIM15_Init 2 */

  /* USER CODE END TIM15_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM16);

  /* TIM16 interrupt Init */
  NVIC_SetPriority(TIM1_UP_TIM16_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  TIM_InitStruct.Prescaler = 47;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 65535;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  TIM_InitStruct.RepetitionCounter = 0;
  LL_TIM_Init(TIM16, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM16);
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 28800;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_RS485Ex_Init(&huart2, UART_DE_POLARITY_HIGH, 0, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, ROLE2_Pin|ROLE1_Pin|ROLE5_Pin|ROLE4_Pin
                          |ROLE3_Pin);

  /**/
  LL_GPIO_SetOutputPin(DALI_TX_GPIO_Port, DALI_TX_Pin);

  /**/
  GPIO_InitStruct.Pin = IN5_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(IN5_GPIO_Port, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = ROLE2_Pin|ROLE1_Pin|ROLE5_Pin|ROLE4_Pin
                          |ROLE3_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = IN1_Pin|IN2_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = IN3_Pin|IN4_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = DALI_TX_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(DALI_TX_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void Tus1(uint32_t status){
  if (status == 1) { // Tuşa basıldı
    tus1_active = true;
    tus1_counter = 0;
    tus1_manual = false;
  } else { // Tuş bırakıldı
    if (tus1_active) {
      if (!tus1_manual) {
        // Uzun basımdaysa bırakıldığı an durdur
        Relay_Toggle(&relays[0]);
      } 
    }
    tus1_active = false;
  }
}
void Tus2(uint32_t status){
  if (status == 1) { // Tuşa basıldı
    tus2_active = true;
    tus2_counter = 0;
    tus2_manual = false;
  } else { // Tuş bırakıldı
    if (tus2_active) {
      if (!tus2_manual) {
        // Uzun basımdaysa bırakıldığı an durdur
        Relay_Toggle(&relays[1]);
      } 
    }
    tus2_active = false;
  }
}
void Tus3(uint32_t status){
  if (status == 1) { // Tuşa basıldı
    tus3_active = true;
    tus3_counter = 0;
    tus3_manual = false;
  } else { // Tuş bırakıldı
    if (tus3_active) {
      if (!tus3_manual) {
        // Uzun basımdaysa bırakıldığı an durdur
        Relay_Toggle(&relays[2]);
      }
    }
    tus3_active = false;
  }
}
void Tus4(uint32_t status){
  if (status == 1) { // Tuşa basıldı
    tus4_active = true;
    tus4_counter = 0;
    tus4_manual = false;
  } else { // Tuş bırakıldı
    if (tus4_active) {
      if (!tus4_manual) {
        // Uzun basımdaysa bırakıldığı an durdur
        Relay_Toggle(&relays[3]);
      }
    }
    tus4_active = false;
  }
}
void Tus5(uint32_t status){
  if (status == 1) { // Tuşa basıldı
    tus5_active = true;
    tus5_counter = 0;
    tus5_reset_triggered = false;
  } else { // Tuş bırakıldı
    if (tus5_active && !tus5_reset_triggered) {
      // Eğer 5 saniye dolmadan bırakıldıysa kısa basım kabul et
      Relay_Toggle(&relays[4]);
    }
    tus5_active = false;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    //Uartta hata oluşur ve flaglar temizlenmez ise uart kilitlenir. 
    //Bu nedenle Uart4 de hata oluşursa bayrakları temizliyoruz
    if (huart->Instance == USART1) { 
        // 1. Hatayı temizle (ORE, FE, NE, PE gibi bayraklar)
        __HAL_UART_CLEAR_OREFLAG(huart); 
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        
        // 2. RX işlemini tekrar başlat
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, uart2_rx_buffer, sizeof(uart2_rx_buffer));
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        // Buffer içinde # karakterini ara
        uint8_t found = 0;
        for(uint16_t i = 0; i < Size; i++) {
            if(uart2_rx_buffer[i] == '#') { found = 1; break; }
                                           }
        if (found==1) {
          command_process((char *)uart2_rx_buffer);
        }
        // 3. DMA'yı tekrar kur
        memset(uart2_rx_buffer, 0, sizeof(uart2_rx_buffer));
        HAL_UARTEx_ReceiveToIdle_DMA(&huart2, uart2_rx_buffer, sizeof(uart2_rx_buffer));
    }
} 


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
