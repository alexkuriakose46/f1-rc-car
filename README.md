# F1 RC Car Dashboard & Telemetry System 🏎️

![GitHub](https://img.shields.io/github/license/alexkuriakose46/f1-rc-car)
![Python](https://img.shields.io/badge/Python-3.x-blue.svg)
![ESP32](https://img.shields.io/badge/Platform-ESP32-brightgreen)

A completely custom, high-performance ESP32-based F1 RC car featuring a live Pygame telemetry dashboard, wireless UDP control, and authentic F1 features like a functioning front hinge, dynamic exhaust backfire, and rain lights.

## 🌟 Features

- **Live Telemetry Dashboard:** A Pygame-based desktop interface displaying speed, gear, throttle/brake input, and steering angle in real-time.
- **Wireless UDP Control:** Ultra-fast, low-latency communication over Wi-Fi between the Python dashboard and the ESP32.
- **Controller Support:** Full support for Xbox and PlayStation gamepads.
- **Authentic Lighting System:**
  - **Exhaust Backfire:** Bright yellow LEDs that aggressively flash when shifting gears.
  - **F1 Rain / Brake Light:** A red rear LED that flashes rapidly under hard braking and features a toggleable F1 rain pattern.
  - **RGB Headlights:** Addressable WS2812B NeoPixel headlights with color cycling (White, Red, Green, Blue, Yellow).
- **Dual Motor Support:** Dedicated channels for the main drive motor and a secondary front-wing hinge actuator.

## 🛠️ Hardware Requirements

- **ESP32 Development Board**
- **Motor Driver** (TB6612FNG, L298N, or similar)
- **Main DC Motor** & **DC Hinge Actuator Motor**
- **Steering Servo** (SG90 or standard size)
- **Addressable RGB LEDs** (WS2812B / NeoPixel)
- **Standard LEDs** (Red for F1 light, Yellow for exhaust)
- **Game Controller** (Xbox/PS connected to PC)
- **Power Delivery:** 5V Power Bank (for ESP32 logic) + Lithium Battery (for Motor Driver `VMOT`)

## ⚡ Quick Setup

### 1. ESP32 Firmware
1. Open `esp32_firmware/rc_car_f1/rc_car_f1.ino` in the Arduino IDE.
2. Install the following libraries via the Arduino Library Manager:
   - `ESP32Servo` (by Kevin Harrington)
   - `Adafruit NeoPixel` (by Adafruit)
3. Upload the firmware to your ESP32.
4. *Note: The code disables the brown-out detector and drops the CPU speed to 80MHz to help stabilize Wi-Fi if using lower voltages, but a 5V power bank is highly recommended.*

### 2. Python Dashboard
1. Ensure Python 3.x is installed on your computer.
2. Install the required dependency:
   ```bash
   pip install pygame
   ```
3. Connect your computer to the `F1_RC_CAR` Wi-Fi network hosted by the ESP32 (No password).
4. Connect your gamepad to your PC.
5. Run the dashboard:
   ```bash
   python python_dashboard/f1_dashboard.py
   ```

## 🎮 Controls

| Button | Action |
| :--- | :--- |
| **Right Trigger (RT)** | Accelerate |
| **Left Trigger (LT)** | Brake / Stop |
| **Left Stick (X-Axis)**| Steering |
| **Right Bumper (RB)** | Gear Up |
| **Left Bumper (LB)** | Gear Down |
| **D-Pad Up / Down** | Adjust Front Wing Hinge |
| **D-Pad Left/Right**| Cycle RGB Headlight Colors |
| **Y / Triangle** | Toggle F1 Rain Light Pattern |
| **X / Square** | Quick Neutral |

## 🔌 Wiring Guide

**Power:**
Connect the 5V Power Bank to the ESP32 `VIN`/`5V` and `GND`. Connect your main motor battery to the Motor Driver `VMOT` and `GND`. **All Grounds must be tied together!**

**Motor Driver (TB Module):**
- **PWMA** -> Pin 14
- **AIN1** -> Pin 27
- **AIN2** -> Pin 26
- **PWMB** -> Pin 25 (Hinge)
- **BIN1** -> Pin 33
- **BIN2** -> Pin 13
- **STBY** -> Pin 15

**Actuators & Lights:**
- **Steering Servo** -> Pin 32
- **RGB Headlight (Data)** -> Pin 5
- **Exhaust LEDs** -> Pin 4 (Use a resistor!)
- **F1 Rear LED** -> Pin 2 (Use a resistor!)

---
*Created by [alexkuriakose46](https://github.com/alexkuriakose46)*
