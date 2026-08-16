#ifndef DALI3_H_
#define DALI3_H_

/* Zamanlama Sabitleri */
#define DALI_HALF_BIT_TIME 416
#define DALI_FULL_BIT_TIME 833
#define DALI_STOP_TIME 1666
#define DALI_MAX_RETRIES 3

#include "main.h"
#include "stdbool.h"
#include "eprom.h"

typedef struct {
    GPIO_TypeDef* port;
    uint16_t      pin;
} DALI_Pin_t;

typedef struct {
    TIM_TypeDef* tim;      // Register bazlı erişim
    uint32_t channel;      // LL_TIM_CHANNEL_CH1 vb.
} DALI_Timer_t;

typedef enum { STATE_IDLE, STATE_RX } dali_rx_state_t;

typedef void (*DALI_ErrCallback_t)(uint8_t err);
typedef void (*DALI_DataCallback_t)(uint32_t rxdata, uint8_t bit);

typedef struct DALI_Obj {
    // --- Donanım Handle'ları ---
    DALI_Timer_t rxTimer;
    TIM_TypeDef* delayTimer; 
    TIM_TypeDef* twiceTimer; 
    TIM_TypeDef* sleepTimer; 
    DALI_Pin_t Rx_Pin;
    DALI_Pin_t Tx_Pin;
    IRQn_Type uart_irq;
    volatile DALI_Address_t *Adres;
    volatile uint8_t DTR0;
    volatile uint32_t search_address;

    //Counterlar
    volatile uint16_t sleep_counter;
    volatile uint8_t hat_error_counter;
    volatile uint8_t dirty_counter;
    volatile uint32_t last_full_frame;
    //------ receive 
    volatile dali_rx_state_t rx_state;
    volatile uint8_t bit_idx;
    volatile uint32_t rx_data;
    volatile uint8_t half_bit_counter;
    volatile uint8_t addr_byte;
    volatile uint8_t data_byte;

    //Flaglar
     struct {
        volatile uint16_t hat_error_flag   :1;
        volatile uint16_t is_sending       :1;
        volatile uint16_t loopback         :1;
        volatile uint16_t is_dirty         :1;
        volatile uint16_t save_request_flag:1;
        volatile uint16_t sleep_flag       :1;
        volatile uint16_t is_init          :1;
        volatile uint16_t is_withdrawn     :1;
   
    } flags;

    //Callbackler
    DALI_ErrCallback_t error_callback;
    DALI_DataCallback_t data_callback;

    //implementler
    bool (*send_backword)(struct DALI_Obj* self,uint8_t data);
    bool (*send)(struct DALI_Obj* self, uint32_t data, uint8_t bit_len);
    void (*capture_handle)(struct DALI_Obj* self, TIM_TypeDef *TIMx);
    void (*timeout_handle)(struct DALI_Obj* self, TIM_TypeDef *TIMx);
    void (*loop)(struct DALI_Obj* self);
    bool (*Send_Test)(struct DALI_Obj* self);

    void (*init)(struct DALI_Obj* self,
                 uint8_t channel, 
                 DALI_Timer_t rxtim, 
                 TIM_TypeDef* delaytim,
                 TIM_TypeDef* twctim,
                 TIM_TypeDef* slptim,
                 DALI_Pin_t Rxpin,
                 DALI_Pin_t Txpin,
                 IRQn_Type uirq);
} DALI_t;


void DALI_Init(DALI_t* self, 
               DALI_Timer_t rxtim, 
               TIM_TypeDef* delaytim, 
               TIM_TypeDef* twctim, 
               TIM_TypeDef* slptim,
               DALI_Pin_t Rxpin,
               DALI_Pin_t Txpin,
               IRQn_Type uirq
               );
void DALI_Set_DataCallback( DALI_t* self, DALI_DataCallback_t cb);   
void DALI_Set_ErrorCallback( DALI_t* self, DALI_ErrCallback_t cb); 
void DALI_Set_Address(DALI_t* self, volatile DALI_Address_t *adr); 
void DALI_Send_Test(DALI_t* self);



#endif