"""USB badge audio -> local macOS speech recognition -> clipboard. Never submits."""
import argparse
import array
from concurrent.futures import ThreadPoolExecutor
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import wave

import serial

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "build/Pulse Speech.app"
RATE = 16000
MAX_BYTES = RATE * 2 * 30


def audio_header(line):
    match = re.fullmatch(rb"PULSE_AUDIO (\d{1,10}) (\d{1,7})\r?\n", line)
    if not match:
        raise ValueError("Invalid audio header")
    session, size = map(int, match.groups())
    if not 0 < session <= 0xffffffff or not RATE <= size <= MAX_BYTES or size % 2:
        raise ValueError("Audio length or session is out of range")
    return session, size


def read_audio(device, size):
    result = bytearray()
    deadline = time.monotonic() + 15
    while len(result) < size and time.monotonic() < deadline:
        result.extend(device.read(min(4096, size - len(result))))
    if len(result) != size:
        raise ValueError("Incomplete USB audio; recording discarded")
    return bytes(result)


def has_signal(pcm):
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    if not samples:
        return False
    mean = sum(samples) / len(samples)
    rms = math.sqrt(sum((value - mean) ** 2 for value in samples) / len(samples))
    return rms >= 30


def normalize_pcm(pcm):
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max((abs(value) for value in samples), default=0)
    # The small badge mic is quiet at desk distance; leave loud recordings alone.
    gain = max(1, min(8, 24000 / peak)) if peak else 1
    boosted = array.array("h", (round(value * gain) for value in samples))
    if sys.byteorder != "little":
        boosted.byteswap()
    return boosted.tobytes()


def speech_helper(argument, directory):
    result = directory / "result.json"
    log = directory / "speech.log"
    # LaunchServices gives the helper its own macOS privacy identity.
    subprocess.run(["/usr/bin/open", "-n", "-g", "-W", "-a", str(APP),
                    "--stdout", str(result), "--stderr", str(log),
                    "--args", str(argument)], check=True, timeout=55,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not result.exists() or result.stat().st_size > 128000:
        raise RuntimeError("Speech helper did not return a valid result")
    return json.loads(result.read_text())


def transcribe(pcm, copy=True):
    if not has_signal(pcm):
        return "empty"
    with tempfile.TemporaryDirectory(prefix="pulse-talk-", dir=ROOT / "build") as temp:
        directory = Path(temp)
        audio = directory / "recording.wav"
        with wave.open(str(audio), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(normalize_pcm(pcm))
        result = speech_helper(audio, directory)
        text = result.get("text", "")
        if not isinstance(text, str) or not text.strip() or len(text) > 16000:
            return "permission" if result.get("error") == "permission" else "empty" if result.get("error") == "empty" else "error"
        if copy:
            subprocess.run(["/usr/bin/pbcopy"], input=text.encode("utf-8"), check=True, timeout=5)
        return "ready"


def connect(port):
    device = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2, exclusive=True)
    device.dtr = False
    device.rts = False
    device.port = port
    device.open()
    return device


def run(port):
    with tempfile.TemporaryDirectory(prefix="pulse-check-", dir=ROOT / "build") as temp:
        check = speech_helper("--check", Path(temp))
    if check.get("authorization") != 3 or not check.get("onDevice"):
        raise RuntimeError("Allow Pulse Speech in macOS Speech Recognition settings, then restart Pulse Talk.")
    print("Pulse Talk is running. Hold the badge, then paste in Codex with Cmd+V.", flush=True)
    with ThreadPoolExecutor(max_workers=1) as workers:
        while True:
            try:
                with connect(port) as device:
                    print("Badge connected; audio stays on this Mac.", flush=True)
                    heartbeat = 0
                    pending = None
                    line_buffer = b""
                    while True:
                        now = time.monotonic()
                        if now - heartbeat >= 2:
                            device.write(b"bridge ready\n")
                            heartbeat = now
                        if pending and pending[1].done():
                            session, future = pending
                            try:
                                result = future.result()
                            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
                                result = "error"
                            device.write(f"voice {session} {result}\n".encode())
                            print("Ready to paste." if result == "ready" else f"Transcription: {result}.", flush=True)
                            pending = None
                        line_buffer += device.read_until(b"\n", 256)
                        if len(line_buffer) > 256:
                            raise ValueError("Oversized USB status line")
                        if not line_buffer.endswith(b"\n"):
                            continue
                        line, line_buffer = line_buffer, b""
                        if not line.startswith(b"PULSE_AUDIO "):
                            continue
                        session, size = audio_header(line)
                        audio = read_audio(device, size)
                        if pending:
                            device.write(f"voice {session} error\n".encode())
                        else:
                            pending = (session, workers.submit(transcribe, audio))
            except (serial.SerialException, OSError, ValueError) as error:
                print(f"USB disconnected: {error}. Retrying in 2 seconds.", flush=True)
                time.sleep(2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Badge USB serial port")
    arguments = parser.parse_args()
    try:
        run(arguments.port)
    except KeyboardInterrupt:
        print("Pulse Talk stopped.")
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
