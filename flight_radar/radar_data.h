#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr int MAXP = 128, TRAIL = 14;
constexpr uint32_t STALE_MS = 60000;
constexpr double RAD = 3.14159265358979323846 / 180.0;

struct Plane {
  char hex[8], flight[10], type[6], reg[10];
  double lat, lon;
  int alt;
  bool ground, altKnown;
  float gs, track;
  int squawk;
  uint32_t fixMs;
  float tlat[TRAIL], tlon[TRAIL];
  int tn;
  bool near;
  float sx, sy, dist, brg;
};

inline bool validCoordinates(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) &&
         lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

inline bool isStale(const Plane &p, uint32_t now) {
  return uint32_t(now - p.fixMs) >= STALE_MS;
}

inline bool isEmergency(const Plane &p) {
  return p.squawk == 7500 || p.squawk == 7600 || p.squawk == 7700;
}

inline void projectPosition(double lat, double lon, double homeLat, double homeLon,
                            float &east, float &north) {
  // shortcut: local tangent plane is sufficient at 100 nm; use geodesics for wider views.
  east = std::remainder(lon - homeLon, 360.0) * 60 * std::cos(homeLat * RAD);
  north = (lat - homeLat) * 60;
}

inline void deadReckon(const Plane &p, uint32_t now, double &lat, double &lon) {
  lat = p.lat;
  lon = p.lon;
  if (p.ground || !std::isfinite(p.gs) || !std::isfinite(p.track) || p.gs < 1) return;
  double distance = p.gs * std::min(uint32_t(now - p.fixMs), uint32_t(45000)) / 3600000.0;
  double angle = distance / (60.0 / RAD);
  double phi = p.lat * RAD, theta = p.track * RAD;
  double next = std::asin(std::clamp(std::sin(phi) * std::cos(angle) +
      std::cos(phi) * std::sin(angle) * std::cos(theta), -1.0, 1.0));
  lon = std::remainder(p.lon + std::atan2(std::sin(theta) * std::sin(angle) * std::cos(phi),
      std::cos(angle) - std::sin(phi) * std::sin(next)) / RAD, 360.0);
  lat = next / RAD;
}

inline bool parsePlane(JsonObjectConst a, Plane &p, uint32_t now) {
  if (!a["lat"].is<double>() || !a["lon"].is<double>() || !a["seen_pos"].is<double>()) return false;
  double lat = a["lat"], lon = a["lon"], seen = a["seen_pos"];
  if (!validCoordinates(lat, lon) || !std::isfinite(seen) || seen < 0 || seen >= STALE_MS / 1000) return false;
  const char *hex = a["hex"] | "";
  size_t len = std::strlen(hex), start = hex[0] == '~' ? 1 : 0;
  if (len != 6 + start) return false;
  for (size_t i = start; i < len; ++i) {
    if (!((hex[i] >= '0' && hex[i] <= '9') || (hex[i] >= 'a' && hex[i] <= 'f') ||
          (hex[i] >= 'A' && hex[i] <= 'F'))) return false;
  }
  p = {};
  std::snprintf(p.hex, sizeof p.hex, "%s", hex);
  std::snprintf(p.flight, sizeof p.flight, "%s", a["flight"] | "");
  for (size_t i = std::strlen(p.flight); i && p.flight[i - 1] == ' '; --i) p.flight[i - 1] = 0;
  std::snprintf(p.type, sizeof p.type, "%s", a["t"] | "");
  std::snprintf(p.reg, sizeof p.reg, "%s", a["r"] | "");
  p.lat = lat;
  p.lon = lon;
  p.ground = a["alt_baro"] == "ground";
  p.altKnown = a["alt_baro"].is<int>();
  p.alt = a["alt_baro"] | 0;
  p.gs = a["gs"] | NAN;
  p.track = a["track"] | NAN;
  if (p.gs < 0 || p.gs > 2000) p.gs = NAN;
  if (p.track < 0 || p.track >= 360) p.track = NAN;
  const char *sq = a["squawk"] | "";
  if (std::strlen(sq) == 4) {
    bool valid = true;
    for (int i = 0; i < 4; ++i) {
      if (sq[i] < '0' || sq[i] > '7') { valid = false; break; }
      p.squawk = p.squawk * 10 + sq[i] - '0';
    }
    if (!valid) p.squawk = 0;
  }
  p.fixMs = now - uint32_t(seen * 1000);
  return true;
}
