// ============================================================================
//  buttons.h  -  Obsluha tlacitek a cekani na uzivatele
// ----------------------------------------------------------------------------
//  PROC VLASTNI HLAVICKA A NE PRIMO V .ino:
//   Arduino IDE generuje prototypy funkci a vklada je NAD uzivatelsky kod.
//   Kdyby struktura BtnEvent byla az v .ino, prototyp "BtnEvent waitForButton();"
//   by vznikl driv nez sama struktura a preklad by skoncil hlaskou
//   "'BtnEvent' does not name a type".
//
// ----------------------------------------------------------------------------
//  CEKANI NA TLACITKO - DVE VARIANTY
//
//  A) POLLING (zaloha, RDR_USE_LIGHT_SLEEP = 0)
//     Aktivni cekani se snizenym taktem CPU. Spolehlive, ale bere radove
//     20-40 mA. Pro cteni to staci: pri 1000 mAh vydrzi baterie desitky
//     hodin ciste doby cteni a po 5 minutach necinnosti se deska uspi
//     do deep-sleepu (~15 uA).
//
//  B) LIGHT-SLEEP (VYCHOZI, RDR_USE_LIGHT_SLEEP = 1)
//     Radove uspornejsi - misto desitek mA jednotky mA nebo mene.
//
//     Drivejsi restarty rst:0x10 (RTCWDT_RTC_RST) NEBYLY chybou ESP-IDF,
//     ale nasi: attachInterrupt() a gpio_wakeup_enable() zapisuji do TEHOZ
//     registru typu udalosti, takze probouzeni prepsalo hranove preruseni
//     na urovnove. Po probuzeni s drzenym tlacitkem se pak preruseni
//     spoustelo stale dokola a watchdog desku resetoval.
//     Nyni se preruseni pred spankem odpojuje a po probuzeni navesuje zpet.
//
//  Tlacitka jsou na GPIO39/40/41, ktere NEJSOU RTC piny. Z deep-sleepu
//  jimi probudit nelze - slouzi k tomu RESET nebo vypnuti napajeni.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "config.h"
#include "log.h"

// ---------------------------------------------------------------------------
//  ZACHYTAVANI STISKU NA POZADI (preruseni)
//
//  PROBLEM: prekresleni e-inku trva 0,4 az 3,5 s a po tu dobu kod nikde
//  necte tlacitka. Stisk behem refreshe se proto ztratil a uzivateli to
//  pripadalo, ze tlacitko "nekdy nereaguje".
//
//  RESENI: na vsechny tri piny je navesene preruseni, ktere stisk zapise
//  do zamku (latch). waitForButton() pak nejdriv zkontroluje zamek, takze
//  se obslouzi i stisk, ktery padl doprostred kresleni.
//
//  Debounce se resi uz v obsluze preruseni casovym oknem - mechanicke
//  tlacitko generuje pri sepnuti nekolik hran.
// ---------------------------------------------------------------------------
static volatile uint8_t  g_btnLatch = 0;      // bitova maska zachycenych stisku

// Stav pro potlaceni zakmitu. Ke kazdemu tlacitku si pamatujeme cas
// posledni hrany a posledni USTALENOU uroven.
static volatile uint32_t g_edgeAt[3]    = { 0, 0, 0 };
static volatile uint8_t  g_stable[3]    = { 1, 1, 1 };   // 1 = HIGH (nestisknuto)

#define RDR_LATCH_UP    0x01
#define RDR_LATCH_PUSH  0x02
#define RDR_LATCH_DOWN  0x04

