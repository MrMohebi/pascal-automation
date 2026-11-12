#pragma once
#include <array>
#include <cstdint>

// Layout v3: owner's installed wiring, 128x64 OLED, no keypad.
namespace boardpins {
constexpr uint8_t dht = 17;
constexpr uint8_t oledSda = 21;
constexpr uint8_t oledScl = 22;
// Heater, humidifier, light 1, light 2.
constexpr std::array<uint8_t, 4> relays{{25, 26, 27, 14}};
} // namespace boardpins
