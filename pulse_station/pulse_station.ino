#include <M5Unified.h>
#include <esp_ota_ops.h>
#include <atomic>
#include "focus_timer.h"
#include "talk_state.h"
#include "helvetica_generated.h"

constexpr uint32_t C_WHITE = 0xFFFFFF, C_BLACK = 0x000000;
constexpr uint32_t C_BLUE = 0x0062CF, C_ORANGE = 0xEB6800;
constexpr uint32_t C_GREEN = 0x009952, C_YELLOW = 0xF6BC26, C_GRAY = 0x666666;
constexpr const char *MODE_NAMES[] = {"Agents", "Talk", "Focus"};
constexpr const char *ROUTES[] = {"A", "T", "F"};
constexpr uint32_t ROUTE_COLORS[] = {C_BLUE, C_ORANGE, C_GREEN};
M5Canvas canvas(&M5.Display);
FocusTimer focus;
TalkState talk;
int16_t *audioBuffer = nullptr;
std::atomic<uint32_t> completedSamples{0};
uint32_t queuedSamples = 0;
bool touchTalking = false, focusAlertPending = false;
const char *talkMessage = "Hold to talk";
int mode = 2, battery = -1;
uint32_t lastFrame = 0, lastBattery = 0, lastStatus = 0;
uint32_t vibrationAt = 0;
bool vibrating = false, dirty = true;
const uint8_t *activeFont = nullptr;
char command[64];
size_t commandLength = 0;
bool discardCommand = false;

void text(const char *value, int x, int y, const uint8_t *font,
          uint32_t foreground, uint32_t background,
          lgfx::textdatum_t align = lgfx::textdatum_t::middle_left) {
  if (activeFont != font) {
    if (!canvas.loadFont(font)) {
      Serial.println("PULSE_ERROR font_load_failed");
      canvas.setFont(&fonts::Font2);
    }
    activeFont = font;
  }
  canvas.setTextColor(foreground, background);
  canvas.setTextDatum(align);
  canvas.drawString(value, x, y);
}

void centered(const char *value, int x, int y, const uint8_t *font,
              uint32_t foreground = C_BLACK, uint32_t background = C_WHITE) {
  text(value, x, y, font, foreground, background, lgfx::textdatum_t::middle_center);
}

void buzz() {
  M5.Power.setVibration(150);
  vibrationAt = millis();
  vibrating = true;
}

void drawBattery() {
  char value[8];
  if (battery < 0) snprintf(value, sizeof value, "--%%");
  else snprintf(value, sizeof value, "%d%%", battery);
  canvas.drawRect(189, 438, 22, 12, C_BLACK);
  canvas.fillRect(211, 441, 3, 6, C_BLACK);
  if (battery > 0) canvas.fillRect(191, 440, max(1, battery * 18 / 100), 8, C_BLACK);
  text(value, 222, 444, font_body, C_BLACK, C_WHITE);
}

void drawNavigation() {
  for (int i = 0; i < 3; ++i) {
    const int x = 133 + i * 100;
    if (i == mode) {
      canvas.fillCircle(x, 390, 27, C_BLACK);
      canvas.fillCircle(x, 390, 25, C_WHITE);
    }
    canvas.fillCircle(x, 390, 19, ROUTE_COLORS[i]);
    centered(ROUTES[i], x, 390, font_route, i == 1 ? C_BLACK : C_WHITE, ROUTE_COLORS[i]);
  }
}

void drawFocus() {
  canvas.fillRect(0, 154, 466, 120, C_YELLOW);
  char time[8];
  const unsigned seconds = focus.seconds();
  snprintf(time, sizeof time, "%02u:%02u", seconds / 60, seconds % 60);
  centered(time, 233, 208, font_timer, C_BLACK, C_YELLOW);
  centered(focus.remaining ? "time remaining" : "Time for a break", 233, 254, font_body, C_BLACK, C_YELLOW);
  canvas.fillRect(88, 289, 182, 52, C_BLACK);
  const char *action = focus.running ? "Pause" : !focus.remaining ? "Start again"
                       : focus.remaining < FocusTimer::duration ? "Resume" : "Start focus";
  centered(action, 179, 315, font_label, C_WHITE, C_BLACK);
  const uint32_t resetColor = !focus.running && focus.remaining == FocusTimer::duration ? 0x999999 : C_BLACK;
  canvas.drawRect(280, 289, 98, 52, resetColor);
  centered("Reset", 329, 315, font_label, resetColor);
}

