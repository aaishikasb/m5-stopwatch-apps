#pragma once
#include <StopWatchFace.h>

namespace ReactionsDesign {
// Fixed-hue OKLCH stops converted to sRGB with Culori; stored as native RGB888.
namespace Palette {
constexpr uint32_t navy950=0x060D1C, navy850=0x14203C, navy800=0x25324F;
constexpr uint32_t navy500=0x6F80A5, navy200=0xB1BED8, navy50=0xF2F5FB;
constexpr uint32_t coral300=0xFF8577, mint200=0x7AE7BA;
}
constexpr Face::Theme ui{Palette::navy950, Palette::navy850, Palette::navy50,
  Palette::navy200, Palette::navy500, Palette::coral300, Palette::navy950, 10};
constexpr uint32_t track=Palette::navy800;
// Mint always marks a positive state; coral is reserved for the primary action.
constexpr uint32_t connected=Palette::mint200, onConnected=Palette::navy950;
constexpr uint32_t cue=Palette::mint200, onCue=Palette::navy950;
constexpr uint32_t winner=Palette::mint200, onWinner=Palette::navy950;
}
