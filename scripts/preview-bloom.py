#!/usr/bin/env python3
"""Source preview: exact firmware petals and pixel clock, illustrative UI labels."""
from io import BytesIO
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build" / "bloom-preview"
OUTPUT.parent.mkdir(exist_ok=True)
subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT / "bloom"),
                str(ROOT / "scripts" / "bloom-preview.cpp"), "-o", str(OUTPUT)], check=True)
font_path = "/System/Library/Fonts/Helvetica.ttc"


def font(size):
    return ImageFont.truetype(font_path, size) if Path(font_path).exists() else ImageFont.load_default(size=size)


def face(scene, elapsed=1400, *, echo=False, reply_ready=None):
    ppm = subprocess.check_output([str(OUTPUT), str(scene), str(elapsed), str(int(echo))])
    face_image = Image.open(BytesIO(ppm)).convert("RGB")
    draw = ImageDraw.Draw(face_image)
    full = scene and not echo and elapsed < 10000
    can_reply = scene == 1 if reply_ready is None else reply_ready
    if not full:
        draw.text((233, 286), "Tue, 13 Oct", font=font(16), fill="#BCAEBB", anchor="mm")
        draw.text((233, 397), "B: quiet mode", font=font(16), fill="#BCAEBB", anchor="mm")
    draw.text((233, 414 if full else 349), "Double tap to reply" if can_reply else "Tap to send a bloom", font=font(16), fill="#F7EBF4", anchor="mm")
    mask = Image.new("L", (466, 466))
    ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
    tile = Image.new("RGB", (506, 506), "#EEE8F1")
    ImageDraw.Draw(tile).ellipse((14, 14, 491, 491), fill="#322A36")
    tile.paste(face_image, (20, 20), mask)
    return tile


preview = Image.new("RGB", (1578, 644), "#EEE8F1")
draw = ImageDraw.Draw(preview)
for i, title in enumerate(("A quiet clock", "Your touch, in pink", "Their reply, in violet")):
    left = 20 + i * 516
    draw.text((left + 253, 53), title, font=font(24), fill="#36263B", anchor="mm")
    preview.paste(face(i), (left, 90))
draw.text((789, 617), "Source-rendered concept. Device rendering and haptics still need a hardware check.",
          font=font(16), fill="#69576D", anchor="mm")
preview.save(ROOT / "bloom" / "preview.png")

# Show both ends of one exchange, using the firmware's original animation timing.
idle = face(0).resize((380, 380), Image.Resampling.LANCZOS)
exchange = []
for elapsed in range(0, 20000, 100):
    if 1500 <= elapsed < 2900:
        left = face(1, elapsed - 1500, echo=True, reply_ready=False)
    elif 7500 <= elapsed < 17500:
        left = face(2, elapsed - 7500)
    else:
        left = None
    right = face(1, elapsed - 1800, reply_ready=elapsed < 7500) if 1800 <= elapsed < 11800 else None
    frame = Image.new("RGB", (820, 500), "#EEE8F1")
    draw = ImageDraw.Draw(frame)
    draw.text((210, 36), "Your watch", font=font(21), fill="#36263B", anchor="mm")
    draw.text((610, 36), "Their watch", font=font(21), fill="#36263B", anchor="mm")
    frame.paste(idle if left is None else left.resize((380, 380), Image.Resampling.LANCZOS), (20, 60))
    frame.paste(idle if right is None else right.resize((380, 380), Image.Resampling.LANCZOS), (420, 60))
    caption = ("Tap your watch" if elapsed < 1800 else
               "A pink bloom arrives on their watch" if elapsed < 6500 else
               "They double tap to reply" if elapsed < 7500 else
               "Violet petals bloom behind pink on your watch" if elapsed < 14500 else
               "The petals fade back into the clock" if elapsed < 17500 else
               "Both clocks are ready for another touch")
    draw.text((410, 464), caption, font=font(20), fill="#36263B", anchor="mm")
    exchange.append(frame)

# A shared palette prevents color flicker as the two blooms brighten and fade.
palette_source = Image.new("RGB", (820, 1500))
for index, sample in enumerate((exchange[0], exchange[32], exchange[89])):
    palette_source.paste(sample, (0, index * 500))
palette = palette_source.quantize(colors=256)
exchange = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in exchange]
exchange[0].save(ROOT / "bloom" / "exchange.gif", save_all=True, append_images=exchange[1:],
                 duration=100, loop=0, optimize=False, disposal=1)
print("Created bloom/preview.png and bloom/exchange.gif")
