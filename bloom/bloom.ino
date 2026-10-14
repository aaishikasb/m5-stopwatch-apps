#include <M5Unified.h>
#include <StopWatchFace.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <atomic>
#include <ctime>
#include <esp_system.h>
#include "interaction.h"
#include "visual.h"
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

using Bloom::Kind;
using Bloom::Event;
enum class Link : uint8_t { setup, wifi, clock, connecting, ready, token, taskFailed };
enum class Notice : uint8_t { received, accepted, failed, rejected };
struct Update { Notice type; Event event; };
M5Canvas canvas(&M5.Display);
Bloom::Interaction interaction;
QueueHandle_t outgoing = nullptr, incoming = nullptr;
std::atomic<Link> connectionState{Link::setup};
std::atomic<uint32_t> lastOnline{0};
bool sending = false, haptics = true, dirty = true;
uint8_t motorLevel = 0;
uint32_t lastFrame = 0, lastActivity = 0, hintAt = 0;
const char *hint = "";

bool online(uint32_t now) { return lastOnline.load() && uint32_t(now - lastOnline.load()) < 6000; }
bool validId(const char *id) {
  if (!id || strlen(id) != 32) return false;
  for (unsigned i = 0; i < 32; ++i) if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
  return true;
}
void notify(Notice type, const Event &event) {
  Update update{type, event};
  xQueueSend(incoming, &update, portMAX_DELAY);
}

