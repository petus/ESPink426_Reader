// ============================================================================
//  book.h  -  Cteni .book souboru a strankovani textu
// ----------------------------------------------------------------------------
//  ZMENY PROTI PUVODNI VERZI (a proc):
//
//  1) POZICE = (offset odstavce, poradi radku v nem)
//     Puvodne se pamatoval jen bajtovy offset odstavce. Kdyz se odstavec
//     nevesel cely, dalsi stranka zacala znovu od jeho zacatku, takze se
//     radky ze spodku obrazovky OPAKOVALY nahore na dalsi strance a konec
//     odstavce se naopak ztratil. Ted se pamatuje i poradi radku uvnitr
//     odstavce, takze se odstavec korektne rozdeli mezi stranky.
//
//  2) STRANKA SE NEJDRIV SPOCITA, TEPRVE POTOM KRESLI
//     Puvodne se ze souboru cetlo primo uvnitr GxEPD2 smycky
//     firstPage()/nextPage(). Ta muze probehnout vicekrat a navic je SPI
//     sbernice SDILENA s SD kartou - cist ze souboru behem kresleni na
//     displej je primy konflikt. Ted layout() naplni pole hotovych radku
//     a vlastni kresleni uz na kartu nesaha.
//
//  3) TRI VELIKOSTI PISMA
//     Layout se pocita podle zvolene velikosti, takze zmena fontu
//     preskladá text korektne vcetne deleni odstavcu.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <U8g2_for_Adafruit_GFX.h>
#include "config.h"
#include "textmap.h"

// Pozice v knize: zacatek odstavce + kolik jeho radku uz je za nami.
struct RdrPos {
  uint32_t para = 0;   // bajtovy offset zacatku odstavce
  uint16_t line = 0;   // kolik radku toho odstavce preskocit
};

// Jeden vysazeny radek pripraveny k vykresleni
struct RdrLine {
  String  text;
  uint8_t style;   // 0 = telo, 1 = nadpis H, 2 = kapitola C
  int16_t y;       // baseline
};

#define RDR_MAX_LINES 44   // strop radku na stranku (staci i pro nejmensi pismo)

class Book {
public:
  bool open(const String &path) {
    close();
    _f = SD.open(path.c_str(), FILE_READ);
    if (!_f) return false;
    _size = _f.size();
    _title = ""; _author = "";
    readHeader();
    _pos.para = _bodyStart; _pos.line = 0;
    _page = 0;
    _history.clear();
    return true;
  }
  void close() { if (_f) _f.close(); }
  bool isOpen() { return (bool)_f; }

  const String& title()  const { return _title; }
  const String& author() const { return _author; }
  uint32_t fileSize()    const { return _size; }
  RdrPos   position()    const { return _pos; }
  int      page()        const { return _page; }
  int      lineCount()   const { return _nLines; }
  const RdrLine& line(int i) const { return _lines[i]; }
  bool     atEnd()       const { return _atEnd; }
  bool     hasHistory()  const { return !_history.empty(); }

  // Kolik procent knihy je za nami. Pocita se z BAJTU, ne ze stranek -
  // pocet stranek se meni s velikosti pisma, procenta ne.
  int percent() const {
    if (_size == 0) return 0;
    uint32_t p = (uint32_t)((uint64_t)_pos.para * 100ULL / _size);
    return (p > 100) ? 100 : (int)p;
  }

  void restore(RdrPos p, int pageNumber) {
    if (p.para < _bodyStart) { p.para = _bodyStart; p.line = 0; }
    _pos = p;
    _page = (pageNumber < 0) ? 0 : pageNumber;
    _history.clear();
  }

  void setFontSize(uint8_t s) { _fontSize = (s > 2) ? 2 : s; }
  uint8_t fontSize() const { return _fontSize; }

