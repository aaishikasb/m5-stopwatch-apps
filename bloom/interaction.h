#pragma once
#include <cstdint>
#include <cstring>

namespace Bloom {
enum class Kind : uint8_t { none, bloom, reply };
struct Event {
  Kind kind = Kind::none;
  char id[33] = {};
  char replyTo[33] = {};
};

// All intervals use unsigned subtraction so millis() rollover is harmless.
struct Interaction {
  static constexpr uint32_t doubleTapMs = 360, replyMs = 120000, animationMs = 10000;
  Kind scene = Kind::none;
  uint32_t sceneAt = 0, receivedAt = 0, tapAt = 0;
  char replyTo[33] = {}, lastReceived[33] = {};
  bool firstTap = false, echo = false;

  bool canReply(uint32_t now) const {
    return replyTo[0] && uint32_t(now - receivedAt) < replyMs;
  }
  bool animating(uint32_t now) const {
    return scene != Kind::none && uint32_t(now - sceneAt) < (echo ? 1400u : animationMs);
  }
  void show(Kind kind, uint32_t now, bool localEcho = false) {
    scene = kind; sceneAt = now; echo = localEcho;
  }
  bool receive(const Event &event, uint32_t now) {
    if (event.kind == Kind::none || !event.id[0] || !std::strcmp(lastReceived, event.id)) return false;
    std::strcpy(lastReceived, event.id);
    if (event.kind == Kind::bloom) {
      std::strcpy(replyTo, event.id); receivedAt = now;
    }
    show(event.kind, now);
    return true;
  }
  Kind tap(uint32_t now) {
    if (firstTap && uint32_t(now - tapAt) <= doubleTapMs) {
      firstTap = false;
      return canReply(now) ? Kind::reply : Kind::bloom;
    }
    firstTap = true; tapAt = now;
    return Kind::none;
  }
  Kind poll(uint32_t now) {
    // Retire expired reply IDs; they must not become valid after a full clock wrap.
    if (replyTo[0] && uint32_t(now - receivedAt) >= replyMs) replyTo[0] = 0;
    if (scene != Kind::none && !animating(now)) scene = Kind::none;
    if (!firstTap || uint32_t(now - tapAt) <= doubleTapMs) return Kind::none;
    firstTap = false;
    return Kind::bloom;
  }
  void sent(const Event &event, uint32_t now) {
    if (event.kind == Kind::reply && !std::strcmp(replyTo, event.replyTo)) replyTo[0] = 0;
    // Do not interrupt a received bloom just to confirm a send.
    if (!animating(now) || echo) show(event.kind, now, true);
  }
};
}
