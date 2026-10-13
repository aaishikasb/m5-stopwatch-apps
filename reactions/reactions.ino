#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <StopWatchFace.h>
#include "game.h"
#include "design.h"

using namespace Reactions;
M5Canvas canvas(&M5.Display);
WiFiUDP udp;
Room room;
Trial trial;
Packet snapshot;
IPAddress peers[maxPlayers];
constexpr uint16_t gamePort=42170;
constexpr Face::Rect hostButton{108,202,250,56}, joinButton{108,274,250,56};
constexpr Face::Rect challengeButton{92,260,282,54};
enum class Screen { choose, rooms, pin, connecting, game, problem } screen=Screen::choose;
bool hosting=false, scanning=false, dirty=true, previousDown=false, touchDown=false;
uint8_t slot=0;
uint32_t token=0, lastReceive=0, lastSend=0, connectAt=0, lastFrame=0;
uint32_t sequence=0;
char ssid[33]{}, password[9]{}, pin[7]{}, rooms[4][33]{};
unsigned pinLength=0, roomCount=0;
const char *problem="";

void sendPacket(IPAddress ip, const Packet &packet) {
  if(udp.beginPacket(ip,gamePort)) { udp.write(reinterpret_cast<const uint8_t*>(&packet),sizeof packet); udp.endPacket(); }
}
Packet stateFor(unsigned player) {
  Packet p;
  p.kind=Kind::state; p.session=room.session; p.round=room.round;
  p.sequence=++sequence;
  p.challenge=room.challenge; p.phase=room.phase; p.slot=player;
  p.token=room.players[player].token; p.mask=room.mask; p.value=room.waitMs;
  if(room.phase==Phase::lobby) {
    p.mask=0; for(unsigned i=0;i<maxPlayers;++i) if(room.players[i].present) p.mask|=1<<i;
  }
  for(unsigned i=0;i<maxPlayers;++i) p.scores[i]=room.scores[i];
  return p;
}
void fail(const char *message) { problem=message; screen=Screen::problem; dirty=true; }
void resetNetwork() {
  udp.stop(); WiFi.scanDelete(); WiFi.disconnect(true); WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF); hosting=false; trial=Trial{}; snapshot=Packet{};
  touchDown=false; previousDown=false; scanning=false;
}
void home() { resetNetwork(); screen=Screen::choose; dirty=true; }
void hostRoom() {
  resetNetwork(); hosting=true;
  WiFi.persistent(false); WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  const uint32_t id=esp_random();
  snprintf(ssid,sizeof ssid,"reactions-%04X",unsigned(id&0xffff));
  snprintf(pin,sizeof pin,"%06u",unsigned(esp_random()%1000000));
  snprintf(password,sizeof password,"rx%s",pin);
  if(!WiFi.softAP(ssid,password,1,false,4) || !udp.begin(gamePort)) { fail("Room could not start"); return; }
  room.host(id,millis()); slot=0; screen=Screen::game; dirty=true;
}
void scanRooms() {
  resetNetwork(); WiFi.persistent(false); WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false); screen=Screen::rooms; roomCount=0;
  scanning=WiFi.scanNetworks(true)==WIFI_SCAN_RUNNING;
  if(!scanning) fail("Wi-Fi scan failed");
  dirty=true;
}
void connectRoom() {
  if(pinLength!=6) return;
  snprintf(password,sizeof password,"rx%s",pin);
  token=esp_random(); if(!token) token=1;
  WiFi.setSleep(false); WiFi.begin(ssid,password);
  connectAt=millis(); lastReceive=0; lastSend=0;
  screen=Screen::connecting; dirty=true;
}
void clientSend(Kind kind) {
  Packet p;
  p.kind=kind; p.token=token; p.session=snapshot.session; p.round=snapshot.round; p.slot=slot;
  if(kind==Kind::result) { p.valid=trial.valid; p.value=trial.duration<=10000 ? trial.duration : 10000; }
  sendPacket(WiFi.gatewayIP(),p);
}
void receive() {
  // Bound work per frame so a noisy peer cannot starve button timing.
  for(unsigned n=0;n<8;++n) {
    const int size=udp.parsePacket(); if(!size) break;
    const IPAddress from=udp.remoteIP(); const uint16_t port=udp.remotePort();
    uint8_t bytes[sizeof(Packet)];
    const int got=udp.read(bytes,sizeof bytes); udp.clear();
    Packet p;
    if(port!=gamePort || size!=int(sizeof(Packet)) || got!=size || !decode(bytes,size,p)) continue;
    const uint32_t now=millis();
    if(hosting) {
      if(p.kind==Kind::join) {
        // A token may reconnect only from its original address during this room.
        bool conflict=false;
        for(unsigned i=1;i<maxPlayers;++i)
          if(room.players[i].present && (room.players[i].token==p.token || peers[i]==from)
              && !(room.players[i].token==p.token && peers[i]==from)) conflict=true;
        if(conflict) continue;
        int joined=room.join(p.token,now);
        if(joined<0) { Packet full; full.kind=Kind::full; full.token=p.token; sendPacket(from,full); }
        else { peers[joined]=from; sendPacket(from,stateFor(joined)); dirty=true; }
      } else if(p.slot>0 && room.players[p.slot].present && peers[p.slot]==from
                && room.players[p.slot].token==p.token && p.session==room.session) {
        room.players[p.slot].seenAt=now;
        if(p.kind==Kind::ready) room.ready(p.slot,p.round);
        if(p.kind==Kind::result) room.submit(p.slot,p.round,p.valid,p.value);
      }
    } else if(from==WiFi.gatewayIP() && p.token==token) {
      if(p.kind==Kind::full && !snapshot.session) { fail("Room full or playing"); continue; }
      if(p.kind!=Kind::state || p.slot==0 || !(p.mask&(1<<p.slot))
          || !p.session || (snapshot.session && p.session!=snapshot.session)) continue;
      if(snapshot.session && int32_t(p.sequence-snapshot.sequence)<=0) continue;
      if(p.phase!=snapshot.phase || p.round!=snapshot.round || p.mask!=snapshot.mask
          || p.challenge!=snapshot.challenge || !snapshot.session
          || memcmp(p.scores,snapshot.scores,sizeof p.scores)) dirty=true;
      slot=p.slot; snapshot=p; lastReceive=now; screen=Screen::game;
    }
  }
}
void networkTick(uint32_t now) {
  if(screen==Screen::rooms && scanning) {
    int found=WiFi.scanComplete();
    if(found>=0) {
      for(int i=0;i<found && roomCount<4;++i) {
        String name=WiFi.SSID(i);
        if(name.startsWith("reactions-") && name.length()==14) name.toCharArray(rooms[roomCount++],33);
      }
      WiFi.scanDelete(); scanning=false; dirty=true;
    } else if(found==WIFI_SCAN_FAILED) { scanning=false; fail("Wi-Fi scan failed"); }
  }
  if(screen==Screen::connecting) {
    if(WiFi.status()==WL_CONNECTED) {
      if(!udp.begin(gamePort)) { fail("Network unavailable"); return; }
      screen=Screen::game; lastReceive=now; dirty=true;
    } else if(uint32_t(now-connectAt)>15000) fail("Check PIN and retry");
  }
  if(screen!=Screen::game) return;
  receive();
  if(screen!=Screen::game) return;
  if(hosting) {
    Phase before=room.phase; unsigned count=room.count();
    room.tick(now);
    if(before!=room.phase || count!=room.count()) dirty=true;
    snapshot=stateFor(0);
  } else if(WiFi.status()!=WL_CONNECTED || uint32_t(now-lastReceive)>6000) { fail("Host disconnected"); return; }
  if(uint32_t(now-lastSend)>=100) {
    lastSend=now;
    if(hosting) {
      for(unsigned i=1;i<maxPlayers;++i) if(room.players[i].present) sendPacket(peers[i],stateFor(i));
    } else if(!snapshot.session) clientSend(Kind::join);
    else if(snapshot.phase==Phase::prepare) clientSend(Kind::ready);
    else if(snapshot.phase==Phase::play && trial.canSubmit(snapshot.round)
        && snapshot.scores[slot]==pending) clientSend(Kind::result);
    else clientSend(Kind::heartbeat);
  }
  if(snapshot.phase==Phase::play && snapshot.round!=trial.round) {
    trial.start(snapshot.challenge,now,snapshot.value,snapshot.round); dirty=true;
  }
  if(snapshot.phase==Phase::lobby || snapshot.phase==Phase::prepare) trial=Trial{};
  if(hosting && room.phase==Phase::play && trial.stage==Trial::finished)
    room.submit(0,room.round,trial.valid,trial.duration<=10000 ? trial.duration : 10000);
}
void changeChallenge() {
  if(hosting && room.phase==Phase::lobby) { room.challenge=Challenge((unsigned(room.challenge)+1)%3); dirty=true; }
}
void primaryAction() {
  if(hosting && room.phase==Phase::lobby) { room.start(millis(),2000+esp_random()%3001); dirty=true; }
  else if(hosting && room.phase==Phase::results) { room.lobby(); dirty=true; }
}
void touch(int x,int y) {
  if(screen==Screen::choose) {
    if(hostButton.contains(x,y)) hostRoom();
    else if(joinButton.contains(x,y)) scanRooms();
  } else if(screen==Screen::rooms) {
    for(unsigned i=0;i<roomCount;++i) if(Face::Rect{103,130+int(i)*54,260,44}.contains(x,y)) {
      memcpy(ssid,rooms[i],sizeof ssid); pinLength=0; pin[0]=0; screen=Screen::pin; dirty=true; return;
    }
    if(!scanning && Face::primary.contains(x,y)) scanRooms();
  } else if(screen==Screen::pin) {
    for(int i=0;i<12;++i) if(Face::Rect{121+(i%3)*80,158+(i/3)*56,64,44}.contains(x,y)) {
      if(i==9) { if(pinLength) pin[--pinLength]=0; }
      else if(i==11) connectRoom();
      else if(pinLength<6) { pin[pinLength++]=i==10 ? '0' : '1'+i; pin[pinLength]=0; }
      dirty=true;
    }
  } else if(screen==Screen::problem && Face::primary.contains(x,y)) home();
  else if(screen==Screen::game) {
    if(challengeButton.contains(x,y)) changeChallenge();
    if(Face::primary.contains(x,y)) primaryAction();
  }
}
void render() {
  using namespace Face;
  using namespace ReactionsDesign;
  canvas.fillScreen(ui.background);
  canvas.drawCircle(233,233,220,track);
  if(screen!=Screen::choose) ui.label(canvas,"reactions",233,66,heading);
  char value[40];
  if(screen==Screen::choose) {
    ui.label(canvas,"LOCAL MULTIPLAYER",233,54,caption,ui.secondaryText);
    ui.label(canvas,"reactions",233,100,heading);
    ui.label(canvas,"Find your timing.",233,147,body);
    ui.button(canvas,hostButton,"Host game",true); ui.button(canvas,joinButton,"Join game");
    ui.label(canvas,"2-5 players. One badge each.",233,357,caption,ui.secondaryText);
    ui.label(canvas,"Hold. React. Keep the beat.",233,387,caption,ui.secondaryText);
  } else if(screen==Screen::rooms) {
    ui.label(canvas,scanning ? "Finding rooms..." : roomCount ? "Choose a room" : "No rooms found",233,108,caption,ui.secondaryText);
    for(unsigned i=0;i<roomCount;++i) ui.button(canvas,{103,130+int(i)*54,260,44},rooms[i]+10);
    if(!scanning) ui.button(canvas,primary,"Scan again",true);
    ui.label(canvas,"A: back",233,423,caption,ui.secondaryText);
  } else if(screen==Screen::pin) {
    snprintf(value,sizeof value,"PIN: %s",pin); ui.label(canvas,value,233,125,body);
    for(int i=0;i<12;++i) {
      char key[4]{}; if(i==9) strcpy(key,"Del"); else if(i==11) strcpy(key,"Go"); else { key[0]=i==10?'0':'1'+i; }
      ui.button(canvas,{121+(i%3)*80,158+(i/3)*56,64,44},key,i==11 && pinLength==6);
    }
    ui.label(canvas,"Enter host PIN - A: back",233,414,caption,ui.secondaryText);
  } else if(screen==Screen::connecting) {
    ui.label(canvas,"Connecting",233,210,heading); ui.label(canvas,"A: cancel",233,294,caption,ui.secondaryText);
  } else if(screen==Screen::problem) {
    ui.label(canvas,problem,233,220,body); ui.button(canvas,primary,"Back",true);
  } else if(!snapshot.session) ui.label(canvas,"Joining room...",233,220,body);
  else if(snapshot.phase==Phase::lobby) {
    if(hosting) {
      snprintf(value,sizeof value,"Room %s",ssid+10); ui.label(canvas,value,233,117,body);
      canvas.fillRoundRect(117,139,232,51,10,ui.surface);
      Face::label(canvas,pin,233,163,number,ui.text,ui.surface); ui.label(canvas,"Share this PIN",233,198,caption,ui.secondaryText);
    } else {
      snprintf(value,sizeof value,"Player %u",slot+1); ui.label(canvas,value,233,153,heading);
      ui.label(canvas,"Host chooses the challenge",233,198,caption,ui.secondaryText);
    }
    for(unsigned i=0;i<maxPlayers;++i) {
      snprintf(value,sizeof value,"%u",i+1);
      canvas.fillCircle(137+i*48,231,16,(snapshot.mask&(1<<i))?connected:ui.surface);
      Face::label(canvas,value,137+i*48,231,caption,(snapshot.mask&(1<<i))?onConnected:ui.secondaryText,(snapshot.mask&(1<<i))?connected:ui.surface);
    }
    if(hosting) ui.button(canvas,challengeButton,name(room.challenge));
    else ui.label(canvas,name(snapshot.challenge),233,287,body);
    if(hosting) { ui.label(canvas,"A: challenge   B: start",233,326,caption,ui.secondaryText); ui.button(canvas,primary,room.count()<2?"Need 2 players":"Start round",room.count()>=2); }
    else ui.label(canvas,"Waiting for host",233,376,body,ui.secondaryText);
  } else if(snapshot.phase==Phase::prepare) {
    ui.label(canvas,"Ready?",233,190,heading); ui.label(canvas,"Release all controls",233,250,body);
    ui.label(canvas,"Checking all players",233,307,caption,ui.secondaryText);
  } else if(snapshot.phase==Phase::results) {
    ui.label(canvas,"Round results",233,121,body);
    unsigned row=0;
    for(unsigned place=1;place<=maxPlayers+1;++place) for(unsigned i=0;i<maxPlayers;++i) if(snapshot.mask&(1<<i)) {
      unsigned r=rank(snapshot.scores,snapshot.mask,i);
      if((r?r:maxPlayers+1)!=place) continue;
      const int y=168+row++*34;
      const uint32_t paper=i==slot?ui.surface:ui.background;
      if(i==slot) canvas.fillRoundRect(66,y-16,334,32,8,paper);
      const uint32_t medal=r==1?winner:ui.surface;
      canvas.fillCircle(91,y,13,medal);
      if(r) snprintf(value,sizeof value,"%u",r); else strcpy(value,"-");
      Face::label(canvas,value,91,y,caption,r==1?onWinner:ui.secondaryText,medal);
      snprintf(value,sizeof value,"P%u%s",i+1,i==slot?"*":"");
      Face::label(canvas,value,139,y,body,ui.text,paper);
      if(r) snprintf(value,sizeof value,"%ld ms%s",long(snapshot.scores[i]),snapshot.challenge==Challenge::quick?"":" off");
      else strcpy(value,"No score");
      Face::label(canvas,value,280,y,body,ui.text,paper);
    }
    ui.label(canvas,"* Your badge",233,330,caption,ui.secondaryText);
    if(hosting) ui.button(canvas,primary,"Next round",true);
    else ui.label(canvas,"Waiting for host",233,379,caption,ui.secondaryText);
  } else {
    ui.label(canvas,name(snapshot.challenge),233,128,body);
    const char *title=trial.stage==Trial::waiting?"Wait...":trial.stage==Trial::finished?(trial.valid?"Done":"Too soon / timeout")
      :trial.stage==Trial::measuring?(trial.challenge==Challenge::hold?"Holding":"Tap again"):"GO";
    const bool go=trial.stage==Trial::cuePending || trial.stage==Trial::armed;
    if(go) {
      canvas.fillCircle(233,214,65,cue);
      Face::label(canvas,title,233,214,heading,onCue,cue);
    } else {
      canvas.drawRoundRect(80,171,306,86,18,ui.border);
      ui.label(canvas,title,233,214,trial.stage==Trial::finished?body:heading);
    }
    const char *hint=trial.stage==Trial::waiting?"Do not press yet":trial.stage==Trial::finished?"Waiting for scores"
      :trial.challenge==Challenge::hold?"Hold, then release at 2 seconds":trial.challenge==Challenge::quick?"Press as soon as GO appears":"Two taps, 500 ms apart";
    ui.label(canvas,hint,233,300,caption,ui.secondaryText);
    ui.button(canvas,primary,"B / Touch");
  }
  canvas.pushSprite(0,0); dirty=false; lastFrame=millis();
  if(screen==Screen::game && snapshot.phase==Phase::play && trial.stage==Trial::cuePending) {
    // Measure from the displayed cue, not packet arrival or the start of drawing.
    if(previousDown) trial.press(millis()); else trial.cueShown(millis());
    dirty=true;
  }
}
void setup() { Face::begin(canvas); WiFi.persistent(false); WiFi.mode(WIFI_OFF); render(); }
void loop() {
  M5.update();
  const uint32_t now=millis();
  if(M5.BtnA.wasPressed()) {
    if(screen==Screen::game && hosting && room.phase==Phase::lobby) changeChallenge();
    else if(screen!=Screen::game || snapshot.phase==Phase::lobby || snapshot.phase==Phase::results) home();
  }
  if(M5.BtnB.wasPressed() && screen==Screen::game && (snapshot.phase==Phase::lobby || snapshot.phase==Phase::results)) primaryAction();
  touchDown=false;
  if(M5.Touch.getCount()) {
    const auto point=M5.Touch.getDetail();
    touchDown=point.isPressed() && Face::primary.contains(point.x,point.y);
    if(point.wasPressed()) touch(point.x,point.y);
  }
  const bool down=M5.BtnB.isPressed() || touchDown;
  if(screen==Screen::game && snapshot.phase==Phase::play) {
    auto before=trial.stage;
    if(down && !previousDown) trial.press(now);
    if(!down && previousDown) trial.release(now);
    trial.tick(now);
    if(before!=trial.stage) dirty=true;
  }
  previousDown=down;
  networkTick(millis());
  if(dirty && uint32_t(millis()-lastFrame)>=20) render();
  delay(1);
}