  // ------------------------------------------------------------------
  //  Vysazeni stranky do pole _lines. Na displej NIC nekresli, jen meri
  //  sirku textu. Po zavolani je _next zacatkem dalsi stranky.
  // ------------------------------------------------------------------
  void layout(U8G2_FOR_ADAFRUIT_GFX &u8, int top, int bottom, int left, int right) {
    _nLines = 0;
    _atEnd  = false;

    const int lh = lineHeightFor(u8, 0);   // radkovani zakladniho textu
    int y = top + lh;
    _f.seek(_pos.para);
    uint16_t skip = _pos.line;

    // Kapacita pro zalomene radky JEDNOHO odstavce. Musi pojmout radky,
    // ktere uz jsou za nami (skip), plus jednu celou stranku. Drive tu bylo
    // pevnych RDR_MAX_LINES (44) radku na cely odstavec a delsi odstavce
    // (u velkeho pisma uz kolem 1500 znaku) se na konci tise USEKLY.
    static std::vector<String> wrapped;

    while (_nLines < RDR_MAX_LINES) {
      uint32_t paraStart = _f.position();
      char type; String text;
      if (!readPara(type, text)) {
        _atEnd = true;
        _next.para = paraStart; _next.line = 0;
        return;
      }

      if (text.length() == 0) { y += lh / 2; skip = 0; continue; }

      uint8_t style  = (type == 'C') ? 2 : (type == 'H') ? 1 : 0;

      // NOVA KAPITOLA ZACINA NA NOVE STRANCE.
      // Kdyz uz na strance neco je, ukoncime ji tady a kapitola zacne
      // nahore na dalsi strance. Bez teto podminky by nadpis kapitoly
      // visel na spodku predchozi stranky.
      if (style == 2 && _nLines > 0 && skip == 0) {
        _next.para = paraStart; _next.line = 0;
        return;
      }

      int lineH  = lineHeightFor(u8, style);
      int gapTop = (style == 2) ? (lineH / 2) : (style == 1) ? (lineH / 3) : 0;

      setFont(u8, style);
      // Zalamujeme jen tolik radku, kolik muze byt potreba: preskocene plus
      // jedna stranka. Zbytek odstavce prijde na radu na dalsi strance.
      int cap = (int)skip + RDR_MAX_LINES;
      if ((int)wrapped.size() < cap) wrapped.resize(cap);
      bool truncated = false;
      int n = wrap(u8, text, right - left, wrapped.data(), cap, truncated);
      if ((int)skip >= n) skip = 0;   // pojistka: pozice z jine sazby

      for (int i = (int)skip; i < n; i++) {
        int yy = y + ((i == (int)skip) ? gapTop : 0);
        if (yy > bottom) {
          // dalsi radek uz se nevejde -> stranka konci PRESNE tady
          _next.para = paraStart; _next.line = (uint16_t)i;
          return;
        }
        if (i == (int)skip) y += gapTop;
        _lines[_nLines].text  = wrapped[i];
        _lines[_nLines].style = style;
        _lines[_nLines].y     = (int16_t)y;
        _nLines++;
        y += lineH;
        if (_nLines >= RDR_MAX_LINES) {
          if (i + 1 >= n && !truncated) {
            // posledni radek odstavce -> dalsi stranka zacne dalsim odstavcem
            _next.para = (uint32_t)_f.position(); _next.line = 0;
          } else {
            _next.para = paraStart; _next.line = (uint16_t)(i + 1);
          }
          return;
        }
      }
      skip = 0;
    }
    _next.para = (uint32_t)_f.position(); _next.line = 0;
  }

  // Posun na dalsi stranku. Vola se AZ PO layout().
  bool nextPage() {
    if (_atEnd) return false;
    _history.push_back(_pos);
    if (_history.size() > 300) _history.erase(_history.begin());
    _pos = _next;
    _page++;
    return true;
  }

