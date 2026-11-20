// ============================================================================
//  ui.h  -  Vykreslovani obrazovek a partial refresh
// ----------------------------------------------------------------------------
//  Rozvrzeni displeje (na vysku, 480x800):
//
//   +--------------------------------------+  0
//   | Rozecteno | Neprecteno | Precteno    |  zalozky (RDR_HEADER_H)
//   +--------------------------------------+  76
//   | > Nazev knihy                        |
//   |   45 %  ....................         |  radky seznamu (RDR_ROW_H)
//   |   Dalsi kniha                        |
//   |   ...                                |
//   +--------------------------------------+  760
//   | stav zarizeni | baterie              |  stavovy radek (RDR_STATUS_H)
//   +--------------------------------------+  800
//
//  PARTIAL REFRESH
//   Plny refresh trva u tohoto panelu kolem dvou sekund. Listovat s tim
//   seznamem by bylo neunosne, proto se pri posunu kurzoru prekresluji
//   POUZE DVA RADKY: ten, ze ktereho kurzor odchazi, a ten, na ktery
//   prichazi. Kazdych RDR_FULL_REFRESH_EVERY prekresleni se udela plny
//   refresh, aby se vycistily "duchy".
//
//  SDILENA SPI
//   Vsechny funkce zde POUZE kresli. Necti tu ze souboru - sbernice je
//   sdilena s SD kartou. Data si nachystej driv (Book::layout, knihovna).
// ============================================================================
#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "config.h"
#include "book.h"
#include "library.h"
#include "battery.h"
#include "log.h"

// Stav zarizeni pro stavovy radek
enum RdrDevState : uint8_t {
  RDR_DEV_ACTIVE = 0,   // uzivatel prave neco dela
  RDR_DEV_IDLE   = 1,   // ceka v uspornem rezimu, reaguje na tlacitka
  RDR_DEV_SLEEP  = 2    // uspano, probudi RESET nebo vypinac
};

template <typename RdrDisplayT>
class RdrUI {
public:
  RdrUI(RdrDisplayT &d, U8G2_FOR_ADAFRUIT_GFX &u) : _d(d), _u(u) {}

  void begin() {
    _d.setRotation(RDR_ROTATION);
    _u.begin(_d);
    _u.setForegroundColor(GxEPD_BLACK);
    _u.setBackgroundColor(GxEPD_WHITE);
    _refreshes = 0;
  }

  // ------------------------------------------------------------------
  //  VOLBA REZIMU PREKRESLENI
  //  Panel zvlada plny refresh za ~3,5 s, castecny za ~0,4 s. Proto je
  //  PARTIAL VYCHOZI a plny refresh se dela jen obcas, aby se vycistily
  //  "duchy" po mnoha castecnych prekreslenich.
  //
  //  Pozn.: uplne prvni update po display.init() dela GxEPD2 vzdy plny,
  //  at uz zvolime cokoli. To je spravne - vycisti se tim panel.
  // ------------------------------------------------------------------
  void beginFrame(bool forceFull) {
    if (forceFull || _sinceFull >= RDR_FULL_REFRESH_EVERY) {
      _d.setFullWindow();
      _sinceFull = 0;
    } else {
      _d.setPartialWindow(0, 0, RDR_SCREEN_W, RDR_SCREEN_H);
      _sinceFull++;
    }
  }