void networkTask(void *) {
  Event pending;
  bool hasPending = false;
  char ack[33] = {};
  uint32_t queuedAt = 0, lastRequest = 0, lastJoin = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(BLOOM_TIMEZONE, "time.cloudflare.com", "pool.ntp.org");
  for (;;) {
    const uint32_t now = millis();
    if (!hasPending && xQueueReceive(outgoing, &pending, 0) == pdTRUE) {
      hasPending = true; queuedAt = now; lastRequest = now - BLOOM_POLL_MS;
    }
    if (hasPending && uint32_t(now - queuedAt) >= 15000) {
      notify(Notice::failed, pending); hasPending = false;
    }
    if (WiFi.status() != WL_CONNECTED) {
      connectionState = Link::wifi;
      if (uint32_t(now - lastJoin) > 15000) { WiFi.reconnect(); lastJoin = now; }
      vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    // Certificate validation needs a trustworthy clock; never downgrade TLS.
    if (time(nullptr) < 1735689600) {
      connectionState = Link::clock; vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    const uint32_t interval = connectionState == Link::ready ? BLOOM_POLL_MS : 2500;
    if (uint32_t(now - lastRequest) < interval) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    lastRequest = now;
    WiFiClientSecure client;
    client.setCACert(BLOOM_ROOT_CA);
    client.setHandshakeTimeout(5);
    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(2500);
    http.useHTTP10(true);
    if (!http.begin(client, String(BLOOM_RELAY_URL) + "/v1/exchange")) { connectionState = Link::connecting; continue; }
    http.addHeader("Authorization", String("Bearer ") + BLOOM_DEVICE_TOKEN);
    http.addHeader("Content-Type", "application/json");
    JsonDocument request;
    request["ack"] = ack;
    if (hasPending) {
      request["event"]["id"] = pending.id;
      request["event"]["kind"] = pending.kind == Kind::reply ? "reply" : "bloom";
      request["event"]["reply_to"] = pending.replyTo;
    }
    String payload;
    serializeJson(request, payload);
    const int code = http.POST(payload);
    if (code != 200) {
      connectionState = code == 401 ? Link::token : Link::connecting;
      if (hasPending && (code == 400 || code == 401 || code == 409 || code == 422)) {
        notify(Notice::rejected, pending); hasPending = false;
      }
      http.end(); continue;
    }
    const int size = http.getSize();
    if (size < 1 || size > 1024) { http.end(); connectionState = Link::connecting; continue; }
    char body[1025];
    const size_t count = http.getStreamPtr()->readBytes(body, size);
    http.end();
    JsonDocument response;
    if (count != size || deserializeJson(response, body, count) || !response["accepted"].is<const char *>()
        || response["event"].isUnbound()
        || (!response["event"].is<JsonObject>() && !response["event"].isNull())) {
      connectionState = Link::connecting; continue;
    }
    lastOnline = millis(); connectionState = Link::ready;
    if (hasPending && !strcmp(response["accepted"].as<const char *>(), pending.id)) {
      notify(Notice::accepted, pending); hasPending = false;
    }
    JsonVariant remote = response["event"];
    if (!remote.is<JsonObject>()) continue;
    const char *id = remote["id"] | "", *kind = remote["kind"] | "", *parent = remote["reply_to"] | "";
    if (!validId(id) || (strcmp(kind, "bloom") && strcmp(kind, "reply"))
        || (strcmp(kind, "reply") == 0 && !validId(parent)) || strlen(parent) > 32) continue;
    if (!strcmp(id, ack)) continue;
    if (remote["expires_at"].as<double>() > double(time(nullptr))) {
      Event event;
      event.kind = !strcmp(kind, "reply") ? Kind::reply : Kind::bloom;
      strlcpy(event.id, id, sizeof event.id); strlcpy(event.replyTo, parent, sizeof event.replyTo);
      // Acknowledge only after the event is safely in the UI queue.
      notify(Notice::received, event);
    }
    strlcpy(ack, id, sizeof ack);
  }
}

void showHint(const char *message, uint32_t now) { hint = message; hintAt = now; dirty = true; }
void sendGesture(Kind kind, uint32_t now) {
  if (kind == Kind::none) return;
  if (sending) { showHint("Sending your touch...", now); return; }
  if (!online(now)) { showHint("Connect to send a bloom", now); return; }
  Event event;
  event.kind = kind;
  for (int i = 0; i < 4; ++i) snprintf(event.id + i * 8, 9, "%08lx", (unsigned long)esp_random());
  if (kind == Kind::reply) strlcpy(event.replyTo, interaction.replyTo, sizeof event.replyTo);
  if (xQueueSend(outgoing, &event, 0) == pdTRUE) {
    sending = true; showHint("Sending your touch...", now);
  }
}
void label(const char *text, int y, uint32_t ink = Bloom::muted, Face::Type type = Face::caption) {
  Face::label(canvas, text, 233, y, type, ink);
}
void render(uint32_t now) {
  canvas.fillScreen(Bloom::black);
  auto rect = [](int x, int y, int w, int h, uint32_t color) { canvas.fillRect(x, y, w, h, color); };
  if (connectionState == Link::setup) {
    label("Set up your pair", 207, Bloom::white, Face::body);
    label("Add Wi-Fi, relay and device token", 251);
    label("to bloom/secrets.h and rebuild", 277);
  } else if (connectionState == Link::taskFailed) {
    label("Bloom could not start", 210, Bloom::pink, Face::body);
    label("Restart the badge", 254);
  } else {
    const bool active = interaction.animating(now), full = active && !interaction.echo;
    char clockText[6] = "--:--", dateText[32] = "Waiting for time";
    const time_t utc = time(nullptr);
    if (utc >= 1735689600) {
      struct tm local;
      localtime_r(&utc, &local);
      strftime(clockText, sizeof clockText, "%H:%M", &local);
      strftime(dateText, sizeof dateText, "%a, %d %b", &local);
    }
    if (active) Bloom::flower(rect, interaction.scene, now - interaction.sceneAt, interaction.echo, BLOOM_REDUCED_MOTION);
    if (full) {
      const uint32_t elapsed = now - interaction.sceneAt;
      // The bloom settles into the same full-size clock during the final 1.5 seconds.
      if (elapsed > 8500) Bloom::clock(rect, clockText, 177, 18, (elapsed - 8500) / 1500.0f);
      else Bloom::clock(rect, clockText, 437, 4, 0.8f);
    } else {
      Bloom::clock(rect, clockText, 177, 18);
      label(dateText, 286);
    }
    const bool hasHint = hint[0] && uint32_t(now - hintAt) < 3500;
    const char *status = hasHint ? hint : sending ? "Sending your touch..."
      : interaction.canReply(now) ? "Double tap to reply" : "Tap to send a bloom";
    label(status, full ? 414 : 349, Bloom::white);
    if (!full) {
      const Link state = connectionState.load();
      const char *connection = state == Link::wifi ? "Joining Wi-Fi"
        : state == Link::clock ? "Setting the clock"
        : state == Link::token ? "Check device token"
        : online(now) ? (haptics ? "B: quiet mode" : "Quiet mode / B: haptics") : "Reconnecting to relay";
      label(connection, 397);
    }
  }
  canvas.pushSprite(0, 0); lastFrame = now; dirty = false;
}
void setup() {
  Face::begin(canvas);
  const bool configured = strcmp(WIFI_SSID, "your-network") && strlen(WIFI_SSID)
    && !strncmp(BLOOM_RELAY_URL, "https://", 8) && !strstr(BLOOM_RELAY_URL, "example.com")
    && strlen(BLOOM_DEVICE_TOKEN) >= 32 && !strstr(BLOOM_ROOT_CA, "paste-your");
  if (configured) {
    outgoing = xQueueCreate(1, sizeof(Event)); incoming = xQueueCreate(6, sizeof(Update));
    connectionState = Link::wifi;
    if (!outgoing || !incoming || xTaskCreatePinnedToCore(networkTask, "bloom-net", 16384, nullptr, 1, nullptr, 0) != pdPASS)
      connectionState = Link::taskFailed;
  }
  render(millis());
}
void loop() {
  M5.update();
  const uint32_t now = millis();
  Update update;
  while (incoming && xQueueReceive(incoming, &update, 0) == pdTRUE) {
    if (update.type == Notice::received) {
      if (interaction.receive(update.event, now)) { lastActivity = now; hint = ""; dirty = true; }
    } else {
      sending = false;
      if (update.type == Notice::accepted) {
        interaction.sent(update.event, now);
        showHint(update.event.kind == Kind::reply ? "Reply sent to relay" : "Bloom sent to relay", now);
      } else showHint(update.type == Notice::rejected ? "Touch rejected; try again" : "Could not confirm delivery", now);
    }
  }
  sendGesture(interaction.poll(now), now);
  bool tap = M5.BtnA.wasClicked();
  // Trigger on a released tap; dragging or holding the screen does not send.
  if (M5.Touch.getCount() == 1 && M5.Touch.getDetail().wasClicked()) tap = true;
  if (tap) { lastActivity = now; sendGesture(interaction.tap(now), now); dirty = true; }
  if (M5.BtnB.wasPressed()) {
    haptics = !haptics; lastActivity = now;
    showHint(haptics ? "Haptics on" : "Quiet mode on", now);
  }
  const bool active = interaction.animating(now);
  const uint8_t target = active && !interaction.echo ? Bloom::haptic(interaction.scene, now - interaction.sceneAt, haptics) : 0;
  if (target != motorLevel) { M5.Power.setVibration(target); motorLevel = target; }
  static uint8_t brightness = 140;
  const uint8_t desired = uint32_t(now - lastActivity) > 30000 && !active ? 45 : 140;
  if (brightness != desired) { M5.Display.setBrightness(desired); brightness = desired; }
  if (uint32_t(now - lastFrame) >= (active ? 33u : 250u) || (dirty && uint32_t(now - lastFrame) >= 33)) render(now);
  delay(1);
}
