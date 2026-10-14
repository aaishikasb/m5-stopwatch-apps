#pragma once
// A POSIX time-zone string, independently set on each badge (handles DST).
// Examples: "PST8PDT,M3.2.0,M11.1.0", "GMT0BST,M3.5.0/1,M10.5.0", "IST-5:30".
constexpr char BLOOM_TIMEZONE[] = "PST8PDT,M3.2.0,M11.1.0";
constexpr bool BLOOM_REDUCED_MOTION = false;
constexpr uint32_t BLOOM_POLL_MS = 1000;
