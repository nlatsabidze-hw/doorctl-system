// ═══════════════════════════════════════════════════════════
// DoorCTL - Configuration Template
// ═══════════════════════════════════════════════════════════
// This file is PUBLIC (safe to upload to GitHub).
// Real credentials must NEVER be written here.
//
// Usage:
//   1. Copy this file and rename it to "config.h"
//   2. In config.h, replace all YOUR_..._HERE placeholders
//      with the real credentials
//   3. config.h must NEVER be uploaded to GitHub
//      (already protected in .gitignore)
// ═══════════════════════════════════════════════════════════

// ── BLYNK ──
#define BLYNK_TEMPLATE_ID   "YOUR_BLYNK_TEMPLATE_ID_HERE"
#define BLYNK_TEMPLATE_NAME "YOUR_BLYNK_TEMPLATE_NAME_HERE"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN_HERE"

// ── WIFI NETWORKS (up to 3) ──
#define WIFI_SSID_1 "YOUR_WIFI_SSID_1"
#define WIFI_PASS_1 "YOUR_WIFI_PASSWORD_1"

#define WIFI_SSID_2 "YOUR_WIFI_SSID_2"
#define WIFI_PASS_2 "YOUR_WIFI_PASSWORD_2"

#define WIFI_SSID_3 "YOUR_WIFI_SSID_3"
#define WIFI_PASS_3 "YOUR_WIFI_PASSWORD_3"

// ── SUPABASE ──
#define SUPABASE_URL_STR "https://YOUR_PROJECT_REF.supabase.co"
#define SUPABASE_KEY_STR "YOUR_SUPABASE_ANON_KEY_HERE"

// ── OTA (over-the-air firmware updates) ──
#define OTA_PASSWORD_STR "YOUR_OTA_PASSWORD_HERE"
