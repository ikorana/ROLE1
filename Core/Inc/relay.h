#ifndef RELAY_H_
#define RELAY_H_

#include "main.h"
#include <stdbool.h>
#include "eprom.h"
#include "dali_tool.h"
#include "dali_cln03.h"

typedef void (*Relay_SaveCallback_t)(void);

/**
 * @brief Röle nesnesi yapısı
 */
typedef struct {
    GPIO_TypeDef* port;
    uint32_t pin;
    bool is_active;
    volatile DALI_Address_t* config; // Röleye ait DALI ayarları

    // DALI Komisyonlama (Commissioning) Durumları
    bool is_init;
    bool is_withdrawn;
    uint32_t search_address;
    uint8_t dtr;
    bool changed;
    bool has_saved;
    volatile bool is_identfy;
    volatile uint16_t identfy_count;
    uint16_t dirty_counter;
    volatile DALI_Status_t status;

    DALI_t* dali_ctrl; // Geri bildirim göndermek için ana DALI objesi
    Relay_SaveCallback_t save_callback;
} Relay_t;

/* Röle Kontrol Fonksiyonları */
void Relay_Init(Relay_t* self, GPIO_TypeDef* port, uint32_t pin, volatile DALI_Address_t* config, DALI_t* dali_ctrl, Relay_SaveCallback_t save_cb);
void Relay_On(Relay_t* self);
void Relay_Off(Relay_t* self);
void Relay_Toggle(Relay_t* self);
void Relay_SetStatus(Relay_t* self, bool status);
bool Relay_GetStatus(Relay_t* self);
uint8_t Relay_HandleDALI(Relay_t* self, DALI_DecodedAddr_t decoded, uint8_t data);
void Relay_HandleTIMER(Relay_t* self);


#endif /* RELAY_H_ */