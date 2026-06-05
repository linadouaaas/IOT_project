# IOT_project

---
![Project Banner](images/monitoring (2).png)
##  Project Info

- **Course/Context:** iot project 
- **Institution:** usthb
- **Date:** 05 June 2025
- **Supervisor:** Dr HARKAT Yacine 

---

## done by 4th year students :

### BOUZIDI Sara
### BOUTAMINE Lina 
### RAMDANI Ilhem 
### AHMED SID Lina Douaa

# 🏠 ESP32 Smart Home System

A complete offline smart home automation solution built with **ESP32** firmware and a **Flutter** mobile app. Control up to 15 relays, monitor environmental sensors, and automate lighting with motion detection — all without cloud dependency.


## 🔭 Overview

This project creates a standalone smart home controller using an ESP32 microcontroller paired with a cross-platform Flutter mobile application. The system operates entirely on your local WiFi network — no internet connection or cloud services required. 

The ESP32 manages 15 relay channels for controlling lights, outlets, and switches, while reading temperature, humidity, gas levels, and motion detection. The Flutter app provides real-time monitoring, manual control, room-based organization, and automatic theme switching.

---

##  Features

### 🔌 Device Control
- **15 relay channels** (K1-K15) individually controllable
- Room-based organization (Living Room, Bedroom, Kitchen, etc.)
- Device types: Lamps 💡, Switches 🔌, Outlets ⚡
- Bulk "Turn All Off" per room
- Real-time status polling (1-second refresh)

### 🌡️ Environmental Monitoring
- **DHT11** temperature & humidity sensor
- **MQ gas sensor** with configurable leak threshold
- **Buzzer alarm** triggers on dangerous gas levels
- Visual alerts in app (orange for high temp, red for gas leak)

### 🏃 Motion Detection
- **PIR sensor** for automatic motion-triggered lighting
- Auto-turns on relays K1-K4 for 30 seconds when motion detected
- Toggle motion mode ON/OFF from app
- Visual motion status indicator

### 🎨 App Features
- **Dark/Light theme** with automatic time-based switching (6AM-6PM light)
- **mDNS auto-discovery** — finds ESP32 without entering IP
- **Offline-first** — all data stored locally on device
- **Customizable rooms & devices** — add, rename, delete
- **Connection status** indicator (Online/Offline)

---

## 🔧 Hardware Components

| Component | Purpose | Pin |
|-----------|---------|-----|
| ESP32 DevKit | Main microcontroller | — |
| DHT11 | Temperature & humidity | GPIO 4 |
| MQ-2/MQ-5 Gas Sensor | Gas leak detection | GPIO 34 (ADC) |
| HC-SR501 PIR | Motion detection | GPIO 35 |
| Active Buzzer | Gas alarm | GPIO 25 |
| 16-Channel Relay Module | Device switching | GPIO 13, 2, 14, 27, 26, 33, 32, 16, 23, 22, 21, 19, 18, 5, 17 |

### Relay Pin Mapping
```
K1  → GPIO 13    K6  → GPIO 33    K11 → GPIO 21
K2  → GPIO 2     K7  → GPIO 32    K12 → GPIO 19
K3  → GPIO 14    K8  → GPIO 16    K13 → GPIO 18
K4  → GPIO 27    K9  → GPIO 23    K14 → GPIO 5
K5  → GPIO 26    K10 → GPIO 22    K15 → GPIO 17
```

---

## 🔌 Circuit Diagram

```
ESP32 DevKit V1
┌─────────────────┐
│ 3.3V ──┬────────┤
│        │        │
│ GPIO 4 ├───────►│ DHT11 (Temp/Hum)
│ GPIO 34├───────►│ MQ Gas Sensor (AO)
│ GPIO 35├───────►│ PIR Sensor (OUT)
│ GPIO 25├───────►│ Buzzer (+)
│        │        │
│ GPIO 13├───────►│ Relay K1
│ GPIO 2 ├───────►│ Relay K2
│ ...    │        │ ... (15 relays total)
│ GPIO 17├───────►│ Relay K15
│        │        │
│ 5V ────┼────────┤◄─── Relay Module VCC
│ GND ───┴────────┤◄─── Common GND
└─────────────────┘

Power Supply: 5V/2A minimum for relay module
              ESP32 via USB or 3.3V regulator
```

---

## 🏗️ Software Architecture

```
┌─────────────────────────────────────────┐
│           FLUTTER MOBILE APP            │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │Dashboard│ │ Settings│ │Edit Rooms│  │
│  └────┬────┘ └────┬────┘ └────┬─────┘  │
│       └─────────────┴───────────┘       │
│              Services Layer             │
│    ┌──────────┐ ┌──────────┐            │
│    │ESPService│ │Storage   │            │
│    │(HTTP)    │ │Service   │            │
│    └────┬─────┘ │(Local DB)│            │
│         └───────┴──────────┘            │
└─────────────────┬───────────────────────┘
                  │ HTTP (WiFi LAN)
┌─────────────────▼───────────────────────┐
│           ESP32 FIRMWARE                │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐  │
│  │WebServer│ │ Sensors │ │  Relays  │  │
│  │ (API)   │ │ (DHT11) │ │ (GPIO)   │  │
│  └─────────┘ │ (MQ)    │ └──────────┘  │
│              │ (PIR)   │               │
│              └─────────┘               │
└─────────────────────────────────────────┘
```

