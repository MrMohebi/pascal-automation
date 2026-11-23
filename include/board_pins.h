#pragma once
#include <array>
#include <cstdint>

// Layout v4: owner's installed wiring, 128x64 OLED, no keypad.
namespace boardpins {
constexpr uint8_t dht = 4;
constexpr uint8_t oledSda = 21;
constexpr uint8_t oledScl = 22;
// Lamp, exhaust fan, humidifier (same order as Controller::outputs).
constexpr std::array<uint8_t, 3> relays{{25, 26, 27}};
// Previously wired relay input (old light 2). Held inactive so it cannot float.
constexpr std::array<uint8_t, 1> unusedRelays{{14}};
} // namespace boardpins
