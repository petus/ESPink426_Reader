// ============================================================================
//  config.h  -  Konfigurace pro ctecku eknih na LaskaKit ESPink-4.26 (ESP32-S3)
// ----------------------------------------------------------------------------
//  Piny podle SCHEMATU desky (ESPink426_v1-4).
//
//  POZOR: README vyrobce na GitHubu uvadi pro "SPI sbernici" piny
//  CS 46 / MOSI 37 / CLK 14 / MISO 21. To je popis uSup KONEKTORU
//  (a i tam ma MOSI chybne), NIKOLI microSD karty. Nezamenovat.
//
//  Displej: Good Display GDEQ0426T82, NATIVNE 480x800 (na vysku), BW.
//  Trida GxEPD2: GxEPD2_426_GDEQ0426T82
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
//  PREFIX RDR_
//  Vsechny makra tohoto projektu maji prefix RDR_ (reader). Duvod: varianty
//  desek v pins_arduino.h definuji obecna jmena jako SD_CS, I2C_SDA nebo
//  MOSI/MISO/SCK, a priklady GxEPD2 pouzivaji EPD_CS/EPD_DC/EPD_RST/EPD_BUSY.
//  Bez prefixu by pri zmene desky nebo pri kopirovani ukazkoveho kodu
//  vznikaly hlasky typu "redefinition of ...".
//
//  Prefix take odlisuje tento projekt od firmwaru MeteoEink na teze desce,
//  ktery pouziva vlastni sadu jmen (PIN_POWER, EPD_CS, FW_VERSION, ...).
// ---------------------------------------------------------------------------

// ===========================================================================
//  SPI SBERNICE - SDILENA MEZI ePAPEREM A SD KARTOU
// ---------------------------------------------------------------------------
//  Spolecne:  CLK 12, MOSI 11
//  Rozdilne:  CS  (ePaper 10, SD 17)
//             MISO (jen SD, pin 13 - ePaper se pouze zapisuje)
//
//  DUSLEDEK PRO FIRMWARE:
//   - Pouzije se JEDINA instance SPI (nikoli dve!).
//   - ePaper a SD nesmi komunikovat soucasne. Kazdy pristup se ohranicuje
//     vlastnim CS; GxEPD2 i SD knihovna pouzivaji SPI transakce, takze
//     pri korektnim poradi volani si nelezou do cesty.
//   - NIKDY nekreslit na displej uprostred cteni souboru a naopak.
// ===========================================================================
#define RDR_SPI_SCK     12    // spolecne hodiny
#define RDR_SPI_MOSI    11    // spolecna data ven
#define RDR_SPI_MISO    13    // data dovnitr - vyuziva jen SD karta

// Taktovaci frekvence SPI pro ePAPER.
//
// GxEPD2 pouziva ve vychozim stavu jen 4 MHz. Ramec 800x480 po jednom bitu
// ma 48 000 bajtu a mereni ukazalo, ze samotny PRENOS dat trva 541 ms
// (u plneho refreshe 721 ms, protoze se buffer posila dvakrat) - vic nez
// samotne kresleni e-inku. Zvysenim frekvence se tento cas zkrati.
//
// Kdyby se na displeji objevily artefakty nebo by se prestal prekreslovat,
// snizte hodnotu (napr. na 8 nebo 4 MHz).
#define RDR_EPD_SPI_HZ  10000000UL

// Karta na sdilene sbernici ma vlastni frekvenci - kazde zarizeni si
// nastavuje parametry pro svou transakci, takze se navzajem neomezuji.
#define RDR_SD_SPI_HZ   10000000UL

// --- ePaper ----------------------------------------------------------------
#define RDR_EPD_CS      10
#define RDR_EPD_DC      48
#define RDR_EPD_RST     45
#define RDR_EPD_BUSY    38
// (ePaper MISO nema - je write-only)

// --- microSD karta ---------------------------------------------------------
#define RDR_SD_CS       17
#define RDR_SD_INSERT   16    // detekce vlozene karty
                          // GPIO16 JE RTC pin (rozsah 0-21), takze by sel
                          // pouzit i pro ext0/ext1 probuzeni z deep-sleep.
                          // Polaritu spinace overit na desce (obvykle LOW
                          // pri vlozene karte).

// ===========================================================================
//  uSup SPI KONEKTOR - SAMOSTATNA, NEZAVISLA SBERNICE
//  Ctecka ji nepouziva. Uvedeno, aby se piny omylem nepouzily pro neco jineho.
// ===========================================================================
#define RDR_USUP_CS    46
#define RDR_USUP_MOSI   3
#define RDR_USUP_SCK   14
#define RDR_USUP_MISO  21

// ===========================================================================
//  DISPLEJ - rozmery a orientace
//
//  DULEZITE: GxEPD2 povazuje tento panel za NATIVNE NA SIRKU.
//  Trida GxEPD2_426_GDEQ0426T82 ma WIDTH = 800 a HEIGHT = 480. Je to videt
//  i v hlaskach prekladace: GxEPD2_BW<GxEPD2_426_GDEQ0426T82, 480>.
//  Rotace 0 tedy dava 800x480, tedy NA SIRKU.
//
//  Pro ctecku NA VYSKU je nutna ROTACE 1 (nebo 3) -> 480x800.
// ===========================================================================
#define RDR_PANEL_W    800     // nativni sirka pri rotaci 0
#define RDR_PANEL_H    480     // nativni vyska pri rotaci 0

