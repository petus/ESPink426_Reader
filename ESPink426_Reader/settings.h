// ============================================================================
//  settings.h  -  Nastaveni ctecky, ulozene v NVS
// ----------------------------------------------------------------------------
//  Proc NVS a ne SD: nastaveni se meni zridka (nekolikrat za zivot), takze
//  omezeny pocet zapisu do flash nevadi, a nastaveni musi byt k dispozici
//  i kdyz karta chybi nebo je vadna. Pozice cteni je naopak na SD, protoze
//  se zapisuje po kazde strance.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

struct RdrSettings {
  uint8_t  fontSize  = RDR_FONT_DEFAULT;   // 0 male, 1 stredni, 2 velke
  uint16_t sleepMin  = 5;                  // po kolika minutach necinnosti uspat

  void load() {
    Preferences pr;
    if (!pr.begin("rdrset", true)) return;
    fontSize = pr.getUChar("font", RDR_FONT_DEFAULT);
    sleepMin = pr.getUShort("sleep", 5);
    pr.end();
    clamp();
  }

  void save() {
    clamp();
    Preferences pr;
    if (!pr.begin("rdrset", false)) return;
    pr.putUChar("font", fontSize);
    pr.putUShort("sleep", sleepMin);
    pr.end();
  }

  // Timeout necinnosti v milisekundach
  uint32_t idleMs() const { return (uint32_t)sleepMin * 60UL * 1000UL; }

  const char *fontName() const {
    switch (fontSize) { case 0: return "Malé"; case 2: return "Velké"; default: return "Střední"; }
  }

private:
  void clamp() {
    if (fontSize > 2) fontSize = 2;
    if (sleepMin < 1)  sleepMin = 1;
    if (sleepMin > 60) sleepMin = 60;
  }
};