// ---------------------------------------------------------------------------
//  POTLACENI ZAKMITU PRIMO V OBSLUZE PRERUSENI
//
//  Mechanicky kontakt zakmitava pri sepnuti I pri rozepnuti. Puvodne bylo
//  preruseni na SESTUPNOU HRANU, takze zakmit pri UVOLNENI vypadal jako
//  novy stisk. Resil jsem to tim, ze se po kazde akci zamek vyprazdnil -
//  jenze to zahodilo i SKUTECNY stisk, ktery uzivatel udelal behem
//  prekreslovani displeje. Projevovalo se to tim, ze rychly druhy stisk
//  "propadl" a musel se zopakovat.
//
//  Ted je preruseni na OBE HRANY a rozhoduje se podle sledu:
//   - zakmit je shluk hran; zapocita se jen PRVNI hrana shluku,
//     dalsi behem RDR_BTN_DEBOUNCE_MS se ignoruji
//   - shluk pri stisku zacina hranou dolu  -> zaznamena se stisk
//   - shluk pri uvolneni zacina hranou nahoru -> nezaznamena se nic
//  Zamek uz proto neni potreba plosne maza a skutecne stisky se neztraci.
// ---------------------------------------------------------------------------
static void IRAM_ATTR btnEdge(int idx, int pin, uint8_t bit) {
  uint32_t now = millis();
  if (now - g_edgeAt[idx] < RDR_BTN_DEBOUNCE_MS) return;   // uvnitr shluku
  g_edgeAt[idx] = now;

  uint8_t level = (uint8_t)digitalRead(pin);
  if (level == g_stable[idx]) return;                      // zadna zmena stavu
  g_stable[idx] = level;

  if (level == 0) g_btnLatch |= bit;                       // sestup = stisk
}

static void IRAM_ATTR isrUp()   { btnEdge(0, RDR_BTN_UP,   RDR_LATCH_UP);   }
static void IRAM_ATTR isrPush() { btnEdge(1, RDR_BTN_PUSH, RDR_LATCH_PUSH); }
static void IRAM_ATTR isrDown() { btnEdge(2, RDR_BTN_DOWN, RDR_LATCH_DOWN); }

struct BtnEvent {
  int  pin       = 0;       // 0 = zadna udalost (zakmit)
  bool longPress = false;   // dlouhy stisk PUSH = o uroven zpet
  bool timeout   = false;   // vyprsela necinnost -> uspat do deep-sleepu
};

inline void buttonsInit() {
  pinMode(RDR_BTN_UP,   INPUT_PULLUP);
  pinMode(RDR_BTN_PUSH, INPUT_PULLUP);
  pinMode(RDR_BTN_DOWN, INPUT_PULLUP);

  // Tlacitka jsou aktivni v LOW, takze zachytavame sestupnou hranu.
  // OBE hrany - jen tak lze odlisit stisk od zakmitu pri uvolneni.
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_UP),   isrUp,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_PUSH), isrPush, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_DOWN), isrDown, CHANGE);
  g_btnLatch = 0;
}

// Zahodi stisky nasbirane behem dlouhe operace, u kterych uz akce nedava
// smysl (napr. po prechodu na jinou obrazovku).
inline void btnClearLatch() { noInterrupts(); g_btnLatch = 0; interrupts(); }

// Vyprazdni zamek POUZE u jednoho tlacitka - to prave obsluhovane.
// Stisky ostatnich (i tehoz tlacitka, kdyz prijdou pozdeji) zustanou.
inline void btnClearLatchFor(int pin) {
  uint8_t bit = (pin == RDR_BTN_UP)   ? RDR_LATCH_UP :
                (pin == RDR_BTN_PUSH) ? RDR_LATCH_PUSH : RDR_LATCH_DOWN;
  noInterrupts(); g_btnLatch &= ~bit; interrupts();
}

// Vyzvedne a vynuluje zamek. Vraci pin, nebo 0.
inline int btnTakeLatch() {
  noInterrupts();
  uint8_t l = g_btnLatch;
  g_btnLatch = 0;
  interrupts();
  if (l & RDR_LATCH_PUSH) return RDR_BTN_PUSH;   // PUSH ma prednost
  if (l & RDR_LATCH_DOWN) return RDR_BTN_DOWN;
  if (l & RDR_LATCH_UP)   return RDR_BTN_UP;
  return 0;
}

inline bool btnPressed(int pin) { return digitalRead(pin) == LOW; }

inline bool anyPressed() {
  return btnPressed(RDR_BTN_UP) || btnPressed(RDR_BTN_PUSH) || btnPressed(RDR_BTN_DOWN);
}

