/*
  Smart Home ESP32 - main controller (AP_STA stable edition)
  REQUIRED: ArduinoJson, DHT sensor library (Adafruit), Adafruit Unified Sensor
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================= SUPABASE (fallback values) =================
// The app can overwrite these at runtime (saved in /supabase.json).
const char* SUPABASE_URL_DEFAULT      = "https://wzxulnazpomnrcwpagpm.supabase.co";
const char* SUPABASE_ANON_KEY_DEFAULT = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Ind6eHVsbmF6cG9tbnJjd3BhZ3BtIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODUwODU1MTMsImV4cCI6MjEwMDY2MTUxM30.jLlKiKDV7xeMuOwG8pTnFVBWNJoe78cPAoyb8JjdbDk";
const unsigned long MOTION_SYNC_INTERVAL = 10000;

// Secret word the app must send to change the Supabase config.
// CHANGE THIS, and type the same word in the app.
const char* CONFIG_TOKEN = "change-me-1234";

// ================= HOTSPOT (always on) =================
const char* AP_SSID = "SmartHome-Setup";
const char* AP_PASS = "12345678";

// ================= PINS =================
#define DHTPIN      4
#define DHTTYPE     DHT11
#define MQ_PIN      34
#define BUZZER      25
#define PIR_PIN     35

#define R1  13
#define R2  2
#define R3  14
#define R4  27
#define R5  26
#define R6  33
#define R7  32
#define R8  16
#define R9  23
#define R10 22
#define R11 21
#define R12 19
#define R13 18
#define R14 5
#define R15 17
#define R16 15

const int relayPins[16] = {R1,R2,R3,R4,R5,R6,R7,R8,R9,R10,R11,R12,R13,R14,R15,R16};
bool relayState[16] = {false};

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

const char* WIFI_FILE     = "/wifi.json";
const char* MOTION_FILE   = "/motion_cache.json";
const char* SUPABASE_FILE = "/supabase.json";

// Active Supabase config (default or the one sent by the app)
String supabaseUrl;
String supabaseKey;
bool motionSyncNow = false;   // force a sync right after the config changes

float currentHum  = 0;
float currentTemp = 0;
int   currentGas  = 0;
int   currentSoundAnalog  = 0;
int   currentSoundDigital = 0;

bool motionDetected = false;
unsigned long motionStartTime = 0;
const unsigned long MOTION_DURATION = 30000;

// ================= ESP-NOW =================
// Sound node -> main
typedef struct struct_message {
  int soundAnalog;
  int soundDigital;
} struct_message;
struct_message incomingData;

// Main -> sound node (which has the OLED display)
typedef struct ip_message {
  uint8_t connected;   // 1 = connected to home WiFi
  char ip[16];         // e.g. "192.168.1.14"
} ip_message;

uint8_t senderMac[6];            // learned from the first sound packet
bool senderSeen = false;
bool senderPeerAdded = false;

void onEspNowRecv(const esp_now_recv_info *info,
                  const uint8_t *data,
                  int len)
{
  if (len != sizeof(incomingData))
  {
    Serial.println("Wrong packet size");
    return;
  }

  memcpy(&incomingData, data, sizeof(incomingData));

  // Remember who sent it so we can send the IP back
  memcpy(senderMac, info->src_addr, 6);
  senderSeen = true;

  currentSoundAnalog = incomingData.soundAnalog;
  currentSoundDigital = incomingData.soundDigital;

  Serial.println();
  Serial.println("========== ESP-NOW ==========");

  Serial.printf("Sender: %02X:%02X:%02X:%02X:%02X:%02X\n",
                info->src_addr[0],
                info->src_addr[1],
                info->src_addr[2],
                info->src_addr[3],
                info->src_addr[4],
                info->src_addr[5]);

  Serial.print("Analog : ");
  Serial.println(currentSoundAnalog);

  Serial.print("Digital: ");
  Serial.println(currentSoundDigital);

  Serial.println("=============================");
}

// Sends the current IP to the sound node every 3 seconds
void sendIpToDisplay() {
  static unsigned long lastSend = 0;
  if (!senderSeen || millis() - lastSend < 3000) return;
  lastSend = millis();

  if (!senderPeerAdded) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, senderMac, 6);
    peer.channel = 0;              // follow the current channel
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;
    esp_err_t e = esp_now_add_peer(&peer);
    if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) {
      Serial.println("[ESP-NOW] add display peer failed");
      return;
    }
    senderPeerAdded = true;
  }

  ip_message msg = {};
  if (WiFi.status() == WL_CONNECTED) {
    msg.connected = 1;
    WiFi.localIP().toString().toCharArray(msg.ip, sizeof(msg.ip));
  } else {
    msg.connected = 0;
    strcpy(msg.ip, "192.168.4.1");
  }
  esp_now_send(senderMac, (uint8_t*)&msg, sizeof(msg));
}

// ================= LITTLEFS =================
bool readJson(const char* path, JsonDocument& doc) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  return !err;
}

bool writeJson(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// ================= SUPABASE CONFIG =================
void loadSupabaseConfig() {
  supabaseUrl = SUPABASE_URL_DEFAULT;
  supabaseKey = SUPABASE_ANON_KEY_DEFAULT;

  StaticJsonDocument<1024> doc;
  if (readJson(SUPABASE_FILE, doc)) {
    const char* u = doc["url"];
    const char* k = doc["key"];
    if (u && k && strlen(u) > 0 && strlen(k) > 0) {
      supabaseUrl = u;
      supabaseKey = k;
    }
  }
  Serial.print("[Supabase] Using: ");
  Serial.println(supabaseUrl);
}

// POST /api/supabase/config   fields: token, url, key   (or token + reset=1)
void handleSupabaseConfig() {
  if (!server.hasArg("token") || server.arg("token") != CONFIG_TOKEN) {
    server.send(401, "application/json", "{\"success\":false,\"error\":\"Wrong token\"}");
    return;
  }

  if (server.hasArg("reset")) {
    LittleFS.remove(SUPABASE_FILE);
    LittleFS.remove(MOTION_FILE);   // cache belongs to the old project
    loadSupabaseConfig();
    motionSyncNow = true;
    server.send(200, "application/json", "{\"success\":true,\"mode\":\"default\"}");
    return;
  }

  String url = server.arg("url");
  String key = server.arg("key");
  url.trim();
  key.trim();

  if (!url.startsWith("https://") || key.length() < 20) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid url or key\"}");
    return;
  }

  StaticJsonDocument<1024> doc;
  doc["url"] = url;
  doc["key"] = key;
  if (!writeJson(SUPABASE_FILE, doc)) {
    server.send(500, "application/json", "{\"success\":false,\"error\":\"Could not save\"}");
    return;
  }

  LittleFS.remove(MOTION_FILE);     // cache belongs to the old project
  loadSupabaseConfig();
  motionSyncNow = true;
  server.send(200, "application/json", "{\"success\":true,\"mode\":\"custom\"}");
}

// ================= WIFI (NON-BLOCKING) =================
void loadAndConnectWifi() {
  StaticJsonDocument<256> doc;
  if (readJson(WIFI_FILE, doc)) {
    const char* ssid = doc["ssid"];
    const char* pass = doc["pass"];
    if (ssid && strlen(ssid) > 0) {
      Serial.printf("[WIFI] Will connect to: %s\n", ssid);
      WiFi.begin(ssid, pass);
    }
  }
}

// Call this every loop(). It reconnects without ever blocking.
void manageWiFi() {
  static unsigned long lastAttempt = 0;
  static bool wasConnected = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      wasConnected = true;
      Serial.print("[WIFI] Connected! IP: ");
      Serial.println(WiFi.localIP());

      // Force AP to same channel as router so it stays visible
      int ch = WiFi.channel();
      if (ch > 0) {
        WiFi.softAP(AP_SSID, AP_PASS, ch, 0, 4);
        Serial.printf("[AP] Synced to router channel %d\n", ch);
      }
    }
    return;
  }

  // We are disconnected
  wasConnected = false;

  // If WiFi is currently idle/disconnected, retry every 10s
  if (millis() - lastAttempt < 10000) return;
  lastAttempt = millis();

  StaticJsonDocument<256> doc;
  if (readJson(WIFI_FILE, doc)) {
    const char* ssid = doc["ssid"];
    const char* pass = doc["pass"];
    if (ssid && strlen(ssid) > 0) {
      Serial.printf("[WIFI] Retrying %s...\n", ssid);
      WiFi.begin(ssid, pass);
    }
  }
}

void handleWifiConfig() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing ssid\"}");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid;
  doc["pass"] = pass;
  writeJson(WIFI_FILE, doc);

  server.send(200, "application/json", "{\"success\":true}");

  // NON-BLOCKING: just disconnect STA and let manageWiFi() reconnect in loop()
  Serial.println("[WIFI] New credentials saved. Reconnecting...");
  WiFi.disconnect(false, false); // keep AP alive, don't erase flash
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

// ================= MOTION =================
bool motionEnabledGlobal() {
  StaticJsonDocument<512> doc;
  if (!readJson(MOTION_FILE, doc)) return false;
  return (bool)doc["enabled"];
}

bool relayIsMotionControlled(int relayIndex) {
  StaticJsonDocument<512> doc;
  if (!readJson(MOTION_FILE, doc)) return false;
  if (!(bool)doc["enabled"]) return false;
  for (JsonVariant v : doc["relays"].as<JsonArray>()) {
    if ((int)v == relayIndex) return true;
  }
  return false;
}

void syncMotionFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = supabaseUrl + "/rest/v1/motion_config?id=eq.1&select=enabled,relays";
  if (!http.begin(client, url)) return;
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", String("Bearer ") + supabaseKey);

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, payload)) {
      JsonArray arr = doc.as<JsonArray>();
      if (arr.size() > 0) {
        StaticJsonDocument<512> cache;
        cache["enabled"] = arr[0]["enabled"];
        JsonArray relays = cache.createNestedArray("relays");
        for (JsonVariant v : arr[0]["relays"].as<JsonArray>()) relays.add(v.as<int>());
        writeJson(MOTION_FILE, cache);
      }
    }
  } else {
    Serial.printf("[Supabase] motion sync failed HTTP %d (using cache)\n", code);
  }
  http.end();
}

// ================= API =================
void handleStatus() {
  DynamicJsonDocument doc(1024);
  doc["t"] = currentTemp;
  doc["h"] = currentHum;
  doc["g"] = currentGas;
  doc["soundA"] = currentSoundAnalog;
  doc["soundD"] = currentSoundDigital;
  doc["m"] = motionEnabledGlobal() ? 1 : 0;
  doc["p"] = motionDetected ? 1 : 0;
  JsonArray r = doc.createNestedArray("r");
  for (int i = 0; i < 16; i++) r.add(relayState[i] ? 1 : 0);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleToggle() {
  int r = server.arg("r").toInt();
  if (r >= 0 && r < 16) {
    relayState[r] = !relayState[r];
    digitalWrite(relayPins[r], relayState[r]);
  }
  handleStatus();
}

void handleAllOff() {
  for (int i = 0; i < 16; i++) {
    relayState[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
  handleStatus();
}

// ================= MOTION LOGIC =================
void checkMotion() {
  if (!motionEnabledGlobal()) return;
  int pir = digitalRead(PIR_PIN);

  if (pir == HIGH && !motionDetected) {
    motionDetected = true;
    for (int i = 0; i < 16; i++) {
      if (relayIsMotionControlled(i)) {
        relayState[i] = true;
        digitalWrite(relayPins[i], HIGH);
      }
    }
    motionStartTime = millis();
  }

  if (motionDetected && millis() - motionStartTime > MOTION_DURATION) {
    for (int i = 0; i < 16; i++) {
      if (relayIsMotionControlled(i)) {
        relayState[i] = false;
        digitalWrite(relayPins[i], LOW);
      }
    }
    motionDetected = false;
  }
}

// ================= SENSORS =================
void readSensors() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 2000) {
    lastRead = millis();
    currentTemp = dht.readTemperature();
    currentHum  = dht.readHumidity();
    currentGas  = analogRead(MQ_PIN);
    digitalWrite(BUZZER, currentGas > 3000 ? HIGH : LOW);
  }
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed!");
  }

  loadSupabaseConfig();   // default, or the one sent by the app

  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER, OUTPUT);
  for (int i = 0; i < 16; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Start AP immediately with fixed IP so it never disappears
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(
    IPAddress(192,168,4,1),
    IPAddress(192,168,4,1),
    IPAddress(255,255,255,0)
  );
  WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);
  Serial.print("[AP] Hotspot IP: ");
  Serial.println(WiFi.softAPIP());

  // Load home WiFi credentials and connect (non-blocking)
  loadAndConnectWifi();

  // ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("[ESP-NOW] Receiver ready");
  } else {
    Serial.println("[ESP-NOW] init failed");
  }

  // Server
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.on("/api/alloff", HTTP_GET, handleAllOff);
  server.on("/api/wifi/config", HTTP_POST, handleWifiConfig);
  server.on("/api/supabase/config", HTTP_POST, handleSupabaseConfig);
  server.begin();
}

void loop() {
  server.handleClient();
  manageWiFi(); // handles reconnection without blocking

  static bool mdnsStarted = false;
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("esp32-smart-home")) {
      Serial.println("[mDNS] esp32-smart-home.local ready");
      mdnsStarted = true;
    }
  }

  static unsigned long lastMotionSync = 0;
  if (motionSyncNow || millis() - lastMotionSync > MOTION_SYNC_INTERVAL) {
    lastMotionSync = millis();
    motionSyncNow = false;
    syncMotionFromSupabase();
  }

  checkMotion();
  readSensors();
  sendIpToDisplay(); // push IP to the sound node's OLED
}
