// ============================================================================
//  zip.h  -  Cteni ZIP archivu ze SD karty (EPUB je ZIP)
// ----------------------------------------------------------------------------
//  Cte se STREAMOVANE primo z karty. Archiv se NIKDY nenacita cely do RAM -
//  EPUB ma bezne jednotky MB, kdezto volne haldy mame kolem 300 kB.
//
//  DEKOMPRESE
//   ESP-IDF obsahuje miniz, ale v ROM jsou jen nizkourovnove funkce
//   tinfl_* / tdefl_*; archivni a ZIP API tam NENI. Strukturu archivu si
//   proto ctem sami a miniz pouzivame jen na samotny deflate.
//
//   POZOR NA PAMET: tinfl potrebuje 32 kB LZ slovnik. Ten MUSI byt na halde.
//   Hlavni uloha ma jen 4 kB zasobniku a alokace takoveho pole lokalne
//   zpusobi tezko dohledatelne pady.
//
//  STRUKTURA ZIP (to podstatne)
//   Na konci souboru je "End of Central Directory" (EOCD, 0x06054b50),
//   ktery rika, kde zacina centralni adresar a kolik ma polozek.
//   Kazda polozka centralniho adresare (0x02014b50) nese jmeno, metodu
//   komprese, velikosti a offset lokalni hlavicky.
//   U lokalni hlavicky (0x04034b50) je pak potreba precist delky jmena
//   a "extra" pole, protoze se od centralniho adresare mohou lisit,
//   a teprve za nimi zacinaji vlastni data.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "log.h"

// Miniz se v ruznych verzich jadra jmenuje ruzne. Zjistime to za prekladu.
#if defined(__has_include)
  #if __has_include("miniz.h")
    #include "miniz.h"
    #define RDR_HAVE_MINIZ 1
  #elif __has_include("rom/miniz.h")
    #include "rom/miniz.h"
    #define RDR_HAVE_MINIZ 1
  #elif __has_include("esp32s3/rom/miniz.h")
    #include "esp32s3/rom/miniz.h"
    #define RDR_HAVE_MINIZ 1
  #else
    #define RDR_HAVE_MINIZ 0
  #endif
#else
  #define RDR_HAVE_MINIZ 0
#endif

#define RDR_ZIP_MAX_ENTRIES 64      // staci na obsah bezneho EPUBu

struct RdrZipEntry {
  String   name;
  uint32_t compSize   = 0;
  uint32_t uncompSize = 0;
  uint32_t localHdr   = 0;    // offset lokalni hlavicky v souboru
  uint16_t method     = 0;    // 0 = ulozeno bez komprese, 8 = deflate
};

class RdrZip {
public:
  bool open(const String &path) {
    close();
    _f = SD.open(path.c_str(), FILE_READ);
    if (!_f) { RDR_LOG("zip: soubor '%s' nelze otevrit", path.c_str()); return false; }
    _size = _f.size();
    if (!readCentralDirectory()) { close(); return false; }
    return true;
  }
  void close() { if (_f) _f.close(); _count = 0; }

  int count() const { return _count; }
  const RdrZipEntry& entry(int i) const { return _entries[i]; }

  // Najde polozku podle presneho jmena, -1 kdyz neni.
  int find(const String &name) const {
    for (int i = 0; i < _count; i++) if (_entries[i].name == name) return i;
    return -1;
  }
  // Najde prvni polozku koncici danym retezcem (napr. ".opf").
  int findEndsWith(const String &suffix) const {
    for (int i = 0; i < _count; i++) if (_entries[i].name.endsWith(suffix)) return i;
    return -1;
  }

  // Offset, kde v souboru zacinaji vlastni data polozky.
  // Musi se precist z LOKALNI hlavicky - delky jmena a extra pole se
  // mohou lisit od tech v centralnim adresari.
  uint32_t dataOffset(int i) {
    if (i < 0 || i >= _count) return 0;
    _f.seek(_entries[i].localHdr);
    uint8_t h[30];
    if (_f.read(h, 30) != 30) return 0;
    if (rd32(h, 0) != 0x04034b50UL) {
      RDR_LOG("zip: chybna lokalni hlavicka u '%s'", _entries[i].name.c_str());
      return 0;
    }
    uint16_t nameLen  = rd16(h, 26);
    uint16_t extraLen = rd16(h, 28);
    return _entries[i].localHdr + 30 + nameLen + extraLen;
  }

