#pragma once
#include <M5Unified.h>

namespace Face {
namespace Palette {
constexpr uint32_t black = 0x000000, charcoal = 0x1A1A1A, gray = 0xB0B0B0;
constexpr uint32_t white = 0xFFFFFF, yellow = 0xFFD43B, midGray = 0x707070;
}
constexpr uint32_t background = Palette::black, surface = Palette::charcoal;
constexpr uint32_t text = Palette::white, secondaryText = Palette::gray;
constexpr uint32_t border = Palette::midGray, action = Palette::yellow;
constexpr uint32_t onAction = Palette::black, beat = Palette::white;
enum Type { caption, body, heading, number, bigNumber };

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x+w && py >= y && py < y+h; }
};
constexpr Rect primary{133, 350, 200, 52};

inline void label(M5Canvas &c, const char *value, int x, int y, Type type = body,
                  uint32_t ink = text, uint32_t paper = background) {
  c.setFont(type == caption ? &fonts::Font2 : type == body ? &fonts::Font4
    : type == heading ? static_cast<const lgfx::IFont *>(&fonts::FreeSansBold18pt7b)
    : static_cast<const lgfx::IFont *>(&fonts::Font6));
  c.setTextSize(type == bigNumber ? 2 : 1);
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(ink, paper);
  c.drawString(value, x, y);
}
struct Theme {
  uint32_t background, surface, text, secondaryText, border, action, onAction;
  uint8_t cornerRadius;
  void label(M5Canvas &c, const char *value, int x, int y, Type type = body) const {
    Face::label(c, value, x, y, type, text, background);
  }
  void label(M5Canvas &c, const char *value, int x, int y, Type type, uint32_t ink) const {
    Face::label(c, value, x, y, type, ink, background);
  }
  void button(M5Canvas &c, Rect r, const char *value, bool filled = false) const {
    c.fillRoundRect(r.x, r.y, r.w, r.h, cornerRadius, filled ? action : surface);
    if (!filled) c.drawRoundRect(r.x, r.y, r.w, r.h, cornerRadius, border);
    Face::label(c, value, r.x+r.w/2, r.y+r.h/2, body, filled ? onAction : text, filled ? action : surface);
  }
};
// The default preserves metronome's monochrome face and yellow primary action.
constexpr Theme monochrome{background, surface, text, secondaryText, border, action, onAction, 12};
inline void button(M5Canvas &c, Rect r, const char *value, bool filled = false) {
  monochrome.button(c, r, value, filled);
}
inline void begin(M5Canvas &c) {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.internal_mic = false;
  M5.begin(cfg);
  M5.Mic.end();
  M5.Speaker.end();
  M5.Power.setVibration(0);
  M5.Display.setBrightness(140);
  c.setPsram(true);
  c.setColorDepth(16);
  if (!c.createSprite(466,466)) {
    M5.Display.fillScreen(background);
    M5.Display.setTextColor(text, background);
    M5.Display.drawString("Display memory error", 100, 220);
    while (true) delay(1000);
  }
}
}
