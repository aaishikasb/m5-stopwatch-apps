#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/.."
if [[ ! -f flight_radar/secrets.h ]]; then
  echo 'Run ./scripts/setup.sh first.' >&2
  exit 1
fi
./scripts/arduino.sh compile --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,EraseFlash=none \
  --build-path "$PWD/build/cache" --output-dir "$PWD/build" flight_radar
