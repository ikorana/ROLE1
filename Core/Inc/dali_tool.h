#ifndef DALI_TOOL_H_
#define DALI_TOOL_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DALI_ADDR_SHORT,     // 0-63 Kısa Adres
    DALI_ADDR_GROUP,     // 0-15 Grup Adresi
    DALI_ADDR_BROADCAST, // Yayın (Tüm cihazlar)
    DALI_ADDR_SPECIAL,   // Özel Komutlar (0xA0-0xCB)
    DALI_ADDR_UNKNOWN    // Tanımlanamayan
} DALI_AddrType_t;

typedef struct {
    DALI_AddrType_t type;
    uint8_t value;      // Adres değeri (0-63 veya 0-15)
    bool is_command;    // true ise Komut, false ise DAPC (Parlaklık) verisidir
} DALI_DecodedAddr_t;

/**
 * @brief DALI Adres byte'ını (16 bitlik framin ilk 8 biti) çözümler
 * @param addr_byte: Çözümlenecek adres byte'ı
 * @return Çözümlenmiş adres yapısı
 */
DALI_DecodedAddr_t DALI_Decode_Address(uint8_t addr_byte);

#endif /* DALI_TOOL_H_ */