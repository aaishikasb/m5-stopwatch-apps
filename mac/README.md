# Pulse Talk and Pulse Speech

[All apps](../README.md)

Two Mac companions for [Pulse Station](../pulse_station/README.md):

- **Pulse Talk** (`pulse_talk.py`) receives microphone recordings over USB and
  copies successful transcripts to the clipboard.
- **Pulse Speech** (`PulseSpeech.swift`) is the local speech-recognition helper
  that Pulse Talk launches as a macOS app.

## Build and authorize

Requires macOS, Xcode Command Line Tools, Python 3, and `pyserial`.
Run these commands from the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install pyserial
./scripts/build-speech.sh
open -n -W -a "$PWD/build/Pulse Speech.app" --args --authorize
```

Approve **Speech Recognition** for **Pulse Speech** in the macOS prompt. Launch
the helper through `open`, as the bridge does, so macOS attributes speech access
to its own app bundle. It never uses the Mac microphone. No Accessibility
permission or automated keystrokes are used.

## Run

Complete [build and authorization](#build-and-authorize) first. Install
[Pulse Station](../pulse_station/README.md#build) on the badge, then connect it by
USB. From the repository root, with the Python environment active, list ports
and pass the badge's port explicitly:

```sh
python3 -m serial.tools.list_ports
./scripts/talk.sh --port /dev/cu.usbmodemXXXX
```

Replace the example port with your badge's port. The bridge runs until Ctrl+C.
Close it before flashing or using a serial monitor. It reconnects if the badge
returns on the same port; otherwise restart it with the new port. It is not
installed as a login item or background system service.

Audio travels from the badge microphone over USB to macOS's on-device English
speech recognizer. No API key or cloud transcription is used. Quiet recordings
are amplified before recognition. Successful transcripts replace the clipboard;
silence and failed recognition leave it alone. Audio and transcript working files
are private temporary files removed after each transcription.

In Pulse Station's Talk mode, hold the yellow panel or blue **B** button, wait for
**Listening**, then speak. Release to transcribe. When **Ready to paste** appears,
press **Cmd+V** in your chosen app and review the text. Nothing is automatically
inserted or submitted.

For recording limits and device diagnostics, see the
[Pulse Station guide](../pulse_station/README.md#hold-to-talk).
