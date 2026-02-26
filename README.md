<p align="center">
  <img src="https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_1.png" width="300" alt="HamView on SenseCAP Indicator">
  <img src="https://github.com/user-attachments/assets/d6ae8685-5951-40a8-96d0-46f06478e5ee" width="300">
</p>

<div align="center">

# **HamView for SenseCAP Indicator**

A feature-rich ham radio companion display for the Seeed Studio SenseCAP Indicator.

**Real-time HamAlert.org spots • ICOM IC-705 integration • Weather & propagation data**<br>
(***HamAlert Spots work, but IC-705 intergration as not been sucessful yet, and I also don't think Wx/propagation works***)
</div>

<p align="center">
  <a href="https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32/">
    <img src="https://img.shields.io/badge/esp--idf-v5.1-00b202" alt="ESP-IDF v5.1">
  </a>
  <a href="https://raw.githubusercontent.com/RF-YVY/HamView_SenseCAP/main/LICENSE">
    <img src="https://img.shields.io/badge/license-Apache%202.0-blue" alt="license">
  </a>
</p>

---

## Features

### 📡 Live DX Spot Feed
- **HamAlert Integration** - Connects to [HamAlert.org](https://hamalert.org) for real-time DX cluster spots
- **Customizable Filters** - Filter spots by callsign, band, and mode (CW, Digital, Voice)
- **Spot Age Control** - Configure how long spots remain visible (TTL)
- **Priority Alerts** - Set up audio alerts for specific callsigns, states, or countries
- **Activity Charts** - Visual timeline showing spot activity over time with mode distribution

### 📻 ICOM IC-705 Integration
  (not successfully completed)
- **Real-time Radio Display** - Shows frequency, mode, and S-meter readings from your IC-705
- **Bluetooth & WiFi Support** - Connect via BLE or CI-V over WiFi
- **S-Meter Visualization** - Live signal strength bar display

### 🌤️ Weather Information
  (may not work, I can't remember)
- **Current Conditions** - Temperature, humidity, wind, and weather description
- **12-Hour Forecast** - Hourly temperature and precipitation predictions
- **Severe Weather Alerts** - Automatic display of NWS weather warnings
- **Sunrise/Sunset Times** - Perfect for grey-line propagation planning
- **Lightning & High Wind Warnings** - Know when to disconnect your antenna!

### 📊 Additional Features
- **RF Radar Panel** - Scan and display nearby WiFi networks, BLE devices, and LoRa activity
- **Event Logging** - System event log for troubleshooting
- **Adjustable Display** - Screen brightness control and auto-sleep timeout
- **Temperature Units** - Switch between Fahrenheit and Celsius
- **Time Formats** - 12-hour or 24-hour clock display
- **Touch Interface** - Full touchscreen UI built with LVGL

---

## Hardware Requirements

- **Seeed Studio SenseCAP Indicator** (D1 or D1L model)
- WiFi network connection
- (Optional) ICOM IC-705 for radio integration

<div align="center"><img width="400" src="https://files.seeedstudio.com/wiki/SenseCAP/SenseCAP_Indicator/SenseCAP_Indicator_1.png"/></div>

<p align="center"><a href="https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html"><img src="https://files.seeedstudio.com/wiki/RS485_500cm%20ultrasonic_sensor/image%202.png" border="0" /></a></p>

---

## Installation

### Prerequisites

> [!Warning]  
> **ESP-IDF version `v5.1.x` is required.** Do not use any lower or higher versions.

1. Install the [ESP-IDF v5.1](https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32/get-started/index.html#installation-step-by-step) development framework

### Build & Flash

1. **Clone this repository:**
   ```bash
   git clone https://github.com/RF-YVY/HamView_SenseCAP.git
   cd HamView_SenseCAP
   ```

2. **Navigate to the HamView example:**
   ```bash
   cd examples/hamview
   ```

3. **Set up ESP-IDF environment:**
   - **Windows:** Run the ESP-IDF PowerShell or Command Prompt from the Start menu
   - **Linux/macOS:** Run `. ~/esp/esp-idf/export.sh` (adjust path as needed)

4. **Build the firmware:**
   ```bash
   idf.py build
   ```

5. **Connect your SenseCAP Indicator via USB-C** (use the ESP32 port, not the RP2040 port)

6. **Flash the firmware:**
   ```bash
   idf.py -p COM_PORT flash
   ```
   Replace `COM_PORT` with your actual port (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux)

7. **Monitor serial output (optional):**
   ```bash
   idf.py -p COM_PORT monitor
   ```

For detailed flashing instructions, see the [SenseCAP Indicator Flashing Guide](https://wiki.seeedstudio.com/SenseCAP_Indicator_How_To_Flash_The_Default_Firmware/).

---

## Configuration

After flashing, tap the **Settings** button (gear icon) on the touchscreen to configure:

### HamAlert Settings
| Setting | Description |
|---------|-------------|
| **Username** | Your HamAlert.org username |
| **Password** | Your HamAlert.org password |
| **Callsign Filter** | Filter spots for specific callsigns |
| **Band Filter** | Limit spots to specific bands |
| **Spot TTL** | How long spots stay visible (minutes) |
| **Spot Age Filter** | Only show spots newer than X minutes |

### Alert Settings
| Setting | Description |
|---------|-------------|
| **Alert Callsigns** | Comma-separated callsigns to trigger alerts |
| **Alert States** | US states to trigger alerts (e.g., "CA,TX,NY") |
| **Alert Countries** | Countries to trigger alerts |

### ICOM IC-705 Settings
| Setting | Description |
|---------|-------------|
| **WiFi Enabled** | Enable CI-V over WiFi connection |
| **IP Address** | IC-705 WiFi IP address |
| **Port** | CI-V port (default: 50001) |
| **Username/Password** | IC-705 WiFi credentials |

### Display Settings
| Setting | Description |
|---------|-------------|
| **Screen Brightness** | Display brightness percentage |
| **Screen Timeout** | Auto-sleep after X minutes of inactivity |
| **Weather ZIP** | US ZIP code for local weather |

---

## User Interface

HamView features a tabbed interface with four main sections:

1. **Spots Tab** - Live DX spot table with filtering and activity charts
2. **Weather Tab** - Current conditions, forecast, and alerts
3. **IC-705 Tab** - Radio integration display
4. **Radar Tab** - RF environment scanning (WiFi/BLE/LoRa)

### Status Bar
The top status bar shows:
- WiFi connection status and signal strength
- HamAlert connection status
- Current local time and UTC time

---

## Credits

- Based on the [SenseCAP Indicator ESP32 SDK](https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32) by Seeed Studio
- Weather data provided by the National Weather Service API
- DX spots provided by [HamAlert.org](https://hamalert.org)
- UI built with [LVGL](https://lvgl.io/)

---

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

## SenseCAP Indicator Hardware

The SenseCAP Indicator is a 4-inch touch screen driven by ESP32-S3 and RP2040 Dual-MCU with Wi-Fi/Bluetooth/LoRa support.

### Key Specifications
- **Display:** 4-inch 480x480 IPS touchscreen
- **MCUs:** ESP32-S3 + RP2040 dual processors
- **Connectivity:** WiFi, Bluetooth, LoRa (optional)
- **Interfaces:** 2x Grove ports (ADC/I2C), 2x USB-C
- **Sensors:** Built-in tVOC and CO2 sensors

