// ============================================================================
//  version.h  -  Verze firmwaru ctecky eknih pro LaskaKit ESPink-4.26
// ----------------------------------------------------------------------------
//  Semanticke verzovani (semver):  MAJOR.MINOR.PATCH
//    MAJOR  nekompatibilni zmena (jiny format .book, jiny pinout, ...)
//    MINOR  nova funkce zpetne kompatibilni
//    PATCH  oprava chyby
//
//  Verze 0.x.y znamena, ze firmware jeste NENI oveden na skutecne desce
//  a rozhrani se muze menit. Prvni odladena verze bude 1.0.0.
// ============================================================================
#pragma once
#include <string.h>   // strstr  - hlavicka musi byt sobestacna
#include <stdio.h>    // snprintf


#define RDR_FW_NAME        "ESPink426Reader"
#define RDR_FW_VERSION     "0.1.2"

// __DATE__ ma tvar "Sep  7 2026" (u jednocifernehio dne dve mezery).
// Prevedeme na ISO 2026-09-07, at se to dobre radi a vsude vypada stejne.
inline const char* rdrBuildDate() {
  static char iso[11] = { 0 };
  if (iso[0]) return iso;
  const char *d = __DATE__;
  static const char *RDR_MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon3[4] = { d[0], d[1], d[2], 0 };
  const char *p = strstr(RDR_MONTHS, mon3);
  int m = p ? (int)((p - RDR_MONTHS) / 3 + 1) : 0;
  int day = (d[4] == ' ') ? (d[5] - '0') : ((d[4] - '0') * 10 + (d[5] - '0'));
  snprintf(iso, sizeof(iso), "%c%c%c%c-%02d-%02d", d[7], d[8], d[9], d[10], m, day);
  return iso;
}

// "0.1.0 (2026-09-07)" - jednotny retezec pro vsechny vypisy
inline const char* rdrVersionFull() {
  static char buf[40] = { 0 };
  if (!buf[0]) snprintf(buf, sizeof(buf), "%s (%s)", RDR_FW_VERSION, rdrBuildDate());
  return buf;
}
