#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <atomic>
#include <cstdarg>
#include <ctime>
#include "secrets.h"
#include "config.h"
#include "radar_data.h"
#include "trusted_roots.h"

constexpr int CX = 233, CY = 233, R = 214;
constexpr float RANGES[] = {5, 10, 25, 50, 100};
constexpr int NRANGES = sizeof(RANGES) / sizeof(RANGES[0]);
constexpr uint32_t FETCH_MS = 10000;

Plane planes[MAXP];
Plane incoming[MAXP];
int nplanes = 0;
SemaphoreHandle_t lock;
M5Canvas canvas(&M5.Display);

constexpr double homeLat = HOME_LAT, homeLon = HOME_LON;
constexpr const char *homeName = HOME_NAME;
static_assert(HOME_LAT > -85 && HOME_LAT < 85 && HOME_LON >= -180 && HOME_LON <= 180, "Invalid radar center");
std::atomic<int> rangeIdx{2};
std::atomic<int> alertSeq{0};
char selHex[8] = "";
char status[80] = "";
std::atomic<uint32_t> lastOk{0};
uint32_t backoffMs = 0;
std::atomic<int> provider{0};
const char *PROVIDERS[] = {"adsb.lol", "adsb.fi"};
uint32_t vibeUntil = 0;
int seenAlert = 0;

uint16_t C_BG, C_RING, C_RING_DIM, C_TEXT, C_DIM, C_SEL;

void vibe(uint8_t level, uint32_t ms) {
  M5.Power.setVibration(level);
  vibeUntil = millis() + ms;
}

void setStatus(const char *format, ...) {
  char message[sizeof status];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  xSemaphoreTake(lock, portMAX_DELAY);
  strlcpy(status, message, sizeof status);
  xSemaphoreGive(lock);
  if (Serial) Serial.printf("RADAR_STATUS %s\n", message);
}

void project(double lat, double lon, float &dxNm, float &dyNm) {
  projectPosition(lat, lon, homeLat, homeLon, dxNm, dyNm);
}

