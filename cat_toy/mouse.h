#pragma once
#include <cmath>
#include <cstdint>

struct MouseGame {
  static constexpr uint32_t sessionMs=120000;
  float x=233, y=233, angle=0;
  unsigned speed=1, catches=0;
  uint32_t remaining=sessionMs, last=0, turnAt=0, randomState=1;
  bool running=false;
  uint32_t random() { randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5; return randomState; }
  void start(uint32_t now) {
    if(!remaining) { remaining=sessionMs; catches=0; x=233; y=233; }
    running=true; last=now; turnAt=now;
  }
  void pause(uint32_t now) { tick(now); running=false; }
  void tick(uint32_t now) {
    if(!running) return;
    uint32_t elapsed=now-last; last=now;
    if(elapsed>=remaining) { remaining=0; running=false; return; }
    remaining-=elapsed;
    if(uint32_t(now-turnAt)>=1400) { turnAt=now; angle=float(random()%6283)/1000; }
    // Pause briefly before each dart; cap motion after a delayed frame.
    if(uint32_t(now-turnAt)<220) return;
    float distance=float(elapsed>50 ? 50 : elapsed)*(50+speed*45)/1000;
    x+=std::cos(angle)*distance; y+=std::sin(angle)*distance;
    float dx=x-233, dy=y-233, length=std::sqrt(dx*dx+dy*dy);
    if(length>165) {
      x=233+dx*165/length; y=233+dy*165/length;
      angle=std::atan2(-dy,-dx)+float(int(random()%100)-50)/100;
    }
  }
  bool catchAt(int px,int py,uint32_t now) {
    if(!running) return false;
    float dx=px-x,dy=py-y;
    if(dx*dx+dy*dy>34*34) return false;
    ++catches;
    angle=float(random()%6283)/1000;
    x=233+std::cos(angle)*130; y=233+std::sin(angle)*130;
    turnAt=now;
    return true;
  }
};
