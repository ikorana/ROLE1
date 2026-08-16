
#include "dali_command.h"

 #define BCK_TIMEOUT 2000

static bool cevapla01(DALI_t* self)
{
    uint8_t cmd = self->data_byte;
    uint8_t dtr = self->DTR0;
    bool has_response = false;
    uint8_t response_data = 0xFF;
    bool changed = false;

    // --- 1. GRUP: Yapılandırma Yazma Komutları (0x21 - 0x2B) ---
    if (cmd >= 0x21 && cmd <= 0x2B) {
        changed = true;
        if      (cmd == L_SET_MAX_LEVEL)       self->Adres->max_level = dtr;
        else if (cmd == L_SET_MIN_LEVEL)       self->Adres->min_level = dtr;
        else if (cmd == L_SET_SYS_ERR_LEVEL)   self->Adres->system_failure_level = dtr;
        else if (cmd == L_SET_POWER_ON_LEVEL)  self->Adres->power_on_level = dtr;
        else if (cmd == L_SET_FADE_TIME)       self->Adres->fade_time = dtr & 0x0F;
        else if (cmd == L_SET_FADE_RATE)       self->Adres->fade_rate = dtr & 0x0F;
        else if (cmd == L_SET_SHORT_ADDR)      self->Adres->short_address = dtr;
        else if (cmd == L_SET_EFADE_TIME) {
            if (dtr > 0x4F) {
                self->Adres->efade_time_base = 0;
                self->Adres->efade_multiplayer = 0;
            } else {
                self->Adres->efade_multiplayer = (dtr & 0x70) >> 4;
                self->Adres->efade_time_base = (dtr & 0x0F);
            }
        }
    }
    // --- 2. GRUP: Sahne İşlemleri (0x40 - 0x5F) ---
    else if (cmd >= 0x40 && cmd <= 0x5F) {
        uint8_t s_idx = cmd & 0x0F;
        self->Adres->scene[s_idx] = (cmd <= 0x4F) ? dtr : 0xFF; // ADD veya REMOVE
        changed = true;
    }
    // --- 3. GRUP: Grup İşlemleri (0x60 - 0x7F) ---
    else if (cmd >= 0x60 && cmd <= 0x7F) {
        uint8_t g_idx = cmd & 0x0F;
        if (cmd <= 0x6F) self->Adres->groups |= (1 << g_idx);
        else             self->Adres->groups &= ~(1 << g_idx);
        changed = true;
    }
    // --- 4. GRUP: Sorgulama Komutları (0x90 - 0xBF ve diğerleri) ---
    else {
        has_response = true;
        // Sahne Sorgulama (0xB0 - 0xBF)
        if (cmd >= 0xB0 && cmd <= 0xBF) {
            response_data = self->Adres->scene[cmd & 0x0F];
        } 
        else {
            // Münferit Sorgular
            switch (cmd) {
                case L_QUERY_DEV_TYPE: response_data = self->Adres->device_type; break;
                case L_QUERY_NEXT_DEV_TYPE: response_data = self->Adres->next_device_type; break;
                case L_QUERY_GROUP0:        response_data = (uint8_t)(self->Adres->groups & 0xFF); break;
                case L_QUERY_GROUP1:        response_data = (uint8_t)(self->Adres->groups >> 8); break;
                case L_QUERY_RAND_ADR_H:    response_data = (self->Adres->random_address >> 16) & 0xFF; break;
                case L_QUERY_RAND_ADR_M:    response_data = (self->Adres->random_address >> 8) & 0xFF; break;
                case L_QUERY_RAND_ADR_L:    response_data = (self->Adres->random_address) & 0xFF; break;
                case L_QUERY_CONTENT_DTR0:  response_data = dtr; break;
                case L_QUERY_MIN_LEVEL:     response_data = self->Adres->min_level; break;
                case L_QUERY_TIME_RATE:     response_data = (self->Adres->fade_time << 4) | self->Adres->fade_rate; break;
                case L_QUERY_MISSING_SHORT_ADR: has_response = (self->Adres->short_address == 0xFF); break;
                case L_SAVE_PERS_VAR:       
                    if (self->flags.is_dirty) { write_eeprom(self->Adres); self->flags.is_dirty = false; }
                    has_response = false; break;
                default: has_response = false; break;
            }
        }
    }

    if (changed) self->flags.is_dirty = true;

    if (has_response) {
        _delay_us(self, BCK_TIMEOUT); 
        send_impl(self, response_data, 8);
        return true;
    }
    return (changed) ? true : false;
}

