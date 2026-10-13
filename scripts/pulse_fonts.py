"""Rasterize the local Mac Helvetica faces for this badge's private build."""
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = "/System/Library/Fonts/Helvetica.ttc"
FACES = [("body", 18, 0), ("label", 18, 1), ("heading", 24, 1),
         ("route", 28, 1), ("title", 40, 1), ("timer", 72, 1)]


def vlw(size, index):
    font = ImageFont.truetype(FONT, size, index=index)
    assert font.getname() == ("Helvetica", "Bold" if index else "Regular")
    ascent, descent = font.getmetrics()
    metrics, pixels = bytearray(), bytearray()
    for code in range(32, 127):
        char = chr(code)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        width, height = right - left, bottom - top
        advance = round(font.getlength(char))
        assert 0 <= width < 256 and 0 <= advance < 256 and -128 <= left < 128
        glyph = Image.new("L", (width, height))
        ImageDraw.Draw(glyph).text((-left, -top), char, font=font, fill=255, anchor="ls")
        metrics.extend(struct.pack(">7i", code, height, width, advance, -top, left, 0))
        pixels.extend(glyph.tobytes())
    return struct.pack(">6i", 95, 11, size, 0, ascent, descent) + metrics + pixels


def main():
    header = ["// Generated locally by scripts/pulse_fonts.py; excluded from Git.",
              "#pragma once", "#include <cstdint>"]
    for name, size, index in FACES:
        data = vlw(size, index)
        header.append(f"static const uint8_t font_{name}[] = {{")
        header.extend(",".join(str(n) for n in data[i:i + 24]) + "," for i in range(0, len(data), 24))
        header.append("};")
    target = ROOT / "pulse_station/helvetica_generated.h"
    target.write_text("\n".join(header) + "\n")
    target.chmod(0o600)
    print("Generated six Helvetica sizes for the local badge build.")


if __name__ == "__main__":
    main()
