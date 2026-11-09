// ============================================================================
//  library.h  -  Knihovna knih na SD karte
// ----------------------------------------------------------------------------
//  Prochazi /books, ke kazde knize dohleda stav cteni a zaradi ji do jedne
//  ze tri kategorii: Rozecteno / Neprecteno / Precteno.
//
//  STAV CTENI = SIDECAR SOUBOR
//   Vedle "kniha.book" lezi "kniha.book.pos" s obsahem:
//       <paraOffset>\n<lineInPara>\n<page>\n<done>\n<seq>\n
//   Duvody pro sidecar misto jednoho spolecneho indexu:
//     - smazani knihy odstrani i jeji stav, nic nezustane viset
//     - poskozeni jednoho souboru neshodi celou knihovnu
//     - zapis je atomicky pres docasny soubor (desku lze kdykoli vypnout)
//
//  KATEGORIE se odvozuje ze stavu, neuklada se zvlast:
//     done == 1                -> PRECTENO
//     sidecar existuje, off>0  -> ROZECTENO
//     jinak                    -> NEPRECTENO
//
//  PROCENTA se pocitaji z BAJTOVEHO offsetu vuci velikosti souboru, nikoli
//  z cisla stranky. Pocet stranek se meni s velikosti pisma, takze
//  "zbyva 42 stran" by po prenastaveni fontu lhalo. Procenta z bajtu jsou
//  stabilni a nic navic nestoji.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include "config.h"
#include "textmap.h"

enum RdrCategory : uint8_t {
  RDR_CAT_READING = 0,   // Rozecteno
  RDR_CAT_UNREAD  = 1,   // Neprecteno
  RDR_CAT_DONE    = 2,   // Precteno
  RDR_CAT_COUNT   = 3
};

static const char *RDR_CAT_NAME[RDR_CAT_COUNT] = {
  "Rozečteno", "Nepřečteno", "Přečteno"
};

// Jedna kniha v knihovne
struct RdrBookEntry {
  String   file;          // jen jmeno souboru, napr. "rytir-pajka.book"
  String   title;         // nazev z hlavicky T:, jinak jmeno souboru
  uint32_t size    = 0;   // velikost .book v bajtech
  uint32_t offset  = 0;   // paraOffset posledni cteni
  uint16_t line    = 0;   // radek uvnitr odstavce
  int      page    = 0;   // cislo stranky (jen pro zobrazeni)
  bool     done    = false;
  uint32_t seq     = 0;   // poradi posledniho otevreni (vetsi = novejsi)

  // Kniha zatim existuje jen jako .epub a nema svuj .book. Otevreni takove
  // knihy spusti konverzi primo ve ctecce. Kdyz .book uz existuje, EPUB se
  // v seznamu SKRYJE - zdrojovy soubor na karte zustava, ale uzivatele
  // nezajima a mate ho videt kazdou knihu dvakrat.
  bool     needsConv = false;

  RdrCategory category() const {
    if (done)       return RDR_CAT_DONE;
    if (offset > 0) return RDR_CAT_READING;
    return RDR_CAT_UNREAD;   // nezkonvertovany EPUB je take "neprecteny"
  }
  // Kolik procent knihy je precteno (0-100)
  int percent() const {
    if (done) return 100;
    if (size == 0) return 0;
    uint32_t p = (uint64_t)offset * 100ULL / size;
    return (p > 100) ? 100 : (int)p;
  }
  String path() const { return String(RDR_BOOKS_DIR) + "/" + file; }
  String posPath() const { return path() + RDR_POS_SUFFIX; }
};

class RdrLibrary {
public:
  // Nacte knihovnu z karty. Vraci pocet nalezenych knih.
  int scan() {
    _count = 0;
    File dir = SD.open(RDR_BOOKS_DIR);
    if (!dir) return 0;

    while (_count < RDR_MAX_BOOKS) {
      File e = dir.openNextFile();
      if (!e) break;
      if (e.isDirectory()) { e.close(); continue; }

      String n = e.name();
      int sl = n.lastIndexOf('/');
      if (sl >= 0) n = n.substring(sl + 1);

      // Zajimaji nas .book a .epub, nikoli sidecary .book.pos
      if (n.endsWith(".book") || n.endsWith(".epub")) {
        RdrBookEntry &b = _books[_count];
        b = RdrBookEntry();
        b.file = n;
        b.size = e.size();
        b.title = "";
        b.needsConv = n.endsWith(".epub");
        loadPos(b);
        if (b.title.length() == 0) b.title = prettyName(n);
        _count++;
      }
      e.close();
    }
    dir.close();

    hideConvertedEpubs();
    readTitles();
    sortAll();
    return _count;
  }

  int  count() const { return _count; }
  RdrBookEntry& at(int i) { return _books[i]; }

  // Pocet knih v dane kategorii
  int countIn(RdrCategory c) const {
    int n = 0;
    for (int i = 0; i < _count; i++) if (_books[i].category() == c) n++;
    return n;
  }

  // Index n-te knihy v kategorii (poradi uz je serazene), -1 kdyz neni
  int indexIn(RdrCategory c, int nth) const {
    int n = 0;
    for (int i = 0; i < _count; i++) {
      if (_books[i].category() == c) {
        if (n == nth) return i;
        n++;
      }
    }
    return -1;
  }

  // Prvni kategorie, ktera neni prazdna, poradi od "start".
  // Vraci RDR_CAT_COUNT, kdyz jsou prazdne vsechny.
  RdrCategory firstNonEmpty(RdrCategory start) const {
    for (int k = 0; k < RDR_CAT_COUNT; k++) {
      RdrCategory c = (RdrCategory)((start + k) % RDR_CAT_COUNT);
      if (countIn(c) > 0) return c;
    }
    return RDR_CAT_COUNT;
  }

