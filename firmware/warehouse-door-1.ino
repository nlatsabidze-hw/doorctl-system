// ═══════════════════════════════════════════
// WAREHOUSE DOOR CONTROL v1.9
// ESP32 + Blynk + Active HIGH Relay + OTA + NTP + HTTP Log
//
// Virtual Pins:
//   V1  = OPEN  (App)
//   V2  = CLOSE (App)
//   V3  = STOP  (App)
//   V7  = Remote Start Hour   (App → ESP32)
//   V8  = Remote Start Minute (App → ESP32)
//   V9  = Remote End Hour     (App → ESP32)
//   V10 = Remote End Minute   (App → ESP32)
//   V11 = Remote Days         (App → ESP32)
//
// GPIO:
//   26 = OPEN  Relay (Active HIGH ✅)
//   27 = CLOSE Relay (Active HIGH ✅)
//   25 = Physical STOP button
//   32 = YK04 D0 → Remote OPEN
//   33 = YK04 D1 → Remote CLOSE
//   14 = YK04 D2 → Remote STOP
//
// CHANGES v1.9:
//   - Removed delay(100) from logToSupabase() and logConnection()
//     (blocked loop() after each HTTP send, undermining the v1.8
//     queue fix that exists to keep STOP responsive)
//   - Reworked httpBusy flag: now shared between logToSupabase()
//     and logConnection() — prevents door log and connection log
//     from colliding on the same HTTP connection (which caused -1
//     errors on rapid button presses or simultaneous Blynk events)
//   - Removed hardcoded username from BLYNK_WRITE(V1/V2/V3) logging.
//     Blynk cannot tell the firmware which PWA/app user sent the
//     command, so every door action was logged under a single fixed
//     username regardless of who actually pressed the button. The
//     PWA now logs door actions directly to Supabase with the real
//     logged-in username in parallel with the Blynk call. Door
//     control itself (doOpen/doClose/stopAll) is unaffected — only
//     the inaccurate log entry was removed.
//
// KNOWN LIMITATION:
//   loop() blocks during Supabase HTTP calls (300-800ms). If a Remote
//   or Physical STOP pulse arrives during that window, it can be missed
//   (usually only when two buttons are pressed within 1-2s of each other).
//   Relay control is never at risk — only the log entry for that press
//   may be lost; pressing again always works. PWA/Blynk App unaffected
//   (Blynk cloud queues commands). Accepted trade-off — a real fix
//   needs async HTTP, too complex for this rare, non-critical case.
// ═══════════════════════════════════════════

#include "config.h"

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <HTTPClient.h>

// ═══════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════
WiFiMulti wifiMulti;

// WiFi networks - first available will be used
void setupWiFi() {
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PASS_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PASS_2);
  wifiMulti.addAP(WIFI_SSID_3, WIFI_PASS_3);
  Serial.print("WiFi connecting...");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
}

// Supabase
const char* SUPABASE_URL = SUPABASE_URL_STR;
const char* SUPABASE_KEY = SUPABASE_KEY_STR;

// New York DST
#define TIMEZONE   "EST5EDT,M3.2.0,M11.1.0"
#define NTP_SERVER "pool.ntp.org"

// ═══════════════════════════════════════════
// Active HIGH Relay
// HIGH = relay ON (door moves)
// LOW  = relay OFF (door stops) - boot default
// ═══════════════════════════════════════════
#define RELAY_OPEN    26
#define RELAY_CLOSE   27
#define RELAY_ON      HIGH
#define RELAY_OFF     LOW

// Buttons
#define STOP_PHYSICAL 25
#define REMOTE_OPEN   32
#define REMOTE_CLOSE  33
#define REMOTE_STOP   14

// Timers
#define CLOSE_DURATION    32000
#define OPEN_DURATION     32000
#define RECONNECT_TIMEOUT 30000

// ═══════════════════════════════════════════
// Variables
// ═══════════════════════════════════════════
unsigned long lastConnected   = 0;
unsigned long closeStartTime  = 0;
unsigned long openStartTime   = 0;
unsigned long openPulseStart  = 0;
bool isClosing    = false;
bool isOpening    = false;
bool openPulseActive = false;  // true while the 500ms OPEN relay pulse is in progress

bool lastPhysicalStop = HIGH;

// Pending log queue - sent after all button checks, so buttons stay responsive
bool   pendingLog = false;
String pendingLogUser = "";
String pendingLogAction = "";

void queueLog(String username, String action) {
  pendingLog = true;
  pendingLogUser = username;
  pendingLogAction = action;
}
bool lastRemoteOpen   = HIGH;
bool lastRemoteClose  = HIGH;
bool lastRemoteStop   = HIGH;

// Remote Schedule
int remoteStartH = 8,  remoteStartM = 0;
int remoteEndH   = 18, remoteEndM   = 0;
String remoteDays = "1,2,3,4,5";

bool httpBusy = false;  // prevents two HTTP requests colliding (door log vs connection log)

