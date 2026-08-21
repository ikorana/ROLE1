
#include "dali_cln03.h"


#define HARDW_LOW 0
#define HARDW_HIGH 1
#define DALI_COLLISION false

#define Set_tx_high(self) LL_GPIO_SetOutputPin((self)->Tx_Pin.port, (self)->Tx_Pin.pin)
#define Set_tx_low(self)  LL_GPIO_ResetOutputPin((self)->Tx_Pin.port, (self)->Tx_Pin.pin)
#define read_rx(self)     LL_GPIO_IsInputPinSet((self)->Rx_Pin.port, (self)->Rx_Pin.pin)


static inline void _delay_us(DALI_t* self, uint16_t us);
void my_srand(uint32_t seed);
uint32_t my_rand(void);
static inline bool Send_LOW(DALI_t* self);
static inline bool Send_HIGH(DALI_t* self, uint16_t target_time);
static bool DALI_Write_Bit(DALI_t* self, uint8_t bit);
static bool send_backword_impl(DALI_t* self, uint8_t data);
static bool send_impl(DALI_t *self, uint32_t data, uint8_t bit_len); 
static void capture_impl(DALI_t* self, TIM_TypeDef *htim);
static void timeout_impl(DALI_t* self, TIM_TypeDef *htim);
static bool Is_Send_Twice_Command(DALI_t* self);
static uint8_t Check_Send_Twice(DALI_t* self, uint16_t current_frame);
static inline bool Is_Special(uint8_t addr);
void loop_impl(DALI_t* self);

// --- ANA INIT ---
void DALI_Init(DALI_t* self, 
               DALI_Timer_t rxtim, 
               TIM_TypeDef* delaytim, 
               TIM_TypeDef* twctim,
               TIM_TypeDef* slptim,
               DALI_Pin_t Rxpin,
               DALI_Pin_t Txpin,              
               IRQn_Type uirq) 
{
    self->rxTimer = rxtim;
    self->delayTimer = delaytim;
    self->twiceTimer = twctim;
    self->sleepTimer = slptim;
    self->Rx_Pin = Rxpin;
    self->Tx_Pin = Txpin;
    self->uart_irq = uirq;

    self->error_callback = NULL;
    //self->Adres = NULL;

    self->sleep_counter = 0;
    self->flags.hat_error_flag = false;
    self->flags.is_sending = false;
    self->flags.loopback = true; //Gonderdiğini alır

    self->rx_state = STATE_IDLE;
    self->bit_idx = 0;
    self->rx_data = 0x0;
    self->half_bit_counter = 0;

    self->send_backword = send_backword_impl;
    self->send = send_impl;
    self->capture_handle = capture_impl;
    self->timeout_handle = timeout_impl;
    self->loop = loop_impl;

    // Donanımı ayağa kaldır
    // 1. Gecikme Timer'ını başlat (Bloklayan delaylar için)
    LL_TIM_EnableIT_UPDATE(self->delayTimer);
    LL_TIM_EnableCounter(self->delayTimer);

    LL_TIM_EnableIT_UPDATE(self->sleepTimer);
    LL_TIM_EnableCounter(self->sleepTimer);

    LL_TIM_ClearFlag_UPDATE(self->rxTimer.tim);
    LL_TIM_CC_EnableChannel(self->rxTimer.tim, self->rxTimer.channel);
    LL_TIM_EnableIT_CC1(self->rxTimer.tim); // CH1 varsayıldı
    LL_TIM_EnableIT_UPDATE(self->rxTimer.tim);
    LL_TIM_EnableCounter(self->rxTimer.tim); // Timer'ın her zaman dönmesini sağla

    my_srand(0x1234); // HAL_GetTick yerine sabit veya donanımsal RNG
    //Target time 40 dan küçükse hattı kontrol etmez
    //Hattı kontrol et eger RX hattı düşükte ise kablo takılı degildir
    //Error flagını kaldır
    if (!Send_HIGH(self,100))
      if (!Send_HIGH(self,100))
          if (!Send_HIGH(self,100)) {
            self->flags.hat_error_flag = true;
          }
    self->flags.is_init = true;
}

static uint32_t xorshift_state = 0xACE1u; // Başlangıç tohumu (Seed)

// stdlib: srand() alternatifi
void my_srand(uint32_t seed) {
    if (seed == 0) seed = 0xACE1u;
    xorshift_state = seed;
}

