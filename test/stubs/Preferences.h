#pragma once
#include <map>
#include <string>
using String = std::string;
// Host NVS substitute: retains committed values across Storage instances.
class Preferences {
public:
  inline static std::map<std::string, std::string> values;
  inline static bool failWrite = false;
  bool begin(const char *, bool) { return true; }
  bool isKey(const char *key) { return values.count(key) != 0; }
  String getString(const char *key, const char *fallback) {
    return isKey(key) ? values[key] : fallback;
  }
  size_t putString(const char *key, const String &value) {
    if (failWrite)
      return 0;
    values[key] = value;
    return value.length();
  }
};
