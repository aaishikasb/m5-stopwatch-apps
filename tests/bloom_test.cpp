#include <cassert>
#include <cstring>
#include <iostream>
#include "interaction.h"
#include "visual.h"
using namespace Bloom;

Event event(Kind kind, const char *id = "0123456789abcdef0123456789abcdef") {
  Event e; e.kind = kind; std::strcpy(e.id, id); return e;
}
int main() {
  Interaction s;
  assert(s.tap(10) == Kind::none);
  assert(s.poll(370) == Kind::none);
  assert(s.poll(371) == Kind::bloom);
  assert(s.poll(372) == Kind::none);
  assert(s.tap(400) == Kind::none);
  assert(s.tap(600) == Kind::bloom); // A double tap without an incoming bloom sends only once.
  assert(s.poll(1000) == Kind::none);
  const auto bloom = event(Kind::bloom);
  assert(s.receive(bloom, 1000));
  assert(!s.receive(bloom, 1200)); // Duplicate delivery must not restart animation or motor.
  assert(s.sceneAt == 1000);
  assert(s.tap(1300) == Kind::none);
  assert(s.tap(1500) == Kind::reply);
  auto reply = event(Kind::reply, "1123456789abcdef0123456789abcdef");
  std::strcpy(reply.replyTo, bloom.id);
  s.sent(reply, 1600);
  assert(!s.canReply(1600));
  assert(s.scene == Kind::bloom && !s.echo); // A send confirmation does not obscure an incoming bloom.
  assert(s.receive(reply, 3000));
  assert(s.scene == Kind::reply && s.animating(12999));
  s.poll(13000);
  assert(!s.animating(13000));
  assert(!s.canReply(13000)); // A reply never creates a reply-to-reply loop.

  Interaction late;
  late.receive(bloom, 500);
  late.poll(11000);
  assert(!late.animating(11000) && late.canReply(11000)); // Reply after clock returns.
  assert(late.canReply(120499));
  assert(!late.canReply(120500));
  late.poll(120500);
  assert(!late.replyTo[0]);

  Interaction wrap;
  wrap.receive(bloom, UINT32_MAX - 100);
  assert(wrap.animating(10) && wrap.canReply(10));
  wrap.tap(UINT32_MAX - 100);
  assert(wrap.tap(50) == Kind::reply);
  wrap.poll(120000);
  assert(!wrap.animating(120000) && !wrap.canReply(120000));
  wrap.show(Kind::bloom, UINT32_MAX - 200, true);
  assert(wrap.animating(1000) && !wrap.animating(1200));

  Interaction crossing;
  crossing.receive(bloom, 100);
  auto newer = event(Kind::bloom, "2123456789abcdef0123456789abcdef");
  crossing.receive(newer, 200);
  crossing.sent(reply, 300); // Confirmation for old reply must preserve newer incoming bloom.
  assert(crossing.canReply(300) && !std::strcmp(crossing.replyTo, newer.id));
  for (uint32_t t = 0; t <= 12000; ++t) {
    assert(haptic(Kind::bloom, t, true) <= 143);
    assert(haptic(Kind::reply, t, false) == 0);
    if (t >= 8400) assert(haptic(Kind::reply, t, true) == 0);
  }
  assert(haptic(Kind::reply, 300, true) && !haptic(Kind::bloom, 300, true));
  int pixels = 0;
  auto bounds = [&](int x, int y, int w, int h, uint32_t) {
    assert(x >= 0 && y >= 0 && x + w <= 466 && y + h <= 466); ++pixels;
  };
  for (uint32_t t = 0; t <= 10000; t += 100) flower(bounds, Kind::reply, t);
  clock(bounds, "23:59", 182, 16);
  assert(pixels > 0);
  std::cout << "Bloom gesture, expiry, rollover, haptic and drawing checks passed\n";
}