// stdlib: rand() alternatifi
uint32_t my_rand(void) {
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

static inline void _delay_us(DALI_t* self, uint16_t us) 
{
    TIM_TypeDef *TIMx = self->delayTimer;
    LL_TIM_SetCounter(TIMx, 0);
    while (LL_TIM_GetCounter(TIMx) < us);
}

static inline bool Send_LOW(DALI_t* self) 
{
    Set_tx_low(self);
    _delay_us(self,DALI_HALF_BIT_TIME);
    return true;
}

static inline bool Send_HIGH(DALI_t* self, uint16_t target_time) 
{   
    Set_tx_high(self);
    TIM_TypeDef *TIMx = self->delayTimer;
    LL_TIM_SetCounter(TIMx, 0);

    // 1. ADIM: Rise time geçene kadar bekle (Gereksiz okumadan kaçın)
    while (LL_TIM_GetCounter(TIMx) < 40); 

    // 2. ADIM: Stratejik Kontrol (Sürekli değil, aralıklı)
    // 40. us ile 400. us arası çakışma ara
    while (LL_TIM_GetCounter(TIMx) < (target_time - 15)) 
    {
        // Sadece hat LOW ise "Robust" kontrolü tetikle
        if (read_rx(self) == HARDW_LOW) 
        {
            // Tek bir okuma yetmez, 10-15 us boyunca hala LOW mu?
            uint8_t low_count = 0;
            for(int i=0; i<10; i++) {
                if(read_rx(self) == HARDW_LOW) low_count++;
            }            
            //if(low_count > 7) return DALI_COLLISION; // Gerçek bir çakışma!
        }
        
        // İşlemciyi her döngüde saniyenin milyonda biri kadar beklet (Örnekleme hızını düşür)
        for(volatile int i=0; i<50; i++); 
    }

    // 3. ADIM: Kalan süreyi tamamla
    while (LL_TIM_GetCounter(TIMx) < target_time);
    return true;  
}

static bool DALI_Write_Bit(DALI_t* self, uint8_t bit) 
{
    if (bit) {
        if (!Send_LOW(self)) return false; 
        return Send_HIGH(self, DALI_HALF_BIT_TIME);
    } 
    
    if (!Send_HIGH(self, DALI_HALF_BIT_TIME)) return false;
    return Send_LOW(self);
}

static bool send_backword_impl(DALI_t* self, uint8_t data)
{
    self->sleep_counter=0;
    if (self->flags.hat_error_flag) return false;

    // send_impl ile aynı gerekçe: TX/RX aynı hat üzerinde olduğu için, bit-bang
    // sırasında RX yakalama kesmesi kendi gönderdiğimiz kenarlara tepki verip
    // hem zamanlamayı hem alım durum makinesini bozabilir.
    NVIC_DisableIRQ(self->uart_irq);

    // 1. Hat Meşguliyet Kontrolü (600us)
    LL_TIM_SetCounter(self->delayTimer, 0);
    while (LL_TIM_GetCounter(self->delayTimer) < 600) {
        if (read_rx(self) == 0) {
            NVIC_EnableIRQ(self->uart_irq);
            return false; // Hat meşgulse gönderimden vazgeç
        }
    }

    if (!DALI_Write_Bit(self, 1)) {
        NVIC_EnableIRQ(self->uart_irq);
        return false;
    }
    for (int8_t i = 7; i >= 0; i--) {
        if (!DALI_Write_Bit(self, (data >> i) & 0x01)) {
            NVIC_EnableIRQ(self->uart_irq);
            return false; // Çakışma anında hemen çık
        }
    }
    bool ok = Send_HIGH(self, DALI_STOP_TIME);
    NVIC_EnableIRQ(self->uart_irq);
    return ok;
}

static bool send_impl(DALI_t *self, uint32_t data, uint8_t bit_len) 
{
    uint8_t retry = 0;
    self->flags.is_sending = 1;
    self->sleep_counter = 0;
    NVIC_DisableIRQ(self->uart_irq);

    while (retry < DALI_MAX_RETRIES) 
    {
        // Hat hatası varsa (Kısa devre vb.) döngüden çık
        if (self->flags.hat_error_flag) break;

        // 1. START BIT (Her zaman 1)
        if (DALI_Write_Bit(self, 1)) 
        {
            bool collision = false;
            // 2. DATA BITS
            for (int8_t i = (int8_t)bit_len - 1; i >= 0; i--) {
                if (!DALI_Write_Bit(self, (data >> i) & 0x01)) {
                    collision = true;
                    break;
                }
            }
            // 3. STOP BITS & SUCCESS CHECK
            if (!collision && Send_HIGH(self, DALI_STOP_TIME)) {
                NVIC_EnableIRQ(self->uart_irq);
                self->flags.is_sending = 0;
                return true;
            }
        }
        // --- ÇAKIŞMA VEYA HATA DURUMU ---
        Set_tx_high(self); // Hattı her ihtimale karşı serbest bırak
        retry++;
        
        // Rastgele bekleme (Çakışma kaçınma - Backoff)
        // 6.2ms - 16.2ms arası (my_rand sonucuna göre)
        _delay_us(self, 6250 + (my_rand() % 10000));
    }

    // --- BAŞARISIZLIK DURUMU ---
    NVIC_EnableIRQ(self->uart_irq);
    if (self->error_callback) self->error_callback(2); //Collesion var
    self->flags.is_sending = 0;
    return false;
}


// Kenar yakalama ve Manchester çözme lojiği    
static void capture_impl(DALI_t* self, TIM_TypeDef *TIMx) {
    // TIMx kontrolü üst katmanda yapıldı

uint32_t duration = LL_TIM_IC_GetCaptureCH1(TIMx);
LL_TIM_SetCounter(TIMx, 0);
self->sleep_counter = 0;
if (!self->flags.loopback && self->flags.is_sending) return;
uint8_t current_level = read_rx(self);

if (self->rx_state == STATE_IDLE) {
    if (current_level == 0) { // Start bit iniş kenarı
        self->rx_state = STATE_RX;
        self->bit_idx = 0;
        self->rx_data = 0;
        self->half_bit_counter = 1;
        
        // Timer IT reset (Register seviyesi)
        LL_TIM_DisableCounter(TIMx);
        LL_TIM_ClearFlag_UPDATE(TIMx);
        LL_TIM_EnableIT_UPDATE(TIMx);
        LL_TIM_EnableCounter(TIMx);
    }
} else {
    // Duration kontrolü: 650us altında ise 1 yarım bit, üstünde ise 2 yarım bit geçmiştir
    self->half_bit_counter += (duration < 650) ? 1 : 2;
    // Her iki yarım bitte bir (tam bit oluştuğunda) veriyi işle
    if (!(self->half_bit_counter & 0x01)) {
        // Start bitini (bit_idx 0) atla, geri kalanını (1-8 veya 1-16) kaydet
        if (self->bit_idx > 0) {
            self->rx_data = (self->rx_data << 1) | current_level;
        }
        self->bit_idx++;
    }
}  
}

//#include "dali_cln_command.c"

static void timeout_impl(DALI_t* self, TIM_TypeDef *TIMx) {

    //100ms içinde gelecek çiftlenmiş komutlar için kullanılır
    //program içinden gerektiğinde kurulur
    if (TIMx == self->twiceTimer) {
        self->last_full_frame = 0;
        LL_TIM_ClearFlag_UPDATE(TIMx);
        LL_TIM_DisableCounter(TIMx);
        return; // İşlem bitti, çık
    }

    // 2. SLEEP TIMER (10ms Ana Kontrol Döngüsü)
    if (TIMx == self->sleepTimer) {
        // --- HAT HATA & YAZMA KONTROLÜ ---

      //  LL_GPIO_TogglePin(GPIOA, ROLE5_Pin);

        if (!self->flags.is_sending && self->rx_state == STATE_IDLE) {
            if (read_rx(self)) { // Hat HIGH (Normal)
                if (self->flags.hat_error_flag) {
                    self->flags.hat_error_flag = false;
                    if (self->error_callback) self->error_callback(0); // Hata düzeldi
                }
                self->hat_error_counter = 0;
            } else { // Hat LOW (Hata durumu)
                if (self->hat_error_counter < 250) {
                    if (++self->hat_error_counter > 10 && !self->flags.hat_error_flag) {
                        self->flags.hat_error_flag = true;
                        if (self->error_callback) self->error_callback(1); // Hata oluştu
                    }
                }
            }
        }
        return;
    }

    //---- PAKET SONU KONTROLU --------
    if (TIMx == self->rxTimer.tim) 
    {
        LL_TIM_DisableCounter(TIMx);
        LL_TIM_SetCounter(TIMx, 0);
        LL_TIM_ClearFlag_UPDATE(TIMx);
        LL_TIM_DisableIT_UPDATE(TIMx);
        if (self->rx_state == STATE_RX) 
        {
            uint8_t bt = self->bit_idx-1;
            if (bt==16) {
                self->addr_byte = (self->rx_data >> 8) & 0xFF; // İlk 8 bit
                self->data_byte = self->rx_data & 0xFF;
                if (Is_Send_Twice_Command(self)) 
                {
                    if (!Check_Send_Twice(self, self->rx_data)) goto CIK;
                }
                self->data_callback(self->rx_data,bt);
            } else if (self->error_callback && bt!=8) self->error_callback(9);
            
        }
        CIK:
        self->rx_state = STATE_IDLE;
        self->bit_idx = 0;
        self->rx_data = 0;
        self->half_bit_counter = 0; 
    }    


}

// Özel komutlar 101 (0xA0) ile başlar. 
// addr & 0xE0 (1110 0000) maskesiyle en üst 3 biti izole ederiz.
// Eğer sonuç 0xA0 (101x xxxx) veya 0xC0 (110x xxxx) ise özel komuttur.

static inline bool Is_Special(uint8_t addr) {
    uint8_t top_bits = addr & 0xE0; 
    // 0xA0: Special (Terminate, Init, Search vb.)
    // 0xC0: Special (Data Transfer, vb.)
    return (top_bits == 0xA0 || top_bits == 0xC0);
}


static bool Is_Send_Twice_Command(DALI_t* self) {
    uint8_t addr = self->addr_byte;
    uint8_t data = self->data_byte; 
    // 1. Özel Komutlar: INITIALISE (0xA1) veya RANDOMISE (0xA3)
    // (addr | 0x02) == 0xA3 kontrolü hem A1 hem A3'ü tek hamlede yakalar.
    
    //if ((addr | 0x02) == 0xA3) return true;
    if (addr == 0xA5 || addr == 0xA7) return true;

    // 2. Standart Yapılandırma Komutları (0x20 - 0x81)
    // Şartlar: 
    // a) Data bu aralıkta mı?
    // b) Adres byte'ının son biti 1 mi? (YAAA AAA1 -> Command, 0 -> DAPC)
    // c) Adres geçerli mi? (0x82 - 0xFD arası çift gönderilmez)
    
    if (data >= 0x20 && data <= 0x81) {
        if ((addr & 0x01) && !(addr >= 0x82 && addr <= 0xFD)) {
            return true;
        }
    }
    return false;
}

// Return 1: Komut Onaylandı (İşleme koyabilirsin)
// Return 0: Pencere Açıldı veya Yenilendi (Beklemede kal)
static uint8_t Check_Send_Twice(DALI_t* self, uint16_t current_frame) 
{  
    bool is_window_open = LL_TIM_IsEnabledCounter(self->twiceTimer);
    if (is_window_open) {
        // Eğer tam 16-bit frame bir öncekiyle birebir aynıysa ONAYLA
        if (current_frame == self->last_full_frame) {
            LL_TIM_DisableIT_UPDATE(self->twiceTimer);
            LL_TIM_DisableCounter(self->twiceTimer);
            self->last_full_frame = 0; // Belleği temizle
            return 1; // Başarılı, komutu icra et
        } else {
            // Farklı bir frame geldi, pencereyi bu yeni frame için resetle
            self->last_full_frame = current_frame;
            LL_TIM_SetCounter(self->twiceTimer, 0);
            return 0; // Beklemede kal
        }
    } else {
        // İlk kez bir "twice" adayı frame geldi, pencereyi aç
        self->last_full_frame = current_frame;
        LL_TIM_DisableIT_UPDATE(self->twiceTimer);
        LL_TIM_SetCounter(self->twiceTimer, 0);
        LL_TIM_ClearFlag_UPDATE(self->twiceTimer);
        LL_TIM_EnableIT_UPDATE(self->twiceTimer);
        LL_TIM_EnableCounter(self->twiceTimer);
        return 0; // Beklemede kal
    }
}

void loop_impl(DALI_t* self) {
      if (self->flags.sleep_flag) {
        self->sleep_counter = 0;
        self->flags.sleep_flag=false;
        if (self->error_callback) self->error_callback(0xFE);
      }  
}

void DALI_Set_DataCallback( DALI_t* self, DALI_DataCallback_t cb)
{
    self->data_callback = cb;
}
void DALI_Set_ErrorCallback( DALI_t* self, DALI_ErrCallback_t cb)
{
    self->error_callback=cb;
}

void DALI_Send_Test(DALI_t* self){
    while(1) {
        DALI_Write_Bit(self,1);
        _delay_us(self,10000);
    }
}

// DALI_Send_Test'in blocking olmayan hali: tek bir "1" biti gönderip
// hemen döner. rl1_dalitst özelliği için TIM2 10ms tick'inden çağrılır.
void DALI_Send_Test_Tick(DALI_t* self){
    DALI_Write_Bit(self,1);
}
