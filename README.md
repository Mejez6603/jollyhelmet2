# 🪖 Jolly Helmet: Automated Helmet Sanitation System

![ESP32](https://img.shields.io/badge/ESP32-Hardware-blue?style=for-the-badge&logo=espressif)
![C++](https://img.shields.io/badge/C++-Firmware-00599C?style=for-the-badge&logo=c%2B%2B)
![Blynk](https://img.shields.io/badge/Blynk-IoT_Cloud-10b981?style=for-the-badge&logo=blynk)
![HTML/JS](https://img.shields.io/badge/Web-Dashboard-E34F26?style=for-the-badge&logo=html5)

**Jolly Helmet** is a fully automated, IoT-connected helmet sanitization station. Built on a distributed network of ESP32 microcontrollers, it features a coin-operated payment system, RFID authentication, automated safety locks, an auto-refilling fluid system, and a responsive web dashboard for remote management.

---

## ✨ Key Features
*   **Automated Sanitation Cycle:** Uses Ultrasonic sensors to detect helmet placement, automatically locking the enclosure and triggering a timed UV + Humidifier cleaning cycle.
*   **Smart Payment & Access:** Integrates a coin slot with Accept/Refund servo routing, plus an RFID scanner with Guest, VIP (Free Cleaning), and Master (Debug/Admin) access tiers.
*   **Distributed ESP-NOW Architecture:** The system is split across four ESP32 boards communicating via high-speed, localized ESP-NOW radio, reducing wire clutter and isolating concerns.
*   **Bulletproof Auto-Refill Brain:** Tank A (Primary) is automatically refilled by Tank D (Reserve) using ultrasonic depth calculations. The logic actively protects the pumps from running dry.
*   **On-Board Touchscreen Debugger:** Node B features a multi-page TFT touchscreen UI allowing admins to manually test every relay, servo, pump, and sensor without a PC.
*   **Responsive Cloud Dashboard:** A custom HTML/JS web dashboard connected to the Blynk REST API provides real-time sensor monitoring, manual hardware overrides, and a live scrolling terminal log.

---

## 🏗️ System Architecture (The Nodes)

The system relies on a Master/Slave topology via MAC Address routing:

### 1. Node A (The Hardware Controller)
*Handles the primary physical interactions and sanitization.*
*   **Inputs:** RFID Scanner, Coin Slot, Ultrasonic Helmet Detection, Ultrasonic Alcohol Level (Tank A).
*   **Outputs:** Buzzer, Refund/Accept Servos, UV Lights, Humidifier.

### 2. Node B (The Door & UI Controller)
*Handles security, enclosure state, and the local admin touchscreen.*
*   **Inputs:** Enclosure/Panel/Backdoor physical sensors, Analog Joystick.
*   **Outputs:** Solenoid Lock Relays (Panel, Enclosure, Backdoor), TFT Touchscreen UI.

### 3. Node D (The Pump & Liquid Manager)
*Handles the reserve tanks and liquid routing.*
*   **Inputs:** Ultrasonic Alcohol Levels (Tank C & D).
*   **Outputs:** Water Pumps (C & D), Cycle Status LEDs, OLED Status Display.

### 4. Node E (The Boss / Cloud Gateway)
*The central router and internet bridge.*
*   **Role:** Connects to local WiFi and Blynk Cloud. Receives all ESP-NOW traffic, translates it to Virtual Pins for the cloud, and routes commands from the Web Dashboard down to the specific physical nodes. Houses the "Auto-Refill" logic loop.

---

## 💻 The Web Dashboard
Included in the repository is `index.html`. This is a fully responsive, dependency-free web dashboard that can be run from any browser (PC, Tablet, or Mobile).

*   **Live Sensors:** Polls the Blynk API safely (with anti-rate-limit sequential fetching) to show door statuses, liquid levels, and helmet detection.
*   **Manual Overrides:** Toggle switches and trigger buttons to remotely unlock doors, pulse pumps, test servos, or cycle LEDs.
*   **Live Terminal:** A hacker-style console that prints and timestamps every physical event (e.g., `[SYSTEM] Door Opened`, `[SYSTEM] Auto-Refill STARTED`) by reading Virtual Pin V0.

---

## 🛠️ Hardware Requirements
*   4x ESP32 WROOM Development Boards
*   4x Ultrasonic Sensors (HC-SR04)
*   3x Solenoid Locks & Relay Modules
*   2x Submersible 5V Water Pumps
*   2x Standard Servos (SG90/MG996R)
*   1x TFT Touchscreen Display (ILI9341 or similar)
*   1x 0.96" OLED Display (SSD1306)
*   RFID Reader (RC522) & Coin Acceptor
*   Humidifier / Atomizer module & UV LED Strips

---

## 🚀 Setup & Installation

1.  **Configure MAC Addresses:** In the `.ino` files for Nodes A, B, and D, ensure the `nodeE_MAC[]` array matches the physical MAC address of your Node E board.
2.  **Flash the Firmware:** Upload the respective code to each ESP32 board using the Arduino IDE. Ensure the `WiFi` and `ESP-NOW` channels are set to the exact same number across all 4 boards (Default: Channel `10`).
3.  **Blynk Cloud Setup:** 
    *   Create a Blynk template and map the Virtual Pins (V0 - V36) according to the system map.
    *   Paste your `AUTH_TOKEN` into the ESP32 E firmware and the `index.html` JavaScript configuration.
4.  **Run the Dashboard:** Simply double-click `index.html` in any modern web browser to connect to your live hardware.

---
*Designed and engineered for seamless, automated helmet hygiene.*