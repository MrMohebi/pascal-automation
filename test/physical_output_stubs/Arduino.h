#pragma once
#include <vector>
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
struct PinEvent {
  char operation;
  int pin;
  int value;
};
inline std::vector<PinEvent> pinEvents;
inline void digitalWrite(int pin, int value) { pinEvents.push_back({'w', pin, value}); }
inline void pinMode(int pin, int mode) { pinEvents.push_back({'m', pin, mode}); }
