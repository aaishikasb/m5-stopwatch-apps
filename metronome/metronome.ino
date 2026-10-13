#include <M5Unified.h>
#include <esp_timer.h>
#include <atomic>
#include <StopWatchFace.h>
#include "clock.h"

M5Canvas canvas(&M5.Display);
TapTempo taps;
unsigned tempo=120, beats=4, division=1;
bool playing=false, audible=false, haptic=true, settings=false, speakerReady=false, dirty=true;
std::atomic<uint32_t> configuration{0}, visibleTick{0};
std::atomic<bool> audioFailed{false};
uint32_t lastFrame=0, pulseAt=0, lastTick=0;
constexpr Face::Rect minus{103,258,48,48}, plus{315,258,48,48}, tapPad{163,258,140,48};
constexpr Face::Rect playButton{133,398,200,46}, settingsButton{169,318,128,44};

void publish() {
  configuration.store(tempo | (beats<<8) | (division<<12) | (unsigned(playing)<<16)
    | (unsigned(audible)<<17) | (unsigned(haptic)<<18));
  dirty=true;
}
void beatTask(void *) {
  BeatClock clock;
  uint64_t vibrationOff=0;
  bool motor=false, wasSound=false;
  uint32_t sequence=0;
  for(;;) {
    const uint64_t now=esp_timer_get_time();
    const uint32_t config=configuration.load();
    const bool run=config&(1<<16), sound=config&(1<<17), vibrate=config&(1<<18);
    clock.configure(config&255,(config>>8)&15,(config>>12)&15,run,now);
    if(motor && (!run || !vibrate || now>=vibrationOff)) { M5.Power.setVibration(0); motor=false; }
    if(wasSound && (!run || !sound)) M5.Speaker.stop();
    wasSound=run && sound;
    const auto tick=clock.poll(now);
    if(tick.due) {
      visibleTick.store((++sequence<<8) | (tick.beat<<4) | tick.subdivision);
      if(sound && speakerReady && !M5.Speaker.tone(tick.subdivision?900:tick.beat?1500:2100,tick.subdivision?12:22)) audioFailed=true;
      // The motor marks main beats; subdivisions would blur its mechanical pulses.
      if(vibrate && !tick.subdivision) {
        M5.Power.setVibration(tick.beat?130:190); motor=true; vibrationOff=now+35000;
      }
    }
    vTaskDelay(1);
  }
}
void tap() {
  unsigned measured=taps.tap(esp_timer_get_time());
  if(measured>=30 && measured<=240) tempo=measured;
  publish();
}
void touch(int x,int y) {
  if(settings) {
    if(Face::Rect{103,133,260,44}.contains(x,y)) beats=beats%7+1;
    else if(Face::Rect{103,189,260,44}.contains(x,y)) division=division%4+1;
    else if(Face::Rect{103,245,124,48}.contains(x,y) && speakerReady) audible=!audible;
    else if(Face::Rect{239,245,124,48}.contains(x,y)) haptic=!haptic;
    else if(Face::primary.contains(x,y)) settings=false;
  } else {
    if(minus.contains(x,y) && tempo>30) --tempo;
    else if(plus.contains(x,y) && tempo<240) ++tempo;
    else if(tapPad.contains(x,y)) tap();
    else if(playButton.contains(x,y)) playing=!playing;
    else if(settingsButton.contains(x,y)) settings=true;
  }
  publish();
}
void render() {
  using namespace Face;
  canvas.fillScreen(background);
  char value[40];
  if(settings) {
    label(canvas,"metronome",233,78,heading);
    snprintf(value,sizeof value,"Beats / bar: %u",beats); button(canvas,{103,133,260,44},value);
    snprintf(value,sizeof value,"Subdivisions: %u",division); button(canvas,{103,189,260,44},value);
    button(canvas,{103,245,124,48},audible?"Sound on":"Sound off");
    button(canvas,{239,245,124,48},haptic?"Buzz on":"Buzz off");
    label(canvas,!speakerReady||audioFailed.load()?"Audio unavailable":"Visual beat is always on",233,321,caption,secondaryText);
    button(canvas,primary,"Done",true);
  } else {
    const unsigned currentBeat=(lastTick>>4)&15, currentSub=lastTick&15;
    const bool pulse=playing && uint32_t(millis()-pulseAt)<85;
    for(unsigned b=0;b<beats;++b) for(unsigned s=0;s<division;++s) {
      const float angle=-1.5707963f+6.2831853f*(b*division+s)/(beats*division);
      const int x=233+roundf(cosf(angle)*174), y=208+roundf(sinf(angle)*174);
      bool active=pulse && b==currentBeat && s==currentSub;
      if(active) canvas.fillCircle(x,y,s?5:10,beat);
      else canvas.drawCircle(x,y,s?3:8,border);
    }
    label(canvas,"metronome",233,97,body);
    snprintf(value,sizeof value,"%u beats / %u sub",beats,division); label(canvas,value,233,127,caption,secondaryText);
    snprintf(value,sizeof value,"%03u",tempo); label(canvas,value,233,188,bigNumber);
    label(canvas,"BPM",233,247,caption,secondaryText);
    button(canvas,minus,"-"); button(canvas,plus,"+"); button(canvas,tapPad,"Tap tempo");
    button(canvas,playButton,playing?"Stop":"Start",true);
    button(canvas,settingsButton,"Settings");
  }
  canvas.pushSprite(0,0); dirty=false; lastFrame=millis();
}
void setup() {
  Face::begin(canvas);
  speakerReady=M5.Speaker.begin(); M5.Speaker.setVolume(72);
  publish();
  if(xTaskCreatePinnedToCore(beatTask,"metronome",4096,nullptr,3,nullptr,0)!=pdPASS) {
    Face::label(canvas,"Timing task failed",233,233); canvas.pushSprite(0,0);
    while(true) delay(1000);
  }
  render();
}
void loop() {
  M5.update();
  if(M5.BtnA.wasPressed()) tap();
  if(M5.BtnB.wasPressed()) { playing=!playing; publish(); }
  if(M5.Touch.getCount()) { auto point=M5.Touch.getDetail(); if(point.wasPressed()) touch(point.x,point.y); }
  uint32_t tick=visibleTick.load();
  if(tick!=lastTick) { lastTick=tick; pulseAt=millis(); dirty=true; }
  if(uint32_t(millis()-lastFrame)>=33 && (dirty || playing)) render();
  delay(1);
}
