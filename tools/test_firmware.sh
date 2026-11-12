#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
task_build_dir=$(mktemp -d)
trap 'rm -rf "$task_build_dir"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude test/control_test.cpp -o "$task_build_dir/control"
"$task_build_dir/control"
c++ -std=c++17 -Wall -Wextra -Werror -Itest/stubs -Iinclude test/output_test.cpp -o "$task_build_dir/outputs"
"$task_build_dir/outputs"
if [ -d .pio/libdeps/esp32dev/ArduinoJson/src ]; then
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -I.pio/libdeps/esp32dev/ArduinoJson/src test/config_json_test.cpp -o "$task_build_dir/json"
  "$task_build_dir/json"
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Itest/stubs -Iinclude -I.pio/libdeps/esp32dev/ArduinoJson/src test/storage_test.cpp -o "$task_build_dir/storage"
  "$task_build_dir/storage"
else
  echo 'Run pio run first to install ArduinoJson before running serialization tests.' >&2
  exit 1
fi
