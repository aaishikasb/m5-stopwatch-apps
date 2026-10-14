# M5Stack StopWatch apps

Firmware and Mac companion apps for the M5Stack StopWatch badge.

## Apps

Each firmware app's guide includes a screen preview, controls, and build
instructions. The Mac companions have no standalone graphical screen.

| App                                   | Runs on                         | What it does                                                                                                                                                          | Get started                                                                                                                                   |
| ------------------------------------- | ------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| [Bloom](bloom/)                       | Two StopWatches + internet relay | Send a pink pixel bloom and breathing haptics across different Wi-Fi networks; double tap to return a violet layer, then settle into the clock.                        | [Preview](bloom/README.md#bloom) · [Controls](bloom/README.md#the-interaction) · [Setup](bloom/README.md#set-up-the-internet-relay)                |
| [Flight Radar](flight_radar/)         | StopWatch + Wi-Fi               | Shows nearby aircraft around a configurable location, with heading markers, trails, altitude colors, aircraft details, and adjustable range.                          | [Preview](flight_radar/README.md#preview) · [Controls](flight_radar/README.md#controls) · [Build](flight_radar/README.md#build)               |
| [Pulse Station](pulse_station/)       | StopWatch                       | A subway-inspired interface with a 25-minute Focus timer, USB hold-to-talk mode, and battery percentage. The Agents screen is a disconnected placeholder.             | [Preview](pulse_station/README.md#preview) · [Guide and controls](pulse_station/README.md#controls) · [Build](pulse_station/README.md#build)  |
| [Reactions](reactions/)               | 2–5 StopWatch badges            | Host a local room and compete in hold timing, quick reaction, and rhythm challenges.                                                                                  | [Preview](reactions/README.md#preview) · [Guide and controls](reactions/README.md#create-or-join-a-game) · [Build](reactions/README.md#build) |
| [Metronome](metronome/)               | StopWatch                       | Tap tempo with circular beat/subdivision markers, speaker clicks, and main-beat vibration.                                                                            | [Preview](metronome/README.md#preview) · [Controls](metronome/README.md#controls) · [Build](metronome/README.md#build)                        |
| [Cat Toy](cat_toy/)                   | StopWatch                       | A scurrying mouse reacts to paw taps during quiet, timed play sessions.                                                                                               | [Preview](cat_toy/README.md#preview) · [Controls](cat_toy/README.md#controls) · [Build](cat_toy/README.md#build)                              |
| [Pulse Talk](mac/pulse_talk.py)       | macOS + USB-connected StopWatch | Receives badge microphone recordings, passes them to Pulse Speech, and copies successful transcripts to the clipboard for review and pasting. Never submits messages. | [Setup and run](mac/README.md#run)                                                                                                            |
| [Pulse Speech](mac/PulseSpeech.swift) | macOS                           | The speech-recognition helper used by Pulse Talk. Transcribes badge audio with macOS's on-device English recognizer; no API key or cloud transcription required.      | [Build and authorize](mac/README.md#build-and-authorize)                                                                                      |

Each badge app is separate firmware; install one at a time. Pulse Station's Focus
timer runs on its own. Its Talk mode uses both Mac
companions over USB.

## Development

Run `./scripts/setup.sh` once to install the pinned Arduino tools. Build any firmware
app with `./scripts/build-app.sh APP`, using its directory name (for example,
`bloom` or `pulse_station`). Outputs stay in the ignored `build/APP/` directory.
App READMEs describe any credentials or extra dependencies required before building.

`./scripts/test.sh` runs the available Bloom regression tests, checks shell syntax,
and compares all six partition tables. It requires C++17 and Python 3; the relay
test opens a temporary localhost port. Legacy app-specific test sources are not
included. Test binaries use temporary storage and are removed when the checks finish.

The PNG previews and Bloom's paired animation are documentation assets. Keep
`scripts/preview-bloom.py` and its C++ renderer to refresh them after visual changes.
Build output, Python caches, and `.arduino/downloads/` archives are disposable;
`.arduino/data/` and `.arduino/user/` contain the installed compiler and libraries.

## Installation and recovery

Building does **not** modify the badge. Installing replaces its current
application, so make a fresh full-flash backup and confirm replacement first.
Store device backups outside this repository. Recovery images are not included.

All six firmware apps use the init() v1.2.0 partition layout: two 3 MB app slots,
existing NVS and FAT data regions.
The firmware does not mount or format profile storage. Verify the actual connected
device's table and boot selection before uploading; a matching build table alone
does not verify a physical installation. Keep chip erase disabled.

The [official init() installer](https://workos.com/init/badge/install) provides the
conference firmware. Restoring a full-flash backup overwrites all device data
with the contents of that snapshot.