  // ---------------------------------------------------------------------
  //  SEZNAM KNIH
  // ---------------------------------------------------------------------
  //  cat     - vybrana zalozka
  //  lib     - knihovna
  //  cursor  - index vybrane knihy V RAMCI kategorie
  //  scroll  - index prvni zobrazene knihy v kategorii
  void drawList(RdrLibrary &lib, RdrCategory cat, int cursor, int scroll,
                RdrDevState dev, int battPct, bool forceFull) {
    beginFrame(forceFull);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      drawTabs(cat);
      int n = lib.countIn(cat);
      if (n == 0) {
        _u.setFont(RDR_FONT_ROW);
        centerText("V této záložce nejsou žádné knihy.", RDR_SCREEN_H / 2 - 12);
        _u.setFont(RDR_FONT_ROW_SUB);
        centerText("Nahrajte knihy do složky /books na kartě", RDR_SCREEN_H / 2 + 16);
        centerText("nebo přes WiFi (podržte PUSH při zapnutí).", RDR_SCREEN_H / 2 + 40);
        // Napoveda, jak se z prazdne zalozky dostat dal - bez ni neni
        // zrejme, ze se ovlada dlouhym stiskem.
        centerText("Tlačítky UP a DOWN přepnete záložku.", RDR_SCREEN_H / 2 + 76);
      } else {
        for (int r = 0; r < RDR_LIST_ROWS; r++) {
          int idx = scroll + r;
          if (idx >= n) break;
          int gi = lib.indexIn(cat, idx);
          if (gi < 0) break;
          drawRow(lib.at(gi), rowY(r), idx == cursor);
        }
        drawScrollHint(n, scroll);
      }
      drawStatus(dev, battPct, statusRight(lib, cat, cursor));
    } while (_d.nextPage());
    _refreshes++;
  }

  // Prekresli jen dva radky pri posunu kurzoru (rychle listovani)
  void updateCursor(RdrLibrary &lib, RdrCategory cat,
                    int oldCursor, int newCursor, int scroll) {
    int rOld = oldCursor - scroll;
    int rNew = newCursor - scroll;
    if (rOld < 0 || rOld >= RDR_LIST_ROWS || rNew < 0 || rNew >= RDR_LIST_ROWS) return;

    int top = rowY(min(rOld, rNew));
    int h   = RDR_ROW_H * (abs(rNew - rOld) + 1);

    _d.setPartialWindow(0, top, RDR_SCREEN_W, h);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      for (int r = min(rOld, rNew); r <= max(rOld, rNew); r++) {
        int idx = scroll + r;
        int gi = lib.indexIn(cat, idx);
        if (gi < 0) continue;
        drawRow(lib.at(gi), rowY(r), idx == newCursor);
      }
    } while (_d.nextPage());
    _refreshes++; _sinceFull++;
  }

  // Prekresli jen pruh se zalozkami (prepinani UP/DOWN nahore)
  void updateTabs(RdrCategory cat) {
    _d.setPartialWindow(0, 0, RDR_SCREEN_W, RDR_HEADER_H);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      drawTabs(cat);
    } while (_d.nextPage());
    _refreshes++;
  }

  // ---------------------------------------------------------------------
  //  CTENI
  // ---------------------------------------------------------------------
  void drawPage(Book &book, RdrDevState dev, int battPct, bool full) {
    beginFrame(full);

    // Merime tri casti zvlast, abychom vedeli, kam cas skutecne jde:
    //   fillScreen - vymazani platna v pameti
    //   text       - kresleni radku pres U8g2 (pixel po pixelu do GFX)
    //   panel      - to, co dela nextPage(): prenos dat a refresh e-inku
    uint32_t tFill = 0, tText = 0, tPanel = 0, tMark;
    int glyphs = 0;

    _d.firstPage();
    bool more = true;
    while (more) {
      tMark = millis();
      _d.fillScreen(GxEPD_WHITE);
      tFill += millis() - tMark;

      tMark = millis();
      for (int i = 0; i < book.lineCount(); i++) {
        const RdrLine &ln = book.line(i);
        book.setFont(_u, ln.style);
        _u.setCursor(RDR_MARGIN_X, ln.y);
        _u.print(ln.text);
        glyphs += ln.text.length();
      }
      String right = String(book.percent()) + " %   str. " + String(book.page() + 1);
      drawStatus(dev, battPct, right);
      tText += millis() - tMark;

      // nextPage() prenese data do panelu a spusti refresh - to je ta
      // cast, kterou uz neovlivnime, zbytek je nas kod.
      tMark = millis();
      more = _d.nextPage();
      tPanel += millis() - tMark;
    }

    RDR_LOG("  rozpad: platno %lu ms | text %lu ms (%d znaku, %d radku) | panel %lu ms",
            (unsigned long)tFill, (unsigned long)tText,
            glyphs, book.lineCount(), (unsigned long)tPanel);
    _refreshes++;
  }

  void drawEndOfBook(Book &book, RdrDevState dev, int battPct) {
    beginFrame(false);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_BIG);
      centerText("Konec knihy", RDR_SCREEN_H / 2 - 40);
      _u.setFont(RDR_FONT_ROW);
      centerText(book.title(), RDR_SCREEN_H / 2 - 2);
      _u.setFont(RDR_FONT_ROW_SUB);
      centerText("Označeno jako přečtené.", RDR_SCREEN_H / 2 + 34);
      centerText("PUSH: zpět do seznamu   UP: poslední stránka", RDR_SCREEN_H / 2 + 58);
      drawStatus(dev, battPct, "100 %");
    } while (_d.nextPage());
    _refreshes++;
  }

  // ---------------------------------------------------------------------
  //  HLASKY
  // ---------------------------------------------------------------------
  void drawMessage(const String &l1, const String &l2, const String &l3,
                   RdrDevState dev, int battPct) {
    beginFrame(false);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_BIG);
      centerText(l1, RDR_SCREEN_H / 2 - 34);
      if (l2.length()) { _u.setFont(RDR_FONT_ROW);     centerText(l2, RDR_SCREEN_H / 2 + 4); }
      if (l3.length()) { _u.setFont(RDR_FONT_ROW_SUB); centerText(l3, RDR_SCREEN_H / 2 + 36); }
      drawStatus(dev, battPct, "");
    } while (_d.nextPage());
    _refreshes++;
  }

  // ---------------------------------------------------------------------
  //  REZIM NAHRAVANI KNIH (portal)
  //  Vzdy plny refresh: obrazovka zustane dlouho a uzivatel z ni opisuje
  //  heslo, takze na ni nesmi byt duchove.
  // ---------------------------------------------------------------------
  void drawPortalAP(const String &ssid, const String &pass, const String &ip,
                    bool fellBack, int battPct) {
    _d.setFullWindow(); _sinceFull = 0;
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_BIG);
      centerText("Nahrávání knih", 70);

      if (fellBack) {
        _u.setFont(RDR_FONT_ROW_SUB);
        centerText("Domácí WiFi není dostupná, běží vlastní síť.", 104);
      }

      int y = 170;
      _u.setFont(RDR_FONT_ROW_SUB);  centerText("Připojte se k síti", y);
      _u.setFont(RDR_FONT_CHAPTER_L); centerText(ssid, y + 42);

      y += 110;
      _u.setFont(RDR_FONT_ROW_SUB);  centerText("Heslo", y);
      _u.setFont(RDR_FONT_CHAPTER_L); centerText(pass, y + 42);

      y += 110;
      _u.setFont(RDR_FONT_ROW_SUB);  centerText("V prohlížeči otevřete", y);
      _u.setFont(RDR_FONT_CHAPTER_L); centerText("http://" + ip, y + 42);

      _u.setFont(RDR_FONT_ROW_SUB);
      centerText("Stránka se u většiny telefonů otevře sama.", RDR_SCREEN_H - 140);
      centerText("Režim se vypne po 5 minutách nečinnosti.", RDR_SCREEN_H - 112);
      drawStatus(RDR_DEV_ACTIVE, battPct, "");
    } while (_d.nextPage());
    _refreshes++;
  }

  void drawPortalSTA(const String &ssid, const String &ip, const String &host,
                     int battPct) {
    _d.setFullWindow(); _sinceFull = 0;
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_BIG);
      centerText("Režim nahrávání knih", 80);

      int y = 200;
      _u.setFont(RDR_FONT_ROW_SUB);   centerText("Připojeno k WiFi", y);
      _u.setFont(RDR_FONT_CHAPTER_L); centerText(ssid, y + 42);

      y += 120;
      _u.setFont(RDR_FONT_ROW_SUB);   centerText("V prohlížeči otevřete", y);
      _u.setFont(RDR_FONT_CHAPTER_L); centerText("http://" + ip, y + 42);
      if (host.length()) {
        _u.setFont(RDR_FONT_ROW);
        centerText("nebo http://" + host, y + 82);
      }

      _u.setFont(RDR_FONT_ROW_SUB);
      centerText("Režim se vypne po 5 minutách nečinnosti.", RDR_SCREEN_H - 120);
      drawStatus(RDR_DEV_ACTIVE, battPct, "");
    } while (_d.nextPage());
    _refreshes++;
  }

  // Prubehova obrazovka (napr. dopocitavani stranek pri kroku zpet)
  void drawBusy(const String &what) {
    // Jen uzky pruh kolem textu (y H/2-16 .. H/2+16, nasobky 8). Sirsi okno
    // mazalo spodek nadpisu i radek pod nim v drawMessage.
    _d.setPartialWindow(0, RDR_SCREEN_H / 2 - 16, RDR_SCREEN_W, 32);
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_ROW);
      centerText(what, RDR_SCREEN_H / 2 + 6);
    } while (_d.nextPage());
    _refreshes++; _sinceFull++;
  }

  // Obrazovka pred uspanim. Zustane na e-inku i bez napajeni.
  void drawSleep(const String &what, int battPct) {
    // Tady zamerne VZDY plny refresh: obrazovka zustane na e-inku i bez
    // napajeni, treba nekolik tydnu. Duchove po castecnem prekresleni
    // by na ni byli trvale videt.
    _d.setFullWindow();
    _sinceFull = 0;
    _d.firstPage();
    do {
      _d.fillScreen(GxEPD_WHITE);
      _u.setFont(RDR_FONT_ROW_SUB);
      centerText(what, 120);
      _u.setFont(RDR_FONT_BIG);
      centerText("Čtečka je uspána", RDR_SCREEN_H / 2 - 20);
      _u.setFont(RDR_FONT_ROW);
      centerText("Probudíte ji tlačítkem RESET", RDR_SCREEN_H / 2 + 20);
      centerText("nebo vypnutím a zapnutím napájení.", RDR_SCREEN_H / 2 + 48);
      _u.setFont(RDR_FONT_ROW_SUB);
      centerText("Pozice čtení je uložena na kartě.", RDR_SCREEN_H - 120);
      drawStatus(RDR_DEV_SLEEP, battPct, "");
    } while (_d.nextPage());
    _refreshes++;
  }