// Vyhodnoti uz stisknute tlacitko: rozlisi kratky a dlouhy stisk.
//
// DULEZITE: na konci se VZDY vyprazdni zamek. Preruseni totiz na tentyz
// stisk nastavilo priznak jeste driv, nez se sem kod dostal. Bez vyprazdneni
// by se zamek vyzvedl pri dalsim pruchodu a jedno stisknuti by se
// zpracovalo DVAKRAT - kurzor by preskocil o dve polozky.
inline BtnEvent classifyPress() {
  BtnEvent ev;
  delay(RDR_BTN_DEBOUNCE_MS);

  int pin = 0;
  if      (btnPressed(RDR_BTN_UP))   pin = RDR_BTN_UP;
  else if (btnPressed(RDR_BTN_PUSH)) pin = RDR_BTN_PUSH;
  else if (btnPressed(RDR_BTN_DOWN)) pin = RDR_BTN_DOWN;
  if (!pin) return ev;                  // byl to jen zakmit

  ev.pin = pin;
  // Preruseni tento stisk uz zaznamenalo do zamku. Kdyby tam zustal,
  // vyzvedl by se pri dalsim pruchodu a akce by probehla dvakrat.
  btnClearLatchFor(pin);
  uint32_t t0 = millis();

  if (pin == RDR_BTN_PUSH) {
    while (btnPressed(pin)) {
      if (millis() - t0 >= RDR_LONG_PRESS_MS) {
        ev.longPress = true;
        while (btnPressed(pin)) delay(10);   // pockat na uvolneni
        return ev;
      }
      delay(10);
    }
    return ev;
  }

  // UP / DOWN: prvni udalost hned, autorepeat resi volajici pres btnRepeat()
  while (btnPressed(pin) && millis() - t0 < RDR_REPEAT_DELAY_MS) delay(10);
  return ev;
}

// ---------------------------------------------------------------------------
//  Ceka na tlacitko. Vraci udalost, nebo timeout po RDR_IDLE_TIMEOUT_MS.
// ---------------------------------------------------------------------------
inline BtnEvent waitForButton(uint32_t idleMs) {
  BtnEvent ev;

  // 1) Stisk zachyceny prerusenim behem prekreslovani displeje.
  //    Bez tohoto kroku by se ztratil.
  int latched = btnTakeLatch();
  if (latched) {
    if (btnPressed(latched)) {
      RDR_LOG("tlacitko %d ze zamku, stale drzeno", latched);
      return classifyPress();          // porad drzi -> zmerit delku stisku
    }
    RDR_LOG("tlacitko %d ze zamku (stisk behem kresleni)", latched);
    ev.pin = latched;                  // uz pustil -> kratky stisk
    return ev;
  }

  if (anyPressed()) return classifyPress();   // uz nekdo drzi

#if RDR_USE_LIGHT_SLEEP
  // --- varianta B: light-sleep (usporne) ---------------------------------
  //
  // KLICOVE: GPIO nema zvlast "preruseni" a zvlast "probouzeni" - je to
  // TENTYZ hardware a tentyz registr typu udalosti.
  //   attachInterrupt(..., FALLING)  nastavi pinu sestupnou hranu
  //   gpio_wakeup_enable(..., LOW_LEVEL) to prepise na uroven LOW
  //
  // Kdyby zustala navesena obsluha preruseni, po probuzeni by pin byl
  // porad LOW (uzivatel drzi tlacitko), urovnove preruseni by se spoustelo
  // stale dokola a procesor by se nedostal k nicemu jinemu. Watchdog by
  // desku resetoval - presne ty restarty rst:0x10 (RTCWDT_RTC_RST).
  //
  // Reseni: pred spankem preruseni ODPOJIT, po probuzeni probouzeni
  // vypnout a preruseni zase navesit.
  detachInterrupt(digitalPinToInterrupt(RDR_BTN_UP));
  detachInterrupt(digitalPinToInterrupt(RDR_BTN_PUSH));
  detachInterrupt(digitalPinToInterrupt(RDR_BTN_DOWN));

  gpio_wakeup_enable((gpio_num_t)RDR_BTN_UP,   GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)RDR_BTN_PUSH, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)RDR_BTN_DOWN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)idleMs * 1000ULL);

  esp_light_sleep_start();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // Probouzeni vypnout DRIV, nez se vrati preruseni - jinak by na pinu
  // zustal nastaveny urovnovy rezim.
  gpio_wakeup_disable((gpio_num_t)RDR_BTN_UP);
  gpio_wakeup_disable((gpio_num_t)RDR_BTN_PUSH);
  gpio_wakeup_disable((gpio_num_t)RDR_BTN_DOWN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

  // Zpet na hranove preruseni, aby se zachytily stisky behem kresleni.
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_UP),   isrUp,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_PUSH), isrPush, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RDR_BTN_DOWN), isrDown, CHANGE);
  // Stav tlacitek po probuzeni znovu nacist, jinak by prvni hrana
  // vypadala jako zmena proti neaktualnimu "ustalenemu" stavu.
  g_stable[0] = (uint8_t)digitalRead(RDR_BTN_UP);
  g_stable[1] = (uint8_t)digitalRead(RDR_BTN_PUSH);
  g_stable[2] = (uint8_t)digitalRead(RDR_BTN_DOWN);
  btnClearLatch();          // probuzeni samo neni "stisk k obslouzeni"

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    ev.timeout = true;
    return ev;
  }
  return classifyPress();

