// ============================================================================
//  htmltext.h  -  Streamovany prevod XHTML na cisty text formatu .book
// ----------------------------------------------------------------------------
//  Data prichazeji po KOUSCICH z dekomprese, takze parser musi byt stavovy:
//  znacka nebo entita muze byt rozdelena mezi dva kousky. Drzi se proto jen
//  maly rozpracovany buffer, nikdy cely dokument.
//
//  CO PARSER DELA
//   - zahodi obsah <script>, <style>, <head>
//   - blokove znacky (p, div, br, li, h1-h6) ukoncuji odstavec
//   - h1-h6 oznaci jako nadpis (prefix H:), prvni v souboru jako kapitolu (C:)
//   - dekoduje HTML entity vcetne ciselnych (&#233; i &#xE9;)
//   - normalizuje bile znaky, kazdy odstavec je JEDEN radek
//
//  DIAKRITIKA
//   Text z EPUBu je v UTF-8, ale muze byt v ROZLOZENE forme (pismeno +
//   kombinujici hacek). U8g2 by pak vykreslil holé pismeno a hacek zahodil.
//   Prevadime proto nejcastejsi ceske kombinace na slozenou (NFC) podobu.
//   Skript epub2book.py na pocitaci dela totez pres unicodedata.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "log.h"
#include "textmap.h"

class RdrHtmlText {
public:
  // out    - otevreny soubor .book, kam se zapisuji hotove radky
  // firstHeadingIsChapter - prvni nadpis v dokumentu dostane prefix C:
  void begin(File *out, bool firstHeadingIsChapter) {
    _out = out;
    _state = TEXT;
    _tag = ""; _ent = ""; _para = "";
    _skipDepth = 0;
    _headingLevel = 0;
    _chapterPending = firstHeadingIsChapter;
    _lastWasSpace = true;
  }

  // Zpracuje kousek dat. Vraci false pri chybe zapisu.
  bool feed(const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++) {
      char c = (char)d[i];
      switch (_state) {

        case TEXT:
          textChar(c);
          break;

        case TAG:
          if (c == '>') { handleTag(_tag); _state = TEXT; }
          else {
            if (_tag.length() < 64) _tag += c;
            // Posledni znak pred '>' sledujeme zvlast - jmeno znacky se
            // uklada jen do 64 znaku, takze "/" u dlouhe znacky by se ztratilo.
            _tagSlash = (c == '/');
          }
          break;

        case ENTITY: {
          bool entChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '#';
          if (c == ';') { if (!_skipDepth) appendEntity(_ent); _state = TEXT; }
          else if (entChar && _ent.length() < 12) _ent += c;
          else {
            // Nebyla to entita, ale obycejny ampersand ("Tom & Jerry").
            // Drive se nasledujicich az 12 znaku zahodilo. Vratime je do
            // textu a aktualni znak zpracujeme normalne.
            _state = TEXT;
            if (!_skipDepth) { appendChar('&'); for (size_t k = 0; k < _ent.length(); k++) appendChar(_ent[k]); }
            textChar(c);
          }
          break;
        }
      }
    }
    return _ok;
  }

  // Dopsat posledni rozpracovany odstavec
  bool finish() { flushPara(); return _ok; }

private:
  void textChar(char c) {
    if (c == '<') { _state = TAG; _tag = ""; _tagSlash = false; }
    else if (c == '&') { _state = ENTITY; _ent = ""; }
    else if (!_skipDepth) appendChar(c);
  }
public:

private:
  enum St : uint8_t { TEXT, TAG, ENTITY };

  File   *_out = nullptr;
  St      _state = TEXT;
  String  _tag, _ent, _para;
  int     _skipDepth = 0;      // >0 = jsme uvnitr script/style/head
  int     _headingLevel = 0;   // 1-6 kdyz jsme uvnitr nadpisu
  bool    _chapterPending = false;
  bool    _lastWasSpace = true;
  bool    _ok = true;
  bool    _tagSlash = false;   // znacka konci "/>" (samouzaviraci)

  void appendChar(char c) {
    // Bile znaky se sloucí do jedne mezery - odstavec je jeden radek.
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      if (!_lastWasSpace && _para.length()) { _para += ' '; _lastWasSpace = true; }
      return;
    }
    _para += c;
    _lastWasSpace = false;
    if (_para.length() > 4000) flushPara();   // pojistka proti obri "vete"
  }

  void appendStr(const char *s) {
    while (*s) { char c = *s++; _para += c; }
    _lastWasSpace = false;
  }

  void flushPara() {
    if (!_out) return;
    String t = _para;
    _para = "";
    _lastWasSpace = true;
    t.trim();
    if (t.length() == 0) return;

    normalizeCzech(t);

    // Typografickou interpunkci (uvozovky, pomlcky, vypustku) font _te
    // neobsahuje - lezi nad kodovym bodem 701. Bez teto nahrady by se
    // na displeji nevykreslila vubec a v textu by chybely uvozovky.
    rdrSanitizeForDisplay(t);

    String line;
    if (_headingLevel > 0) {
      if (_chapterPending) { line = "C:" + t; _chapterPending = false; }
      else                 line = "H:" + t;
    } else {
      line = t;
    }
    if (_out->println(line) == 0) { _ok = false; }
  }

  void handleTag(const String &raw) {
    if (raw.length() == 0) return;
    if (raw[0] == '!' || raw[0] == '?') return;      // komentar, DOCTYPE, XML deklarace

    bool closing = (raw[0] == '/');
    int  s = closing ? 1 : 0;

    // vytahnout jmeno znacky
    String name;
    while (s < (int)raw.length()) {
      char c = raw[s];
      if (c == ' ' || c == '\t' || c == '\n' || c == '/' || c == '>') break;
      name += (char)tolower(c);
      s++;
    }
    if (name.length() == 0) return;

    // bloky, jejichz obsah nechceme vubec
    if (name == "script" || name == "style" || name == "head" || name == "svg") {
      if (closing) { if (_skipDepth) _skipDepth--; }
      else if (!_tagSlash) _skipDepth++;   // <script ... /> nema obsah ani konec
      return;
    }
    if (_skipDepth) return;

    // nadpisy
    if (name.length() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
      flushPara();
      if (closing) { _headingLevel = 0; }
      else         { _headingLevel = name[1] - '0'; }
      return;
    }

    // blokove znacky ukoncuji odstavec
    if (name == "p" || name == "div" || name == "br" || name == "li" ||
        name == "blockquote" || name == "tr" || name == "hr" ||
        name == "section" || name == "article" || name == "body") {
      flushPara();
      return;
    }
    // ostatni (span, em, strong, a, i, b...) ignorujeme - text zustava
  }

  void appendEntity(const String &e) {
    if (e.length() == 0) return;

    if (e[0] == '#') {
      long cp = (e.length() > 1 && (e[1] == 'x' || e[1] == 'X'))
                ? strtol(e.c_str() + 2, NULL, 16)
                : strtol(e.c_str() + 1, NULL, 10);
      if (cp > 0) appendCodepoint((uint32_t)cp);
      return;
    }
    // pojmenovane entity - jen ty, ktere se v knihach realne vyskytuji
    if      (e == "nbsp")   { appendChar(' '); }
    else if (e == "amp")    appendStr("&");
    else if (e == "lt")     appendStr("<");
    else if (e == "gt")     appendStr(">");
    else if (e == "quot")   appendStr("\"");
    else if (e == "apos")   appendStr("'");
    else if (e == "hellip") appendStr("…");
    else if (e == "mdash")  appendStr("—");
    else if (e == "ndash")  appendStr("–");
    else if (e == "lsquo")  appendStr("'");
    else if (e == "rsquo")  appendStr("'");
    else if (e == "ldquo")  appendStr("\u201E");   // ceske uvozovky dole
    else if (e == "rdquo")  appendStr("\u201C");
    else if (e == "bdquo")  appendStr("\u201E");
    else if (e == "shy")    { /* meke deleni - zahodit */ }
    // neznamou entitu radeji zahodime, nez abychom do textu psali smeti
  }

  // Zapise kodovy bod v UTF-8
  void appendCodepoint(uint32_t cp) {
    char b[5]; int n = 0;
    if (cp < 0x80) { b[n++] = (char)cp; }
    else if (cp < 0x800) {
      b[n++] = (char)(0xC0 | (cp >> 6));
      b[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      b[n++] = (char)(0xE0 | (cp >> 12));
      b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      b[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
      b[n++] = (char)(0xF0 | (cp >> 18));
      b[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
      b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      b[n++] = (char)(0x80 | (cp & 0x3F));
    }
    b[n] = 0;
    appendStr(b);
  }

  // Prevod rozlozene diakritiky na slozenou (NFC).
  // U8g2 nezna kombinujici znaky - vykreslil by holé pismeno a hacek
  // zahodil, takze by z "č" bylo "c".
  static void normalizeCzech(String &t) {
    if (t.indexOf("\u0301") < 0 && t.indexOf("\u030C") < 0 &&
        t.indexOf("\u030A") < 0) return;             // nic rozlozeneho, rychla cesta

    // POZOR na jmena: Print.h definuje makra DEC, HEX, OCT a BIN.
    // Pole se proto jmenuje RDR_DECOMP, nikoli DEC - jinak preprocesor
    // vyrobi "static const char *10[]" a preklad spadne.
    static const char *RDR_DECOMP[] = {
      "a\u0301","c\u030C","d\u030C","e\u0301","e\u030C","i\u0301","n\u030C",
      "o\u0301","r\u030C","s\u030C","t\u030C","u\u0301","u\u030A","y\u0301","z\u030C",
      "A\u0301","C\u030C","D\u030C","E\u0301","E\u030C","I\u0301","N\u030C",
      "O\u0301","R\u030C","S\u030C","T\u030C","U\u0301","U\u030A","Y\u0301","Z\u030C" };
    static const char *RDR_COMPOSED[] = {
      "á","č","ď","é","ě","í","ň","ó","ř","š","ť","ú","ů","ý","ž",
      "Á","Č","Ď","É","Ě","Í","Ň","Ó","Ř","Š","Ť","Ú","Ů","Ý","Ž" };
    for (int i = 0; i < 30; i++) t.replace(RDR_DECOMP[i], RDR_COMPOSED[i]);
  }
};