private:
  RdrDisplayT &_d;
  U8G2_FOR_ADAFRUIT_GFX &_u;
  uint32_t _refreshes = 0;
  uint16_t _sinceFull = 0;   // kolik castecnych prekresleni od posledniho plneho

  static int rowY(int r) { return RDR_LIST_TOP + r * RDR_ROW_H; }

  void centerText(const String &s, int y) {
    int w = _u.getUTF8Width(s.c_str());
    _u.setCursor((RDR_SCREEN_W - w) / 2, y);
    _u.print(s);
  }

  // --- zalozky nahore ---------------------------------------------------
  void drawTabs(RdrCategory cat) {
    const int tabW = RDR_SCREEN_W / RDR_CAT_COUNT;
    _u.setFont(RDR_FONT_TAB);
    for (int i = 0; i < RDR_CAT_COUNT; i++) {
      int x = i * tabW;
      bool sel = (i == (int)cat);
      if (sel) {
        _d.fillRect(x, 0, tabW, RDR_HEADER_H - 6, GxEPD_BLACK);
        _u.setForegroundColor(GxEPD_WHITE);
        _u.setBackgroundColor(GxEPD_BLACK);
      }
      String t = RDR_CAT_NAME[i];
      int w = _u.getUTF8Width(t.c_str());
      _u.setCursor(x + (tabW - w) / 2, 34);
      _u.print(t);
      if (sel) {
        _u.setForegroundColor(GxEPD_BLACK);
        _u.setBackgroundColor(GxEPD_WHITE);
      }
      if (i < RDR_CAT_COUNT - 1)
        _d.drawFastVLine(x + tabW, 6, RDR_HEADER_H - 18, GxEPD_BLACK);
    }
    _d.drawFastHLine(0, RDR_HEADER_H - 6, RDR_SCREEN_W, GxEPD_BLACK);
  }

  // --- jeden radek seznamu ----------------------------------------------
  void drawRow(RdrBookEntry &b, int y, bool selected) {
    if (selected) {
      _d.fillRect(4, y + 2, RDR_SCREEN_W - 8, RDR_ROW_H - 6, GxEPD_BLACK);
      _u.setForegroundColor(GxEPD_WHITE);
      _u.setBackgroundColor(GxEPD_BLACK);
    }

    // nazev, zkraceny aby se vesel
    _u.setFont(RDR_FONT_ROW);
    String t = b.title;
    bool cut = false;
    while (_u.getUTF8Width(t.c_str()) > RDR_SCREEN_W - 150 && t.length() > 4) {
      // Ubirat po celych UTF-8 znacich. Drive se ubiraly 2 bajty, coz
      // u ceskych nazvu rozpulilo znak s diakritikou a vzniklo smeti.
      int k = t.length() - 1;
      while (k > 0 && (((uint8_t)t[k]) & 0xC0) == 0x80) k--;
      t.remove(k);
      cut = true;
    }
    if (cut) { t.trim(); t += "..."; }
    _u.setCursor(RDR_MARGIN_X, y + 22);
    _u.print(t);

    // druhy radek: procenta nebo stav
    _u.setFont(RDR_FONT_ROW_SUB);
    String sub;
    if (b.needsConv) {
      // EPUB, ktery jeste nema svuj .book
      sub = "EPUB · ke zpracování · " + velikost(b.size);
    } else {
      switch (b.category()) {
        case RDR_CAT_READING: sub = "Přečteno " + String(b.percent()) + " %"; break;
        case RDR_CAT_DONE:    sub = "Dokončeno"; break;
        default:              sub = velikost(b.size); break;
      }
    }
    _u.setCursor(RDR_MARGIN_X, y + 40);
    _u.print(sub);

    // ukazatel postupu vpravo u rozectenych
    if (b.category() == RDR_CAT_READING && !b.needsConv) {
      int bx = RDR_SCREEN_W - 110, by = y + 28, bw = 84, bh = 8;
      uint16_t fg = selected ? GxEPD_WHITE : GxEPD_BLACK;
      _d.drawRect(bx, by, bw, bh, fg);
      int fill = (bw - 2) * b.percent() / 100;
      if (fill > 0) _d.fillRect(bx + 1, by + 1, fill, bh - 2, fg);
    }

    if (selected) {
      _u.setForegroundColor(GxEPD_BLACK);
      _u.setBackgroundColor(GxEPD_WHITE);
    }
  }

  void drawScrollHint(int total, int scroll) {
    if (total <= RDR_LIST_ROWS) return;
    int trackTop = RDR_LIST_TOP, trackH = RDR_LIST_BOTTOM - RDR_LIST_TOP;
    int thumbH = max(20, trackH * RDR_LIST_ROWS / total);
    int thumbY = trackTop + (trackH - thumbH) * scroll / max(1, total - RDR_LIST_ROWS);
    _d.fillRect(RDR_SCREEN_W - 6, thumbY, 3, thumbH, GxEPD_BLACK);
  }

  String statusRight(RdrLibrary &lib, RdrCategory cat, int cursor) {
    int n = lib.countIn(cat);
    if (n == 0) return "";
    if (cursor < 0) return "Knih: " + String(n);      // vyber zalozky, bez kurzoru
    return String(cursor + 1) + "/" + String(n);
  }

  static String velikost(uint32_t b) {
    if (b < 1024) return String(b) + " B";
    if (b < 1024UL * 1024UL) return String(b / 1024) + " kB";
    return String(b / (1024UL * 1024UL)) + "," +
           String((b % (1024UL * 1024UL)) / 104858) + " MB";
  }

  // --- stavovy radek dole -----------------------------------------------
  void drawStatus(RdrDevState dev, int battPct, const String &right) {
    int y = RDR_SCREEN_H - RDR_STATUS_H;
    _d.drawFastHLine(0, y, RDR_SCREEN_W, GxEPD_BLACK);

    _u.setFont(RDR_FONT_STATUS);
    const char *st;
    switch (dev) {
      case RDR_DEV_ACTIVE: st = "Aktivní";   break;
      case RDR_DEV_IDLE:   st = "Úsporný režim"; break;
      default:             st = "Uspáno";    break;
    }
    _u.setCursor(RDR_MARGIN_X, y + 26);
    _u.print(st);

    if (right.length()) {
      int w = _u.getUTF8Width(right.c_str());
      _u.setCursor((RDR_SCREEN_W - w) / 2, y + 26);
      _u.print(right);
    }

    String bs = String(battPct) + " %";
    int bw = _u.getUTF8Width(bs.c_str());
    _u.setCursor(RDR_SCREEN_W - RDR_MARGIN_X - bw, y + 26);
    _u.print(bs);
    drawBattery(RDR_SCREEN_W - RDR_MARGIN_X - bw - 44, y + 13, battPct);
  }

  void drawBattery(int x, int y, int pct) {
    int w = 32, h = 14;
    _d.drawRect(x, y, w, h, GxEPD_BLACK);
    _d.fillRect(x + w, y + 4, 3, h - 8, GxEPD_BLACK);
    int fill = (w - 4) * pct / 100;
    if (fill > 0) _d.fillRect(x + 2, y + 2, fill, h - 4, GxEPD_BLACK);
  }
};
