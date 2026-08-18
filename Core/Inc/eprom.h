#ifndef DALI_EPROM_H_
#define DALI_EPROM_H_

#include "main.h" 
#include "stdbool.h"

//Bu tanımlama işlemciye ozeldir
/* 
   STM32L4 Page size = 2KB. 
   64KB cihazlar için son sayfa (Page 31): 0x0800F800
   128KB cihazlar için son sayfa (Page 63): 0x0801F800
   256KB cihazlar için son sayfa (Page 127): 0x0803F800
*/
#define DALI_SETTINGS_FLASH_ADDR 0x0800F800 
#define DALI_ADDRESS_COUNT       5
#define EPROM_TOTAL_SIZE         (sizeof(DALI_Address_t) * DALI_ADDRESS_COUNT)

typedef struct __attribute__((packed, aligned(8))) {
    uint8_t  def;                   // 1 byte
    uint8_t  short_address;         // 1 byte
    uint32_t random_address;        // 4 byte
    uint16_t groups;                // 2 byte
    uint8_t  scene[16];             // 16 byte

    uint8_t  power_on_level;        // 1 byte
    uint8_t  system_failure_level;  // 1 byte
    uint8_t  min_level;             // 1 byte
    uint8_t  max_level;             // 1 byte
    uint8_t  fade_rate;             // 1 byte
    uint8_t  fade_time;             // 1 byte
    uint8_t  efade_time_base;       // 1 byte
    uint8_t  efade_multiplayer;     // 1 byte
    uint8_t  PHM;                   // 1 byte
    uint8_t  device_type;
    uint8_t  next_device_type;

    // Kartın RS485 kimliği (yalnızca AdresList[0].uart_ID anlamlıdır).
    // Fabrika/blank-flash varsayılanı 254 — configürasyon sırasında kalıcı bir
    // kimliğe değiştirilir. 254 "henüz atanmamış", 255 ileride broadcast için
    // ayrılmış tutulur.
    uint8_t  uart_ID;

    // Bu kanalın (Lamba N) fiziksel tuşu nasıl davranacak:
    // 0 = toggle (basma onaylanınca aç/kapa değiştir, mevcut varsayılan)
    // 1 = momentary (basılıyken aç, bırakılınca kapat)
    uint8_t  tus_type;

    uint8_t  reserved[3];           // Toplam boyutu 40 byte'a (8'in katı) tamamlamak için padding
} DALI_Address_t; // Toplam: 40 Byte

bool read_eeprom(volatile DALI_Address_t *settings_array);
bool write_eeprom(volatile DALI_Address_t *settings_array);

#endif