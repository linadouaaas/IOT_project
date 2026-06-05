#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

const char* ssid = "WIFI_SSID";
const char* password = "PASSWORD_OF_WIFI";

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

#define R9   23
#define R10  22
#define R11  21
#define R12  19
#define R13  18
#define R14  5
#define R15  17

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

const int relayPins[15] = {R1,R2,R3,R4,R5,R6,R7,R8,R9,R10,R11,R12,R13,R14,R15};
bool relayState[15] = {false};

bool motionDetected = false;
bool motionActive = true;

unsigned long motionStartTime = 0;
const unsigned long MOTION_DURATION = 30000;

float currentTemp = 0;
float currentHum = 0;
int currentGas = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("  SMART HOME SYSTEM STARTING");
  Serial.println("========================================");
  
  dht.begin();
  Serial.println("[INIT] DHT11 initialized OK");
  
  pinMode(PIR_PIN, INPUT);
  Serial.println("[INIT] PIR sensor initialized OK");
  
  Serial.println("[INIT] Initializing 15 relay pins...");
  for (int i = 0; i < 15; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
    Serial.print("[INIT] Relay K");
    Serial.print(i + 1);
    Serial.print(" on GPIO ");
    Serial.print(relayPins[i]);
    Serial.println(" = OFF");
  }
  
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  Serial.println("[INIT] Buzzer initialized to OFF");
  
  // WiFi
  Serial.print("[WIFI] Connecting to: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("[WIFI] Connected successfully!");
    Serial.print("[WIFI] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] MAC Address: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("[WIFI] WARNING: Running without WiFi connection");
  }

  // mDNS
  if (MDNS.begin("esp32-smart-home")) {
    Serial.println("[mDNS] Responder started: esp32-smart-home.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("[mDNS] ERROR: Failed to start mDNS!");
  }

  // Web server
  Serial.println("[SERVER] Setting up web server...");
  server.on("/api/status", handleStatus);
  server.on("/api/toggle", handleToggle);
  server.on("/api/alloff", handleAllOff);
  server.on("/api/motion", handleMotionToggle);
  server.begin();
  Serial.println("[SERVER] Web server started on port 80");
  
  Serial.println("========================================");
  Serial.println("  SYSTEM READY");
  Serial.println("========================================");
  Serial.print("[STATUS] Motion detection: ");
  Serial.println(motionActive ? "ACTIVE" : "INACTIVE");
  Serial.print("[STATUS] PIR current reading: ");
  Serial.println(digitalRead(PIR_PIN));
  Serial.println("Waiting for requests...");
  Serial.println();
  Serial.println("========================================");
  Serial.print("ESP32 IP ADDRESS: ");
  Serial.println(WiFi.localIP());
  Serial.println("TYPE THIS IP IN YOUR APP");
  Serial.println("========================================");
}  

void loop() {
  server.handleClient();
  checkMotion();
  readSensors();
}

void readSensors() {
  static unsigned long lastRead = 0;
  static int readCount = 0;
  
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    readCount++;
    
    Serial.println("----------------------------------------");
    Serial.print("[SENSOR] Reading #");
    Serial.println(readCount);
    
    currentHum = dht.readHumidity();
    currentTemp = dht.readTemperature();
    
    if (isnan(currentHum) || isnan(currentTemp)) {
      Serial.println("[SENSOR] ERROR: DHT11 failed to read!");
    } else {
      Serial.print("[SENSOR] Temperature: ");
      Serial.print(currentTemp);
      Serial.println(" C");
      Serial.print("[SENSOR] Humidity: ");
      Serial.print(currentHum);
      Serial.println(" %");
    }
    
    currentGas = analogRead(MQ_PIN);
    Serial.print("[SENSOR] Gas level: ");
    Serial.println(currentGas);
    
    if (currentGas > 4095) {
      Serial.println("[ALERT] HIGH GAS DETECTED! Buzzer ON");
      digitalWrite(BUZZER, HIGH);
    } else {
      digitalWrite(BUZZER, LOW);
    }
    
    Serial.print("[RELAY] States: ");
    for (int i = 0; i < 15; i++) {
      Serial.print(relayState[i]);
      if (i < 14) Serial.print(",");
    }
    Serial.println();
    
    int pirReading = digitalRead(PIR_PIN);
    Serial.print("[MOTION] PIR reading: ");
    Serial.print(pirReading);
    Serial.print(" | Motion active: ");
    Serial.print(motionActive ? "YES" : "NO");
    Serial.print(" | Motion detected: ");
    Serial.println(motionDetected ? "YES" : "NO");
    
    Serial.println("----------------------------------------");
  }
}

