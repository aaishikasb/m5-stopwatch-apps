#pragma once
#include <cstdint>
#include <cstring>

namespace Reactions {
constexpr unsigned maxPlayers = 5;
constexpr int32_t pending = -2, disqualified = -1;
enum class Challenge : uint8_t { hold, quick, rhythm };
enum class Phase : uint8_t { lobby, prepare, play, results };
enum class Kind : uint8_t { join, heartbeat, ready, result, state, full };
inline const char *name(Challenge c) {
  return c == Challenge::hold ? "Hold 2 seconds" : c == Challenge::quick ? "Quick draw" : "Match 500 ms";
}
inline int32_t score(Challenge c, uint32_t value) {
  if (value > 10000) return disqualified;
  if (c == Challenge::quick) return value <= 5000 ? int32_t(value) : disqualified;
  const int32_t target = c == Challenge::hold ? 2000 : 500;
  return value > uint32_t(target) ? int32_t(value)-target : target-int32_t(value);
}
inline unsigned rank(const int32_t *scores, uint8_t mask, unsigned who) {
  if (who >= maxPlayers || !(mask & (1<<who)) || scores[who] < 0) return 0;
  unsigned place = 1;
  for (unsigned i=0; i<maxPlayers; ++i)
    if ((mask & (1<<i)) && scores[i] >= 0 && scores[i] < scores[who]) ++place;
  return place;
}

struct Trial {
  enum Stage { idle, waiting, cuePending, armed, measuring, finished } stage = idle;
  Challenge challenge = Challenge::hold;
  uint32_t startedAt = 0, measuredAt = 0, wait = 0, duration = 0, round = 0;
  bool valid = false;
  void start(Challenge c, uint32_t now, uint32_t waitMs, uint32_t id) {
    challenge=c; startedAt=now; wait=waitMs; stage=waiting; valid=false; duration=0; round=id;
  }
  bool tick(uint32_t now) {
    if (stage == waiting && uint32_t(now-startedAt) >= wait) { stage=cuePending; return true; }
    if (stage != idle && stage != finished && uint32_t(now-startedAt) >= wait+12000) {
      stage=finished; valid=false;
    }
    return false;
  }
  void cueShown(uint32_t now) { if (stage == cuePending) { stage=armed; measuredAt=now; } }
  void press(uint32_t now) {
    if (stage == waiting || stage == cuePending) { stage=finished; valid=false; }
    else if (stage == armed) {
      if (challenge == Challenge::quick) finish(now-measuredAt);
      else { stage=measuring; measuredAt=now; }
    } else if (stage == measuring && challenge == Challenge::rhythm) finish(now-measuredAt);
  }
  void release(uint32_t now) {
    if (stage == measuring && challenge == Challenge::hold) finish(now-measuredAt);
  }
  void finish(uint32_t ms) { duration=ms; valid=score(challenge, ms)>=0; stage=finished; }
  bool canSubmit(uint32_t id) const { return stage==finished && round==id; }
};

// All peers are ESP32 badges; the versioned wire format is little-endian.
#pragma pack(push, 1)
struct Packet {
  uint32_t magic = 0x52454143;
  uint8_t version = 1;
  Kind kind = Kind::heartbeat;
  Challenge challenge = Challenge::hold;
  Phase phase = Phase::lobby;
  uint32_t session = 0, round = 0, token = 0, sequence = 0;
  uint8_t slot = 0, mask = 0, valid = 0, reserved = 0;
  uint32_t value = 0;
  int32_t scores[maxPlayers] = {pending,pending,pending,pending,pending};
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 52, "Protocol layout changed");
inline bool decode(const void *data, unsigned size, Packet &out) {
  if (size != sizeof(Packet)) return false;
  Packet p;
  std::memcpy(&p, data, sizeof p);
  if (p.magic != 0x52454143 || p.version != 1 || unsigned(p.kind)>unsigned(Kind::full)
      || unsigned(p.challenge)>2 || unsigned(p.phase)>3 || p.slot>=maxPlayers
      || (p.mask & ~31) || p.valid>1 || p.reserved || p.value>10000) return false;
  for (int32_t s : p.scores) if (s < pending || s > 10000) return false;
  out=p;
  return true;
}
struct Player {
  uint32_t token=0, seenAt=0;
  bool present=false, ready=false;
};
struct Room {
  Phase phase=Phase::lobby;
  Challenge challenge=Challenge::hold;
  uint32_t session=1, round=0, phaseAt=0, waitMs=0;
  Player players[maxPlayers]{};
  int32_t scores[maxPlayers]={pending,pending,pending,pending,pending};
  uint8_t mask=0;
  void host(uint32_t id, uint32_t now) {
    *this=Room{}; session=id ? id : 1;
    players[0]={1,now,true,true};
  }
  unsigned count() const {
    unsigned n=0; for (auto p:players) if(p.present) ++n; return n;
  }
  int join(uint32_t token, uint32_t now) {
    if (!token) return -1;
    for (unsigned i=1;i<maxPlayers;++i)
      if (players[i].present && players[i].token==token) { players[i].seenAt=now; return i; }
    if (phase!=Phase::lobby) return -1;
    for (unsigned i=1;i<maxPlayers;++i) if(!players[i].present) {
      players[i]={token,now,true,false}; return i;
    }
    return -1;
  }
  bool start(uint32_t now, uint32_t delay) {
    if (phase!=Phase::lobby || count()<2 || delay<2000 || delay>5000) return false;
    ++round; phase=Phase::prepare; phaseAt=now; waitMs=delay; mask=0;
    for(unsigned i=0;i<maxPlayers;++i) {
      scores[i]=pending; players[i].ready=i==0;
      if(players[i].present) mask |= 1<<i;
    }
    return true;
  }
  bool ready(unsigned slot, uint32_t r) {
    if(phase!=Phase::prepare || r!=round || slot>=maxPlayers || !(mask&(1<<slot))) return false;
    players[slot].ready=true; return true;
  }
  bool submit(unsigned slot, uint32_t r, bool valid, uint32_t value) {
    if(phase!=Phase::play || r!=round || slot>=maxPlayers || !(mask&(1<<slot))
        || scores[slot]!=pending || value>10000) return false;
    scores[slot]=valid ? score(challenge,value) : disqualified;
    return true;
  }
  void lobby() { phase=Phase::lobby; mask=0; }
  void tick(uint32_t now) {
    for(unsigned i=1;i<maxPlayers;++i) if(players[i].present && uint32_t(now-players[i].seenAt)>5000) {
      if(phase==Phase::lobby) players[i]=Player{};
      else if(phase==Phase::play && scores[i]==pending) scores[i]=disqualified;
    }
    if(phase==Phase::prepare) {
      bool all=true;
      for(unsigned i=0;i<maxPlayers;++i) if((mask&(1<<i)) && !players[i].ready) all=false;
      if(all) { phase=Phase::play; phaseAt=now; }
      else if(uint32_t(now-phaseAt)>4000) lobby();
    }
    if(phase==Phase::play) {
      bool all=true;
      for(unsigned i=0;i<maxPlayers;++i) if(mask&(1<<i)) {
        if(scores[i]==pending && uint32_t(now-phaseAt)>waitMs+14000) scores[i]=disqualified;
        if(scores[i]==pending) all=false;
      }
      if(all) { phase=Phase::results; phaseAt=now; }
    }
  }
};
}
