// ============================================================================
//  battery.h  -  Mereni stavu baterie pres napetovy delic na IO9
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// Precte napeti baterie ve voltech. Prumeruje vice vzorku kvuli sumu ADC.
inline float batteryVoltage() {
  const int samples = RDR_VBAT_SAMPLES;
  uint32_t acc = 0;
  for (int i = 0; i < samples; i++) {
    acc += analogReadMilliVolts(RDR_VBAT_PIN);  // vrací mV na pinu (po ADC kalibraci)
    delay(2);
  }
  float pin_mv = (float)acc / samples;         // prumerne napeti na pinu v mV
  float vbat = (pin_mv / 1000.0f) * RDR_VBAT_DIVIDER;  // zpet pres delic na napeti clanku
  return vbat;
}

// Prevede napeti na priblizne procento nabiti (linearni aproximace mezi
// RDR_VBAT_EMPTY a RDR_VBAT_FULL). Pro presnejsi hodnotu by slo pouzit LiPol krivku,
// ale pro indikaci na cteccce je linearni odhad dostacujici.
inline int batteryPercent(float vbat) {
  float pct = (vbat - RDR_VBAT_EMPTY) / (RDR_VBAT_FULL - RDR_VBAT_EMPTY) * 100.0f;
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (int)(pct + 0.5f);
}

// True, pokud je napeti pod kritickou hranici -> nutne uspat, aby nedoslo
// k hlubokemu podvybiti LiPol clanku.
inline bool batteryCritical(float vbat) {
  return vbat < RDR_VBAT_CRITICAL;
}