void checkMotion() {
  int pirState = digitalRead(PIR_PIN);
  
  static int lastPirState = LOW;
  if (pirState != lastPirState) {
    lastPirState = pirState;
    Serial.print("[MOTION] PIR changed to: ");
    Serial.println(pirState == HIGH ? "HIGH (motion detected)" : "LOW (no motion)");
  }
  
  if (pirState == HIGH) {
    if (!motionDetected) {
      motionDetected = true;
      Serial.println("========================================");
      Serial.println("[MOTION] *** MOTION DETECTED! ***");
      Serial.println("========================================");
      
      if (motionActive) {
        Serial.println("[MOTION] Motion mode is ACTIVE -> turning on relays K1-K4");
        turnOnMotionRelays();
        motionStartTime = millis();
        Serial.print("[MOTION] Timer started: ");
        Serial.print(MOTION_DURATION / 1000);
        Serial.println(" seconds");
      } else {
        Serial.println("[MOTION] Motion mode is INACTIVE -> relays stay off");
      }
    }
  } else {
    if (motionDetected) {
      motionDetected = false;
      Serial.println("[MOTION] No motion detected");
    }
  }
  
  if (motionActive && motionStartTime > 0) {
    unsigned long elapsed = millis() - motionStartTime;
    unsigned long remaining = MOTION_DURATION - elapsed;
    
    if (elapsed >= MOTION_DURATION) {
      Serial.println("[MOTION] Timer expired! Turning off motion relays");
      turnOffMotionRelays();
      motionStartTime = 0;
    } else if (elapsed % 5000 < 100) {
      Serial.print("[MOTION] Timer: ");
      Serial.print(remaining / 1000);
      Serial.println(" seconds remaining");
    }
  }
}

void turnOnMotionRelays() {
  Serial.println("[RELAY] Turning ON motion relays:");
  for (int i = 0; i < 4; i++) {
    relayState[i] = true;
    digitalWrite(relayPins[i], HIGH);
    Serial.print("  K");
    Serial.print(i + 1);
    Serial.print(" (GPIO ");
    Serial.print(relayPins[i]);
    Serial.println(") = ON");
  }
}

void turnOffMotionRelays() {
  Serial.println("[RELAY] Turning OFF motion relays:");
  for (int i = 0; i < 4; i++) {
    relayState[i] = false;
    digitalWrite(relayPins[i], LOW);
    Serial.print("  K");
    Serial.print(i + 1);
    Serial.print(" (GPIO ");
    Serial.print(relayPins[i]);
    Serial.println(") = OFF");
  }
}

void handleStatus() {
  Serial.println("[HTTP] Request: GET /api/status");
  
  String j = "{\"t\":" + String(currentTemp) + 
             ",\"h\":" + String(currentHum) + 
             ",\"g\":" + String(currentGas) + 
             ",\"m\":" + String(motionActive ? "1" : "0") +
             ",\"p\":" + String(digitalRead(PIR_PIN)) +
             ",\"r\":[";
  for (int i = 0; i < 15; i++) {
    j += (relayState[i] ? "1" : "0");
    if (i < 14) j += ",";
  }
  j += "]}";
  
  Serial.print("[HTTP] Response: ");
  Serial.println(j);
  server.send(200, "application/json", j);
}

void handleToggle() {
  int r = server.arg("r").toInt();
  Serial.println("[HTTP] Request: GET /api/toggle?r=" + String(r));
  
  if (r >= 0 && r < 15) {
    relayState[r] = !relayState[r];
    digitalWrite(relayPins[r], relayState[r] ? HIGH : LOW);
    
    Serial.print("[RELAY] K");
    Serial.print(r + 1);
    Serial.print(" (GPIO ");
    Serial.print(relayPins[r]);
    Serial.print(") toggled to ");
    Serial.println(relayState[r] ? "ON" : "OFF");
  } else {
    Serial.print("[HTTP] ERROR: Invalid relay number: ");
    Serial.println(r);
  }
  
  handleStatus();
}

void handleAllOff() {
  Serial.println("[HTTP] Request: GET /api/alloff");
  Serial.println("[RELAY] Turning ALL relays OFF");
  
  for (int i = 0; i < 15; i++) {
    relayState[i] = false;
    digitalWrite(relayPins[i], LOW);
  }
  motionStartTime = 0;
  
  Serial.println("[RELAY] All relays are now OFF");
  handleStatus();
}

void handleMotionToggle() {
  Serial.println("[HTTP] Request: GET /api/motion");
  
  motionActive = !motionActive;
  Serial.print("[MOTION] Motion detection toggled to: ");
  Serial.println(motionActive ? "ACTIVE" : "INACTIVE");
  
  if (!motionActive) {
    Serial.println("[MOTION] Motion mode OFF -> turning off motion relays");
    turnOffMotionRelays();
    motionStartTime = 0;
  } else {
    Serial.println("[MOTION] Motion mode ON -> waiting for motion...");
  }
  
  String j = "{\"motion\":" + String(motionActive ? "1" : "0") + "}";
  Serial.print("[HTTP] Response: ");
  Serial.println(j);
  server.send(200, "application/json", j);
}
