#pragma once
extern int pinCalls;
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
inline void digitalWrite(int, int) { pinCalls++; }
inline void pinMode(int, int) { pinCalls++; }
