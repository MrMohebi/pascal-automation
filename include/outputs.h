#pragma once
#include "board_pins.h"
#include <Arduino.h>
#ifndef PASCAL_ENABLE_RELAYS
#define PASCAL_ENABLE_RELAYS 0
#endif
#if PASCAL_ENABLE_RELAYS
#ifndef PASCAL_RELAY_ACTIVE_LOW
#error "Physical commissioning must explicitly define PASCAL_RELAY_ACTIVE_LOW."
#endif
#if defined(PASCAL_RELAY_ACTIVE_LOW) && PASCAL_RELAY_ACTIVE_LOW != 0 && PASCAL_RELAY_ACTIVE_LOW != 1
#error "PASCAL_RELAY_ACTIVE_LOW must be 0 or 1."
#endif
#if !defined(PASCAL_HARDWARE_COMMISSIONED) || PASCAL_HARDWARE_COMMISSIONED != 1
#error "Physical commissioning and independent heater protection must be verified first."
#endif
#endif
namespace outputs {
constexpr bool simulated = !PASCAL_ENABLE_RELAYS;
constexpr auto pins = boardpins::relays;
inline void begin() {
#if PASCAL_ENABLE_RELAYS
  for (int pin : pins) {
    digitalWrite(pin, PASCAL_RELAY_ACTIVE_LOW ? HIGH : LOW);
    pinMode(pin, OUTPUT);
  }
#endif
}
inline void write(const bool states[4]) {
#if PASCAL_ENABLE_RELAYS
  for (int i = 0; i < 4; i++)
    digitalWrite(pins[i], states[i] ? (PASCAL_RELAY_ACTIVE_LOW ? LOW : HIGH)
                                    : (PASCAL_RELAY_ACTIVE_LOW ? HIGH : LOW));
#else
  (void)states; // Deliberately no pinMode/digitalWrite calls in simulated builds.
#endif
}
} // namespace outputs
