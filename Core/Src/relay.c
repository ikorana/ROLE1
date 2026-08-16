#include "relay.h"
#include "dali_cln03.h"

extern uint32_t my_rand(void);

void Relay_Init(Relay_t* self, GPIO_TypeDef* port, uint32_t pin, volatile DALI_Address_t* config, DALI_t* dali_ctrl, Relay_SaveCallback_t save_cb) {
    self->port = port;
    self->pin = pin;
    self->config = config;
    self->dali_ctrl = dali_ctrl;
    self->is_active = false;
    self->is_init = false;
    self->is_withdrawn = false;
    self->search_address = 0xFFFFFF;
    self->dtr = 0;
    self->changed = false;
    self->has_saved = false;
    self->is_identfy = false;
    self->identfy_count = 0;
    self->dirty_counter = 0;
    self->save_callback = save_cb;

    self->status.controlGearFailure = false;
    self->status.lampFailure = false;
    self->status.lampOn = false;
    self->status.limitError = false;
    self->status.fadeRunning = false;
    self->status.resetState = true;
    self->status.missingShortAddress = true;
    self->status.powerCycleSeen = true;

    if (self->config->short_address!=0xFF) {
        self->status.missingShortAddress = false;
    }

    // Başlangıçta röleyi kapalı konuma getir
    Relay_Off(self);
}

void Relay_On(Relay_t* self) {
    self->is_identfy=false; 
    self->status.powerCycleSeen = false;
    self->status.lampOn = true;
    if (self->port == NULL) return;
    LL_GPIO_SetOutputPin(self->port, self->pin);
    self->is_active = true;
}

void Relay_Off(Relay_t* self) {
    self->is_identfy=false; 
    self->status.lampOn = false;
    self->status.powerCycleSeen = false;
    if (self->port == NULL) return;
    LL_GPIO_ResetOutputPin(self->port, self->pin);
    self->is_active = false;
}

void Relay_Toggle(Relay_t* self) {
    self->is_identfy=false; 
    self->status.powerCycleSeen = false;
    if (self->port == NULL) return;
    LL_GPIO_TogglePin(self->port, self->pin);
    self->is_active = !self->is_active;
    if (self->is_active) self->status.lampOn = true; else self->status.lampOn = false;
}

void Relay_SetStatus(Relay_t* self, bool status) {
    if (status) {
        Relay_On(self);
    } else {
        Relay_Off(self);
    }
}

bool Relay_GetStatus(Relay_t* self) {
    return self->is_active;
}

/**
 * @brief Röleye gelen DALI paketinin kendisine gelip gelmediğini kontrol eder ve işler
 */