static bool cevapla02(DALI_t* self)
{
    switch (self->addr_byte)
    {
    case DALI_STORE_DTR:
         self->DTR0 = self->data_byte;
         break;    
    case DALI_TERMINATE:
        self->flags.is_init=false;
        self->flags.is_withdrawn=false;
        if (self->flags.is_dirty) {
            write_eeprom(self->Adres);
            self->flags.is_dirty=false;
        }
        break;
    case DALI_INITIALISE:
        uint8_t received_short_addr = (self->data_byte >> 1) & 0x3F;
        if (self->data_byte==0x00) self->flags.is_init=true;
        if (self->data_byte==0xFF && self->Adres->short_address==0xFF) self->flags.is_init=true;
        if (received_short_addr==self->Adres->short_address) self->flags.is_init=true; 
        if (self->flags.is_init) {
            self->search_address = 0x0;
            self->flags.is_withdrawn=false;
        }
        break; 
    case DALI_RANDOMISE:
         if (self->flags.is_init) {
            self->Adres->random_address = (my_rand() & 0xFFFFFF);   
            self->flags.is_dirty = true;
         }         
        break; 
    case DALI_SEARCHADDRH:
        self->search_address &= 0x00FFFF; // Alt 16 biti koru
        self->search_address |= ((uint32_t)self->data_byte << 16);
        break;
    case DALI_SEARCHADDRM:
        self->search_address &= 0xFF00FF; // Üst ve alt 8 biti koru
        self->search_address |= ((uint32_t)self->data_byte << 8);
        break;
    case DALI_SEARCHADDRL:
        self->search_address &= 0xFFFF00; // Üst 16 biti koru
        self->search_address |= (uint32_t)self->data_byte;
        break;            
    case DALI_COMPARE:
        if (!self->flags.is_withdrawn)
            if (self->Adres->random_address <= self->search_address)
            { 
                _delay_us(self,BCK_TIMEOUT);
                self->send_backword(self,0xFF);
            }
        break;
    case DALI_WITHDRAW:
        if ((self->flags.is_init ) && (self->Adres->random_address == self->search_address)) {
           self->flags.is_withdrawn = true;
        }           
        break;
    case DALI_PROG_SHORT_ADDR:
        if ((self->flags.is_init || self->flags.is_withdrawn) && (self->Adres->random_address == self->search_address))
         {
             self->Adres->short_address = (self->data_byte >> 1) & 0x3F;
             self->flags.is_dirty = true;
         }
        break;  
    case DALI_VERIFY_SHORT_ADDR:
        if (self->flags.is_init)
          if (self->Adres->short_address == ((self->data_byte >> 1) & 0x3F)) {
            _delay_us(self,BCK_TIMEOUT);
            send_impl(self,0xFF,8);
        }
        break;  
    case DALI_QUERY_SHORT_ADDR:
        if (self->flags.is_init && (self->Adres->random_address == self->search_address)) 
        {   
            uint8_t data = (self->Adres->short_address << 1) | 0x01;
            _delay_us(self,BCK_TIMEOUT);
            send_impl(self,data,8);
        }
        break;            
    default:
        break;
    }
    return true;
}


