#pragma once
#define WIFI_SSID "your-network"
#define WIFI_PASS "your-password"
// Public HTTPS relay origin, without a trailing slash. Each badge has its OWN token.
#define BLOOM_RELAY_URL "https://bloom.example.com"
#define BLOOM_DEVICE_TOKEN "replace-with-device-a-or-b-token"
// PEM root CA that signs the relay's TLS certificate. Never use setInsecure().
static const char BLOOM_ROOT_CA[] = R"PEM(
-----BEGIN CERTIFICATE-----
paste-your-relay-root-CA-here
-----END CERTIFICATE-----
)PEM";