  // Krok zpet.
  //
  // Rychla cesta: historie naplnena pri listovani vpred.
  //
  // Pomala cesta: historie je prazdna (po zapnuti, po obnove pozice nebo
  // po ZMENE VELIKOSTI PISMA) - stranka se dopocita prochazenim od zacatku
  // knihy. Nehledame N-tou stranku podle cisla, ale stranku, ktera KONCI
  // tam, kde ta soucasna zacina. Diky tomu to funguje i kdyz se sazba
  // mezitim zmenila a puvodni cislovani stranek uz neplati.
  bool prevPage(U8G2_FOR_ADAFRUIT_GFX &u8, int top, int bottom, int left, int right) {
    if (!_history.empty()) {
      _pos = _history.back();
      _history.pop_back();
      if (_page > 0) _page--;
      return true;
    }
    if (atBookStart()) return false;
    return backToBefore(u8, top, bottom, left, right);
  }

  bool atBookStart() const { return _pos.para <= _bodyStart && _pos.line == 0; }

  // Zmena velikosti pisma: pozice v knize zustava (je to bajtovy offset),
  // ale ulozene hranice stranek platily pro PUVODNI font, takze by po nich
  // slo listovat spatne. Zahodime je; prvni krok zpet se dopocita.
  void onFontChanged() { _history.clear(); }

private:
  static bool posBefore(const RdrPos &a, const RdrPos &b) {
    if (a.para != b.para) return a.para < b.para;
    return a.line < b.line;
  }

  // Projde knihu od zacatku a najde stranku, ktera konci na aktualni pozici.
  // Pri tom naplni historii, takze dalsi kroky zpet uz jsou okamzite.
  bool backToBefore(U8G2_FOR_ADAFRUIT_GFX &u8, int top, int bottom, int left, int right) {
    RdrPos target = _pos;
    RdrPos cur; cur.para = _bodyStart; cur.line = 0;
    std::vector<RdrPos> hist;

    for (int guard = 0; guard < 20000; guard++) {
      // Kazdych par stranek uvolnit procesor. Smycka muze u vzdalene
      // stranky bezet desitky sekund a bez tohoto by uloha loopTask
      // vyhladovela idle task - Task Watchdog by desku resetoval primo
      // uprostred cteni.
      if ((guard & 0x0F) == 0) delay(1);

      _pos = cur;
      layout(u8, top, bottom, left, right);
      if (_atEnd) break;                       // nenasli jsme, konec knihy
      if (!posBefore(_next, target)) {         // tato stranka konci na cili
        _pos = cur;
        _history = hist;
        _page = (int)hist.size();
        layout(u8, top, bottom, left, right);  // pripravit radky k vykresleni
        return true;
      }
      hist.push_back(cur);
      cur = _next;
    }
    _pos = target;                             // nic jsme nenasli, vratit zpet
    layout(u8, top, bottom, left, right);
    return false;
  }

public:

  // Radkovani se pocita ze SKUTECNYCH METRIK fontu, ne z odhadu.
  // getFontAscent() je vyska nad uctarou, getFontDescent() je zaporna
  // hodnota pod ni. Soucet je vyska pisma, nasobek je proklad.
  int lineHeightFor(U8G2_FOR_ADAFRUIT_GFX &u8, uint8_t style) const {
    setFont(u8, style);
    int h = u8.getFontAscent() - u8.getFontDescent();
    if (h <= 0) h = 20;                 // pojistka, kdyby font metriky nemel
    return (h * 145) / 100;             // proklad 1,45 - pohodlne na cteni
  }

  void setFont(U8G2_FOR_ADAFRUIT_GFX &u8, uint8_t style) const {
    if (style == 2) {
      u8.setFont(_fontSize == 0 ? RDR_FONT_CHAPTER_S :
                 _fontSize == 2 ? RDR_FONT_CHAPTER_L : RDR_FONT_CHAPTER_M);
    } else if (style == 1) {
      u8.setFont(_fontSize == 0 ? RDR_FONT_HEADING_S :
                 _fontSize == 2 ? RDR_FONT_HEADING_L : RDR_FONT_HEADING_M);
    } else {
      u8.setFont(_fontSize == 0 ? RDR_FONT_BODY_S :
                 _fontSize == 2 ? RDR_FONT_BODY_L : RDR_FONT_BODY_M);
    }
  }

private:
  File     _f;
  String   _title, _author;
  uint32_t _size = 0, _bodyStart = 0;
  RdrPos   _pos, _next;
  int      _page = 0;
  bool     _atEnd = false;
  uint8_t  _fontSize = RDR_FONT_DEFAULT;
  RdrLine  _lines[RDR_MAX_LINES];
  int      _nLines = 0;
  std::vector<RdrPos> _history;

