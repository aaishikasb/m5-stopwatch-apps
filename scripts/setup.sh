#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
INDEX=https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
./scripts/arduino.sh core update-index --additional-urls "$INDEX"
./scripts/arduino.sh core install m5stack:esp32@3.3.9 --additional-urls "$INDEX"
./scripts/arduino.sh lib install M5GFX@0.2.32 M5Unified@0.2.25 ArduinoJson@7.4.3
if [[ ! -f flight_radar/secrets.h ]]; then
  cp flight_radar/secrets.example.h flight_radar/secrets.h
  chmod 600 flight_radar/secrets.h
fi
echo 'Tools are ready. Build an app with ./scripts/build-app.sh APP; see its README for configuration.'
