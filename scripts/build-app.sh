#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/.."
case "${1:-}" in
  flight_radar|pulse_station|reactions|metronome|cat_toy|bloom) app="$1" ;;
  *) echo 'Usage: ./scripts/build-app.sh flight_radar|pulse_station|reactions|metronome|cat_toy|bloom' >&2; exit 2 ;;
esac
if [[ "$app" == flight_radar && ! -f flight_radar/secrets.h ]]; then
  echo 'Copy flight_radar/secrets.example.h to flight_radar/secrets.h and set your Wi-Fi first.' >&2
  exit 1
fi
if [[ "$app" == pulse_station ]]; then
  python3 -B scripts/pulse_fonts.py
fi
./scripts/arduino.sh compile --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,EraseFlash=none \
  --export-binaries=false \
  --libraries "$PWD/libraries" \
  --build-path "$PWD/build/$app" "$app"
