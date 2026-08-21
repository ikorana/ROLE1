## Fabrika Test Sonuçları (2026-08-21)
ROLE1 kartı fiziksel olarak test edildi: **RS485 test ekranı fonksiyonları
düzgün çalışıyor**, **DALI ekranı fonksiyonları düzgün çalışıyor** (DALI
taramasında cevap veriyor), **telefon05 üzerinden aksiyonlar normal**.
Tuş testi (checkmark) de dahil, ROLE1 için görünür bir sorun yok — testi
geçti.

# ROLE1 — TC5000MH-20I Panel Üzerinden Doğrudan RS485 Erişimi

## Amaç
TC5000MH-20I (Taichuan, Allwinner A133, Android 10) paneli, ANA_KUTU02/ESP32
zincirine hiç ihtiyaç duymadan, **kendi RS485 çıkışı üzerinden doğrudan ROLE1**
kartlarıyla konuşabiliyor. Bu, panel üzerinde koşacak yeni bir uygulama için
temel: panel kendi başına bir "ana kutu" gibi davranıp ROLE1'leri (ve muhtemelen
ROLE_EK'i) doğrudan yönetebilir.

**Durum: uçtan uca doğrulandı (2026-08-19).** `rl1_get_id` komutu gönderildi,
ROLE1'den gerçek cevap alındı: `{"com":"rl1_get_id","id":31,"frmtype":1}#`

## Cihaz Bilgileri
- Model: **TC5000MH-20I** (Taichuan üretimi Android panel)
- `ro.product.model`: `A133-PRODUCT-L`, `ro.product.manufacturer`: `Allwinner`
- Android 10, ethernet + wifi var
- adb ile erişilebiliyor (o anki IP: `192.168.1.52:5678` — DHCP ile değişebilir)
- Testtool'un "获取四元组信息" (dörtlü/donanım kimlik bilgisi) ekranından:
  - 机身码 (cihaz kod/seri no): `AF240613000580`
  - 系统版本 (sistem versiyonu): `TC-5000 MH-201-A133-28R13_CTPJJV-9fs-V1.0.0-20240429`
  - TcTool yazılım versiyonu: `V1.2.0_build20230829`, 版本号 (build no): `2000`
- İlgili paketler: `com.taichuan.boardsdk` (system UID, GPIO/röle kontrolü —
  `GpioInit`/`GpioControl`/`GpioGet`/`ReadAllGpio`, native lib `liba133board.so`),
  `com.taichuan.testtool` (fabrika test aracı, `GpioFragment` içeriyor — arayüzü
  Çince, kullanıcı için okunması zor),
  `com.taichuan.i5` / `ThirdApp.apk` (ana launcher/intercom uygulaması)

