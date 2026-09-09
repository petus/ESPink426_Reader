// ============================================================================
//  usbmsc.h  -  Rezim "deska jako USB flashdisk" (SD karta pres USB-C)
// ----------------------------------------------------------------------------
//  Po pripojeni k pocitaci se microSD karta vystavi jako USB Mass Storage
//  (flashdisk). Pouziva nativni USBMSC z ESP32 Arduino core + SD.readRAW/
//  writeRAW pro pristup po sektorech (LBA).
//
//  DULEZITE - sdileni SD karty:
//   Firmware (cteni knih) a pocitac (USB MSC) NESMI k SD pristupovat
//   soucasne, jinak hrozi poskozeni filesystemu. Proto se rezim voli hned
//   po startu: kdyz je pripojen USB kabel k PC -> USB disk mod (ctecka
//   knihu needituje). Kdyz neni -> normalni ctecka.
//
//   V USB disk modu si SD "vlastni" USB vrstva; firmware jen obsluhuje
//   read/write callbacky na urovni sektoru a jinak necte soubory.
//
//  Pozn. k rychlosti: ESP32-S3 ma jen USB 1.1 Full Speed, takze prenos je
//  radove stovky kB/s az jednotky MB/s. Pro nahravani .book souboru (male
//  textove soubory) naprosto dostacuje.
//
//  Vyzaduje: Arduino IDE -> Tools -> USB Mode: "USB-OTG (TinyUSB)".
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "config.h"

// ---------------------------------------------------------------------------
//  Rezim USB disku vyzaduje, aby bylo v Arduino IDE zvoleno
//    Tools -> USB Mode: "USB-OTG (TinyUSB)"
//  Pri volbe "Hardware CDC and JTAG" tridy USB/USBMSC vubec neexistuji
//  a preklad by spadl na neprehlednych hlaskach. Radeji to rekneme rovnou.
// ---------------------------------------------------------------------------
#if ARDUINO_USB_MODE
  #warning "USB disk mod je vypnuty: v Tools > USB Mode zvolte 'USB-OTG (TinyUSB)'."
  #define RDR_USB_MSC_AVAILABLE 0
#else
  #include "USB.h"
  #include "USBMSC.h"
  #define RDR_USB_MSC_AVAILABLE 1
#endif

#if RDR_USB_MSC_AVAILABLE
// ---- USB MSC je dostupne ----------------------------------------------------

static USBMSC g_msc;
static bool   g_mscReady = false;
static uint32_t g_sectorCount = 0;
static const uint16_t RDR_MSC_SECTOR_SIZE = 512;

// --- Callback: pocitac cte sektory z karty ---------------------------------
static int32_t msc_onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  uint32_t count = bufsize / RDR_MSC_SECTOR_SIZE;
  for (uint32_t i = 0; i < count; i++) {
    if (!SD.readRAW((uint8_t*)buffer + i * RDR_MSC_SECTOR_SIZE, lba + i)) {
      return -1;
    }
  }
  return bufsize;
}

// --- Callback: pocitac zapisuje sektory na kartu ---------------------------
static int32_t msc_onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  uint32_t count = bufsize / RDR_MSC_SECTOR_SIZE;
  for (uint32_t i = 0; i < count; i++) {
    if (!SD.writeRAW(buffer + i * RDR_MSC_SECTOR_SIZE, lba + i)) {
      return -1;
    }
  }
  return bufsize;
}

// --- Callback: pocitac chce kartu "vysunout" / spustit ---------------------
static bool msc_onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  // start=false + load_eject=true => uzivatel bezpecne odebira disk.
  // Nic zvlastniho delat nemusime, sektorovy pristup je bezstavovy.
  return true;
}

// ---------------------------------------------------------------------------
//  Detekce pripojeni k pocitaci pres USB.
//  Nejspolehlivejsi zpusob na ESP32-S3 je zkontrolovat, zda je na USB VBUS
//  napeti. Arduino core to nevystavuje primo, proto pouzivame duvod resetu:
//  po pripojeni USB a stisku RESET je duvod POWERON. Praktictejsi je vsak
//  detekovat VBUS pres GPIO nebo se zeptat uzivatele tlacitkem.
//
//  Zde volime jednoduche, spolehlive reseni: pri startu kratce pockame a
//  necháme uzivatele podrzet tlacitko PUSH pro vstup do USB disk modu.
//  (Bez podrzeni nabootuje ctecka normalne.)
//
//  Alternativa: kdo ma vyvedeny VBUS na GPIO, muze detekovat automaticky.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Spusti USB MSC rezim. SD uz musi byt inicializovana (SD.begin) volajicim.
//  Vraci true pri uspechu.
// ---------------------------------------------------------------------------
inline bool usbMscBegin() {
  g_sectorCount = SD.numSectors();
  if (g_sectorCount == 0) return false;

  g_msc.vendorID("LaskaKit");        // max 8 znaku
  g_msc.productID("ESPink Book");    // max 16 znaku
  g_msc.productRevision("1.0");      // max 4 znaky
  g_msc.onRead(msc_onRead);
  g_msc.onWrite(msc_onWrite);
  g_msc.onStartStop(msc_onStartStop);
  g_msc.mediaPresent(true);
  g_msc.begin(g_sectorCount, RDR_MSC_SECTOR_SIZE);

  USB.begin();
  g_mscReady = true;
  return true;
}

inline bool usbMscIsReady() { return g_mscReady; }
inline uint32_t usbMscSectorCount() { return g_sectorCount; }

#else
// ---- USB MSC neni dostupne (spatny USB Mode) --------------------------------
// Zastupne funkce, aby se skec prelozil a firmware fungoval jako bezna ctecka.
inline bool usbMscBegin()        { return false; }
inline bool usbMscIsReady()      { return false; }
inline uint32_t usbMscSectorCount() { return 0; }
#endif  // RDR_USB_MSC_AVAILABLE

// ---------------------------------------------------------------------------
//  Detekce pozadavku na USB disk mod - funguje vzdy, cte jen tlacitko.
// ---------------------------------------------------------------------------
inline bool usbDiskModeRequested() {
  // Podrzeni tlacitka PUSH pri startu (do 1.2 s) => USB disk mod.
  pinMode(RDR_BTN_PUSH, INPUT_PULLUP);
  unsigned long t0 = millis();
  while (millis() - t0 < 1200) {
    if (digitalRead(RDR_BTN_PUSH) == LOW) {
      // pockej na potvrzeni ze stisk trva aspon 300 ms (ne zakmit)
      unsigned long h = millis();
      while (digitalRead(RDR_BTN_PUSH) == LOW) {
        if (millis() - h > 300) return true;
        delay(10);
      }
    }
    delay(10);
  }
  return false;
}
