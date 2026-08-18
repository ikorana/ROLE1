/* USER CODE BEGIN Header */

    #include <sys/types.h>
    #include <time.h>

    /*
      5 adet  lamba adresler. 
      
      5. Tuş Lamba Aç Kapat
         **** Uzun Basma Adresleri SIL
            ---------
      RS485 destegi var. Tüm komutlar "rl1_" öneki taşır — STM32 bu öneki
      görünce içeriğe bakmadan RS485<->ESP32 arasında olduğu gibi iletir,
      yeni bir rl1_ komutu eklemek STM32 tarafında değişiklik gerektirmez.

      Komutlar:
        {"com":"rl1_adres","dev":xx,"adr":yy}
           dev : 1 Lamba
                 2 Lamba
                 3 Lamba
                 4 Lamba
                 5 Lamba
           adr : 0-63 veya 255 (adresi sil)

        {"com":"rl1_get_adres"}              -> cevap: {"com":"rl1_get_adres","adr":[a1,a2,a3,a4,a5]}
           5 kanalın (Lamba 1-5) o anki DALI adreslerini tek seferde döner.
           255 = adres atanmamış.

        {"com":"rl1_status","dev":xx,"durum":yy}
           dev : 1-5
           durum :   0 Close
                     1 Open
                     2 Toggle

        {"com":"rl1_get_id"}                 -> cevap: {"com":"rl1_get_id","id":X,"frmtype":Y}
           frmtype : firmware/donanım tipi, aynı rl1_ protokolünü konuşan farklı
                     uç kartları ayırt etmek için. ROLE1 (5li röle) = 1.
        {"com":"rl1_set_id","id":X}          -> board'un kalıcı RS485 kimliğini atar
                                                 (X<254; 254/255 ayrılmış)

        {"com":"rl1_set_tustype","dev":xx,"type":yy}
           dev : 1-5 (Lamba N'nin fiziksel tuşu)
           type: 0 = toggle (basma onaylanınca aç/kapa değiştir, varsayılan)
                 1 = momentary (basılıyken aç, bırakılınca kapat)
        {"com":"rl1_get_tustype"}            -> cevap: {"com":"rl1_get_tustype","type":[t1,t2,t3,t4,t5]}
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
#define BTN_DEBOUNCE_TICKS 20 // 200ms (20 * 10ms)
static uint16_t btn_debounce_timers[5] = {BTN_DEBOUNCE_TICKS, BTN_DEBOUNCE_TICKS, BTN_DEBOUNCE_TICKS, BTN_DEBOUNCE_TICKS, BTN_DEBOUNCE_TICKS};
static uint8_t  btn_last_raw_states[5] = {0};

// Tus1-5'in hepsi artık EXTI ile tetikleniyor. Bu bayraklar bir kenar
// geldiğini işaret eder; Button_Filter_Update sadece bayrak set edildiğinde
// ilgili tuşun debounce'unu çalıştırır, aksi halde her 10ms'de boşuna pin okumaz.
volatile bool tus1_watch_active = false;
volatile bool tus2_watch_active = false;
volatile bool tus3_watch_active = false;
volatile bool tus4_watch_active = false;
volatile bool tus5_watch_active = false;

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

// EXTI1'den (Tus1/IN4/PB1, her iki kenar) çağrılır. Hafif tutulmalı: sadece
// "bir kenar geldi, debounce'a bak" bayrağını set eder, işi Button_Filter_Update
// (10ms tick) yapar — aynı bounce toleransı (500ms) korunur.

void Button1_EXTI_Callback(void)
{
    tus1_watch_active = true;
}

// EXTI0'dan (Tus2/IN3/PB0, her iki kenar) çağrılır — Button1_EXTI_Callback ile aynı desen.
void Button2_EXTI_Callback(void)
{
    tus2_watch_active = true;
}

// EXTI9_5'ten (Tus3/IN2/PA7, her iki kenar) çağrılır — aynı desen.
void Button3_EXTI_Callback(void)
{
    tus3_watch_active = true;
}

// EXTI9_5'ten (Tus4/IN1/PA6, her iki kenar) çağrılır — aynı desen.
void Button4_EXTI_Callback(void)
{
    tus4_watch_active = true;
}

// EXTI15_10'dan (Tus5/IN5/PC14, her iki kenar) çağrılır — aynı desen.
void Button5_EXTI_Callback(void)
{
    tus5_watch_active = true;
}

// EXTI ile tetiklenen tuşlar için ortak debounce: bir kenar gelmediyse
// (*watch_active == false) pini her 10ms'de boşuna okumuyoruz; kenar
// geldiyse seviye BTN_DEBOUNCE_TICKS boyunca kararlı kalınca handler'ı
// çağırıp tekrar uykuya geçiyoruz.
static void Debounce_Watched_Button(volatile bool *watch_active, GPIO_TypeDef *port, uint32_t pin,
                                     uint8_t idx, void (*handler)(uint32_t))
{
    if (!*watch_active) return;

    uint8_t raw = !LL_GPIO_IsInputPinSet(port, pin);
    if (raw != btn_last_raw_states[idx]) {
        btn_debounce_timers[idx] = 0;
        btn_last_raw_states[idx] = raw;
    } else if (btn_debounce_timers[idx] < BTN_DEBOUNCE_TICKS) {
        if (++btn_debounce_timers[idx] == BTN_DEBOUNCE_TICKS) {
            handler(raw);
            *watch_active = false;
        }
    }
}

void Button_Filter_Update(void) {
    Debounce_Watched_Button(&tus1_watch_active, IN4_GPIO_Port, IN4_Pin, 0, Tus1);
    Debounce_Watched_Button(&tus2_watch_active, IN3_GPIO_Port, IN3_Pin, 1, Tus2);
    Debounce_Watched_Button(&tus3_watch_active, IN2_GPIO_Port, IN2_Pin, 2, Tus3);
    Debounce_Watched_Button(&tus4_watch_active, IN1_GPIO_Port, IN1_Pin, 3, Tus4);
    Debounce_Watched_Button(&tus5_watch_active, IN5_GPIO_Port, IN5_Pin, 4, Tus5);
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
            if (kk==1) {
              break;
            }
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
#define RL1_FRMTYPE 1 // ROLE1 (5li röle)

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
              if (strcmp(command,"rl1_adres")==0) {
                uint8_t dev=0xFF, adr=0xFF;
                json_get_int(data, tokens, r,"dev",&dev);
                json_get_int(data, tokens, r,"adr",&adr);
                if (dev>0 && dev<6) {
                  if (adr<64 || adr==0xFF) {
                    AdresList[dev-1].short_address = adr;
                    write_eeprom(AdresList);
                  }
                }
              }

              if (strcmp(command,"rl1_get_adres")==0) {
                printf("{\"com\":\"rl1_get_adres\",\"adr\":[%d,%d,%d,%d,%d]}#\r\n",
                    AdresList[0].short_address, AdresList[1].short_address, AdresList[2].short_address,
                    AdresList[3].short_address, AdresList[4].short_address);
              }

              if (strcmp(command,"rl1_status")==0)
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

              if (strcmp(command,"rl1_get_id")==0) {
                printf("{\"com\":\"rl1_get_id\",\"id\":%d,\"frmtype\":%d}#\r\n", AdresList[0].uart_ID, RL1_FRMTYPE);
              }

              if (strcmp(command,"rl1_set_id")==0) {
                uint8_t id=0xFF;
                json_get_int(data, tokens, r,"id",&id);
                // 254: fabrika/blank-flash varsayılanı, 255: ileride broadcast için
                // ayrılmış — ikisi de kalıcı kimlik olarak atanamaz.
                if (id<254) {
                  AdresList[0].uart_ID = id;
                  write_eeprom(AdresList);
                }
                printf("{\"com\":\"rl1_set_id\",\"id\":%d}#\r\n", AdresList[0].uart_ID);
              }

              if (strcmp(command,"rl1_set_tustype")==0) {
                uint8_t dev=0xFF, type=0xFF;
                json_get_int(data, tokens, r,"dev",&dev);
                json_get_int(data, tokens, r,"type",&type);
                if (dev>0 && dev<6 && (type==0 || type==1)) {
                  AdresList[dev-1].tus_type = type;
                  write_eeprom(AdresList);
                }
              }

              if (strcmp(command,"rl1_get_tustype")==0) {
                printf("{\"com\":\"rl1_get_tustype\",\"type\":[%d,%d,%d,%d,%d]}#\r\n",
                    AdresList[0].tus_type, AdresList[1].tus_type, AdresList[2].tus_type,
                    AdresList[3].tus_type, AdresList[4].tus_type);
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

  /* Tus1 (IN4/PB1) artık EXTI ile tetikleniyor — her iki kenarda da
     (basma/bırakma) kesme üretir, debounce Button_Filter_Update'te. */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
  LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE1);
  {
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_1;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);
  }
  NVIC_SetPriority(EXTI1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(EXTI1_IRQn);

  /* Tus2 (IN3/PB0) da aynı şekilde EXTI ile tetikleniyor. */
  LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_LINE0);
  {
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_0;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);
  }
  NVIC_SetPriority(EXTI0_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(EXTI0_IRQn);

  /* Tus3 (IN2/PA7) da aynı şekilde EXTI ile tetikleniyor. PA7, pin 5-9
     grubunun paylaşımlı EXTI9_5_IRQn vektörünü kullanıyor. */
  LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE7);
  {
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_7;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);
  }
  NVIC_SetPriority(EXTI9_5_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* Tus4 (IN1/PA6) da aynı şekilde EXTI ile tetikleniyor. PA6 da EXTI9_5_IRQn
     vektörünü Tus3 (PA7) ile paylaşıyor — NVIC zaten yukarıda açıldı. */
  LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_LINE6);
  {
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_6;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);
  }

  /* Tus5 (IN5/PC14) da aynı şekilde EXTI ile tetikleniyor. PC14, pin 10-15
     grubunun paylaşımlı EXTI15_10_IRQn vektörünü kullanıyor. */
  LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE14);
  {
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_14;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_RISING_FALLING;
    LL_EXTI_Init(&EXTI_InitStruct);
  }
  NVIC_SetPriority(EXTI15_10_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),0, 0));
  NVIC_EnableIRQ(EXTI15_10_IRQn);

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

// tus_type EEPROM'dan okunuyor (AdresList[idx].tus_type): 1 = momentary
// (basılıyken aç, bırakılınca kapat), başka her değer (0 dahil, ve eski
// kartlarda henüz hiç yazılmamış olabilecek rastgele leftover baytlar dahil)
// güvenli varsayılan olan toggle'a düşer.
static void Tus_Common(uint8_t idx, uint32_t status) {
  if (AdresList[idx].tus_type == 1) {
    if (status == 1) Relay_On(&relays[idx]); else Relay_Off(&relays[idx]);
  } else {
    if (status == 1) Relay_Toggle(&relays[idx]);
  }
}

void Tus1(uint32_t status){ Tus_Common(0, status); }
void Tus2(uint32_t status){ Tus_Common(1, status); }
void Tus3(uint32_t status){ Tus_Common(2, status); }
void Tus4(uint32_t status){ Tus_Common(3, status); }
void Tus5(uint32_t status){ Tus_Common(4, status); }

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    //Uartta hata oluşur ve flaglar temizlenmez ise uart kilitlenir. 
    //Bu nedenle Uart4 de hata oluşursa bayrakları temizliyoruz
    if (huart->Instance == USART2) {
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
    if (huart->Instance == USART2)
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