void drawAgents() {
  canvas.fillRect(0, 154, 466, 120, C_YELLOW);
  canvas.fillCircle(67, 216, 22, C_BLACK);
  canvas.fillCircle(67, 216, 20, C_YELLOW);
  centered("A", 67, 216, font_route, C_BLACK, C_YELLOW);
  canvas.fillRect(105, 169, 87, 25, C_BLACK);
  text("OFFLINE", 112, 182, font_label, C_YELLOW, C_BLACK);
  text("Connect your Mac", 105, 215, font_heading, C_BLACK, C_YELLOW);
  text("To see your coding agents", 105, 249, font_body, C_BLACK, C_YELLOW);
  centered("Focus works without your Mac", 233, 311, font_body, C_GRAY);
}

void drawTalk() {
  const bool online = talk.online(millis());
  const uint32_t background = talk.phase == TalkState::recording ? uint32_t(0xD82233) : C_YELLOW;
  const uint32_t foreground = talk.phase == TalkState::recording ? C_WHITE : C_BLACK;
  canvas.fillRect(0, 154, 466, 120, background);
  canvas.drawRoundRect(224, 170, 18, 32, 9, foreground);
  canvas.drawArc(233, 194, 18, 16, 0, 180, foreground);
  canvas.fillRect(232, 211, 2, 8, foreground);
  canvas.fillRect(225, 219, 16, 2, foreground);
  centered(online ? talkMessage : "Connect USB", 233, 247, font_heading, foreground, background);
  const char *hint = !online ? "Run Pulse Talk on your Mac"
    : talk.phase == TalkState::ready ? "Cmd+V in your Codex chat"
    : talk.phase == TalkState::warming ? "Keep holding; wait for red"
    : talk.phase == TalkState::recording ? "Release to transcribe"
    : talk.phase == TalkState::processing ? "Transcribing on your Mac"
    : "Hold here or the blue B button";
  centered(hint, 233, 303, font_body, C_GRAY);
  char detail[40];
  if (talk.phase == TalkState::recording) {
    snprintf(detail, sizeof detail, "%lus / 30s", (unsigned long)(completedSamples.load() / TalkState::sampleRate));
  } else snprintf(detail, sizeof detail, "Draft only - never auto-sends");
  centered(detail, 233, 332, font_body, C_GRAY);
}

void render() {
  canvas.fillScreen(C_WHITE);
  canvas.fillRect(0, 0, 466, 154, C_BLACK);
  canvas.fillCircle(110, 82, 24, ROUTE_COLORS[mode]);
  centered(ROUTES[mode], 110, 82, font_route, mode == 1 ? C_BLACK : C_WHITE, ROUTE_COLORS[mode]);
  text(MODE_NAMES[mode], 148, 83, font_title, C_WHITE, C_BLACK);
  const char *service = mode == 0 ? "Agents not connected"
    : mode == 1 ? (talk.online(millis()) ? "Local speech to text" : "Mac not connected")
    : focus.running ? "Session in progress" : !focus.remaining ? "Session complete"
    : focus.remaining < FocusTimer::duration ? "Session paused" : "25-minute session";
  text(service, 88, 128, font_body, 0xD4D4D4, C_BLACK);
  if (mode == 0) drawAgents();
  else if (mode == 1) drawTalk();
  else drawFocus();
  drawNavigation();
  drawBattery();
  canvas.pushSprite(0, 0);
  dirty = false;
}

void recordingDone(void*, void* data, size_t length) {
  completedSamples.store(static_cast<int16_t*>(data) - audioBuffer + length);
}

