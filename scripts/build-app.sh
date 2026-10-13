#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/.."
case "${1:-}" in
  reactions|metronome|cat_toy) app="$1" ;;
  *) echo 'Usage: ./scripts/build-app.sh reactions|metronome|cat_toy' >&2; exit 2 ;;
esac
./scripts/arduino.sh compile --fqbn m5stack:esp32:m5stack_stopwatch:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,EraseFlash=none \
  --libraries "$PWD/libraries" \
  --build-path "$PWD/build/$app/cache" --output-dir "$PWD/build/$app" "$app"
