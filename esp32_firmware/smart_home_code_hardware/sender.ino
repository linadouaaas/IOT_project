#include <WiFi.h>
#include <esp_now.h>

// ================= KY-037 =================
#define SOUND_ANALOG   34
#define SOUND_DIGITAL  35

// ================= RECEIVER MAC =================
// Your Smart Home ESP32 MAC
uint8_t receiverMAC[] = {0xA0, 0xB7, 0x65, 0x48, 0x01, 0x10};

// ================= DATA =================
typedef struct struct_message {
  int soundAnalog;
  int soundDigital;
} struct_message;

struct_message dataToSend;
esp_now_peer_info_t peerInfo;

// ================= SEND CALLBACK =================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
  Serial.print("Delivery: ");

  if (status == ESP_NOW_SEND_SUCCESS)
    Serial.println("SUCCESS");
  else
    Serial.println("FAILED");
}

void setup()
{
  Serial.begin(115200);

  pinMode(SOUND_DIGITAL, INPUT);

  WiFi.mode(WIFI_STA);

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

  memset(&peerInfo, 0, sizeof(peerInfo));

  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer add failed");
    return;
  }

  Serial.println("Sender ready!");
}

void loop()
{
  dataToSend.soundAnalog  = analogRead(SOUND_ANALOG);
  dataToSend.soundDigital = digitalRead(SOUND_DIGITAL);

  esp_err_t result =
      esp_now_send(receiverMAC,
                   (uint8_t *)&dataToSend,
                   sizeof(dataToSend));

  if (result == ESP_OK)
  {
    Serial.print("Queued -> Analog: ");
    Serial.print(dataToSend.soundAnalog);
    Serial.print("  Digital: ");
    Serial.println(dataToSend.soundDigital);
  }
  else
  {
    Serial.print("esp_now_send() failed: ");
    Serial.println(result);
  }

  delay(1000);
}