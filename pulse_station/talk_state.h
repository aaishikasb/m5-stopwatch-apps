#pragma once
#include <cstdint>

struct TalkState {
  static constexpr uint32_t sampleRate = 16000;
  static constexpr uint32_t chunk = 1600;
  static constexpr uint32_t capacity = sampleRate * 30;
  enum Phase { idle, warming, recording, processing, ready, error };
  Phase phase = idle;
  uint32_t startedAt = 0, session = 0, heartbeatAt = 0;
  bool connected = false;

  bool online(uint32_t now) const { return connected && uint32_t(now - heartbeatAt) < 7000; }
  bool capturing() const { return phase == warming || phase == recording; }
  bool start(uint32_t now) {
    if (!online(now) || capturing() || phase == processing) return false;
    phase = warming;
    startedAt = now;
    ++session;
    return true;
  }
  bool warmed(uint32_t now) const { return phase == warming && uint32_t(now - startedAt) >= 1200; }
  bool acceptResult(uint32_t id) const { return phase == processing && id == session; }
};
