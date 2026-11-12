#include "outputs.h"
#include <cassert>
#include <iostream>
int pinCalls = 0;
int main() {
  static_assert(outputs::simulated);
  outputs::begin();
  bool states[4] = {true, true, true, true};
  outputs::write(states);
  assert(pinCalls == 0);
  std::cout << "Simulated driver makes zero GPIO calls\n";
}