#define RDR_ROTATION   3       // 3 = na vysku, otocene o 180 stupnu  <-- ctecka
                               // 1 = na vysku (480x800)
                               // 0 nebo 2 = na sirku (800x480)

#if (RDR_ROTATION == 1) || (RDR_ROTATION == 3)
  #define RDR_SCREEN_W  RDR_PANEL_H       // 480
  #define RDR_SCREEN_H  RDR_PANEL_W       // 800
#else
  #define RDR_SCREEN_W  RDR_PANEL_W       // 800
  #define RDR_SCREEN_H  RDR_PANEL_H       // 480
#endif

// ===========================================================================
//  NAPAJENI PERIFERII - GPIO47, aktivni v HIGH
//  Spina ePaper a periferie na uSup konektorech.
//
//  GPIO47 NENI RTC pin, takze se jeho stav v deep-sleep neudrzi sam.
//  Pro zachovani LOW behem spanku je nutne gpio_deep_sleep_hold_en().
// ===========================================================================
#define RDR_POWER_PIN   47
#define RDR_POWER_SETTLE_MS   50

// ===========================================================================
//  TLACITKA - vsechna AKTIVNI V LOW
//
//  POZOR: GPIO39/40/41 NEJSOU RTC GPIO (ext0/ext1 wake podporuje jen 0-21),
//  takze z DEEP-SLEEP timito tlacitky probudit nelze - slouzi k tomu RESET.
//  V LIGHT-SLEEP funguje probuzeni pres gpio_wakeup_enable() na libovolnem pinu.
// ===========================================================================
#define RDR_BTN_UP     39   // predchozi stranka
#define RDR_BTN_PUSH   40   // menu / potvrzeni / uspani
#define RDR_BTN_DOWN   41   // dalsi stranka

// ===========================================================================
//  I2C (osazeny SHT40, cidla na uSup) - ctecka nepouziva
// ===========================================================================
#define RDR_I2C_SDA    42
#define RDR_I2C_SCL     2

// ===========================================================================
//  MERENI BATERIE - napetovy delic na IO9
//  Deska nema fuel gauge. Hodnota je okamzite NAPETI, ne stav nabiti, takze
//  pod zatezi klesa. Merit v klidu PRED prekreslenim displeje a prumerovat.
// ===========================================================================
#define RDR_VBAT_PIN         9
#define RDR_VBAT_DIVIDER     1.769388f
#define RDR_VBAT_SAMPLES     16

#define RDR_VBAT_FULL        4.20f
#define RDR_VBAT_EMPTY       3.30f
#define RDR_VBAT_LOW         3.50f    // varovani na displeji
#define RDR_VBAT_NO_WRITE    3.30f    // pod tim nezapisovat na SD
#define RDR_VBAT_CRITICAL    RDR_VBAT_LOW

// ===========================================================================
//  CHOVANI SPANKU
// ===========================================================================
// ---------------------------------------------------------------------------
//  ZPUSOB CEKANI NA TLACITKO
//   0 = polling se snizenym taktem CPU (VYCHOZI, spolehlive, ~20-40 mA)
//   1 = light-sleep (VYCHOZI, radove usporne)
//
//  Drivejsi restarty rst:0x10 (RTCWDT_RTC_RST) mely pricinu v tom, ze
//  attachInterrupt() a gpio_wakeup_enable() zapisuji do TEHOZ registru
//  typu udalosti. Probouzeni prepsalo hranove preruseni na urovnove,
//  takze po probuzeni s drzenym tlacitkem se preruseni spoustelo stale
//  dokola a watchdog desku resetoval. Nyni se preruseni pred spankem
//  odpojuje a po probuzeni zase navesuje.
// ---------------------------------------------------------------------------
#define RDR_USE_LIGHT_SLEEP   1

#define RDR_IDLE_TIMEOUT_MS       (5UL * 60UL * 1000UL)   // 5 minut do deep-sleep
#define RDR_BTN_DEBOUNCE_MS       35
#define RDR_BTN_SETTLE_MS         70    // jak dlouho musi byt tlacitka klidna
                                        // po akci, nez se zahodi zamek
                                        // (osetruje zakmit pri UVOLNENI)
#define RDR_LONG_PRESS_MS         2000    // dlouhy stisk PUSH = o uroven zpet
#define RDR_REPEAT_DELAY_MS       500     // za jak dlouho zacne autorepeat
#define RDR_REPEAT_RATE_MS        160     // perioda autorepeatu UP/DOWN
#define RDR_FULL_REFRESH_EVERY    8       // kazdych N prekresleni plny refresh