## RS485 Bağlantı Parametreleri (doğrulanmış)
| Parametre | Değer |
|---|---|
| Port | `/dev/ttyS3` |
| Baud | **28800** (ROLE1 ve ANA_KUTU02 firmware'iyle birebir aynı) |
| Format | 8N1 (8 veri biti, parity yok, 1 stop bit) |
| A/B polaritesi | Panelin bir ucundan diğerine göre **ters** olabilir — test sırasında bir kez çevirmek gerekti |
| GND | Bağlı olmasa da çalıştı (zorunlu değilmiş, teorik olarak da doğru) |
| İzinler | `/dev/ttyS3` dünyaya açık izinlerle (`rw-rw-rw-`, `system:system`), SELinux **Permissive** modda — üçüncü parti bir uygulama root/sistem yetkisi olmadan doğrudan açıp kullanabilir |
| `/dev/ttyS2` | Bu panelde test edilen RS485 hattına bağlı DEĞİL (sinyal yok) |
| `/dev/ttyS0` | root-only, muhtemelen debug konsolu |
| `/dev/a133gpio` | Üreticinin özel röle/GPIO cihaz dosyası (`crw-rw-rw-`) — protokolü çözüldü, aşağıya bakın |

## Panelin Kendi GPIO/Röle Kontrolü (çözüldü, 2026-08-19)

`/dev/a133gpio` de dünyaya açık izinlerle duruyor (`crw-rw-rw-`, SELinux
Permissive) — root/sistem yetkisi olmadan üçüncü parti bir uygulamadan
doğrudan kullanılabilir. Boardsdk'nın `liba133board.so` kütüphanesini
(JNI, `Java_com_a133_a133board_MainFun_*`) dexdump + disassembly ile
inceleyip tam çağrı zincirini çözdük:

`BoardClient.getInstance().controlRelay(channel, isOpen)`
→ `BoardService.controlRelay` → `A133BoardManager.controlRelay`
→ arkaplan thread'de `MainFun.GpioControl(byte[20])` (ioctl `A133GPIO_IOCGPIO_CTRL`)

**Kendi uygulamamızda yeniden üretmek için gereken adımlar:**
1. `liba133board.so`'yu (armeabi) uygulamaya native lib olarak dahil et.
2. `MainFun.GpioInit(0)` bir kere çağır — `/dev/a133gpio`'yu açar (`g_fd_gpio`).
3. Röle kontrolü için `MainFun.GpioControl(byte[20])` çağır, format:

| Bayt aralığı | İçerik |
|---|---|
| 0-4 | `"relay"` (ASCII: `72 65 6C 61 79`) |
| 5 | Kanal numarası ASCII karakteri: `'1'`,`'2'`,`'3'`,`'4'` (1-4 arası kanal) |
| 6-15 | `0x00` (10 sıfır bayt) |
| 16 | `'1'` (0x31) = AÇ, `'0'` (0x30) = KAPAT |
| 17-19 | `0x00` |

4. İşin bitince `MainFun.GpioDeInit()`.

JNI fonksiyon imzaları (`com.a133.a133board.MainFun`, native metod):
- `GpioInit(int): int`
- `GpioControl(byte[]): int`
- `GpioDeInit(): int`
- `ReadAllGpio(byte[]): int` (formatı henüz çözülmedi — muhtemelen benzer bir
  komut-string yapısı, `GpioControl`'e paralel araştırılabilir)
- `GetVersion()`, `ReadIMEI()`, `WriteIMEI()`, `Watchdogfeed()` de var (ilgili
  değil, tamlık için not edildi)

**Not:** `IBoardClient`/`BoardClient`/`BoardService`/`A133BoardManager` Java
sınıflarının kendisini kopyalamaya gerek yok — sadece `liba133board.so`'yu ve
yukarıdaki byte formatını kendi kodumuzda (Kotlin/JNI native metod deklarasyonu
ile) yeniden üretmek yeterli.

### DÜZELTME (2026-08-19, fiziksel test sonrası) — BU PANELDE FİZİKSEL RÖLE YOK

Yukarıdaki API çağrısı yazılımsal olarak doğru ve hatasız çalışıyor (BoardSdk
test uygulamasında `继电器1开`/`继电器2开` vb. — "Röle 1/2 Aç" butonlarına
basıldı), AMA **hiçbir fiziksel etki gözlenmedi** — röle sesi yok, hareket yok.

Kartın arkasındaki etikete (`TC-5000MH-201/WF/485/CM`, "ECT Security" —
alarm paneli) ve panonun fiziksel incelemesine göre sebebi netleşti:

- **GP1-GP8 (8 pinli J1 konnektörü) çıplak GPIO pinleri** — `GpioControl`
  API'si bunları hem giriş hem çıkış yapabilir, ama şu anki PCB'de her hattın
  ucunda bir **direnç + kapasitör (RC pull-up/down + gürültü filtresi)**
  var — bu, klasik bir GİRİŞ devresi (örn. alarm zonu/sensör kontağı okumak
  için), çıkış sürmek için değil. Zaten dışarıya giden kablo da bir sensöre
  bağlanmak üzere organize edilmiş.
- Panelde **fiziksel röle donanımı yok** — `继电器N开/关` (Röle N Aç/Kapat)
  yazılım seviyesinde `MainFun.GpioControl` çağrısını doğru yapıyor (ioctl
  başarıyla dönüyor olabilir) ama arkasında sürecek bir röle bobini/kontağı
  fiziksel olarak mevcut değil.
- **Panelde önceden hazır, gerçekten çalışan TEK çıkış: Doorbell** (etikette
  ayrı 2 pinlik bir konnektör olarak var) — bu muhtemelen içeride gerçek bir
  sürücü/röle ile donatılmış.

**Sonuç — iki kullanım yolu var:**
1. **Giriş (input) olarak kullan**: GP pinlerini mevcut RC/kablo düzenine
   uygun şekilde `GpioInit`+uygun bir "read" çağrısıyla (muhtemelen
   `ReadAllGpio`, formatı henüz çözülmedi) okuyup dışarıdaki sensörün/kontağın
   durumunu izleyebiliriz — donanım değişikliği gerekmez.
2. **Çıkış (output) olarak kullan**: GP pinini `GpioControl` ile lojik
   seviyede sürüp, bu sinyali **harici bir transistör** (BJT/MOSFET, röle
   bobini sürülecekse flyback diyotlu) üzerinden gerçek bir röleyi/yükü
   anahtarlamak için kullanmamız gerekiyor — panel bunu kendi başına
   yapmıyor, ek donanım (transistör devresi) eklememiz şart.

Hangi yolu seçeceğimize henüz karar verilmedi.

## Protokol Notu — KRİTİK (RS485/rl1_)
ROLE1'in `HAL_UARTEx_RxEventCallback`'i (main.c:1012), tamponda **`#` karakteri
bulamazsa `command_process()`'i hiç çağırmıyor** — mesaj sessizce yok sayılıyor.
Normal akışta bu `#`'ı ANA_KUTU02 (`command_process.cpp:110`, `strcat(mm,"#\n")`)
ekliyor; panelden DOĞRUDAN göndereceğimiz için bu terminatörü **bizim eklememiz
şart**:

```
{"com":"rl1_get_id"}#\n
```

Bunu unutmak, "sinyal geliyor (`HAL_UARTEx_RxEventCallback`/kesme tetikleniyor)
ama hiç cevap yok" şeklinde yanıltıcı bir semptoma yol açıyor — kesme geliyor
diye protokolün doğru olduğu sanılmasın.

## Debug Yöntemi (adb ile, gdb/ST-Link olmadan)
Panelde `stty` (toybox/busybox) **28800 gibi standart olmayan bir baud'u
kabul etmiyor** ("unknown speed"). Çözüm: NDK ile küçük bir native (aarch64)
test programı yazıp `TCSETS2`/`BOTHER` ioctl'iyle tam baud rate'i ayarlamak.
Kaynak: `/tmp/tc5000_apks/rs485_test.c` (bu oturumda yazıldı, kalıcı bir yere
taşınmadı — gerekirse yeniden oluşturulabilir, mantığı: `open()` +
`ioctl(TCSETS2, BOTHER, c_ispeed=c_ospeed=28800)` + `write()` + `poll()`+`read()`).

Kullanım: `adb shell /data/local/tmp/rs485_test /dev/ttyS3 28800` (tek atım,
cevap bekler) ya da `... 28800 <saniye>` (sürekli tekrar gönderim — skopta
sinyal yakalamak için).

## Sorun Giderme Kronolojisi (aynı hatalara düşmemek için)
1. `/dev/ttyS2` ve `/dev/ttyS3` ikisi de denendi → S3 doğru port (S2'de hiç
   sinyal yok, skopla doğrulandı).
2. Yanlış baud (9600/38400 — panelin varsayılanları) denendi → cevap yok.
   Gerçek baud'u **ROLE1 firmware kaynağından** (`huart2.Init.BaudRate`)
   doğrulamak gerekti, tahmin etmemeliydik.
3. A/B ters bağlıydı → skop + ROLE1 debugger ile doğrulandı, fiziksel olarak
   çevrilince `HAL_UARTEx_RxEventCallback` tetiklenmeye başladı.
4. `#` terminatörü unutulmuştu → kesme geliyor ama `command_process`'e hiç
   girmiyordu (main.c:1012-1023'teki `found` kontrolü). Terminatör eklenince
   `HAL_UART_ErrorCallback`'e düşmeye başladı (framing/noise hatası) —
   bu da muhtemelen A/B'nin o sıradaki yanlış yönelimiyle ilişkiliydi.
5. A/B tekrar çevrildi (protokol zaten doğruyken) → tam ve doğru cevap alındı.

## Sıradaki Adım (planlanan)
Bu bulgularla panelin üzerine, ROLE1'leri (ve muhtemelen ROLE_EK'i) doğrudan
RS485 üzerinden yöneten bir **uygulama** geliştirilecek — ANA_KUTU02/ESP32
zincirine ihtiyaç duymadan. Henüz başlanmadı, tasarım/kapsam netleşmedi.

Açık karar noktası: panelin kendi GP1-8 pinlerini **input** (mevcut kablo/RC
düzenine uygun, sensör okuma) olarak mı kullanacağız, yoksa **output**
(harici transistör devresiyle gerçek röle/yük sürme) olarak mı geliştirmek
isteyeceğiz — bkz. yukarıdaki "DÜZELTME" bölümü. 2026-08-19'da yarım kaldı,
devam edilecek.