#else
  // --- varianta A: polling se snizenym taktem (zaloha) -------------------
  // Snizeni taktu z 240 na 80 MHz srazi odber pri cekani zhruba na
  // polovinu a na odezvu tlacitek to nema vliv.
  uint32_t origMhz = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);

  uint32_t t0 = millis();
  while (millis() - t0 < idleMs) {
    int l = btnTakeLatch();
    if (l && !btnPressed(l)) {          // krátký stisk zachycený mezi dotazy
      setCpuFrequencyMhz(origMhz);
      ev.pin = l;
      return ev;
    }
    if (anyPressed()) {
      setCpuFrequencyMhz(origMhz);
      return classifyPress();
    }
    delay(25);
  }

  setCpuFrequencyMhz(origMhz);
  ev.timeout = true;
  return ev;
#endif
}

// ---------------------------------------------------------------------------
//  Pocka, az jsou vsechna tlacitka prokazatelne uvolnena a KLIDNA,
//  a teprve pak vyprazdni zamek.
//
//  PROC: mechanicky kontakt zakmitava nejen pri sepnuti, ale i pri
//  ROZEPNUTI. Pri uvolneni signal vyskoci nahoru, kratce spadne zpet dolu
//  a tim vytvori dalsi sestupnou hranu. Preruseni ji nema jak odlisit od
//  noveho stisku, takze se jedno stisknuti zpracovalo dvakrat.
//
//  Debounce v obsluze preruseni tomu nezabrani: hlida okno po stisku,
//  ale uvolneni prijde treba pul sekundy pozdeji, tedy davno mimo nej.
//
//  Funkce se vola na konci kazde obslouzene akce. Neblokuje nic uzitecneho -
//  dokud uzivatel drzi tlacitko, stejne nemame co delat.
// ---------------------------------------------------------------------------
inline void btnWaitRelease() {
  // ZAMER SE ZMENIL: tato funkce uz zamek NEMAZE.
  //
  // Puvodne po kazde akci zamek vyprazdnila, aby zahodila zakmit pri
  // uvolneni. Tim ale zahazovala i SKUTECNY stisk, ktery uzivatel udelal
  // behem prekreslovani displeje (trva az 2,5 s) - stisk "propadl".
  // Zakmit resi obsluha preruseni, viz btnEdge().
  //
  // Funkce zustava jen jako mirne zdrzeni, aby drzeni tlacitka nespoustelo
  // akci prilis rychle za sebou. Nikdy neceka na uvolneni.
  (void)0;
}

// Drzi uzivatel tlacitko dal? Pouziva se pro autorepeat v seznamu.
// Po uvolneni se vycisti zamek - behem opakovani mohly prijit dalsi hrany
// (zakmit pri rozepnuti) a ty uz nejsou samostatnym stiskem.
inline bool btnRepeat(int pin) {
  if (!btnPressed(pin)) return false;
  delay(RDR_REPEAT_RATE_MS);
  return btnPressed(pin);
}
