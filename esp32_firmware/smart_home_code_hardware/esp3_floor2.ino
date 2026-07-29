#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// ================= HOTSPOT (for first setup) =================
const char* AP_SSID = "SmartHome-Relay2";
const char* AP_PASS = "12345678";

// ================= RELAY =================
#define RELAY1  13
// If you add more later:
// #define RELAY2  12
// #define RELAY3  14

const int relayPins[] = {RELAY1};
bool relayState[] = {false};
const int RELAY_COUNT = sizeof(relayPins) / sizeof(relayPins[0]);

WebServer server(80);
const char* WIFI_FILE = "/wifi.json";

// ================= WIFI =================
void loadAndConnectWifi() {
  if (!LittleFS.exists(WIFI_FILE)) return;
  File f = LittleFS.open(WIFI_FILE, "r");
  StaticJsonDocument<256> doc;
  deserializeJson(doc, f);
  f.close();
  const char* ssid = doc["ssid"];
  const char* pass = doc["pass"];
  if (ssid && strlen(ssid) > 0) {
    Serial.printf("[WIFI] Connecting to: %s\n", ssid);
    WiFi.begin(ssid, pass);
  }
}

void handleWifiConfig() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing ssid\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  doc["ssid"] = server.arg("ssid");
  doc["pass"] = server.hasArg("pass") ? server.arg("pass") : "";
  File f = LittleFS.open(WIFI_FILE, "w");
  serializeJson(doc, f);
  f.close();

  server.send(200, "application/json", "{\"success\":true}");
  Serial.println("[WIFI] New credentials saved. Reconnecting...");

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(doc["ssid"].as<String>().c_str(), doc["pass"].as<String>().c_str());
}

// ================= API =================
void handleStatus() {
  DynamicJsonDocument doc(256);
  JsonArray r = doc.createNestedArray("r");
  for (int i = 0; i < RELAY_COUNT; i++) r.add(relayState[i] ? 1 : 0);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleToggle() {
  int r = server.arg("r").toInt();
  if (r >= 0 && r < RELAY_COUNT) {
    relayState[r] = !relayState[r];
    digitalWrite(relayPins[r], relayState[r]);
  }
  handleStatus();
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed!");
  }

  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[AP] Hotspot IP: ");
  Serial.println(WiFi.softAPIP());

  loadAndConnectWifi();

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.on("/api/wifi/config", HTTP_POST, handleWifiConfig);
  server.begin();

  if (MDNS.begin("esp32-relay2")) {
    Serial.println("[mDNS] esp32-relay2.local ready");
  }
}

void loop() {
  server.handleClient();

  // Non-blocking reconnect
  static unsigned long lastAttempt = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastAttempt > 10000) {
    lastAttempt = millis();
    loadAndConnectWifi();
  }
}