#!/usr/bin/env bash
set -euo pipefail
umask 077
cd "$(dirname "$0")/.."
app="$PWD/build/Pulse Speech.app"
mkdir -p "$app/Contents/MacOS" build/swift-cache
cp mac/Info.plist "$app/Contents/Info.plist"
swiftc -module-cache-path "$PWD/build/swift-cache" -framework Speech \
  mac/PulseSpeech.swift -o "$app/Contents/MacOS/PulseSpeech" \
  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$PWD/mac/Info.plist"
codesign --force --sign - --identifier local.pulse.speech "$app"
"$app/Contents/MacOS/PulseSpeech" --check
