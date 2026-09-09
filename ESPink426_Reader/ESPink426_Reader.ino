// ============================================================================
//  ESPink426_Reader.ino   v0.1.1
//  Ctecka eknih pro LaskaKit ESPink-4.26 (ESP32-S3, GDEQ0426T82 480x800 BW)
// ----------------------------------------------------------------------------
//  OVLADANI
//    Zalozky (Rozecteno / Neprecteno / Precteno)
//       UP / DOWN     prepinani zalozek
//       PUSH kratce   vstup do seznamu knih
//    Seznam knih
//       UP / DOWN     pohyb kurzoru (drzenim autorepeat)
//       PUSH kratce   otevrit knihu
//       PUSH 2 s      zpet na zalozky
//    Cteni
//       UP / DOWN     predchozi / dalsi stranka
//       PUSH 2 s      zpet do seznamu
//
//  SPRAVA ENERGIE
//    Pri cteni je deska v LIGHT-SLEEPU, odkud ji probudi tlacitko behem
//    milisekund. Tlacitka jsou na GPIO39/40/41, ktere NEJSOU RTC piny -
//    v light-sleepu to nevadi (gpio_wakeup_enable funguje na libovolnem
//    pinu), ale z DEEP-SLEEPU jimi probudit nelze.
//
//    Po RDR_IDLE_TIMEOUT_MS necinnosti se deska uspi do DEEP-SLEEPU.
//    Probudi ji RESET nebo vypnuti a zapnuti napajeni. Pozice cteni je
//    ulozena na SD karte, takze se pokracuje tam, kde uzivatel skoncil.
//
//  SDILENA SPI SBERNICE
//    ePaper a SD karta sdili CLK 12 a MOSI 11. Nikdy se nesmi cist ze
//    souboru behem kresleni na displej. Proto se stranka nejdriv vysadi
//    (Book::layout) a teprve pak vykresli.
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <Preferences.h>

#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "esp_sleep.h"
#include "esp_system.h"
#include "version.h"
#include "config.h"
#include "log.h"
#include "battery.h"
#include "buttons.h"
#include "book.h"
#include "library.h"
#include "zip.h"
#include "epub.h"
#include "settings.h"
#include "portal.h"
#include "ui.h"

// ---------------------------------------------------------------------------
//  Zasobnik hlavni ulohy
//
//  Arduino dava uloze loopTask jen 8 kB. Konverze EPUBu pracuje s vetsimi
//  strukturami a hlubsi posloupnosti volani (zip -> inflate -> parser HTML),
//  coz vedlo k padu "Stack canary watchpoint triggered (loopTask)".
//  Volne RAM mame dost, takze zasobnik zvetsime.
// ---------------------------------------------------------------------------
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---------------------------------------------------------------------------
//  Displej
// ---------------------------------------------------------------------------
GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT>
    display(GxEPD2_426_GDEQ0426T82(RDR_EPD_CS, RDR_EPD_DC, RDR_EPD_RST, RDR_EPD_BUSY));

U8G2_FOR_ADAFRUIT_GFX u8;
RdrUI<decltype(display)> ui(display, u8);

RdrLibrary  lib;
Book        book;
RdrSettings settings;

// ---------------------------------------------------------------------------
//  Stavovy automat
// ---------------------------------------------------------------------------
enum RdrScreen : uint8_t { SC_TABS, SC_LIST, SC_READ, SC_END, SC_ERROR };

static RdrScreen   g_screen  = SC_TABS;
static RdrCategory g_cat     = RDR_CAT_READING;
static int         g_cursor  = 0;      // index v ramci kategorie
static int         g_scroll  = 0;      // prvni zobrazeny radek
static int         g_bookIdx = -1;     // index otevrene knihy v knihovne

// Geometrie textove plochy pri cteni
static const int RDR_TXT_TOP    = 16;
static const int RDR_TXT_BOTTOM = RDR_SCREEN_H - RDR_STATUS_H - 12;
static const int RDR_TXT_LEFT   = RDR_MARGIN_X;
static const int RDR_TXT_RIGHT  = RDR_SCREEN_W - RDR_MARGIN_X;