  File& file() { return _f; }

  // Vypise obsah archivu do logu - diagnostika pred implementaci konverze.
  void dump() {
    RDR_LOG("zip: %d polozek, soubor %lu B", _count, (unsigned long)_size);
    for (int i = 0; i < _count; i++) {
      const RdrZipEntry &e = _entries[i];
      RDR_LOG("  %2d  %-42s  %s  %7lu -> %7lu B", i, e.name.c_str(),
              e.method == 0 ? "ulozeno" : e.method == 8 ? "deflate" : "JINE!!!",
              (unsigned long)e.compSize, (unsigned long)e.uncompSize);
    }
  }

private:
  File _f;
  uint32_t _size = 0;
  RdrZipEntry _entries[RDR_ZIP_MAX_ENTRIES];
  int _count = 0;

  static uint16_t rd16(const uint8_t *p, int o) {
    return (uint16_t)p[o] | ((uint16_t)p[o + 1] << 8);
  }
  static uint32_t rd32(const uint8_t *p, int o) {
    return (uint32_t)p[o] | ((uint32_t)p[o+1] << 8) |
           ((uint32_t)p[o+2] << 16) | ((uint32_t)p[o+3] << 24);
  }

  // EOCD je na konci souboru, ale muze za nim byt jeste komentar archivu
  // (az 64 kB). Hledame proto pozpatku v poslednim useku.
  bool findEOCD(uint32_t &cdOffset, uint16_t &entries) {
    const int RDR_WINDOW = 512;
    uint8_t buf[RDR_WINDOW + 4];
    uint32_t searched = 0;

    while (searched < 66000 && searched < _size) {
      uint32_t chunk = min((uint32_t)RDR_WINDOW, _size - searched);
      uint32_t pos = _size - searched - chunk;
      _f.seek(pos);
      if (_f.read(buf, chunk) != (int)chunk) return false;

      for (int i = (int)chunk - 22; i >= 0; i--) {
        if (rd32(buf, i) == 0x06054b50UL) {
          entries  = rd16(buf, i + 10);
          cdOffset = rd32(buf, i + 16);
          return true;
        }
      }
      // Okna se MUSI prekryvat o celou delku zaznamu EOCD (22 B), jinak
      // vznikne mezi nimi mezera, ktera se nikdy neprohleda. Puvodne tu
      // byl presah jen 3 B (delka signatury), coz nechavalo ~19 B slepych.
      if (chunk <= 22) break;
      searched += chunk - 21;
    }
    RDR_LOG("zip: EOCD nenalezeno - neni to platny ZIP?");
    return false;
  }

  bool readCentralDirectory() {
    uint32_t cd = 0; uint16_t n = 0;
    if (!findEOCD(cd, n)) return false;
    if (cd >= _size) { RDR_LOG("zip: neplatny offset adresare"); return false; }

    _f.seek(cd);
    _count = 0;
    uint8_t h[46];

    for (int i = 0; i < n && _count < RDR_ZIP_MAX_ENTRIES; i++) {
      if (_f.read(h, 46) != 46) break;
      if (rd32(h, 0) != 0x02014b50UL) {
        RDR_LOG("zip: neocekavana hlavicka na polozce %d", i);
        break;
      }
      uint16_t method   = rd16(h, 10);
      uint32_t compSz   = rd32(h, 20);
      uint32_t uncompSz = rd32(h, 24);
      uint16_t nameLen  = rd16(h, 28);
      uint16_t extraLen = rd16(h, 30);
      uint16_t cmtLen   = rd16(h, 32);
      uint32_t localHdr = rd32(h, 42);

      String name;
      name.reserve(nameLen);
      for (uint16_t k = 0; k < nameLen; k++) {
        int c = _f.read();
        if (c < 0) return _count > 0;
        name += (char)c;
      }
      // extra a komentar preskocit
      _f.seek(_f.position() + extraLen + cmtLen);

      // slozky (jmeno konci lomitkem) nas nezajimaji
      if (!name.endsWith("/")) {
        RdrZipEntry &e = _entries[_count];
        e.name = name; e.method = method;
        e.compSize = compSz; e.uncompSize = uncompSz; e.localHdr = localHdr;
        _count++;
      }
    }
    return _count > 0;
  }
};