void stopTalk(bool send) {
  if (!talk.capturing()) return;
  M5.Mic.end();
  const uint32_t samples = completedSamples.load();
  talk.phase = TalkState::idle;
  talkMessage = "Hold to talk";
  dirty = true;
  if (!send) return;
  if (samples < 8000) { talk.phase = TalkState::error; talkMessage = "Hold a little longer"; return; }
  if (!talk.online(millis())) { talk.phase = TalkState::error; talkMessage = "Mac disconnected"; return; }
  talk.phase = TalkState::processing;
  talk.startedAt = millis();
  talkMessage = "Transcribing...";
  render();
  Serial.printf("PULSE_AUDIO %lu %lu\n", (unsigned long)talk.session, (unsigned long)(samples * 2));
  const uint8_t *bytes = reinterpret_cast<const uint8_t*>(audioBuffer);
  size_t sent = 0;
  while (sent < samples * 2) {
    size_t count = Serial.write(bytes + sent, min(size_t(512), size_t(samples * 2) - sent));
    if (!count) { talk.phase = TalkState::error; talkMessage = "USB transfer failed"; break; }
    sent += count;
    delay(1);
  }
  Serial.println();
  dirty = true;
}

void startTalk() {
  if (mode != 1 || !talk.start(millis())) return;
  completedSamples = 0;
  queuedSamples = 0;
  talkMessage = "Getting ready...";
  dirty = true;
  render();
  if (!audioBuffer) audioBuffer = static_cast<int16_t*>(ps_malloc(TalkState::capacity * 2));
  if (!audioBuffer) { talk.phase = TalkState::error; talkMessage = "Not enough memory"; dirty = true; return; }
  M5.Speaker.end();
  M5.Mic.setBufferReleaseCallback(nullptr, recordingDone);
  if (!M5.Mic.begin()) { talk.phase = TalkState::error; talkMessage = "Microphone error"; dirty = true; }
}

void changeMode(int next) {
  if (next != mode) stopTalk(false);
  mode = next;
  touchTalking = false;
  dirty = true;
}

void updateTalk(uint32_t now) {
  if (talk.connected && !talk.online(now)) {
    stopTalk(false);
    talk.connected = false;
    if (talk.phase == TalkState::processing) talk.phase = TalkState::error;
    talkMessage = "Mac disconnected";
    dirty = true;
  }
  if (talk.warmed(now)) {
    talk.phase = TalkState::recording;
    talkMessage = "Listening...";
    dirty = true;
  }
  if (talk.phase == TalkState::recording) {
    while (queuedSamples < TalkState::capacity && M5.Mic.isRecording() < 2) {
      if (!M5.Mic.record(audioBuffer + queuedSamples, TalkState::chunk, TalkState::sampleRate)) {
        stopTalk(false);
        talk.phase = TalkState::error;
        talkMessage = "Microphone error";
        break;
      }
      queuedSamples += TalkState::chunk;
    }
    if (completedSamples.load() == TalkState::capacity) stopTalk(true);
  }
  if (talk.phase == TalkState::processing && uint32_t(now - talk.startedAt) > 60000) {
    talk.phase = TalkState::error;
    talkMessage = "Mac took too long";
    dirty = true;
  }
}

void touch(int x, int y) {
  for (int i = 0; i < 3; ++i) {
    const int dx = x - (133 + i * 100), dy = y - 390;
    if (dx * dx + dy * dy <= 32 * 32) { changeMode(i); return; }
  }
  if (mode != 2 || y < 289 || y >= 341) return;
  if (x >= 88 && x < 270) focus.toggle(millis());
  else if (x >= 280 && x < 378) focus.reset();
  dirty = true;
}

void status(bool reply = false) {
  const auto *partition = esp_ota_get_running_partition();
  Serial.printf("PULSE_%s mode=%s running=%d remaining_ms=%lu battery=%d battery_mv=%d board=%d psram=%u heap=%u app=0x%lx talk=%d mic=%d samples=%lu\n",
    reply ? "REPLY" : "STATUS", MODE_NAMES[mode], focus.running, (unsigned long)focus.remaining, battery,
    int(M5.Power.getBatteryVoltage()), int(M5.getBoard()), ESP.getPsramSize(),
    ESP.getFreeHeap(), (unsigned long)(partition ? partition->address : 0),
    int(talk.phase), M5.Mic.isRunning(), (unsigned long)completedSamples.load());
}

