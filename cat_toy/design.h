#pragma once
#include <StopWatchFace.h>

namespace CatDesign {
// Fixed-hue OKLCH stops converted to sRGB with Culori; stored as native RGB888.
namespace Palette {
constexpr uint32_t linen50=0xFDF4E3, linen100=0xF3E7CE, linen200=0xE5D6B6;
constexpr uint32_t forest950=0x16261B, forest700=0x245337, forest600=0x3E5746, forest400=0x5E856B;
constexpr uint32_t brown600=0x995933, brown500=0xA26745, brown200=0xE9AF90, brown950=0x140B06;
}
constexpr Face::Theme ui{Palette::linen50, Palette::linen100, Palette::forest950,
  Palette::forest600, Palette::forest400, Palette::forest700, Palette::linen50, 22};
constexpr uint32_t illustration=Palette::linen200;
constexpr uint32_t mouseBody=Palette::brown600, mouseTail=Palette::brown500;
constexpr uint32_t mouseEar=Palette::brown200, mouseDetail=Palette::brown950;
constexpr uint32_t catchRing=Palette::brown500;
}
