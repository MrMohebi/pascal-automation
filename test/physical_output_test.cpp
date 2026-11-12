#include "outputs.h"
#include <cassert>
#include <iostream>

int main() {
  static_assert(!outputs::simulated);
  constexpr int expectedPins[] = {25, 26, 27, 14};
  constexpr int off = PASCAL_RELAY_ACTIVE_LOW ? HIGH : LOW;
  constexpr int on = PASCAL_RELAY_ACTIVE_LOW ? LOW : HIGH;
  outputs::begin();
  assert(pinEvents.size() == 8);
  for (int i = 0; i < 4; ++i) {
    const auto &write = pinEvents[2 * i];
    const auto &mode = pinEvents[2 * i + 1];
    // Set the inactive level before enabling each pin's output driver.
    assert(write.operation == 'w' && write.pin == expectedPins[i] && write.value == off);
    assert(mode.operation == 'm' && mode.pin == expectedPins[i] && mode.value == OUTPUT);
  }
  for (int mask = 0; mask < 16; ++mask) {
    pinEvents.clear();
    bool states[4];
    for (int i = 0; i < 4; ++i)
      states[i] = (mask & (1 << i)) != 0;
    outputs::write(states);
    assert(pinEvents.size() == 4);
    for (int i = 0; i < 4; ++i) {
      const auto &event = pinEvents[i];
      assert(event.operation == 'w' && event.pin == expectedPins[i]);
      assert(event.value == (states[i] ? on : off));
    }
  }
  std::cout << "Physical driver (mock GPIO), active-low=" << PASCAL_RELAY_ACTIVE_LOW
            << ": OFF-before-output initialization and all 16 state combinations passed\n";
}
