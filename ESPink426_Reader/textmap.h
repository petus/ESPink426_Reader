// ============================================================================
//  textmap.h  -  Nahrazeni znaku, ktere font neobsahuje
// ----------------------------------------------------------------------------
//  PROC TO EXISTUJE
//   Fonty U8g2 s priponou _te obsahuji glyfy s unicode 32 az 701, plus par
//   doplnku (7838, 8320-8329, euro 8364, 8451, 8457, ligatury 64256-64263).
//
//   Ceska a anglicka typograficka interpunkce ale lezi v bloku General
//   Punctuation kolem 8200 az 8230, tedy DALEKO mimo rozsah:
//       „ U+201E    " U+201C    " U+201D    ' U+2018    ' U+2019
//       – U+2013    — U+2014    … U+2026    • U+2022
//
//   Font pro ne nema glyf, takze se na displeji nevykreslilo NIC. V textu
//   pak chybely uvozovky, pomlcky i vypustky bez jakehokoli varovani.
//
//  RESENI
//   Pred vykreslenim text projdeme a tyto znaky nahradime tim, co font zna.
//   Uvozovky tedy budou rovne ("), pomlcka obycejna (-) a vypustka tri tecky.
//   Typograficka krasa se ztrati, ale text je citelny.
//
//  Vola se na dvou mistech:
//   - pri konverzi EPUB (htmltext.h), aby vznikl rovnou cisty .book
//   - pri cteni .book (book.h), aby se spravily i soubory prevedene driv
//     nebo skriptem na pocitaci
// ============================================================================
#pragma once
#include <Arduino.h>

// Nejvyssi kodovy bod, ktery font _te obsahuje souvisle
#define RDR_FONT_MAX_CP 701

// Je kodovy bod ve fontu k dispozici?
inline bool rdrGlyphAvailable(uint32_t cp) {
  if (cp <= RDR_FONT_MAX_CP) return true;
  if (cp == 7838 || cp == 8364 || cp == 8451 || cp == 8457) return true;
  if (cp >= 8320 && cp <= 8329) return true;          // dolni indexy 0-9
  if (cp >= 64256 && cp <= 64263) return true;         // ligatury fi, fl...
  return false;
}

// Nahrada za znak, ktery font nezna. Vraci nullptr, kdyz se ma znak zahodit.
inline const char* rdrReplacementFor(uint32_t cp) {
  switch (cp) {
    // --- uvozovky ---------------------------------------------------------
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: return "\"";
    case 0x2032:                                        return "'";
    case 0x2033:                                        return "\"";
    case 0x2039:                                        return "<";
    case 0x203A:                                        return ">";

    // --- pomlcky a spojovniky ---------------------------------------------
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:
    case 0x2014: case 0x2015: case 0x2212:              return "-";

    // --- ostatni interpunkce ----------------------------------------------
    case 0x2026: return "...";
    case 0x2022: case 0x2023: case 0x25CF: case 0x25AA: return "*";
    case 0x2044: return "/";
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x2122: return "(TM)";

    // --- neviditelne znaky, proste zahodit --------------------------------
    case 0x200B: case 0x200C: case 0x200D: case 0x2060:
    case 0xFEFF: case 0x00AD:
      return "";

    default: return nullptr;   // neznamy znak -> otaznik, at je videt
  }
}

// Delka UTF-8 znaku podle uvodniho bajtu
inline int rdrUtf8Len(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

// Projde retezec a nahradi vse, co font neumi vykreslit.
// Kdyz retezec obsahuje jen bezne znaky, skonci hned po prvnim pruchodu
// bez alokace navic.
inline void rdrSanitizeForDisplay(String &s) {
  // Rychla cesta: vsechno pod 0xC0 je ASCII nebo pokracovaci bajt,
  // ktery nepotrebuje kontrolu. Vetsina radku je takova.
  bool needs = false;
  // size_t, ne uint16_t: odstavec nad 64 kB by jinak pretekl citac
  // a smycka by nikdy neskoncila (watchdog by desku restartoval).
  for (size_t i = 0; i < s.length(); i++) {
    if ((uint8_t)s[i] >= 0xC0) { needs = true; break; }
  }
  if (!needs) return;

  String out;
  out.reserve(s.length() + 8);

  size_t i = 0;
  while (i < s.length()) {
    uint8_t b = (uint8_t)s[i];
    int len = rdrUtf8Len(b);
    if (i + len > s.length()) { i++; continue; }        // useknuty znak

    // dekodovat kodovy bod
    uint32_t cp;
    if (len == 1)      cp = b;
    else if (len == 2) cp = ((uint32_t)(b & 0x1F) << 6)  | ((uint8_t)s[i+1] & 0x3F);
    else if (len == 3) cp = ((uint32_t)(b & 0x0F) << 12) | (((uint8_t)s[i+1] & 0x3F) << 6)
                                                         | ((uint8_t)s[i+2] & 0x3F);
    else               cp = ((uint32_t)(b & 0x07) << 18) | (((uint8_t)s[i+1] & 0x3F) << 12)
                                                         | (((uint8_t)s[i+2] & 0x3F) << 6)
                                                         | ((uint8_t)s[i+3] & 0x3F);

    if (cp == 0x00A0) {           // nedelitelna mezera -> obycejna
      out += ' ';
    } else if (cp == 0x00AD) {    // mekke deleni (&shy;) - zahodit
      // Lezi pod 701, takze by ho font vykreslil jako VIDITELNY spojovnik
      // uprostred slov. Ceske EPUBy ho casto obsahuji v kazdem delsim slove.
    } else if (rdrGlyphAvailable(cp)) {
      for (int k = 0; k < len; k++) out += s[i + k];
    } else {
      const char *rep = rdrReplacementFor(cp);
      if (rep)      out += rep;   // prazdny retezec = zahodit
      else          out += '?';   // neznamy znak, at je videt, ze neco chybi
    }
    i += len;
  }
  s = out;
}