void fetchPlanes() {
  WiFiClientSecure cli;
  cli.setCACert(TRUSTED_ROOTS);
  cli.setHandshakeTimeout(10);
  HTTPClient http;
  http.useHTTP10(true);
  http.setUserAgent("flight-radar-badge/1.0");
  http.setTimeout(9000);
  http.setConnectTimeout(9000);
  int radius = (int)ceilf(RANGES[rangeIdx] * 1.2f);
  if (radius > 250) radius = 250;
  char url[128];
  if (provider == 0) snprintf(url, sizeof url, "https://api.adsb.lol/v2/point/%.4f/%.4f/%d", homeLat, homeLon, radius);
  else snprintf(url, sizeof url, "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d", homeLat, homeLon, radius);
  if (!http.begin(cli, url)) { setStatus("connect failed"); return; }
  int code = http.GET();
  if (code == 429) {
    http.end();
    provider = (provider + 1) % 2;
    backoffMs = backoffMs ? min<uint32_t>(backoffMs * 2, 240000) : 30000;
    setStatus("rate limited, retry in %us", (unsigned)((FETCH_MS + backoffMs) / 1000));
    return;
  }
  if (code != 200) {
    setStatus("feed HTTP %d; retrying", code);
    provider = (provider + 1) % 2;
    http.end();
    return;
  }

  JsonDocument filter;
  const char *key = provider == 0 ? "ac" : "aircraft";
  JsonObject f = filter[key][0].to<JsonObject>();
  for (const char *k : {"hex", "flight", "t", "r", "lat", "lon", "alt_baro", "gs", "track", "squawk", "seen_pos"}) f[k] = true;

  int len = http.getSize();
  constexpr size_t MAX_BODY = 512 * 1024;
  if (len > (int)MAX_BODY) { setStatus("feed too large"); http.end(); return; }
  size_t cap = len > 0 ? size_t(len) : MAX_BODY;
  char *body = (char *)ps_malloc(cap + 1);
  if (!body) { setStatus("out of memory"); http.end(); return; }
  WiFiClient *s = http.getStreamPtr();
  size_t got = 0;
  uint32_t idle = millis(), started = idle;
  while (got < cap && (s->connected() || s->available()) && millis() - started < 15000) {
    int avail = s->available();
    if (avail > 0) {
      int read = s->read((uint8_t *)body + got, min((size_t)avail, cap - got));
      if (read <= 0) break;
      got += size_t(read);
      idle = millis();
    } else if (millis() - idle > 8000) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  body[got] = 0;
  if (len > 0 && got < (size_t)len) {
    setStatus("short read %u/%d", (unsigned)got, len);
    free(body);
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, static_cast<const char *>(body), got, DeserializationOption::Filter(filter));
  free(body);
  if (err) { setStatus("parse %s", err.c_str()); return; }
  if (!doc[key].is<JsonArray>()) { setStatus("invalid feed response"); return; }

  uint32_t now = millis();
  float nearNm = RANGES[rangeIdx] * 0.25f;
  int n = 0;
  int newAlerts = 0;

  xSemaphoreTake(lock, portMAX_DELAY);
  for (JsonObject a : doc[key].as<JsonArray>()) {
    if (n >= MAXP) break;
    Plane &p = incoming[n];
    if (!parsePlane(a, p, now)) continue;

    float dx, dy;
    project(p.lat, p.lon, dx, dy);
    float dist = sqrtf(dx * dx + dy * dy);

    const Plane *old = nullptr;
    for (int i = 0; i < nplanes; i++) if (!strcmp(planes[i].hex, p.hex)) { old = &planes[i]; break; }
    if (old) {
      memcpy(p.tlat, old->tlat, sizeof p.tlat);
      memcpy(p.tlon, old->tlon, sizeof p.tlon);
      p.tn = old->tn;
      if (fabs(old->lat - p.lat) > 1e-5 || fabs(old->lon - p.lon) > 1e-5) {
        if (p.tn == TRAIL) {
          memmove(p.tlat, p.tlat + 1, sizeof(float) * (TRAIL - 1));
          memmove(p.tlon, p.tlon + 1, sizeof(float) * (TRAIL - 1));
          p.tn--;
        }
        p.tlat[p.tn] = old->lat;
        p.tlon[p.tn] = old->lon;
        p.tn++;
      }
      p.near = old->near;
    }
    bool isNear = !p.ground && dist < nearNm;
    if (isNear && !p.near && old) newAlerts++;
    p.near = isNear;
    n++;
  }
  memcpy(planes, incoming, sizeof(Plane) * n);
  nplanes = n;
  if (newAlerts) alertSeq++;
  lastOk = millis();
  backoffMs = 0;
  status[0] = 0;
  xSemaphoreGive(lock);
  if (Serial) Serial.printf("RADAR_FEED ok aircraft=%d provider=%s heap=%u board=%d psram=%u\n",
                            n, PROVIDERS[provider], ESP.getFreeHeap(), int(M5.getBoard()), ESP.getPsramSize());
}

void netTask(void *) {
  uint32_t last = millis() - FETCH_MS;
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      setStatus("wifi lost, retrying");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    if (time(nullptr) < 1700000000) {
      setStatus("syncing clock for HTTPS...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (millis() - last >= FETCH_MS + backoffMs) {
      fetchPlanes();
      last = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

uint16_t altColor(const Plane &p) {
  if (p.ground || !p.altKnown) return canvas.color565(120, 120, 120);
  if (p.alt < 3000) return canvas.color565(255, 110, 60);
  if (p.alt < 10000) return canvas.color565(255, 200, 40);
  if (p.alt < 25000) return canvas.color565(120, 255, 120);
  if (p.alt < 35000) return canvas.color565(60, 210, 255);
  return canvas.color565(190, 130, 255);
}

uint16_t dim(uint16_t c, float k) {
  uint8_t r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  return canvas.color565(r * k, g * k, b * k);
}

void drawPlaneIcon(float x, float y, float trk, uint16_t col, float s) {
  float t = trk * DEG_TO_RAD;
  auto pt = [&](float ang, float len, float &ox, float &oy) { ox = x + sinf(t + ang) * len; oy = y - cosf(t + ang) * len; };
  float nx, ny, lx, ly, rx, ry, bx, by;
  pt(0, 9 * s, nx, ny);
  pt(2.4f, 7 * s, lx, ly);
  pt(-2.4f, 7 * s, rx, ry);
  pt(PI, 3 * s, bx, by);
  canvas.fillTriangle(nx, ny, lx, ly, bx, by, col);
  canvas.fillTriangle(nx, ny, rx, ry, bx, by, col);
}

void drawScope(float sweep) {
  canvas.fillScreen(C_BG);
  for (int i = 0; i < 28; i++) {
    float a0 = sweep - i * 2.2f, a1 = a0 - 2.4f;
    float k = 0.55f * (1.0f - i / 28.0f);
    k = k * k;
    uint16_t c = canvas.color565(10 + 40 * k, 30 + 225 * k, 20 + 90 * k);
    float r0 = a0 * DEG_TO_RAD, r1 = a1 * DEG_TO_RAD;
    canvas.fillTriangle(CX, CY, CX + sinf(r0) * R, CY - cosf(r0) * R, CX + sinf(r1) * R, CY - cosf(r1) * R, c);
  }
  for (int i = 1; i <= 4; i++) canvas.drawCircle(CX, CY, R * i / 4, i == 4 ? C_RING : C_RING_DIM);
  canvas.drawCircle(CX, CY, R + 1, C_RING);
  for (int a = 0; a < 360; a += 10) {
    float r = a * DEG_TO_RAD;
    int len = (a % 30 == 0) ? 10 : 4;
    canvas.drawLine(CX + sinf(r) * (R - len), CY - cosf(r) * (R - len), CX + sinf(r) * R, CY - cosf(r) * R, C_RING);
  }
  canvas.drawFastHLine(CX - R, CY, 2 * R, C_RING_DIM);
  canvas.drawFastVLine(CX, CY - R, 2 * R, C_RING_DIM);
  float sr = sweep * DEG_TO_RAD;
  canvas.drawWideLine(CX, CY, CX + sinf(sr) * R, CY - cosf(sr) * R, 2, canvas.color565(140, 255, 170));

  canvas.setFont(&fonts::Font2);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(C_DIM);
  char buf[16];
  for (int i = 1; i <= 3; i++) {
    float v = RANGES[rangeIdx] * i / 4;
    snprintf(buf, sizeof buf, v < 10 ? "%.1f" : "%.0f", v);
    canvas.drawString(buf, CX + R * i / 4 * 0.707f + 12, CY + R * i / 4 * 0.707f);
  }
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.drawString("N", CX, CY - R + 22);
  canvas.setTextColor(C_DIM);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("E", CX + R - 20, CY);
  canvas.drawString("S", CX, CY + R - 20);
  canvas.drawString("W", CX - R + 20, CY);
  canvas.fillCircle(CX, CY, 4, C_TEXT);
  canvas.drawCircle(CX, CY, 8, C_RING);
}

void drawHud(int count, const char *displayStatus) {
  char buf[48];
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(C_TEXT);
  snprintf(buf, sizeof buf, "%g NM", RANGES[rangeIdx]);
  canvas.drawString(buf, CX, 62);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  snprintf(buf, sizeof buf, "%d aircraft  %s  %d%%", count, PROVIDERS[provider], M5.Power.getBatteryLevel());
  canvas.drawString(buf, CX, 86);
  if (displayStatus[0]) {
    canvas.setTextColor(canvas.color565(255, 120, 80));
    canvas.drawString(displayStatus, CX, 104);
  } else if (lastOk && millis() - lastOk > 20000) {
    canvas.setTextColor(canvas.color565(255, 120, 80));
    canvas.drawString("feed stale", CX, 104);
  }
}

void drawCard(const Plane &p, float dist, float brg) {
  int w = 270, h = 92, x = CX - w / 2, y = 318;
  canvas.fillRoundRect(x, y, w, h, 14, canvas.color565(8, 22, 14));
  canvas.drawRoundRect(x, y, w, h, 14, altColor(p));
  canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(altColor(p));
  canvas.drawString(p.flight[0] ? p.flight : p.hex, x + 14, y + 8);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.setTextDatum(top_right);
  char buf[48];
  snprintf(buf, sizeof buf, "%s %s", p.type, p.reg);
  canvas.drawString(buf, x + w - 14, y + 12);
  canvas.setTextDatum(top_left);
  canvas.setTextColor(C_TEXT);
  char altitude[20], speed[16], heading[12];
  if (p.ground) snprintf(altitude, sizeof altitude, "GROUND");
  else if (p.altKnown) snprintf(altitude, sizeof altitude, "%d ft", p.alt);
  else snprintf(altitude, sizeof altitude, "ALT ?");
  if (std::isfinite(p.gs)) snprintf(speed, sizeof speed, "%.0f kt", p.gs);
  else snprintf(speed, sizeof speed, "SPD ?");
  if (std::isfinite(p.track)) snprintf(heading, sizeof heading, "%03.0f deg", p.track);
  else snprintf(heading, sizeof heading, "HDG ?");
  snprintf(buf, sizeof buf, "%s  %s  %s", altitude, speed, heading);
  canvas.drawString(buf, x + 14, y + 40);
  canvas.setTextColor(C_DIM);
  snprintf(buf, sizeof buf, "%.1f nm  bearing %03.0f", dist, brg);
  if (p.squawk) snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "  sq %04d", p.squawk);
  canvas.drawString(buf, x + 14, y + 62);
}

int sortedIdx[MAXP];

void render() {
  static Plane snap[MAXP];
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = nplanes;
  char displayStatus[sizeof status];
  strlcpy(displayStatus, status, sizeof displayStatus);
  memcpy(snap, planes, sizeof(Plane) * n);
  xSemaphoreGive(lock);

  uint32_t now = millis();
  float sweep = fmodf(now * 0.09f, 360.0f);
  float scale = R / RANGES[rangeIdx];
  drawScope(sweep);

  int visible = 0;
  Plane *sel = nullptr;
  bool flash = (now / 300) % 2;
  bool emergency = false;
  for (int i = 0; i < n; i++) {
    Plane &p = snap[i];
    if (isStale(p, now)) continue;
    double lat, lon;
    deadReckon(p, now, lat, lon);
    float dx, dy;
    project(lat, lon, dx, dy);
    p.dist = sqrtf(dx * dx + dy * dy);
    p.brg = fmodf(atan2f(dx, dy) * RAD_TO_DEG + 360, 360);
    p.sx = CX + dx * scale;
    p.sy = CY - dy * scale;
    if (p.dist > RANGES[rangeIdx]) continue;
    sortedIdx[visible++] = i;
    emergency = emergency || isEmergency(p);
    if (selHex[0] && !strcmp(p.hex, selHex)) sel = &p;
  }
  std::sort(sortedIdx, sortedIdx + visible, [&](int a, int b) { return snap[a].dist < snap[b].dist; });

  for (int k = visible - 1; k >= 0; k--) {
    Plane &p = snap[sortedIdx[k]];
    uint16_t col = altColor(p);
    float lag = fmodf(sweep - p.brg + 360, 360);
    float glow = lag < 60 ? 1.0f - lag / 60.0f * 0.45f : 0.55f;
    if (&p == sel) glow = 1.0f;

    float px = p.sx, py = p.sy;
    for (int t = p.tn - 1; t >= 0; t--) {
      float dx, dy;
      project(p.tlat[t], p.tlon[t], dx, dy);
      float tx = CX + dx * scale, ty = CY - dy * scale;
      float fade = 0.15f + 0.5f * (t + 1) / (float)p.tn;
      canvas.drawLine(px, py, tx, ty, dim(col, fade * glow));
      px = tx;
      py = ty;
    }

    uint16_t c = dim(col, glow);
    if (isEmergency(p)) c = flash ? canvas.color565(255, 40, 40) : canvas.color565(255, 255, 255);
    if (p.ground) canvas.fillCircle(p.sx, p.sy, 3, c);
    else if (std::isfinite(p.track)) drawPlaneIcon(p.sx, p.sy, p.track, c, &p == sel ? 1.4f : 1.0f);
    else canvas.drawCircle(p.sx, p.sy, 5, c);
    if (lag < 8) canvas.drawCircle(p.sx, p.sy, 10 + lag, dim(col, 0.5f));

    if (k < 8 || &p == sel) {
      canvas.setFont(&fonts::Font0);
      canvas.setTextDatum(top_left);
      canvas.setTextColor(dim(col, glow * 0.95f));
      char lbl[24];
      if (p.ground || !p.altKnown) snprintf(lbl, sizeof lbl, "%s", p.flight[0] ? p.flight : p.hex);
      else snprintf(lbl, sizeof lbl, "%s %03d", p.flight[0] ? p.flight : p.hex, p.alt / 100);
      canvas.drawString(lbl, p.sx + 10, p.sy - 4);
    }
  }

  if (sel) {
    canvas.drawCircle(sel->sx, sel->sy, 16, C_SEL);
    canvas.drawCircle(sel->sx, sel->sy, 17, C_SEL);
    for (int d = 0; d < 1000; d += 12) {
      float k0 = d / 1000.0f, k1 = (d + 6) / 1000.0f;
      canvas.drawLine(CX + (sel->sx - CX) * k0, CY + (sel->sy - CY) * k0, CX + (sel->sx - CX) * k1, CY + (sel->sy - CY) * k1, dim(C_SEL, 0.6f));
    }
    drawCard(*sel, sel->dist, sel->brg);
  } else {
    if (selHex[0]) selHex[0] = 0;
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(C_DIM);
    canvas.drawString(homeName, CX, 372);
    canvas.drawString("A range   B next   tap select", CX, 392);
  }

  if (emergency && flash) {
    canvas.drawCircle(CX, CY, R + 4, canvas.color565(255, 40, 40));
    canvas.drawCircle(CX, CY, R + 5, canvas.color565(255, 40, 40));
  }

  drawHud(visible, displayStatus);
  canvas.pushSprite(0, 0);

  if (M5.BtnB.wasPressed() && visible) {
    int cur = -1;
    for (int k = 0; k < visible; k++) if (!strcmp(snap[sortedIdx[k]].hex, selHex)) cur = k;
    int nxt = (cur + 1) % visible;
    strlcpy(selHex, snap[sortedIdx[nxt]].hex, sizeof selHex);
    vibe(120, 25);
  }
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    float best = 36 * 36;
    int pick = -1;
    for (int k = 0; k < visible; k++) {
      Plane &p = snap[sortedIdx[k]];
      float d = (p.sx - t.x) * (p.sx - t.x) + (p.sy - t.y) * (p.sy - t.y);
      if (d < best) { best = d; pick = k; }
    }
    if (pick >= 0) { strlcpy(selHex, snap[sortedIdx[pick]].hex, sizeof selHex); vibe(120, 25); }
    else selHex[0] = 0;
  }
}

void bootScreen(const char *msg, float spin) {
  canvas.fillScreen(C_BG);
  for (int i = 1; i <= 4; i++) canvas.drawCircle(CX, CY, R * i / 4, C_RING_DIM);
  float r = spin * DEG_TO_RAD;
  canvas.drawWideLine(CX, CY, CX + sinf(r) * R, CY - cosf(r) * R, 2, canvas.color565(140, 255, 170));
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(C_TEXT);
  canvas.drawString("FLIGHT RADAR", CX, CY - 30);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_DIM);
  canvas.drawString(msg, CX, CY + 20);
  canvas.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setBrightness(200);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(466, 466)) {
    M5.Display.drawString("PSRAM allocation failed", 80, 233);
    for (;;) { M5.update(); delay(100); }
  }

  C_BG = canvas.color565(2, 10, 6);
  C_RING = canvas.color565(30, 120, 70);
  C_RING_DIM = canvas.color565(14, 55, 32);
  C_TEXT = canvas.color565(200, 255, 215);
  C_DIM = canvas.color565(90, 150, 110);
  C_SEL = canvas.color565(255, 255, 255);

  lock = xSemaphoreCreateMutex();
  if (!lock) {
    bootScreen("memory allocation failed", 0);
    for (;;) { M5.update(); delay(100); }
  }
  String ssid(WIFI_SSID);
  if (ssid.isEmpty() || ssid == "your-network") {
    bootScreen("Set Wi-Fi in secrets.h and rebuild", 0);
    for (;;) { M5.update(); delay(100); }
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    M5.update();
    char msg[64];
    if (millis() - t0 > 20000) snprintf(msg, sizeof msg, "can't join \"%s\", check secrets.h", WIFI_SSID);
    else snprintf(msg, sizeof msg, "joining %s", WIFI_SSID);
    bootScreen(msg, fmodf(millis() * 0.2f, 360));
    delay(16);
  }
  vibe(160, 60);
  if (xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0) != pdPASS)
    setStatus("network task could not start");
}

void loop() {
  M5.update();
  if (vibeUntil && (int32_t)(millis() - vibeUntil) > 0) { M5.Power.setVibration(0); vibeUntil = 0; }
  if (M5.BtnA.wasPressed()) {
    rangeIdx = (rangeIdx + 1) % NRANGES;
    vibe(120, 25);
  }
  if (alertSeq != seenAlert) {
    seenAlert = alertSeq;
    vibe(200, 120);
  }
  render();
  delay(16);
}
