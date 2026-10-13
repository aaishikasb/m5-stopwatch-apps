#pragma once
#include <cstdint>

struct FocusTimer {
  static constexpr uint32_t duration = 25 * 60 * 1000;
  uint32_t remaining = duration;
  uint32_t updatedAt = 0;
  bool running = false;

  bool tick(uint32_t now) {
    if (!running) return false;
    const uint32_t elapsed = now - updatedAt;
    updatedAt = now;
    if (elapsed >= remaining) {
      remaining = 0;
      running = false;
      return true;
    }
    remaining -= elapsed;
    return false;
  }

  void toggle(uint32_t now) {
    if (running) {
      tick(now);
      running = false;
    } else {
      if (!remaining) remaining = duration;
      updatedAt = now;
      running = true;
    }
  }

  void reset() { remaining = duration; running = false; }
  uint32_t seconds() const { return (remaining + 999) / 1000; }
};
