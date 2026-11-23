#include "outputs.h"
#include <cassert>
#include <iostream>
int pinCalls = 0;
int main() {
  static_assert(outputs::simulated);
  static_assert(outputs::count == 3);
  outputs::begin();
  bool states[outputs::count] = {true, true, true};
  outputs::write(states);
  assert(pinCalls == 0);
  std::cout << "Simulated driver makes zero GPIO calls\n";
}
