# StopWatchFace

Small native M5GFX drawing helpers shared by Reactions, Metronome, and Cat Toy.
The build script passes this directory's parent through Arduino CLI's `--libraries`.
No generated Helvetica files are needed by these apps.
`Theme` lets each app set its own semantic colors and button radius. The default
helpers retain Metronome's black, white, and yellow styling. Reactions and Cat Toy
declare their themes in their own `design.h` files.

## Color, typography, and layout checks

Applied the better-colors, better-typography, and better-layout principles to the
fixed 466×466 circular screen. Browser CSS, responsive breakpoints, webfont loading,
and browser zoom do not apply to this firmware.

Metronome's default palette:

| Role | Value | Use |
| --- | --- | --- |
| Background | `#000000` | AMOLED canvas |
| Surface | `#1A1A1A` | Neutral controls |
| Text | `#FFFFFF` | Primary copy and numeric displays |
| Secondary text | `#B0B0B0` | Hints and secondary results |
| Border | `#707070` | Control edges and inactive beat outlines |
| Action / on-action | `#FFD43B` / `#000000` | One primary filled action per view |

Measured after RGB565 quantization: white/black **21.00:1**, gray/black **10.01:1**,
white/charcoal **17.69:1**, gray/charcoal **8.43:1**, black/yellow **14.98:1**.
`tests/face_test.py` recomputes
these values and checks narrow controls and long labels against M5GFX's actual
Font4 widths.

The other apps use independent palettes:

| App | Background / surface | Text / secondary | Primary action / label | Other roles |
| --- | --- | --- | --- | --- |
| Reactions | `#060D1C` / `#14203C` | `#F2F5FB` / `#B1BED8` | `#FF8577` / `#060D1C` | Mint `#7AE7BA` for connected, GO, and winner states; navy `#6F80A5` control borders |
| Cat Toy | `#FDF4E3` / `#F3E7CE` | `#16261B` / `#3E5746` | `#245337` / `#FDF4E3` | Brown `#995933` mouse, peach `#E9AF90` ears, forest `#5E856B` control borders |

These palettes were generated from fixed-hue OKLCH stops using Culori 4.0.2,
checked in sRGB gamut, and emitted as RGB888 constants. Culori is a design tool,
not a firmware or build dependency. After RGB565 quantization, Reactions text
pairs range from **8.33:1** to **18.45:1**; Cat Toy text pairs range from
**6.74:1** to **15.22:1**. Controls and the mouse silhouette also meet the checked
3:1 graphical contrast floor. The existing face check covers the app palettes.

The type scale uses Font2 at 16 px for captions, Font4 at 26 px for body/buttons,
FreeSansBold18pt7b for headings, and Font6's fixed 27×48 digit cells (doubled for tempo).
Only the provided font faces are used; no synthetic weight or truncated text.
Controls have distinct surfaces, separation, and insets within the circular edge.
Screens use short single-line labels; firmware copy is currently English only.

No actionable color findings in the checked text pairs. No actionable typography
or layout findings in the checked labels and circular control bounds.

Not verified: final rendering on the physical panel, touch accuracy, localization,
and hardware brightness. Source-based screen previews are not device captures.

Approve for the measured source-level checks; physical rendering remains unverified.