  bool readPara(char &type, String &text) {
    if (!_f.available()) return false;
    String ln = _f.readStringUntil('\n');
    if (ln.endsWith("\r")) ln.remove(ln.length() - 1);
    if (ln.length() >= 2 && ln[1] == ':' &&
        (ln[0] == 'T' || ln[0] == 'A' || ln[0] == 'C' || ln[0] == 'H')) {
      type = ln[0]; text = ln.substring(2);
    } else {
      type = ' '; text = ln;
    }

    // Nahradit znaky, ktere font nezna. Deje se to i tady, nejen pri
    // konverzi, aby se spravne zobrazily i soubory prevedene starsi verzi
    // nebo skriptem na pocitaci.
    rdrSanitizeForDisplay(text);
    return true;
  }

  void readHeader() {
    _f.seek(0);
    while (_f.available()) {
      uint32_t p = _f.position();
      String ln = _f.readStringUntil('\n');
      if (ln.endsWith("\r")) ln.remove(ln.length() - 1);
      if (ln.startsWith("T:")) { _title  = ln.substring(2); rdrSanitizeForDisplay(_title);  continue; }
      if (ln.startsWith("A:")) { _author = ln.substring(2); rdrSanitizeForDisplay(_author); continue; }
      _f.seek(p); _bodyStart = p; return;
    }
    _bodyStart = (uint32_t)_f.position();
  }

  // Delka jednoho UTF-8 znaku podle uvodniho bajtu. Diakritika je
  // vicebajtova, takze se retezec NESMI delit po bajtech.
  static int utf8Len(uint8_t c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
  }

  // Rozlamani odstavce na radky podle sirky sloupce. Vraci pocet radku.
  // truncated = true, kdyz se do maxLines nevesel cely odstavec.
  static int wrap(U8G2_FOR_ADAFRUIT_GFX &u8, const String &text,
                  int maxW, String *out, int maxLines, bool &truncated) {
    int n = 0, i = 0, len = text.length();
    truncated = false;
    String cur = "";

    while (i < len && n < maxLines) {
      int sp = text.indexOf(' ', i);
      String w = (sp < 0) ? text.substring(i) : text.substring(i, sp);
      i = (sp < 0) ? len : sp + 1;
      if (w.length() == 0) continue;

      // Slovo sirsi nez cely sloupec (dlouha slozenina, URL, ...).
      // Bez tohoto osetreni by preteklo pres pravy okraj, protoze
      // podminka nize vyzaduje neprazdne cur.
      if (u8.getUTF8Width(w.c_str()) > maxW) {
        if (cur.length()) {
          out[n++] = cur; cur = "";
          if (n >= maxLines) { truncated = true; break; }   // slovo w zbyva
        }
        String piece = "";
        int k = 0;
        while (k < (int)w.length() && n < maxLines) {
          int cl = utf8Len((uint8_t)w[k]);
          String tryPiece = piece + w.substring(k, k + cl);
          if (u8.getUTF8Width(tryPiece.c_str()) > maxW && piece.length()) {
            out[n++] = piece + "-";      // deleni se spojovnikem
            piece = "";
            continue;                    // stejny znak zkusit na novy radek
          }
          piece = tryPiece;
          k += cl;
        }
        if (k < (int)w.length()) truncated = true;          // zbytek slova se nevesel
        cur = piece;
        continue;
      }

      String trial = cur.length() ? cur + " " + w : w;
      if (u8.getUTF8Width(trial.c_str()) > maxW && cur.length()) {
        out[n++] = cur;
        cur = w;
      } else {
        cur = trial;
      }
    }
    if (cur.length()) {
      if (n < maxLines) out[n++] = cur;
      else truncated = true;
    }
    if (i < len) truncated = true;
    return n;
  }
};
