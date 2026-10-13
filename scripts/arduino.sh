#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-$(command -v arduino-cli || true)}"
if [[ -z "$CLI" && -x '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' ]]; then
  CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
fi
if [[ -z "$CLI" ]]; then
  echo 'Install Arduino CLI or Arduino IDE, then retry.' >&2
  exit 1
fi
export ARDUINO_DIRECTORIES_DATA="$ROOT/.arduino/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$ROOT/.arduino/downloads"
export ARDUINO_DIRECTORIES_USER="$ROOT/.arduino/user"
exec "$CLI" "$@"
