#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/.."
python3 scripts/pulse_fonts.py
./scripts/arduino.sh compile --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,EraseFlash=none \
  --build-path "$PWD/build/pulse/cache" --output-dir "$PWD/build/pulse" pulse_station