// ---------------------------------------------------------------------------
//  Vykresleni obrazovky portalu. Portal si samo rozhodne, kdy zavolat -
//  nikdy behem zapisu na kartu (sdilena SPI).
// ---------------------------------------------------------------------------
// Prubeh konverze EPUBu.
//
// DULEZITE: u e-inku NEROZHODUJE velikost prekreslovane plochy, ale POCET
// prekresleni. Partial refresh trva 501 ms bez ohledu na to, jestli se meni
// cely displej nebo uzky pruh. Pri 41 dokumentech to znamenalo 20 s jen na
// zobrazovani prubehu, zatimco vlastni prace trvala 5 s.
//
// Prubeh proto kreslime nejvys jednou za RDR_CONV_PROGRESS_MS, plus vzdy
// u prvniho a posledniho dokumentu.
void convProgress(int done, int total, void *) {
  static uint32_t last = 0;
  bool edge = (done <= 1) || (done >= total);
  if (!edge && millis() - last < RDR_CONV_PROGRESS_MS) return;
  last = millis();

  int pct = (total > 0) ? (done * 100 / total) : 0;
  ui.drawBusy("Zpracovávám… " + String(pct) + " %");
}

void drawPortalScreen(const RdrPortalState &st) {
  int b = batteryPercent(batteryVoltage());
  switch (st.mode) {
    case RDR_PORTAL_AP:
      ui.drawPortalAP(st.ssid, st.pass, st.ip, st.fellBack, b); break;
    case RDR_PORTAL_STA:
      ui.drawPortalSTA(st.ssid, st.ip, st.host, b); break;
    default:
      ui.drawMessage("Nahrávání selhalo", st.error,
                     "Stiskněte RESET pro návrat ke čtení.", RDR_DEV_ACTIVE, b);
      break;
  }
}

// ---------------------------------------------------------------------------
//  Napajeni periferii (GPIO47, aktivni v HIGH)
// ---------------------------------------------------------------------------
void peripheralPower(bool on) {
  pinMode(RDR_POWER_PIN, OUTPUT);
  digitalWrite(RDR_POWER_PIN, on ? HIGH : LOW);
  if (on) delay(RDR_POWER_SETTLE_MS);
}

// ---------------------------------------------------------------------------
//  Ulozeni pozice ctene knihy
// ---------------------------------------------------------------------------
void savePosition() {
  if (g_bookIdx < 0) return;
  RdrBookEntry &b = lib.at(g_bookIdx);
  RdrPos p = book.position();
  b.offset = p.para;
  b.line   = p.line;
  b.page   = book.page();
  lib.savePos(b);
}

