# Project context

This repository contains separate Arduino firmware apps for the M5Stack StopWatch
(466×466 round display), plus Mac companions. Reuse `libraries/StopWatchFace`
for display setup. App behavior and installation notes live in each app's README.

## Commands

- Toolchain and library versions are pinned in `scripts/setup.sh`; use
  `scripts/arduino.sh` so builds resolve the repository-local `.arduino` installation.
- Build any firmware app: `./scripts/build-app.sh APP`. Artifacts go to `build/APP/`.
- Run `./scripts/test.sh` for Bloom regression tests, shell syntax, and shared
  partition checks (C++17 and Python 3 standard library; the HTTP integration test
  binds a temporary loopback port). Legacy app-specific tests are not included.
- Render Bloom: `python3 scripts/preview-bloom.py` (Pillow is a preview-only dependency).
- Build the Mac speech helper: `./scripts/build-speech.sh`; run its USB bridge with
  `python3 mac/pulse_talk.py --port PORT`. See `mac/README.md` for dependencies and authorization.

For Bloom networking, credentials, delivery behavior, or deployment, read
[`bloom/README.md`](bloom/README.md). The relay uses Python's standard library and SQLite;
there is no pip or JavaScript package install for runtime use.

Before installing firmware, read the root README's installation and recovery
section. Preserve the init() partition layout and existing profile data.
Builds do not authorize flashing devices. Keep secrets and generated binaries out of Git.