uint8_t Relay_HandleDALI(Relay_t* self, DALI_DecodedAddr_t decoded, uint8_t data) {
    // 1. Özel Komutların (Special Commands - Commissioning) İşlenmesi
    uint8_t ret = 0;
    if (decoded.type == DALI_ADDR_SPECIAL) {
        switch (decoded.value) {
            case 0xA1: // TERMINATE
                self->is_init = false;
                self->is_withdrawn = false;
                break;
            case 0xA3: // DTR
                self->dtr = data;
                break;
            case 0xA5: // INITIALISE
                {
                    uint8_t init_val = data;
                    uint8_t short_addr = (init_val >> 1) & 0x3F;
                    if (init_val == 0x00) self->is_init = true; 
                    else if (init_val == 0xFF && self->config->short_address == 0xFF) self->is_init = true;
                    else if (short_addr == self->config->short_address) self->is_init = true;
                    
                    if (self->is_init) {
                        self->search_address = 0xFFFFFF;
                        self->is_withdrawn = false;
                    }
                }
                break;
            case 0xA7: // RANDOMISE
                if (self->is_init) {
                    self->config->random_address = (my_rand() & 0xFFFFFF);
                }
                break;
            case 0xA9: // COMPARE
                if (self->is_init && !self->is_withdrawn) {
                    if (self->config->random_address <= self->search_address) {
                        LL_mDelay(2); // DALI Backward response timing
                        self->dali_ctrl->send_backword(self->dali_ctrl, 0xFF);
                        ret=1;
                    }
                }
                break;
            case 0xAB: // WITHDRAW
                if (self->is_init && (self->config->random_address == self->search_address)) {
                    self->is_withdrawn = true;
                }
                break;
            case 0xB1: // SEARCHADDRH
                self->search_address = (self->search_address & 0x00FFFF) | ((uint32_t)data << 16);
                break;
            case 0xB3: // SEARCHADDRM
                self->search_address = (self->search_address & 0xFF00FF) | ((uint32_t)data << 8);
                break;
            case 0xB5: // SEARCHADDRL
                self->search_address = (self->search_address & 0xFFFF00) | ((uint32_t)data);
                break;
            case 0xB7: // PROGRAM SHORT ADDRESS
                if ((self->is_init || self->is_withdrawn) && (self->config->random_address == self->search_address)) {
                    self->config->short_address = (data >> 1) & 0x3F;
                    self->status.missingShortAddress = false;
                    self->status.resetState = false;
                    self->changed = true;
                }
                break;
            case 0xB9: // VERIFY SHORT ADDRESS
                if (self->is_init && (self->config->short_address == ((data >> 1) & 0x3F))) {
                    LL_mDelay(2);
                    self->dali_ctrl->send_backword(self->dali_ctrl, 0xFF);
                    ret=1;
                }
                break;
            case 0xBB: // QUERY SHORT ADDRESS
                if (self->is_init && (self->config->random_address == self->search_address)) {
                    uint8_t resp = (self->config->short_address << 1) | 0x01;
                    LL_mDelay(2);
                    self->dali_ctrl->send_backword(self->dali_ctrl, resp);
                    ret = 1;
                }
                break;
            default:
                break;
        }
        return ret; // Özel komut işlendi, standart lojiğe geçme
    }

    bool is_target = false;

    // 2. Adres Eşleşme Kontrolü
    if (decoded.type == DALI_ADDR_BROADCAST) {
        is_target = true;
    } else if (decoded.type == DALI_ADDR_SHORT && decoded.value == self->config->short_address) {
        is_target = true;
    } else if (decoded.type == DALI_ADDR_GROUP && (self->config->groups & (1 << decoded.value))) {
        is_target = true;
    }

    if (!is_target) return ret;

    // 2. Komut İşleme
    if (!decoded.is_command) {
        // Direct Arc Power (DAPC): 0 ise kapat, >0 ise aç (Switching logic)
        Relay_SetStatus(self, (data > 0));
    } else {
        bool has_response = false;
        uint8_t response_data = 0xFF;

        switch (data) {
            case 0x00: Relay_Off(self); break; // OFF
            case 0x05: Relay_On(self);  break; // RECALL MAX
            case 0x06: Relay_On(self);  break; // RECALL MIN
            case 0x0A: Relay_On(self);  break; // GOTO LAST ACTIVE LEVEL

            // --- Yapılandırma Komutları (0x21 - 0x2B) ---
            case 0x21: 
                self->dtr = data; 
                break; //SET_DTR
            case 0x22: self->has_saved = true; break; //SAVE VARIABLE
            case 0x25: {
                self->identfy_count=0;
                self->is_identfy = true;
                } break; //IDENTFY

            case 0x2A: self->config->max_level = self->dtr; self->changed = true; break;
            case 0x2B: self->config->min_level = self->dtr; self->changed = true; break;
            case 0x2C: self->config->system_failure_level = self->dtr; self->changed = true; break;
            case 0x2D: self->config->power_on_level = self->dtr; self->changed = true; break;
            case 0x2E: self->config->fade_time = (self->dtr & 0x0F); self->changed = true; break;
            case 0x2F: self->config->fade_rate = (self->dtr & 0x0F); self->changed = true; break;
            case 0x30: // SET EXTENDED FADE TIME (DTR0)
                if (self->dtr > 0x4F) {
                    self->config->efade_time_base = 0;
                    self->config->efade_multiplayer = 0;
                } else {
                    self->config->efade_time_base = (self->dtr & 0x0F);
                    self->config->efade_multiplayer = (self->dtr & 0x70) >> 4;
                }
                self->status.resetState = false;
                self->changed = true;
                break;
            
            case 0x80: self->config->short_address = (self->dtr >> 1) & 0x3F; self->changed = true; break;

            // --- Sahne ve Grup İşlemleri (0x40 - 0x7F) ---
            case 0x40 ... 0x4F: // ADD TO SCENE
                self->config->scene[data & 0x0F] = self->dtr;
                self->status.resetState = false;
                self->changed = true;
                break;
            case 0x50 ... 0x5F: // REMOVE FROM SCENE
                self->config->scene[data & 0x0F] = 0xFF;
                self->status.resetState = false;
                self->changed = true;
                break;
            case 0x60 ... 0x6F: // ADD TO GROUP
                self->config->groups |= (1 << (data & 0x0F));
                self->status.resetState = false;
                self->changed = true;
                break;
            case 0x70 ... 0x7F: // REMOVE FROM GROUP
                self->config->groups &= ~(1 << (data & 0x0F));
                self->status.resetState = false;
                self->changed = true;
                break;

            // --- Sorgulama Komutları (Cevap Gerektirenler) ---
            case 0x90: // QUERY STATUS
                response_data = *(uint8_t*)&(self->status);
                has_response = true;
                break;
            case 0x96: // QUERY MISSING SHORT ADDRESS
                if (self->config->short_address == 0xFF) {
                    response_data = 0xFF;
                    has_response = true;
                }
                break;
            case 0x97: // QUERY VERSION NUMBER
                response_data = 0x01; // DALI 1.0
                has_response = true;
                break;
            case 0x98: // QUERY DTR
                response_data = self->dtr;
                has_response = true;
                break;
   
            case 0x99: // QUERY DEVICE TYPE
                response_data = self->config->device_type; // DT7: Switching Function
                has_response = true;
                break;
            case 0x9A: // QUERY PYH
                response_data = self->config->PHM;
                has_response = true;
                break;
            case 0x9B: // QUERY Power Failure
                response_data = self->config->power_on_level;
                has_response = true;
                break;
            case 0xA0: // QUERY ACTUAL LEVEL
                response_data = (self->is_active ? 254 : 0);
                has_response = true;
                break;
            case 0xA1: // QUERY MAX LEVEL
                response_data = self->config->max_level;
                has_response = true;
                break;
            case 0xA2: // QUERY MIN LEVEL
                response_data = self->config->min_level;
                has_response = true;
                break; 
            case 0xA3: // QUERY POWER ON LEVEL
                response_data = self->config->power_on_level;
                has_response = true;
                break; 
            case 0xA4: // QUERY SYSTEM FAILURE  LEVEL
                response_data = self->config->system_failure_level;
                has_response = true;
                break;
            case 0xA5: // QUERY FADE TIME / FADE RATE
                response_data = (self->config->fade_time << 4) | (self->config->fade_rate & 0x0F);
                has_response = true;
                break;
            case 0xA7: //QUERY NEXT DEV TYPE
                response_data = self->config->next_device_type;
                has_response = true;
                break;
            case 0xA8: // QUERY EXTENDED FADE TIME
                response_data = (self->config->efade_multiplayer << 4) | (self->config->efade_time_base & 0x0F);
                has_response = true;
                break;                   
            case 0xB0 ... 0xBF: // QUERY SCENE LEVEL (scene 0-15)
                response_data = self->config->scene[data - 0xB0];
                has_response = true;
                break;
            case 0xC0: // QUERY GROUPS 0-7
                response_data = (uint8_t)(self->config->groups & 0xFF);
                has_response = true;
                break;
            case 0xC1: // QUERY GROUPS 8-15
                response_data = (uint8_t)(self->config->groups >> 8);
                has_response = true;
                break;
            case 0xC2: // QUERY RANDOM ADDRESS (H)
                response_data = (uint8_t)((self->config->random_address >> 16) & 0xFF);
                has_response = true;
                break;
            case 0xC3: // QUERY RANDOM ADDRESS (M)
                response_data = (uint8_t)((self->config->random_address >> 8) & 0xFF);
                has_response = true;
                break;
            case 0xC4: // QUERY RANDOM ADDRESS (L)
                response_data = (uint8_t)(self->config->random_address & 0xFF);
                has_response = true;
                break;

            // --- Sahne Çağırma (Görsel İşlem) ---
            case 0x10 ... 0x1F: {
                uint8_t scene_val = self->config->scene[data & 0x0F];
                if (scene_val != 0xFF) Relay_SetStatus(self, (scene_val > 0));
                break;
            }
            default:
                break;
        }

        if (self->changed) {
            self->dirty_counter = 0; // Her yeni değişiklikte kayıt zamanlayıcısını sıfırla
        }

        if (has_response) {
            LL_mDelay(2); // DALI Backward response timing (approx 2ms)
            if (!self->dali_ctrl->send_backword(self->dali_ctrl, response_data))
              {
                //Gitmedi
                self->dali_ctrl->send_backword(self->dali_ctrl, response_data);
              }
        }
    }
    return ret;
}

void Relay_HandleTIMER(Relay_t* self)
{
    // Identify modu aktifse 10 saniye boyunca 1 sn aralıklarla toggle yap
    if (self->is_identfy) {
        self->identfy_count++;
        
        // Her 1 saniyede bir (100 * 10ms) durumu değiştir
        if ((self->identfy_count % 100) == 0) {
            if (self->port != NULL) {
                LL_GPIO_TogglePin(self->port, self->pin);
                self->is_active = !self->is_active;
            }
        }

        // 10 saniye dolduğunda (1000 * 10ms) Identify modunu kapat
        if (self->identfy_count >= 1000) {
            Relay_Off(self); // Identify bittiğinde röleyi kapatır ve flagleri temizler
        }
    }

    if (self->changed) {
        // Programlama (Commissioning) devam ederken yazma işlemini beklet
        if (self->is_init) {
            self->dirty_counter = 0;
            return;
        }

        self->dirty_counter++;
        // 10ms periyotta 250 sayım = 2.5 saniye bekleme
        if (self->dirty_counter > 250) {
            if (self->save_callback) self->save_callback();
            self->changed = false;
            self->dirty_counter = 0;
        }
    }
}