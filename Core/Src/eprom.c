
#include "eprom.h"
#include <string.h>

bool read_eeprom(volatile DALI_Address_t *settings_array) {
    // Flash'taki veriyi direkt struct'ın içine kopyalıyoruz.
    memcpy((void*)settings_array, (const void*)DALI_SETTINGS_FLASH_ADDR, EPROM_TOTAL_SIZE);
    
    // İlk slotun 'def' değerini kontrol ediyoruz. Eğer 0x55 değilse tüm slotları ilklendir.
    if (settings_array[0].def != 0x55) {
        for(int i = 0; i < DALI_ADDRESS_COUNT; i++) {
            settings_array[i].def = 0x55;
            settings_array[i].short_address = 0xFF; 
            settings_array[i].random_address = 0xFFFFFFFF;
            settings_array[i].groups = 0x0000;
            memset((void*)settings_array[i].scene, 0xFF, sizeof(settings_array[i].scene));
            
            settings_array[i].PHM                     = 0x1;
            settings_array[i].power_on_level          = 0xFE;       
            settings_array[i].system_failure_level    = 0xFE;  
            settings_array[i].min_level               = settings_array[i].PHM;             
            settings_array[i].max_level               = 0xFE;            
            settings_array[i].fade_rate               = 0x07;             
            settings_array[i].fade_time               = 0x00;             
            settings_array[i].efade_time_base         = 0x00;       
            settings_array[i].efade_multiplayer       = 0x00;  
            settings_array[i].device_type             = 0x07; 
            settings_array[i].next_device_type        = 0x07;
        }
             
        write_eeprom(settings_array); 
    }
    return true;
}

bool write_eeprom(volatile DALI_Address_t *settings_array) {
    HAL_StatusTypeDef status = HAL_OK;
    HAL_FLASH_Unlock(); // Flash kilidini aç
    
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    
    // STM32L4'te sayfa numarasını hesaplamalıyız
    uint32_t StartPage = (DALI_SETTINGS_FLASH_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Banks     = FLASH_BANK_1;
    eraseInit.Page      = StartPage;
    eraseInit.NbPages = 1;

    __disable_irq(); 
    status = HAL_FLASHEx_Erase(&eraseInit, &pageError);
    if (status != HAL_OK) {
        __enable_irq();
        HAL_FLASH_Lock();
        return false;  
    }

    // STM32L4 DoubleWord (64-bit) yazma kuralına sahiptir.
    uint64_t *dataPtr = (uint64_t*)settings_array;
    uint32_t address = DALI_SETTINGS_FLASH_ADDR;
    for (uint32_t i = 0; i < EPROM_TOTAL_SIZE / 8; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, dataPtr[i]);
        if (status != HAL_OK) {
            __enable_irq();
            HAL_FLASH_Lock();
            return false;
        }
        address += 8; // Her yazmada 8 byte (DoubleWord) ilerle
    }
    __enable_irq(); 
    HAL_FLASH_Lock();
    return true;
}