---

## 📡 API Reference

The ESP32 exposes a REST API on port 80:

### `GET /api/status`
Returns current sensor readings and relay states.

**Response:**
```json
{
  "t": 24.5,           // Temperature (°C)
  "h": 60.0,           // Humidity (%)
  "g": 800,            // Gas level (0-4095)
  "m": 1,              // Motion mode active (1/0)
  "p": 0,              // PIR current reading (1/0)
  "r": [1,1,0,0,0,0,0,0,0,0,0,0,0,0,0]  // Relay states (15)
}
```

### `GET /api/toggle?r=N`
Toggle relay N (0-14) ON/OFF.

**Response:** Same as `/api/status`

### `GET /api/alloff`
Turn all relays OFF.

**Response:** Same as `/api/status`

### `GET /api/motion`
Toggle motion detection mode ON/OFF.

**Response:**
```json
{"motion": 1}  // New motion mode state
```

---

## 🚀 Getting Started

### Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
- [Flutter SDK](https://flutter.dev/docs/get-started/install)
- Android Studio / VS Code with Flutter extension
- ESP32 DevKit V1
- Hardware components listed above

### ESP32 Setup

1. **Install libraries** in Arduino IDE:
   - `DHT sensor library` by Adafruit
   - `ESPmDNS` (built-in with ESP32 core)

2. **Update WiFi credentials** in `smart_home_esp32.ino`:
   ```cpp
   const char* ssid = "WIFI_SSID";
   const char* password = "PASSWORD_OF_PASSWORD";
   ```

3. **Upload firmware** to ESP32:
   - Board: "ESP32 Dev Module"
   - Upload speed: 921600
   - Flash frequency: 80MHz

4. **Open Serial Monitor** (115200 baud) to view IP address

### Flutter App Setup

1. **Navigate to project directory:**
   ```bash
   cd smart_home_app
   ```

2. **Install dependencies:**
   ```bash
   flutter pub get
   ```

3. **Run the app:**
   ```bash
   flutter run
   ```

4. **First launch:**
   - Enter ESP32 IP address OR
   - Tap "Auto Discover" to find via mDNS (`esp32-smart-home.local`)

---

## 📁 Project Structure

```
smart-home-system/
├── esp32_firmware/
│   └── smart_home_esp32.ino      # ESP32 Arduino sketch
│
├── smart_home_app/
│   ├── android/                   # Android-specific config
│   ├── ios/                       # iOS-specific config
│   ├── lib/
│   │   ├── main.dart              # App entry point, theme management
│   │   ├── models/
│   │   │   ├── device.dart        # Device data model
│   │   │   └── room.dart          # Room data model
│   │   ├── screens/
│   │   │   ├── config_screen.dart     # Initial IP configuration
│   │   │   ├── dashboard_screen.dart  # Main control panel
│   │   │   ├── room_screen.dart       # Per-room device control
│   │   │   ├── edit_rooms_screen.dart # Room management
│   │   │   ├── edit_devices_screen.dart # Device management
│   │   │   └── settings_screen.dart   # App settings
│   │   └── services/
│   │       ├── esp_service.dart       # HTTP communication with ESP32
│   │       ├── storage_service.dart   # Local data persistence
│   │       └── theme_service.dart     # Theme preferences
│   │
│   ├── test/
│   │   └── widget_test.dart       # Basic widget tests
│   └── pubspec.yaml               # Dependencies
│
└── README.md                      # This file
```

### Dependencies (`pubspec.yaml`)
```yaml
dependencies:
  flutter:
    sdk: flutter
  http: ^1.1.0              # HTTP requests to ESP32
  shared_preferences: ^2.2.0 # Local storage
  multicast_dns: ^0.3.0      # mDNS discovery
```


## Contributions

This project was developed as a complete IoT solution combining embedded systems and mobile development.

### What We Built

| Component | Description |
|-----------|-------------|
| **ESP32 Firmware** | Custom C++ firmware handling sensor reading, relay control, motion automation, and HTTP API server |
| **Flutter Mobile App** | Cross-platform UI with real-time polling, local persistence, room management, and adaptive theming |
| **Hardware Integration** | Physical wiring of 15 relay channels with sensors on ESP32 GPIO |
| **mDNS Discovery** | Zero-configuration device discovery for seamless setup |
| **Motion Automation** | Autonomous PIR-triggered lighting with configurable timeout |
| **Safety Features** | Gas leak detection with audible alarm and visual app alerts |

### Key Technical Achievements
-  **Sub-second response** via local HTTP polling
-  **100% offline operation** — no cloud dependency
-  **Adaptive UI** with automatic dark/light theme switching
-  **Cross-platform** — single Flutter codebase for Android & iOS
-  **Extensible architecture** — easily add more rooms/devices/relays

---

## 📝 License

This project is open source under the [MIT License](LICENSE).

---

## 🙏 Acknowledgments

- [Adafruit DHT Library](https://github.com/adafruit/DHT-sensor-library)
- [Flutter Team](https://flutter.dev)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

