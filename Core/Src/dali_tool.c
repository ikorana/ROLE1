#include "dali_tool.h"

/**
 * @brief DALI Adres byte'ını (16 bitlik framin ilk 8 biti) çözümler
 */
DALI_DecodedAddr_t DALI_Decode_Address(uint8_t addr_byte) {
    DALI_DecodedAddr_t res;
    res.is_command = (addr_byte & 0x01); // Son bit 1 ise komut, 0 ise Direct Arc Power (DAPC)

    if ((addr_byte & 0x80) == 0x00) {
        res.type = DALI_ADDR_SHORT;
        res.value = (addr_byte >> 1) & 0x3F;
    } else if ((addr_byte & 0xE0) == 0x80) {
        res.type = DALI_ADDR_GROUP;
        res.value = (addr_byte >> 1) & 0x0F;
    } else if ((addr_byte & 0xFE) == 0xFE) {
        res.type = DALI_ADDR_BROADCAST;
        res.value = 0xFF;
    } else if ((addr_byte & 0xE0) == 0xA0 || (addr_byte & 0xE0) == 0xC0) {
        res.type = DALI_ADDR_SPECIAL;
        res.value = addr_byte;
    } else {
        res.type = DALI_ADDR_UNKNOWN;
        res.value = 0;
    }
    return res;
}