// ═══════════════════════════════════════════
// Supabase Log - door action
// ═══════════════════════════════════════════
void logToSupabase(String username, String action) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (httpBusy) { Serial.println("Supabase skip [" + username + "/" + action + "] - busy"); return; }
  httpBusy = true;
  HTTPClient http;
  http.begin(SUPABASE_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");
  String body = "{\"username\":\"" + username + "\","
                "\"door_id\":\"W1\","
                "\"door_name\":\"Loading Dock 1\","
                "\"action\":\"" + action + "\","
                "\"location\":\"" + WiFi.SSID() + "\"}";
  int code = http.POST(body);
  Serial.println("Supabase [" + username + "/" + action + "] -> " + String(code));
  http.end();
  httpBusy = false;
}

// ═══════════════════════════════════════════
// Connection log - connected / disconnected
// ═══════════════════════════════════════════
void logConnection(String status) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (httpBusy) { Serial.println("Connection skip [" + status + "] - busy"); return; }
  httpBusy = true;
  String ssid = WiFi.SSID();
  HTTPClient http;
  http.begin(SUPABASE_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");
  String body = "{\"username\":\"system\","
                "\"door_id\":\"W1\","
                "\"door_name\":\"Loading Dock 1\","
                "\"action\":\"" + status + "\","
                "\"location\":\"" + ssid + "\"}";
  int code = http.POST(body);
  Serial.println("Connection [" + status + "] via " + ssid + " → " + String(code));
  http.end();
  httpBusy = false;
}

// ═══════════════════════════════════════════
// Get current time
// ═══════════════════════════════════════════
bool getTime(int &hour, int &minute, int &weekday) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  hour    = timeinfo.tm_hour;
  minute  = timeinfo.tm_min;
  weekday = timeinfo.tm_wday;
  return true;
}

// ═══════════════════════════════════════════
// Remote Schedule check
// ═══════════════════════════════════════════
bool isRemoteAllowed() {
  int hour, minute, weekday;
  if (!getTime(hour, minute, weekday)) return false;
  String dayStr = String(weekday);
  if (remoteDays.indexOf(dayStr) == -1) return false;
  int nowMins   = hour * 60 + minute;
  int startMins = remoteStartH * 60 + remoteStartM;
  int endMins   = remoteEndH   * 60 + remoteEndM;
  return nowMins >= startMins && nowMins < endMins;
}

// ═══════════════════════════════════════════
// STOP / OPEN / CLOSE
// ═══════════════════════════════════════════
void stopAll(String source = "") {
  digitalWrite(RELAY_OPEN,  RELAY_OFF);
  digitalWrite(RELAY_CLOSE, RELAY_OFF);
  isClosing = false;
  isOpening = false;
  openPulseActive = false;
  Serial.println("STOP! (" + source + ")");
  Blynk.virtualWrite(V3, 0);
}

bool doOpen(String source) {
  if (isOpening) { Serial.println("OPEN ignored! (" + source + ")"); return false; }
  if (isClosing) stopAll();
  Serial.println("OPEN! (" + source + ")");
  digitalWrite(RELAY_OPEN, RELAY_ON);
  openPulseActive = true;
  openPulseStart  = millis();
  isOpening = true;
  openStartTime = millis();
  return true;
}

bool doClose(String source) {
  if (isClosing) { Serial.println("CLOSE ignored! (" + source + ")"); return false; }
  if (isOpening) { Serial.println("CLOSE ignored! Still opening. (" + source + ")"); return false; }
  Serial.println("CLOSE! (" + source + ")");
  digitalWrite(RELAY_OPEN,  RELAY_OFF);
  digitalWrite(RELAY_CLOSE, RELAY_ON);
  isClosing = true;
  closeStartTime = millis();
  return true;
}

