#include <M5Unified.h>
#include <esp_system.h>
#include <StopWatchFace.h>
#include "mouse.h"
#include "design.h"

M5Canvas canvas(&M5.Display);
MouseGame game;
bool dirty=true;
uint32_t lastFrame=0, catchAt=0;
const char *speeds[]={"Slow","Curious","Fast"};
constexpr Face::Rect speedButton{113,306,240,44}, playButton{133,366,200,50};

void mouse(int x,int y,float angle,int scale=1) {
  using namespace CatDesign;
  const float dx=cosf(angle)*scale, dy=sinf(angle)*scale;
  const int noseX=x+dx*20, noseY=y+dy*20;
  canvas.drawLine(x-dx*18,y-dy*18,x-dx*29-dy*8,y-dy*29+dx*8,mouseTail);
  canvas.drawLine(x-dx*29-dy*8,y-dy*29+dx*8,x-dx*37,y-dy*37,mouseTail);
  canvas.fillCircle(x-dx*37,y-dy*37,scale,mouseTail);
  canvas.fillCircle(x,y,17*scale,mouseBody);
  canvas.fillCircle(noseX,noseY,9*scale,mouseBody);
  for(int side : {-1,1}) {
    const int earX=x+dx*9+side*dy*12, earY=y+dy*9-side*dx*12;
    canvas.fillCircle(earX,earY,8*scale,mouseBody);
    canvas.fillCircle(earX,earY,5*scale,mouseEar);
    canvas.fillCircle(noseX+dx*5+side*dy*4,noseY+dy*5-side*dx*4,2*scale,mouseDetail);
  }
  canvas.fillCircle(noseX+dx*9,noseY+dy*9,3*scale,mouseDetail);
}
void toggle() { if(game.running) game.pause(millis()); else game.start(millis()); dirty=true; }
void render() {
  using namespace Face;
  using namespace CatDesign;
  canvas.fillScreen(ui.background);
  char value[32];
  if(game.running) {
    snprintf(value,sizeof value,"%u catches",game.catches); ui.label(canvas,value,233,55,caption,ui.secondaryText);
    mouse(roundf(game.x),roundf(game.y),game.angle);
    if(catchAt && uint32_t(millis()-catchAt)<200) canvas.drawCircle(game.x,game.y,26,catchRing);
  } else {
    ui.label(canvas,"CAT TOY",233,48,caption,ui.secondaryText);
    ui.label(canvas,"mouse hunt",233,91,heading);
    ui.label(canvas,!game.remaining?"All done":game.remaining==MouseGame::sessionMs?"Catch the little mouse":"Taking a break",233,133,body);
    canvas.fillCircle(233,213,66,illustration);
    mouse(238,213,-0.25f,2);
    if(game.remaining==MouseGame::sessionMs) strcpy(value,"2 minutes of play");
    else snprintf(value,sizeof value,"%u catches / %lus left",game.catches,(unsigned long)((game.remaining+999)/1000));
    ui.label(canvas,value,233,285,caption,ui.secondaryText);
    snprintf(value,sizeof value,"Speed: %s",speeds[game.speed]); ui.button(canvas,speedButton,value);
    ui.button(canvas,playButton,!game.remaining?"Play again":game.remaining==MouseGame::sessionMs?"Start":"Resume",true);
    ui.label(canvas,"A: pause   B: speed",233,435,caption,ui.secondaryText);
  }
  canvas.pushSprite(0,0); lastFrame=millis(); dirty=false;
}
void setup() {
  Face::begin(canvas); game.randomState=esp_random(); if(!game.randomState) game.randomState=1;
  render();
}
void loop() {
  M5.update(); const uint32_t now=millis(); bool wasRunning=game.running;
  game.tick(now); if(wasRunning!=game.running) dirty=true;
  if(M5.BtnA.wasPressed()) toggle();
  if(M5.BtnB.wasPressed()) { game.speed=(game.speed+1)%3; dirty=true; }
  if(M5.Touch.getCount()) {
    auto point=M5.Touch.getDetail();
    if(point.wasPressed()) {
      if(game.running) { if(game.catchAt(point.x,point.y,now)) catchAt=now; }
      else if(speedButton.contains(point.x,point.y)) { game.speed=(game.speed+1)%3; dirty=true; }
      else if(playButton.contains(point.x,point.y)) toggle();
    }
  }
  if(uint32_t(now-lastFrame)>=33 && (dirty || game.running)) render();
  delay(1);
}