// ---------------------------------------------------------------------------
//  ROZVRZENI OBRAZOVKY (na vysku, 480x800)
// ---------------------------------------------------------------------------
#define RDR_MARGIN_X       22      // levy a pravy okraj sazby i seznamu
#define RDR_HEADER_H       76      // horni pruh se zalozkami
#define RDR_STATUS_H       40      // spodni stavovy radek
#define RDR_ROW_H          48      // vyska radku v seznamu knih
#define RDR_LIST_TOP       (RDR_HEADER_H + 8)
#define RDR_LIST_BOTTOM    (RDR_SCREEN_H - RDR_STATUS_H)
#define RDR_LIST_ROWS      ((RDR_LIST_BOTTOM - RDR_LIST_TOP) / RDR_ROW_H)

// ---------------------------------------------------------------------------
//  KNIHOVNA
// ---------------------------------------------------------------------------
#define RDR_MAX_BOOKS      128     // strop poctu knih drzenych v pameti
#define RDR_POS_SUFFIX     ".pos"  // sidecar se stavem: kniha.book.pos

// Velikost pisma pri cteni: 0 = male, 1 = stredni, 2 = velke
#define RDR_FONT_DEFAULT   1

// ===========================================================================
//  FONTY - KRITICKE PRO CESTINU
//  U8g2 suffixy:  _tr = ASCII
//                 _tf = + Latin-1           <- NESTACI (chybi c r e s z u)
//                 _te = + Latin Extended-A  <- TOHLE pouzivat
// ===========================================================================
// Tri velikosti pisma pri cteni (male / stredni / velke)
// Puvodni nejmensi stupen (helvR10) byl na cteni prilis maly, proto je
// cela skala posunuta o stupen nahoru: to, co bylo "stredni", je nyni "male".
#define RDR_FONT_BODY_S      u8g2_font_helvR12_te
#define RDR_FONT_BODY_M      u8g2_font_helvR14_te
#define RDR_FONT_BODY_L      u8g2_font_helvR18_te
#define RDR_FONT_HEADING_S   u8g2_font_helvB12_te
#define RDR_FONT_HEADING_M   u8g2_font_helvB14_te
#define RDR_FONT_HEADING_L   u8g2_font_helvB18_te
#define RDR_FONT_CHAPTER_S   u8g2_font_helvB14_te
#define RDR_FONT_CHAPTER_M   u8g2_font_helvB18_te
#define RDR_FONT_CHAPTER_L   u8g2_font_helvB24_te

// Fonty rozhrani (seznam, zalozky, stavovy radek)
#define RDR_FONT_TAB       u8g2_font_helvB12_te
#define RDR_FONT_ROW       u8g2_font_helvR12_te
#define RDR_FONT_ROW_SUB   u8g2_font_helvR10_te
#define RDR_FONT_BIG       u8g2_font_helvB18_te
#define RDR_FONT_STATUS    u8g2_font_helvR10_te

// ===========================================================================
//  REZIM NAHRAVANI KNIH PRES WIFI (portal)
// ===========================================================================
#define RDR_PORTAL_HOLD_MS     5000     // podrzeni PUSH pri startu pro spusteni
#define RDR_PORTAL_TIMEOUT_MS  (5UL*60UL*1000UL)  // vypnuti po necinnosti
#define RDR_STA_CONNECT_MS     15000    // cekani na pripojeni k domaci WiFi
#define RDR_STA_MAX_RETRIES    5        // po kolika vypadcich prejit na hotspot
#define RDR_MDNS_NAME          "ctecka" // -> ctecka.local
#define RDR_MIN_FREE_KB        512      // minimum volneho mista pro nahravani
// Jak casto nejvys prekreslovat prubeh konverze EPUBu.
// Kazde prekresleni e-inku stoji ~0,5 s bez ohledu na velikost plochy,
// takze caste hlaseni prubehu je drazsi nez sama konverze.
#define RDR_CONV_PROGRESS_MS   4000

#define RDR_NAME_MAX           72       // max delka jmena souboru bez pripony
                                        // (drive 40 - dlouhe nazvy se rezaly
                                        //  uprostred a ztracely smysl)
#define RDR_WIFI_VBAT_MIN      3.60f    // pod tim WiFi vubec nespoustet
                                        // (spicky pres 300 mA -> brownout)

// ===========================================================================
//  DIAGNOSTIKA
//  Odkomentujte pro vypis na seriovou linku (duvod resetu, prechody stavu).
//  USB CDC On Boot musi byt Disabled - deska nema nativni USB.
// ===========================================================================
#define RDR_DEBUG_SERIAL  1      // 0 = vypnout ladici vypisy
#define RDR_DEBUG_BAUD    115200

// ===========================================================================
//  SOUBORY NA SD
// ===========================================================================
#define RDR_BOOKS_DIR       "/books"
#define RDR_STATE_FILE      "/reader.state"

// ===========================================================================
//  NASTAVENI ARDUINO IDE
//    Board: ESP32S3 Dev Module, Flash 16MB
//    PSRAM: neni potreba
//    USB Mode: "USB-OTG (TinyUSB)"   <- nutne pro rezim USB disku
// ===========================================================================
