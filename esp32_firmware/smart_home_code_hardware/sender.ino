#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= KY-037 =================
#define SOUND_ANALOG   34
#define SOUND_DIGITAL  35

// ================= OLED (I2C) =================
#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C   // some modules use 0x3D
#define SCREEN_W   128
#define SCREEN_H   64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool displayOk = false;

// ================= RECEIVER MAC =================
// Your Smart Home ESP32 MAC
uint8_t receiverMAC[] = {0xA0, 0xB7, 0x65, 0x48, 0x01, 0x10};

// ================= DATA =================
// Sound node -> main
typedef struct struct_message {
  int soundAnalog;
  int soundDigital;
} struct_message;

// Main -> sound node
typedef struct ip_message {
  uint8_t connected;
  char ip[16];
} ip_message;

struct_message dataToSend;
ip_message lastIp = {};
unsigned long lastIpTime = 0;
esp_now_peer_info_t peerInfo;

// ================= CHANNEL HOPPING =================
// The main ESP32 follows your router's channel. If sends keep failing,
// try the next channel until the main answers.
volatile int failCount = 0;
uint8_t channel = 1;
bool espReady = false;

// ================= CALLBACKS =================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
  if (status == ESP_NOW_SEND_SUCCESS) {
    failCount = 0;
  } else {
    failCount++;
  }
  Serial.print("Delivery: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len)
{
  if (len != sizeof(ip_message)) return;
  memcpy(&lastIp, data, sizeof(lastIp));
  lastIp.ip[15] = 0;
  lastIpTime = millis();
}

// ================= DISPLAY =================
void drawScreen(int analog)
{
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  bool fresh = (lastIpTime != 0) && (millis() - lastIpTime < 10000);

  if (!fresh) {
    display.println("Waiting for main...");
    display.setCursor(0, 14);
    display.println("(ESP-NOW link)");
  }
  else if (lastIp.connected) {
    display.println("ESP32 IP (home WiFi):");

    // Split "192.168.1.14" into "192.168." and "1.14" to use big text
    String ip = String(lastIp.ip);
    int p = ip.indexOf('.', ip.indexOf('.') + 1);
    String a = ip.substring(0, p + 1);
    String b = ip.substring(p + 1);

    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(a);
    display.setCursor(0, 30);
    display.print(b);
    display.setTextSize(1);
  }
  else {
    display.println("No home WiFi");
    display.setCursor(0, 14);
    display.println("Join hotspot:");
    display.setCursor(0, 24);
    display.println("SmartHome-Setup");
    display.setCursor(0, 38);
    display.print("IP ");
    display.println(lastIp.ip);
  }

  // Footer
  display.setCursor(0, 56);
  display.print("Snd:");
  display.print(analog);
  display.print("  ch:");
  display.print(channel);

  display.display();
}

// ================= SETUP =================
void setup()
{
  Serial.begin(115200);

  pinMode(SOUND_DIGITAL, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  displayOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayOk) {
    Serial.println("OLED not found (check wiring / address)");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Smart Home");
    display.println("Sound node starting");
    display.display();
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  Serial.println();
  Serial.println("===== ESP-NOW SENDER =====");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;      // current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer add failed");
    return;
  }

  espReady = true;
  Serial.println("Sender ready!");
}

// ================= LOOP =================
void loop()
{
  dataToSend.soundAnalog  = analogRead(SOUND_ANALOG);
  dataToSend.soundDigital = digitalRead(SOUND_DIGITAL);

  if (espReady)
  {
    // Main not answering on this channel -> try the next one
    if (failCount >= 3) {
      channel = (channel % 13) + 1;
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      failCount = 0;
      Serial.printf("Hopping to channel %d\n", channel);
    }

    esp_err_t result = esp_now_send(receiverMAC,
                                    (uint8_t *)&dataToSend,
                                    sizeof(dataToSend));
    if (result == ESP_OK) {
      Serial.print("Queued -> Analog: ");
      Serial.print(dataToSend.soundAnalog);
      Serial.print("  Digital: ");
      Serial.println(dataToSend.soundDigital);
    } else {
      Serial.print("esp_now_send() failed: ");
      Serial.println(result);
    }
  }

  drawScreen(dataToSend.soundAnalog);
  delay(1000);
}