// ---------------------------------------------------------------------------
//  Uspani
// ---------------------------------------------------------------------------
void goToSleep() {
  savePosition();

  String what;
  if (g_screen == SC_READ && g_bookIdx >= 0)
    what = lib.at(g_bookIdx).title + "  ·  " + String(book.percent()) + " %";

  ui.drawSleep(what, batteryPercent(batteryVoltage()));
  display.hibernate();
  book.close();

  peripheralPower(false);

  // KRITICKE: pred light-sleepem jsme nastavovali casovac. Konfigurace
  // zdroju probuzeni PRETRVAVA i do deep-sleepu, takze bez zruseni by se
  // deska po chvili sama probudila a restartovala.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // POZN.: gpio_hold_en() na GPIO47 tu zamerne NENI. GPIO47 neni RTC pad
  // a hold na pinu mimo RTC domenu muze odber naopak zvysit. Az bude
  // odber zmeren, rozhodneme, jestli ma smysl to doplnit.

  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
//  Prechody mezi obrazovkami
// ---------------------------------------------------------------------------
// POZN.: zadne vychozi hodnoty parametru u funkci v .ino!
// Arduino IDE generuje prototypy vcetne vychozich hodnot a definice je
// pak uvadi podruhe, coz konci chybou "default argument given for parameter".
void showTabs(bool full) {
  g_screen = SC_TABS;
  // Prepinani zalozek meni i seznam pod nimi, takze se prekresluje cela
  // obrazovka - ale PARTIAL refreshem (~0,4 s misto ~3,5 s).
  if (full) ui.drawList(lib, g_cat, -1, g_scroll, RDR_DEV_ACTIVE,
                        batteryPercent(batteryVoltage()), false);
  else ui.updateTabs(g_cat);
}

void showList(bool full) {
  // Do prazdneho seznamu nema smysl vstupovat - uzivatel by v nem uvizl
  // bez viditelneho voditka, ze se musi vratit na zalozky.
  if (lib.countIn(g_cat) == 0) { showTabs(true); return; }

  g_screen = SC_LIST;
  int n = lib.countIn(g_cat);
  if (g_cursor >= n) g_cursor = max(0, n - 1);
  clampScroll();
  if (full) ui.drawList(lib, g_cat, g_cursor, g_scroll, RDR_DEV_ACTIVE,
                        batteryPercent(batteryVoltage()), false);
}

// Navrat ze cteni do seznamu.
//
// Kniha se prectenim presune mezi zalozkami (napr. z "Neprecteno" do
// "Rozecteno"), takze puvodni zalozka muze zustat PRAZDNA. Driv v ni
// kurzor zustal a uzivatel nemel jak poznat, ze musi dlouhym stiskem
// vyskocit na zalozky.
//
// Proto sledujeme KNIHU, ne kategorii: prepneme se na zalozku, kam se
// kniha presunula, a kurzor postavime na ni. Kdyz kniha zmizela uplne
// (napr. ji uzivatel smazal pres web), skocime na prvni neprazdnou
// zalozku; kdyz nezbyla zadna, ukazeme vyber zalozek.
void returnToList(const String &file) {
  lib.scan();

  int gi = -1;
  for (int i = 0; i < lib.count(); i++) {
    if (lib.at(i).file == file) { gi = i; break; }
  }

  if (gi >= 0) {
    RdrCategory c = lib.at(gi).category();
    // poradi knihy v ramci jeji kategorie
    int nth = 0;
    for (int i = 0; i < gi; i++) if (lib.at(i).category() == c) nth++;
    g_cat = c; g_cursor = nth; g_scroll = 0;
    clampScroll();
    RDR_LOG("navrat do seznamu: zalozka %d, polozka %d", (int)g_cat, g_cursor);
    showList(true);
    return;
  }

  RdrCategory c = lib.firstNonEmpty(g_cat);
  if (c == RDR_CAT_COUNT) {
    g_cursor = 0; g_scroll = 0;
    showTabs(true);                 // knihovna je prazdna
    return;
  }
  g_cat = c; g_cursor = 0; g_scroll = 0;
  showList(true);
}

void clampScroll() {
  if (g_cursor < g_scroll) g_scroll = g_cursor;
  if (g_cursor >= g_scroll + RDR_LIST_ROWS) g_scroll = g_cursor - RDR_LIST_ROWS + 1;
  if (g_scroll < 0) g_scroll = 0;
}

void renderReading(bool full) {
  RDR_LOG_MS("sazba stranky", book.layout(u8, RDR_TXT_TOP, RDR_TXT_BOTTOM, RDR_TXT_LEFT, RDR_TXT_RIGHT));
  RDR_LOG_MS("vykresleni stranky",
             ui.drawPage(book, RDR_DEV_ACTIVE, batteryPercent(batteryVoltage()), full));
}

bool openSelectedBook() {
  int gi = lib.indexIn(g_cat, g_cursor);
  if (gi < 0) return false;
  RdrBookEntry &b = lib.at(gi);

  // EPUB bez odpovidajiciho .book. Konverze primo ve ctecce zatim neni
  // hotova - az bude, spusti se prave tady a vysledek se ulozi na kartu.
  if (b.needsConv) {
#if RDR_HAVE_MINIZ
    // Konverze EPUB -> .book primo ve ctecce. Trva desitky sekund, proto
    // se prubeh ukazuje na displeji. Uspani po dobu konverze nehrozi -
    // bezime uvnitr obsluhy tlacitka, ne v cekaci smycce.
    String bookPath = b.path();
    bookPath = bookPath.substring(0, bookPath.length() - 5) + ".book";

    RDR_LOG("EPUB: spoustim konverzi '%s' -> '%s'", b.file.c_str(), bookPath.c_str());
    ui.drawMessage("Zpracovávám knihu", b.title,
                   "Může to trvat i minutu. Nevypínejte čtečku.",
                   RDR_DEV_ACTIVE, batteryPercent(batteryVoltage()));

    // POZOR: RdrEpub obsahuje pole 64 polozek archivu a dalsi buffery,
    // dohromady pres 2 kB. Jako lokalni promenna by se alokoval na
    // ZASOBNIKU a pretekl by ho. Vytvarime ho proto na halde.
    RdrEpub *conv = new RdrEpub();
    if (!conv) {
      ui.drawMessage("Nedostatek paměti", "Zkuste čtečku restartovat.", "",
                     RDR_DEV_ACTIVE, batteryPercent(batteryVoltage()));
      return true;
    }
    RDR_LOG("volna halda pred konverzi: %u B", (unsigned)ESP.getFreeHeap());

    bool ok = conv->convert(b.path(), bookPath, convProgress, nullptr);
    String convErr = conv->error();
    delete conv;

    RDR_LOG("volna halda po konverzi: %u B", (unsigned)ESP.getFreeHeap());

    if (!ok) {
      ui.drawMessage("Zpracování selhalo", convErr,
                     "Zkuste knihu převést skriptem epub2book.py.",
                     RDR_DEV_ACTIVE, batteryPercent(batteryVoltage()));
      return true;
    }

    // Knihovnu je treba nacist znovu - pribyl .book a zdrojovy EPUB se
    // tim ze seznamu skryje.
    lib.scan();
    g_cursor = 0; g_scroll = 0;
    showList(true);
    return true;
#else
    ui.drawMessage("Zatím nelze otevřít",
                   "Tato kniha je ve formátu EPUB.",
                   "Převeďte ji skriptem epub2book.py na .book.",
                   RDR_DEV_ACTIVE, batteryPercent(batteryVoltage()));
    return true;
#endif
  }

  if (!book.open(b.path())) return false;
  book.setFontSize(settings.fontSize);

  RdrPos p; p.para = b.offset; p.line = b.line;
  if (b.offset > 0) book.restore(p, b.page);

  g_bookIdx = gi;
  RDR_LOG("oteviram '%s' (%d %%, str. %d)",
          b.title.c_str(), b.percent(), b.page + 1);
  lib.touchOpened(b);
  g_screen = SC_READ;
  renderReading(true);
  return true;
}

void markAsRead() {
  if (g_bookIdx < 0) return;
  RdrBookEntry &b = lib.at(g_bookIdx);
  b.done = true;
  lib.savePos(b);
}

// ---------------------------------------------------------------------------
//  SETUP
// ---------------------------------------------------------------------------
void setup() {
  // Diagnostika: pri podezreni na nechteny restart odkomentujte a
  // sledujte seriovy monitor. Kdyz se hlaska objevi po KAZDEM stisku
  // tlacitka, deska se restartuje misto aby pokracovala ve smycce.
  // (USB CDC On Boot musi byt Disabled - deska nema nativni USB,
  //  seriova linka jde pres prevodnik.)
  rdrLogBegin();
  RDR_LOG("%s %s  start", RDR_FW_NAME, rdrVersionFull());
  RDR_LOG("duvod resetu: %d, pricina probuzeni: %d",
          (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause());

  WiFi.mode(WIFI_OFF);
  btStop();

  // Cas startu a stav PUSH ctem HNED, jeste pred inicializaci displeje.
  // Podrzeni PUSH 5 s od zapnuti spousti rezim nahravani knih; kdybychom
  // to zjistovali az po inicializaci, musel by uzivatel drzet mnohem dele.
  uint32_t bootMs = millis();
  buttonsInit();
  bool pushAtBoot = rdrPushAtBoot();

  peripheralPower(true);

  // Jedina SPI sbernice pro ePaper i SD kartu.
  // MISO (13) vyuziva pouze SD karta - ePaper se jen zapisuje.
  SPI.begin(RDR_SPI_SCK, RDR_SPI_MISO, RDR_SPI_MOSI, -1);

  // Prvni parametr je rychlost ladiciho vypisu GxEPD2. Nula ho vypne -
  // jinak se do seriove linky sype "_Update_Full : ...". Pri diagnostice
  // ho necháme zapnuty, protoze ukazuje, jak dlouho refresh trval.
  #ifdef RDR_DEBUG_SERIAL
    display.init(115200, true, 2, false);
  #else
    display.init(0, true, 2, false);
  #endif

  // Zvyseni taktu SPI pro ePaper. Bez tohoto jede GxEPD2 na 4 MHz a samotny
  // prenos ramce trva pres pul sekundy - dele nez kresleni e-inku.
  display.epd2.selectSPI(SPI, SPISettings(RDR_EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
  RDR_LOG("ePaper SPI: %lu Hz", (unsigned long)RDR_EPD_SPI_HZ);
  ui.begin();

  int batt = batteryPercent(batteryVoltage());

  // Karta na 10 MHz - na sbernici sdilene s ePaperem spolehlivejsi.
  RDR_LOG("SD: pripojuji kartu na CS %d, %lu Hz", RDR_SD_CS, (unsigned long)RDR_SD_SPI_HZ);
  if (!SD.begin(RDR_SD_CS, SPI, RDR_SD_SPI_HZ)) {
    RDR_LOG("SD: CHYBA - kartu nelze nacist");
    g_screen = SC_ERROR;
    ui.drawMessage("Chyba karty", "SD kartu se nepodařilo načíst.",
                   "Zkontrolujte kartu a stiskněte RESET.", RDR_DEV_SLEEP, batt);
    display.hibernate();
    peripheralPower(false);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
    return;
  }

  if (!SD.exists(RDR_BOOKS_DIR)) SD.mkdir(RDR_BOOKS_DIR);

  settings.load();
  RDR_LOG("nastaveni: pismo %s, uspani po %u min",
          settings.fontName(), (unsigned)settings.sleepMin);

  // ---------------------------------------------------------------------
  //  REZIM NAHRAVANI KNIH: podrzeni PUSH 5 s pri startu.
  //  Bezi ve vlastni smycce, dokud uzivatel neklikne na "Ukoncit" nebo
  //  neuplyne 5 minut necinnosti. Pak se deska uspi.
  // ---------------------------------------------------------------------
  if (rdrPortalRequested(pushAtBoot, bootMs)) {
    RDR_LOG("spoustim rezim nahravani knih");
    if (rdrPortalBegin(lib, settings, drawPortalScreen)) {
      while (rdrPortalLoop()) delay(2);
      rdrPortalEnd();
      RDR_LOG("rezim nahravani ukoncen, pokracuji ke cteni");

      // Po ukonceni se POKRACUJE ke cteni. Drive tady byla hlaska
      // "stisknete RESET" a deska sla spat, coz nedavalo smysl - uzivatel
      // uz rekl, ze skoncil, a ceka, ze se objevi seznam knih.
      settings.load();            // pismo a uspani se mohly zmenit na webu
      btnClearLatch();            // zahodit stisky nasbirane behem portalu
    } else {
      // Portal se nepodarilo spustit (slaba baterie, chybi karta...).
      // Chybovou obrazovku uz vykreslil callback; dame uzivateli cas ji
      // precist a pokracujeme ke cteni, aby nezustal viset.
      RDR_LOG("rezim nahravani se nepodaril spustit");
      delay(4000);
    }
  }

  RDR_LOG_MS("knihovna nactena", lib.scan());
  RDR_LOG("knih celkem: %d (rozecteno %d, neprecteno %d, precteno %d)",
          lib.count(), lib.countIn(RDR_CAT_READING),
          lib.countIn(RDR_CAT_UNREAD), lib.countIn(RDR_CAT_DONE));

  // Po zapnuti je vybrana zalozka Rozecteno. Kdyz je prazdna, preskocime
  // na prvni neprazdnou; kdyz jsou prazdne vsechny, ukazeme hlasku.
  g_cat = RDR_CAT_READING;
  RdrCategory c = lib.firstNonEmpty(RDR_CAT_READING);
  if (c == RDR_CAT_COUNT) {
    g_screen = SC_ERROR;
    ui.drawMessage("Žádné knihy", "Na kartě nejsou žádné knihy.",
                   "Nahrajte soubory .book do složky /books.",
                   RDR_DEV_ACTIVE, batt);
    return;
  }
  g_cat = c;
  g_cursor = 0; g_scroll = 0;
  showTabs(true);
}

// ---------------------------------------------------------------------------
//  LOOP
// ---------------------------------------------------------------------------
void loop() {
  BtnEvent ev = waitForButton(settings.idleMs());

  if (ev.timeout) { RDR_LOG("necinnost, uspavam"); goToSleep(); return; }
  if (!ev.pin) return;
  RDR_LOG("tlacitko %d%s, obrazovka %d", ev.pin,
          ev.longPress ? " (dlouhy stisk)" : "", (int)g_screen);

  // Kriticky nizka baterie - uspat, aby nedoslo k podvybiti clanku
  float vb = batteryVoltage();
  if (batteryCritical(vb)) {
    savePosition();
    ui.drawMessage("Slabá baterie", "Nabijte čtečku.",
                   "Pozice čtení je uložena.", RDR_DEV_SLEEP, batteryPercent(vb));
    display.hibernate();
    peripheralPower(false);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
    return;
  }

  switch (g_screen) {

    // ---- vyber zalozky --------------------------------------------------
    case SC_TABS:
      if (ev.pin == RDR_BTN_UP || ev.pin == RDR_BTN_DOWN) {
        int d = (ev.pin == RDR_BTN_DOWN) ? 1 : (RDR_CAT_COUNT - 1);
        g_cat = (RdrCategory)((g_cat + d) % RDR_CAT_COUNT);
        g_cursor = 0; g_scroll = 0;
        ui.drawList(lib, g_cat, -1, g_scroll, RDR_DEV_ACTIVE,
                    batteryPercent(vb), false);
      } else if (ev.pin == RDR_BTN_PUSH && !ev.longPress) {
        if (lib.countIn(g_cat) > 0) showList(true);
      }
      break;

    // ---- seznam knih ----------------------------------------------------
    case SC_LIST: {
      int n = lib.countIn(g_cat);
      if (ev.pin == RDR_BTN_UP || ev.pin == RDR_BTN_DOWN) {
        int dir = (ev.pin == RDR_BTN_DOWN) ? 1 : -1;
        do {
          int old = g_cursor, oldScroll = g_scroll;
          g_cursor += dir;
          if (g_cursor < 0) g_cursor = 0;
          if (g_cursor >= n) g_cursor = n - 1;
          if (g_cursor == old) break;
          clampScroll();
          // Kdyz se posunul vyrez, musi se prekreslit cela obrazovka;
          // jinak staci PARTIAL REFRESH dvou radku (rychle listovani).
          if (g_scroll != oldScroll)
            ui.drawList(lib, g_cat, g_cursor, g_scroll, RDR_DEV_ACTIVE,
                        batteryPercent(vb), false);
          else
            ui.updateCursor(lib, g_cat, old, g_cursor, g_scroll);
        } while (btnRepeat(ev.pin));
      }
      else if (ev.pin == RDR_BTN_PUSH) {
        if (ev.longPress) showTabs(true);
        else if (!openSelectedBook())
          ui.drawMessage("Chyba", "Knihu se nepodařilo otevřít.", "",
                         RDR_DEV_ACTIVE, batteryPercent(vb));
      }
      break;
    }

    // ---- cteni ----------------------------------------------------------
    case SC_READ:
      if (ev.pin == RDR_BTN_DOWN) {
        if (book.nextPage()) {
          renderReading(false);
          savePosition();
        } else {
          markAsRead();
          g_screen = SC_END;
          ui.drawEndOfBook(book, RDR_DEV_ACTIVE, batteryPercent(vb));
        }
      }
      else if (ev.pin == RDR_BTN_UP) {
        // Po obnove pozice je historie listovani prazdna a prvni krok zpet
        // se musi dopocitat prochazenim od zacatku knihy. U vzdalene
        // stranky to trva nekolik sekund, proto uzivatele informujeme.
        // Bez historie se stranka dopocitava prochazenim od zacatku knihy.
        // U vzdalene stranky to trva nekolik sekund, proto dame vedet.
        if (!book.hasHistory() && !book.atBookStart() && book.percent() > 3)
          ui.drawBusy("Dopočítávám stránky…");
        if (book.prevPage(u8, RDR_TXT_TOP, RDR_TXT_BOTTOM, RDR_TXT_LEFT, RDR_TXT_RIGHT)) {
          renderReading(false);
          savePosition();
        }
      }
      else if (ev.pin == RDR_BTN_PUSH && ev.longPress) {
        savePosition();
        String justRead = (g_bookIdx >= 0) ? lib.at(g_bookIdx).file : String("");
        book.close();
        g_bookIdx = -1;
        returnToList(justRead);
      }
      else if (ev.pin == RDR_BTN_PUSH) {
        // Kratky stisk pri cteni prepina velikost pisma male -> stredni ->
        // velke -> male. Text se preskláda a zustaneme na tomtez miste
        // v knize (pozice je bajtovy offset odstavce, ne cislo stranky).
        settings.fontSize = (settings.fontSize + 1) % 3;
        settings.save();
        book.setFontSize(settings.fontSize);
        book.onFontChanged();   // hranice stranek platily pro puvodni font
        renderReading(true);
        savePosition();
      }
      break;

    // ---- konec knihy ----------------------------------------------------
    case SC_END:
      if (ev.pin == RDR_BTN_UP) {
        g_screen = SC_READ;
        if (book.prevPage(u8, RDR_TXT_TOP, RDR_TXT_BOTTOM, RDR_TXT_LEFT, RDR_TXT_RIGHT))
          renderReading(true);
      } else if (ev.pin == RDR_BTN_PUSH) {
        String justRead = (g_bookIdx >= 0) ? lib.at(g_bookIdx).file : String("");
        book.close();
        g_bookIdx = -1;
        returnToList(justRead);
      }
      break;

    // ---- chybova obrazovka ----------------------------------------------
    case SC_ERROR:
      if (ev.pin == RDR_BTN_PUSH) {
        lib.scan();
        RdrCategory c = lib.firstNonEmpty(RDR_CAT_READING);
        if (c != RDR_CAT_COUNT) { g_cat = c; showTabs(true); }
      }
      break;
  }

  // Pockat, az je tlacitko prokazatelne uvolnene a klidne, a teprve pak
  // zahodit zamek. Bez toho by zakmit pri UVOLNENI vyrobil dalsi sestupnou
  // hranu, preruseni by ji zapsalo jako novy stisk a jedno stisknuti by se
  // zpracovalo dvakrat (kurzor preskocil o dve polozky).
  btnWaitRelease();
}
