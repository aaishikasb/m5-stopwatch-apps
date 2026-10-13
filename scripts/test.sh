#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
for script in scripts/*.sh; do bash -n "$script"; done
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I flight_radar -I .arduino/user/libraries/ArduinoJson/src \
  tests/radar_data_test.cpp -o build/radar_data_test
./build/radar_data_test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I pulse_station tests/focus_timer_test.cpp -o build/focus_timer_test
./build/focus_timer_test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I pulse_station tests/talk_state_test.cpp -o build/talk_state_test
./build/talk_state_test
for app in reactions metronome cat_toy; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$app" "tests/${app}_test.cpp" -o "build/${app}_test"
  "./build/${app}_test"
done
python3 tests/talk_bridge_test.py
python3 tests/partitions_test.py
python3 tests/face_test.py