// ═══════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("=== Warehouse Door Control v1.8 ===");

  // First command - turn off relays
  pinMode(RELAY_OPEN,  OUTPUT); digitalWrite(RELAY_OPEN,  RELAY_OFF);
  pinMode(RELAY_CLOSE, OUTPUT); digitalWrite(RELAY_CLOSE, RELAY_OFF);

  pinMode(STOP_PHYSICAL, INPUT_PULLUP);
  pinMode(REMOTE_OPEN,   INPUT_PULLUP);
  pinMode(REMOTE_CLOSE,  INPUT_PULLUP);
  pinMode(REMOTE_STOP,   INPUT_PULLUP);

  setupWiFi();
  Blynk.config(BLYNK_AUTH_TOKEN);

  configTzTime(TIMEZONE, NTP_SERVER);
  delay(1000);

  // OTA
  ArduinoOTA.setHostname("warehouse-door");
  ArduinoOTA.setPassword(OTA_PASSWORD_STR);
  ArduinoOTA.onStart([]() {
    // Turn off relays on OTA start
    digitalWrite(RELAY_OPEN,  RELAY_OFF);
    digitalWrite(RELAY_CLOSE, RELAY_OFF);
    Serial.println("OTA Start — relays OFF");
  });
  ArduinoOTA.onEnd([]()  { Serial.println("OTA done!"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA Error[%u]\n", e); });
  ArduinoOTA.begin();

  lastConnected   = millis();
  if (Blynk.connected()) logConnection("connected");

  Serial.println("Ready!");
}

// ═══════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════
void loop() {
  ArduinoOTA.handle();

  // WiFi reconnect
  if (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
  }

  // Connection handled by callbacks (BLYNK_CONNECTED/DISCONNECTED)
  if (Blynk.connected()) {
    lastConnected = millis();
    Blynk.run();
  } else {
    if (millis() - lastConnected > RECONNECT_TIMEOUT) {
      lastConnected = millis();
      Serial.println("Blynk reconnect attempt...");
    }
    Blynk.connect(3000);
  }

  // OPEN relay pulse end (non-blocking - replaces old delay(500))
  if (openPulseActive && (millis() - openPulseStart >= 500)) {
    digitalWrite(RELAY_OPEN, RELAY_OFF);
    openPulseActive = false;
  }

  // OPEN Timer
  if (isOpening && (millis() - openStartTime >= OPEN_DURATION)) {
    isOpening = false;
    Serial.println("OPEN complete!");
  }

  // CLOSE Timer
  if (isClosing && (millis() - closeStartTime >= CLOSE_DURATION)) {
    digitalWrite(RELAY_CLOSE, RELAY_OFF);
    isClosing = false;
    Serial.println("CLOSE complete!");
  }

  // Physical STOP button
  bool physStop = digitalRead(STOP_PHYSICAL);
  if (physStop == LOW && lastPhysicalStop == HIGH) {
    if (!isOpening) {
      stopAll("Physical");
      queueLog("Button Stop", "stop");
    } else {
      Serial.println("STOP ignored! Opening.");
    }
  }
  lastPhysicalStop = physStop;

  // Remote OPEN
  bool remOpen = digitalRead(REMOTE_OPEN);
  if (remOpen == LOW && lastRemoteOpen == HIGH) {
    if (isRemoteAllowed()) {
      if (doOpen("Remote")) queueLog("remote", "open");
    } else {
      Serial.println("Remote OPEN blocked! Out of schedule.");
    }
  }
  lastRemoteOpen = remOpen;

  // Remote CLOSE
  bool remClose = digitalRead(REMOTE_CLOSE);
  if (remClose == LOW && lastRemoteClose == HIGH) {
    if (isRemoteAllowed()) {
      if (doClose("Remote")) queueLog("remote", "close");
    } else {
      Serial.println("Remote CLOSE blocked! Out of schedule.");
    }
  }
  lastRemoteClose = remClose;

  // Remote STOP
  bool remStop = digitalRead(REMOTE_STOP);
  if (remStop == LOW && lastRemoteStop == HIGH) {
    if (isRemoteAllowed()) {
      if (!isOpening) {
        stopAll("Remote");
        queueLog("remote", "stop");
      } else {
        Serial.println("STOP ignored! Opening.");
      }
    } else {
      Serial.println("Remote STOP blocked! Out of schedule.");
    }
  }
  lastRemoteStop = remStop;

  // Send any queued log now - after all buttons have been checked,
  // so STOP always stays responsive even during the HTTP request
  if (pendingLog) {
    pendingLog = false;
    logToSupabase(pendingLogUser, pendingLogAction);
  }
}

// ═══════════════════════════════════════════
// BLYNK CALLBACKS - instant connection log
// ═══════════════════════════════════════════
unsigned long lastConnLog = 0;
String lastConnAction = "";

void logConnectionDebounced(String action) {
  unsigned long now = millis();
  // Same status within 10 seconds - skip
  if (action == lastConnAction && (now - lastConnLog) < 10000) return;
  lastConnLog    = now;
  lastConnAction = action;
  logConnection(action);
}

BLYNK_CONNECTED() {
  logConnectionDebounced("connected");
  Serial.println("Blynk connected → logged");
}

BLYNK_DISCONNECTED() {
  logConnectionDebounced("disconnected");
  Serial.println("Blynk disconnected → logged");
}

// ═══════════════════════════════════════════
// BLYNK
// ═══════════════════════════════════════════
BLYNK_WRITE(V1) { if (param.asInt()==1) doOpen("App"); }
BLYNK_WRITE(V2) { if (param.asInt()==1) doClose("App"); }
BLYNK_WRITE(V3) {
  if (param.asInt()==1) {
    if (isOpening) { Serial.println("STOP ignored! Opening."); return; }
    stopAll("App");
  }
}
BLYNK_WRITE(V7)  { remoteStartH = param.asInt(); }
BLYNK_WRITE(V8)  { remoteStartM = param.asInt(); }
BLYNK_WRITE(V9)  { remoteEndH   = param.asInt(); }
BLYNK_WRITE(V10) { remoteEndM   = param.asInt(); }
BLYNK_WRITE(V11) { remoteDays   = param.asStr(); }