void serialCommand() {
  // Bounded USB diagnostics exercise the same input path as the touchscreen.
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (commandLength < sizeof command - 1) command[commandLength++] = c;
      else discardCommand = true;
      continue;
    }
    command[commandLength] = 0;
    if (!discardCommand) {
      int x, y;
      char extra;
      unsigned long resultId;
      char result[12];
      if (!strcmp(command, "status")) status(true);
      else if (!strcmp(command, "bridge ready")) {
        if (!talk.online(millis())) { talkMessage = "Hold to talk"; dirty = true; }
        talk.heartbeatAt = millis();
        talk.connected = true;
      } else if (!strcmp(command, "bridge offline")) {
        stopTalk(false); talk.connected = false; dirty = true;
      } else if (sscanf(command, "voice %10lu %11s %c", &resultId, result, &extra) == 2
                 && talk.acceptResult(resultId)) {
        talk.phase = !strcmp(result, "ready") ? TalkState::ready : TalkState::error;
        talkMessage = !strcmp(result, "ready") ? "Ready to paste"
          : !strcmp(result, "empty") ? "No speech heard"
          : !strcmp(result, "permission") ? "Check Mac permissions" : "Try again";
        dirty = true;
      } else if (!strcmp(command, "hold")) {
        startTalk(); status(true);
      } else if (!strcmp(command, "release")) {
        stopTalk(true); status(true);
      }
      else if (sscanf(command, "tap %3d %3d %c", &x, &y, &extra) == 2
               && x >= 0 && x < 466 && y >= 0 && y < 466) {
        touch(x, y);
        status(true);
      } else if (!strcmp(command, "frame")) {
        if (talk.capturing()) {
          Serial.println("PULSE_ERROR recording_busy");
          commandLength = 0;
          discardCommand = false;
          continue;
        }
        render();
        Serial.printf("PULSE_FRAME %u\n", 466 * 466 * 2);
        const uint8_t *buffer = static_cast<const uint8_t *>(canvas.getBuffer());
        for (size_t offset = 0; offset < 466 * 466 * 2; offset += 512) {
          const size_t count = min(size_t(512), size_t(466 * 466 * 2) - offset);
          Serial.write(buffer + offset, count);
          delay(1);
        }
        Serial.println();
      }
    }
    commandLength = 0;
    discardCommand = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(1000);
  auto config = M5.config();
  config.internal_mic = true;
  M5.begin(config);
  M5.Mic.end();
  M5.Speaker.end();
  M5.Display.setBrightness(160);
  M5.Power.setVibration(0);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(466, 466)) {
    M5.Display.fillScreen(C_BLACK);
    M5.Display.setTextColor(C_WHITE);
    M5.Display.drawString("Display memory error", 90, 200);
    Serial.println("PULSE_ERROR display_memory");
    while (true) delay(1000);
  }
  battery = M5.Power.getBatteryLevel();
  if (battery > 100) battery = 100;
  render();
  Serial.println("PULSE_BOOT version=2 font=Helvetica talk=usb");
  status();
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  if (focus.tick(now)) {
    focusAlertPending = true;
    if (mode != 1) changeMode(2);
    dirty = true;
    Serial.println("PULSE_FOCUS complete");
  }
  if (focusAlertPending && !talk.capturing()) { buzz(); focusAlertPending = false; }
  if (vibrating && uint32_t(millis() - vibrationAt) >= 400) {
    M5.Power.setVibration(0);
    vibrating = false;
  }
  if (M5.BtnA.wasPressed()) changeMode((mode + 1) % 3);
  if (M5.BtnB.wasPressed()) {
    if (mode == 1) startTalk();
    else changeMode(0);
  }
  if (M5.BtnB.wasReleased() && mode == 1) stopTalk(true);
  if (M5.Touch.getCount()) {
    const auto point = M5.Touch.getDetail();
    if (point.wasPressed()) {
      if (mode == 1 && point.x >= 65 && point.x <= 400 && point.y >= 154 && point.y <= 345) {
        touchTalking = true;
        startTalk();
      } else touch(point.x, point.y);
    }
    if (point.wasReleased() && touchTalking) { touchTalking = false; stopTalk(true); }
  }
  if (touchTalking && !M5.Touch.getCount()) { touchTalking = false; stopTalk(true); }
  serialCommand();
  updateTalk(millis());
  if (uint32_t(now - lastBattery) >= 15000) {
    lastBattery = now;
    battery = M5.Power.getBatteryLevel();
    if (battery > 100) battery = 100;
    dirty = true;
  }
  if (dirty || ((focus.running || talk.capturing()) && uint32_t(now - lastFrame) >= 250)) {
    lastFrame = now;
    render();
  }
  if (Serial && uint32_t(now - lastStatus) >= 5000) { lastStatus = now; status(); }
  delay(10);
}
