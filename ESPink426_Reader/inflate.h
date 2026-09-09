// ============================================================================
//  inflate.h  -  Streamovana dekomprese deflate (miniz/tinfl) ze SD karty
// ----------------------------------------------------------------------------
//  PROC STREAMOVANE
//   Nejvetsi HTML v testovanych knihach ma po rozbaleni 252 kB, volne haldy
//   mame kolem 300 kB a samotny tinfl potrebuje 32 kB slovnik. Rozbalit
//   soubor celý do pameti tedy nelze. Data se proto ctou po kouscich,
//   prubezne rozbaluji a rovnou predavaji ke zpracovani.
//
//  POZOR NA ZASOBNIK
//   Slovnik (32 kB) MUSI byt na halde. Hlavni uloha ma jen 4 kB zasobniku
//   a lokalni pole teto velikosti zpusobi tezko dohledatelne pady.
//
//  RAW DEFLATE
//   ZIP uklada holy deflate BEZ zlib hlavicky, takze se NESMI pouzit
//   priznak TINFL_FLAG_PARSE_ZLIB_HEADER.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "zip.h"
#include "log.h"

#if RDR_HAVE_MINIZ

// Callback dostava rozbalena data po kouscich. Vraci false pro preruseni.
typedef bool (*RdrInflateSink)(const uint8_t *data, size_t len, void *ctx);

class RdrInflate {
public:
  ~RdrInflate() { freeBuffers(); }

  bool begin() {
    freeBuffers();
    _dict = (uint8_t *)malloc(TINFL_LZ_DICT_SIZE);   // 32 kB, nutne na halde
    _in   = (uint8_t *)malloc(IN_CHUNK);
    if (!_dict || !_in) {
      RDR_LOG("inflate: nedostatek pameti (volno %u B)", (unsigned)ESP.getFreeHeap());
      freeBuffers();
      return false;
    }
    return true;
  }

  // Rozbali polozku archivu a kousky predava do sink().
  // Vraci pocet rozbalenych bajtu, nebo -1 pri chybe.
  int32_t run(File &f, uint32_t dataOffset, uint32_t compSize, uint16_t method,
              RdrInflateSink sink, void *ctx) {
    if (!_dict || !_in) return -1;

    f.seek(dataOffset);

    // Metoda 0 = ulozeno bez komprese, staci prekopirovat.
    if (method == 0) {
      uint32_t left = compSize, total = 0;
      while (left) {
        uint32_t n = min((uint32_t)IN_CHUNK, left);
        int got = f.read(_in, n);
        if (got <= 0) break;
        if (!sink(_in, got, ctx)) return -1;
        left -= got; total += got;
        delay(0);
      }
      return (int32_t)total;
    }
    if (method != 8) {
      RDR_LOG("inflate: nepodporovana metoda komprese %u", method);
      return -1;
    }

    tinfl_decompressor decomp;
    tinfl_init(&decomp);

    uint32_t compLeft = compSize;
    size_t   dictOfs  = 0;
    uint32_t total    = 0;
    size_t   avail    = 0;
    const uint8_t *next = _in;

    for (;;) {
      if (avail == 0 && compLeft) {
        uint32_t want = min((uint32_t)IN_CHUNK, compLeft);
        int got = f.read(_in, want);
        if (got <= 0) break;
        avail = got; next = _in; compLeft -= got;
      }

      size_t inBytes  = avail;
      size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
      uint32_t flags  = compLeft ? TINFL_FLAG_HAS_MORE_INPUT : 0;

      tinfl_status st = tinfl_decompress(&decomp, next, &inBytes,
                                         _dict, _dict + dictOfs, &outBytes, flags);
      next  += inBytes;
      avail -= inBytes;

      if (outBytes) {
        if (!sink(_dict + dictOfs, outBytes, ctx)) return -1;
        total += outBytes;
        // Slovnik je kruhovy - po zaplneni se zacina znovu od zacatku.
        dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
      }

      delay(0);   // uvolnit procesor, at nevystreli watchdog

      if (st == TINFL_STATUS_DONE) break;
      if (st < TINFL_STATUS_DONE) {
        RDR_LOG("inflate: chyba dekomprese (%d) po %lu B", (int)st, (unsigned long)total);
        return -1;
      }
      if (st == TINFL_STATUS_NEEDS_MORE_INPUT && !compLeft && avail == 0) {
        RDR_LOG("inflate: data doslo predcasne po %lu B", (unsigned long)total);
        return -1;
      }
    }
    return (int32_t)total;
  }

private:
  static const size_t IN_CHUNK = 2048;
  uint8_t *_dict = nullptr;
  uint8_t *_in   = nullptr;

  void freeBuffers() {
    if (_dict) { free(_dict); _dict = nullptr; }
    if (_in)   { free(_in);   _in   = nullptr; }
  }
};

#endif  // RDR_HAVE_MINIZ
