// ============================================================================
//  log.h  -  Ladici vypisy pres UART
// ----------------------------------------------------------------------------
//  Deska NEMA nativni USB - USB-C vede do prevodniku USB-UART. Objekt Serial
//  proto musi byt namapovany na hardwarovy UART0, coz znamena nastavit
//  v Arduino IDE:  Tools -> USB CDC On Boot: DISABLED
//  Pri "Enabled" by Serial miril na nativni USB CDC, ktere nikam nevede,
//  a seriovy monitor by zustal nemy.
//
//  Vypisy se zapinaji v config.h prepinacem RDR_DEBUG_SERIAL.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "config.h"

#if RDR_DEBUG_SERIAL

  inline void rdrLogBegin() {
    Serial.begin(RDR_DEBUG_BAUD);
    delay(150);
    Serial.println();
    Serial.println(F("========================================"));
  }

  // Vypis s casovou znackou v ms - pomaha poznat, co jak dlouho trva
  // (napr. jak dlouho bezi refresh displeje nebo cteni z karty).
  #define RDR_LOG(fmt, ...)  do { \
      Serial.printf("[%8lu] " fmt "\n", (unsigned long)millis(), ##__VA_ARGS__); \
    } while (0)

  #define RDR_LOG_MS(label, expr) do { \
      uint32_t _t0 = millis(); expr; \
      Serial.printf("[%8lu] %s: %lu ms\n", (unsigned long)millis(), label, \
                    (unsigned long)(millis() - _t0)); \
    } while (0)

#else
  inline void rdrLogBegin() {}
  #define RDR_LOG(fmt, ...)        do {} while (0)
  #define RDR_LOG_MS(label, expr)  do { expr; } while (0)
#endif
