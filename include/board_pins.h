#pragma once
#include <array>
#include <cstdint>

// Layout v2: ESP32-WROOM-32, matching the owner's 19-pin J1/J2 diagram.
// GPIO numbers below are not header position numbers. See docs/WIRING.md.
namespace boardpins {
constexpr uint8_t dht = 13;     // J1-15, beside GND at J1-14.
constexpr uint8_t oledSda = 14; // J1-12.
constexpr uint8_t oledScl = 27; // J1-11.
constexpr std::array<uint8_t, 4> keypadRows{{23, 22, 21, 19}};
constexpr std::array<uint8_t, 4> keypadColumns{{18, 17, 16, 4}};
// J1-7 through J1-10: heater, humidifier, light 1, light 2.
constexpr std::array<uint8_t, 4> relays{{32, 33, 25, 26}};
} // namespace boardpins