  // --- ulozeni stavu ------------------------------------------------------
  // Atomicky: zapis do .tmp, pak prejmenovat. Desku lze kdykoli vypnout
  // fyzickym vypinacem, takze rozepsany soubor nesmi prezit.
  bool savePos(RdrBookEntry &b) {
    String tmp = b.posPath() + ".tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) return false;
    f.println(b.offset);
    f.println(b.line);
    f.println(b.page);
    f.println(b.done ? 1 : 0);
    f.println(b.seq);
    f.flush();
    bool ok = f.size() > 0;
    f.close();
    if (!ok) { SD.remove(tmp); return false; }
    SD.remove(b.posPath());
    return SD.rename(tmp, b.posPath());
  }

  // Zvysi poradove cislo (pro razeni "naposledy cteno") a ulozi ho ke knize.
  // Citac je v NVS, zapisuje se jen pri otevreni knihy - radove desitky
  // zapisu za zivot, coz NVS snese bez problemu.
  void touchOpened(RdrBookEntry &b) {
    Preferences pr;
    uint32_t seq = 1;
    if (pr.begin("rdrlib", false)) {
      seq = pr.getUInt("seq", 0) + 1;
      pr.putUInt("seq", seq);
      pr.end();
    }
    b.seq = seq;
    savePos(b);
  }

private:
  RdrBookEntry _books[RDR_MAX_BOOKS];
  int _count = 0;

  // Kdyz ke "kniha.epub" existuje i "kniha.book", zdrojovy EPUB ze seznamu
  // vyradime. Na karte zustava (uzivatel o original neprijde), ale ve
  // ctecce se zobrazuje uz jen vysledek konverze.
  void hideConvertedEpubs() {
    int out = 0;
    for (int i = 0; i < _count; i++) {
      if (_books[i].needsConv) {
        String stem = _books[i].file.substring(0, _books[i].file.length() - 5);
        bool hasBook = false;
        for (int j = 0; j < _count; j++) {
          if (!_books[j].needsConv && _books[j].file == stem + ".book") { hasBook = true; break; }
        }
        if (hasBook) continue;      // preskocit, .book uz existuje
      }
      if (out != i) _books[out] = _books[i];
      out++;
    }
    _count = out;
  }

  static String prettyName(const String &f) {
    String t = f.substring(0, f.length() - 5);   // utnout ".book" / ".epub"
    t.replace('-', ' ');
    t.replace('_', ' ');
    // toupper jen u ASCII - u prvniho bajtu UTF-8 znaku (C s hackem apod.)
    // by vznikl neplatny znak
    if (t.length() && (uint8_t)t[0] < 0x80) t.setCharAt(0, toupper(t[0]));
    return t;
  }

  void loadPos(RdrBookEntry &b) {
    File f = SD.open(b.posPath(), FILE_READ);
    // savePos() maze stary .pos a prejmenovava .tmp. Kdyz prisel vypadek
    // napajeni presne mezi tim, zustal jen .tmp - pouzijeme ho.
    if (!f) f = SD.open(b.posPath() + ".tmp", FILE_READ);
    if (!f) return;
    b.offset = f.readStringUntil('\n').toInt();
    b.line   = f.readStringUntil('\n').toInt();
    b.page   = f.readStringUntil('\n').toInt();
    b.done   = f.readStringUntil('\n').toInt() != 0;
    b.seq    = f.readStringUntil('\n').toInt();
    f.close();
  }

  // Precte hlavicku T: z kazde knihy kvuli hezkemu nazvu v seznamu.
  // Cte se jen prvnich par set bajtu, ne cely soubor.
  void readTitles() {
    for (int i = 0; i < _count; i++) {
      if (_books[i].needsConv) continue;    // EPUB je ZIP, hlavicku v nem nenajdeme
      File f = SD.open(_books[i].path(), FILE_READ);
      if (!f) continue;
      for (int k = 0; k < 4 && f.available(); k++) {
        String ln = f.readStringUntil('\n');
        if (ln.endsWith("\r")) ln.remove(ln.length() - 1);
        if (ln.startsWith("T:")) {
          _books[i].title = ln.substring(2);
          rdrSanitizeForDisplay(_books[i].title);
          break;
        }
        if (!ln.startsWith("A:")) break;    // hlavicka skoncila
      }
      f.close();
    }
  }

  // Razeni: rozectene podle posledniho cteni sestupne (aktualni kniha
  // nahore), ostatni abecedne podle nazvu.
  void sortAll() {
    for (int i = 1; i < _count; i++) {
      RdrBookEntry key = _books[i];
      int j = i - 1;
      while (j >= 0 && isAfter(_books[j], key)) {
        _books[j + 1] = _books[j];
        j--;
      }
      _books[j + 1] = key;
    }
  }

  // Porovnani MUSI byt nejdriv podle kategorie. Drive se knihy ruznych
  // kategorii porovnavaly podle nazvu a rozectene jen mezi sebou podle seq;
  // takove razeni neni tranzitivni a rozectene knihy prolozene ostatnimi
  // pak nebyly serazene od naposledy ctene.
  static bool isAfter(const RdrBookEntry &a, const RdrBookEntry &b) {
    RdrCategory ca = a.category(), cb = b.category();
    if (ca != cb) return ca > cb;
    if (ca == RDR_CAT_READING) return a.seq < b.seq;   // vetsi seq driv
    String x = a.title; x.toLowerCase();
    String y = b.title; y.toLowerCase();
    return x > y;
  }
};
