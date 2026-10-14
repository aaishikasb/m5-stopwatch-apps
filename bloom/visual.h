#pragma once
#include <cmath>
#include <cstdint>
#include "interaction.h"

namespace Bloom {
constexpr uint32_t black = 0x000000, pink = 0xFF78B5, rose = 0xFFD2E6;
constexpr uint32_t violet = 0x9564EF, white = 0xF7EBF4, muted = 0xBCAEBB;
constexpr float pi = 3.14159265f;
constexpr int flowerCenterY = 201;
inline float clamp(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline uint32_t dim(uint32_t color, float strength) {
  strength = clamp(strength);
  return (uint32_t(((color >> 16) & 255) * strength) << 16)
    | (uint32_t(((color >> 8) & 255) * strength) << 8)
    | uint32_t((color & 255) * strength);
}

// Shared by the firmware and source preview; no display or Arduino dependency.
// Six overlapping circles, sampled on a coarse grid, create breathing pixel petals.
template<class Rect> void petals(Rect rect, float radius, float rotation, uint32_t color, float alpha) {
  if (alpha <= 0 || radius < 1) return;
  const float petal = radius * 0.57f, offset = radius * 0.48f;
  float centersX[6], centersY[6];
  for (int p = 0; p < 6; ++p) {
    const float a = rotation + p * pi / 3;
    centersX[p] = std::cos(a) * offset; centersY[p] = std::sin(a) * offset;
  }
  const int extent = static_cast<int>(std::ceil(radius * 1.05f / 6));
  const float petalSquared = petal * petal;
  for (int gy = -extent; gy <= extent; ++gy) for (int gx = -extent; gx <= extent; ++gx) {
    float x = gx * 6, y = gy * 6, intensity = 0;
    for (int p = 0; p < 6; ++p) {
      const float dx = x - centersX[p], dy = y - centersY[p];
      const float squared = dx * dx + dy * dy;
      if (squared < petalSquared) intensity += 0.24f + (1 - std::sqrt(squared / petalSquared)) * 0.37f;
    }
    if (intensity > 0) rect(230 + gx * 6, flowerCenterY - 3 + gy * 6, 6, 6, dim(color, alpha * clamp(intensity)));
  }
}
template<class Rect> void flower(Rect rect, Kind kind, uint32_t elapsed, bool echo = false, bool still = false) {
  const float t = elapsed / 1000.0f;
  const float fade = echo ? clamp(1 - t / 1.4f) * 0.35f : clamp(t / 0.8f) * clamp((10 - t) / 1.8f);
  const float breath = still ? 0.8f : (1 - std::cos(t * 2 * pi / 2.8f)) * 0.5f;
  const float radius = (echo ? 55 : 96 + 48 * breath) * (still ? 1 : 0.65f + 0.35f * clamp((10 - t) / 1.7f));
  const float rotation = still ? 0 : t * 0.12f;
  if (kind == Kind::reply) petals(rect, radius * 1.26f, -rotation + pi / 6, violet, fade);
  petals(rect, radius, rotation, pink, fade);
  rect(227, flowerCenterY - 6, 12, 12, dim(rose, fade));
}

// Pixel clock stays legible without depending on an extra font asset.
template<class Rect> void clock(Rect rect, const char *time, int top = 170, int unit = 9, float alpha = 1) {
  constexpr uint16_t glyphs[] = {0x7B6F,0x2492,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF};
  int width = 0;
  for (const char *p = time; *p; ++p) width += (*p == ':' ? 2 : 4) * unit;
  int x = (466 - width + unit) / 2;
  for (const char *p = time; *p; ++p) {
    if (*p == ':') {
      rect(x, top + unit, unit - 1, unit - 1, dim(muted, alpha));
      rect(x, top + 3 * unit, unit - 1, unit - 1, dim(muted, alpha)); x += 2 * unit;
    } else {
      const uint16_t bits = *p >= '0' && *p <= '9' ? glyphs[*p - '0'] : 0x01C0;
      for (int row = 0; row < 5; ++row) for (int col = 0; col < 3; ++col)
        if (bits & (1 << (14 - row * 3 - col))) rect(x + col * unit, top + row * unit, unit - 1, unit - 1, dim(white, alpha));
      x += 4 * unit;
    }
  }
}
inline uint8_t haptic(Kind kind, uint32_t elapsed, bool enabled) {
  if (!enabled || kind == Kind::none || elapsed >= 8400) return 0;
  const uint32_t phase = elapsed % 2800;
  if (phase < 100) return 110 + phase / 3;
  if (phase < 180) return 140 - (phase - 100) / 2;
  if (kind == Kind::reply && phase >= 280 && phase < 370) return 125;
  return 0;
}
}
