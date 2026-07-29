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

// ================= SUPABASE =================
const char* SUPABASE_URL      = "https://wzxulnazpomnrcwpagpm.supabase.co";
const char* SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Ind6eHVsbmF6cG9tbnJjd3BhZ3BtIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODUwODU1MTMsImV4cCI6MjEwMDY2MTUxM30.jLlKiKDV7xeMuOwG8pTnFVBWNJoe78cPAoyb8JjdbDk";
const unsigned long MOTION_SYNC_INTERVAL = 10000;

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

const char* WIFI_FILE   = "/wifi.json";
const char* MOTION_FILE = "/motion_cache.json";

float currentHum  = 0;
float currentTemp = 0;
int   currentGas  = 0;
int   currentSoundAnalog  = 0;
int   currentSoundDigital = 0;

bool motionDetected = false;
unsigned long motionStartTime = 0;
const unsigned long MOTION_DURATION = 30000;

// ================= ESP-NOW =================
typedef struct struct_message {
  int soundAnalog;
  int soundDigital;
} struct_message;
struct_message incomingData;

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

      // 🔥 CRITICAL: Force AP to same channel as router so it stays visible
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

  // 🔥 NON-BLOCKING: just disconnect STA and let manageWiFi() reconnect in loop()
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

  String url = String(SUPABASE_URL) + "/rest/v1/motion_config?id=eq.1&select=enabled,relays";
  if (!http.begin(client, url)) return;
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

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
    currentTemp = dht.readTemperature();  // 🔥 FIX: was missing — that's why temp was 0
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
  server.begin();
}

void loop() {
  server.handleClient();
  manageWiFi(); // 🔥 handles reconnection without blocking

  static bool mdnsStarted = false;
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("esp32-smart-home")) {
      Serial.println("[mDNS] esp32-smart-home.local ready");
      mdnsStarted = true;
    }
  }

  static unsigned long lastMotionSync = 0;
  if (millis() - lastMotionSync > MOTION_SYNC_INTERVAL) {
    lastMotionSync = millis();
    syncMotionFromSupabase();
  }

  checkMotion();
  readSensors();
}