/* static int cevapla01(DALI_t* self)
{
    bool flag=false;
    bool retflag=false;
    uint8_t data = 0xFF;   
    
    switch (self->data_byte)
    {    
    case L_SET_MAX_LEVEL:
         flag=true;
         self->Adres->max_level = self->DTR0;
         self->is_dirty=true;
         break; 
    case L_SET_MIN_LEVEL:
         flag=true;
         self->Adres->min_level = self->DTR0;
         self->is_dirty=true;
         break;     
    case L_SET_SYS_ERR_LEVEL:
         flag=true;
         self->Adres->system_failure_level = self->DTR0;
         self->is_dirty=true;
         break;  
    case L_SET_POWER_ON_LEVEL:
         flag=true;
         self->Adres->power_on_level = self->DTR0;
         self->is_dirty=true;
         break;  
    case L_SET_FADE_TIME:
         flag=true;
         self->Adres->fade_time = self->DTR0&0x0F;
         self->is_dirty=true;
         break;
    case L_SET_FADE_RATE:
         flag=true;
         self->Adres->fade_rate = self->DTR0&0x0F;
         self->is_dirty=true;
         break;  
    case L_SET_EFADE_TIME:
         flag=true;
         if (self->DTR0 > 0x4F) {
                // Kural: 0x4F'den büyükse en hızlı fade (0) seçilir.
                self->Adres->efade_time_base = 0;
                self->Adres->efade_multiplayer = 0;
            } else {
                // Kural: xYYY AAAA -> Multiplier orta 3 bit, Base son 4 bit
                // xYYY xxxx maskesi (0x70) ve 4 sağa kaydırma
                self->Adres->efade_multiplayer = (self->DTR0 & 0x70) >> 4;                
                // xxxx AAAA maskesi (0x0F)
                self->Adres->efade_time_base = (self->DTR0 & 0x0F);
            }
         self->is_dirty=true;
         break;             
         
    case L_SET_SHORT_ADDR:
         flag=true;
         self->Adres->short_address = self->DTR0;
         self->is_dirty=true;
         break;      
    case L_ADD_SCENE ... L_ADD_SCENE+0x0F:
         flag=true;
         uint8_t scene_index = self->data_byte & 0x0F;
         self->Adres->scene[scene_index] = self->DTR0;
         self->is_dirty=true;
         break; 
    case L_REMOVE_SCENE ... L_REMOVE_SCENE+0x0F:
         flag=true;
         scene_index = self->data_byte & 0x0F;
         self->Adres->scene[scene_index] = 0xFF;
         self->is_dirty=true;  
         break;
    case L_QUERY_SCENE ... L_QUERY_SCENE+0x0F:
         scene_index = self->data_byte & 0x0F;
         data = self->Adres->scene[scene_index];
         retflag=true;
         break; 
    case L_ADD_GROUP ... L_ADD_GROUP+0x0F:
         uint8_t group_index = self->data_byte & 0x0F;
         self->Adres->groups |= (1 << group_index);
         flag = true;
         self->is_dirty = true;
         break;
    case L_REMOVE_GROUP ... L_REMOVE_GROUP+0x0F:
         group_index = self->data_byte & 0x0F;
         self->Adres->groups &= ~(1 << group_index);
         flag = true;
         self->is_dirty = true;
         break;  
    case L_QUERY_GROUP0:
         data = (uint8_t)(self->Adres->groups & 0x00FF);
         retflag=true;
         break;     
    case L_QUERY_GROUP1:
         data = (uint8_t)((self->Adres->groups >> 8) & 0x00FF);
         retflag=true;
         break;
    case L_SAVE_PERS_VAR:
         if (self->is_dirty) {
            flag=true;
            write_eeprom(self->Adres);
            self->is_dirty = false;
         }   
         break;  
    case L_QUERY_MISSING_SHORT_ADR:
        if (self->Adres->short_address!=0xFF) retflag=true;
        break;
    case L_QUERY_RAND_ADR_H: 
        data= (self->Adres->random_address>>16)&0xFF;
        retflag=true;    
        break;
    case L_QUERY_RAND_ADR_M:
        data= (self->Adres->random_address>>8)&0xFF;
        retflag=true;     
        break;
    case L_QUERY_RAND_ADR_L:
        data= (self->Adres->random_address)&0xFF;
        retflag=true;     
        break;  
    case L_QUERY_CONTENT_DTR0:
        data= self->DTR0;
        retflag=true;     
        break;    
    case L_QUERY_PHY_MIN:
        data= self->Adres->PHM;
        retflag=true;     
        break;   
    case L_QUERY_POWER_FAILURE:
        data= self->Adres->system_failure_level;
        retflag=true;     
        break;   
    case L_QUERY_MIN_LEVEL:
        data= self->Adres->min_level;
        retflag=true;     
        break;
    case L_QUERY_POWER_ON_LEVEL:
        data= self->Adres->power_on_level;
        retflag=true;     
        break;  
    case L_QUERY_SYS_FAIL_LEVEL:
        data= self->Adres->system_failure_level;
        retflag=true;     
        break;  
    case L_QUERY_TIME_RATE:
        data= (self->Adres->fade_time<<4) | self->Adres->fade_rate;
        retflag=true;     
        break;          
    case L_QUERY_EFADE_TIME:
        data= (self->Adres->efade_multiplayer << 4) | (self->Adres->efade_time_base);
        retflag=true;     
        break; 
    case L_QUERY_DEV_TYPE: 
        data = self->Adres->device_type;  
        retflag=true;         
        break; 
    case L_QUERY_NEXT_DEV_TYPE:
        data = self->Adres->next_device_type;  
        retflag=true;            
        break;    
    default:
        break;
    }
    if (retflag) {
        flag=true;
        _delay_us(self,2000); 
        bsend_impl(self,data,8);
    }
    if (flag) return 2;
    return -1;
} */