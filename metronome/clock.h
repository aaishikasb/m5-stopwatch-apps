#pragma once
#include <cstdint>

struct BeatClock {
  unsigned bpm=120, beats=4, subdivisions=1;
  bool running=false;
  uint64_t origin=0, next=0;
  struct Tick { bool due=false; unsigned beat=0, subdivision=0; uint64_t index=0; };
  void configure(unsigned tempo, unsigned count, unsigned division, bool play, uint64_t now) {
    if(tempo<30 || tempo>240 || count<1 || count>7 || division<1 || division>4) return;
    if(tempo!=bpm || count!=beats || division!=subdivisions || play!=running) {
      origin=now; next=0;
    }
    bpm=tempo; beats=count; subdivisions=division; running=play;
  }
  Tick poll(uint64_t now) {
    if(!running || now<origin) return {};
    const uint64_t elapsed=now-origin;
    const uint64_t index=elapsed*(bpm*subdivisions)/60000000;
    if(index<next) return {};
    next=index+1;
    return {true,unsigned((index/subdivisions)%beats),unsigned(index%subdivisions),index};
  }
};
struct TapTempo {
  uint64_t last=0;
  uint32_t sum=0;
  unsigned count=0;
  bool started=false;
  unsigned tap(uint64_t now) {
    if(!started || now<=last || now-last>2000000) {
      started=true; last=now; sum=0; count=0; return 0;
    }
    const uint64_t gap=now-last;
    if(gap<250000) return 0;
    last=now;
    if(count==4) { sum=0; count=0; }
    sum+=uint32_t(gap); ++count;
    return unsigned((60000000ULL*count+sum/2)/sum);
  }
};
