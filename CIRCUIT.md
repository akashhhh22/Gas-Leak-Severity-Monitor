# Circuit Connections – ESP32 Gas Leak Detector

This document describes the exact wiring used by `GasLeakv2.ino`.
All pin names and roles are taken directly from the sketch's `#define` section.

---

## 1. Overview

| Item           | Detail                                              |
|----------------|-----------------------------------------------------|
| Controller     | ESP32 DevKit V1 (30-pin)                            |
| Sensors        | MQ2, MQ135, DHT11                                   |
| Indicators     | Traffic light LED module (Green / Yellow / Red)     |
| Power LED      | White LED on VIN + 220Ω → GND (always on)          |
| Alarm          | 1× passive buzzer (PWM)                             |
| Input          | 1× push button (mute, active LOW)                  |

All grounds are common. The ESP32 is powered via USB during development.
Sensor modules use the 5V and GND pins from the dev board.

> **Traffic light module note:** The module has built-in current-limiting resistors on each
> LED lane. Connect the signal pins directly to the ESP32 GPIOs — **no external resistors needed**.

---

## 2. Complete Pin Connection Table

| Component                     | Module Pin       | ESP32 Pin | Label | Notes                                         |
|-------------------------------|-----------------|-----------|-------|-----------------------------------------------|
| **MQ2 gas sensor**            | VCC             | 5V        | 5V    | Power from ESP32 5V rail                      |
|                               | GND             | GND       | GND   | Common ground                                 |
|                               | AO (analog out) | 34        | D34   | `MQ2_PIN 34` — input-only ADC pin             |
|                               | DO              | —         | —     | Leave unconnected (not used)                  |
| **MQ135 gas sensor**          | VCC             | 5V        | 5V    | Power from ESP32 5V rail                      |
|                               | GND             | GND       | GND   | Common ground                                 |
|                               | AO (analog out) | 35        | D35   | `MQ135_PIN 35` — input-only ADC pin           |
|                               | DO              | —         | —     | Leave unconnected (not used)                  |
| **DHT11 sensor/module**       | VCC             | 5V        | 5V    | Or 3.3V if your module supports it            |
|                               | GND             | GND       | GND   | Common ground                                 |
|                               | DATA            | 25        | D25   | `DHT_PIN 25`                                  |
| **Traffic light — Green**     | G / S (signal)  | 5         | D5    | `LED_SAFE 5` — module resistor built-in       |
|                               | GND             | GND       | GND   | Common ground                                 |
| **Traffic light — Yellow**    | Y / S (signal)  | 12        | D12   | `LED_WARN 12` — module resistor built-in      |
|                               | GND             | GND       | GND   | Common ground                                 |
| **Traffic light — Red**       | R / S (signal)  | 13        | D13   | `LED_ALARM 13` — module resistor built-in     |
|                               | GND             | GND       | GND   | Common ground                                 |
| **White LED (power indicator)**| Anode (+)      | VIN       | VIN   | VIN → 220Ω resistor → White LED Anode         |
|                               | Cathode (–)     | GND       | GND   | Always ON when board is powered               |
| **Passive buzzer**            | Signal (S / +)  | 4         | D4    | `BUZZER_PIN 4` — PWM driven                   |
|                               | GND (–)         | GND       | GND   |                                               |
| **Mute button**               | Leg 1           | 14        | D14   | `BTN_MUTE 14` — INPUT_PULLUP                  |
|                               | Leg 2           | GND       | GND   | Active LOW when pressed                       |

---

## 3. Detailed Wiring Notes

### 3.1 MQ2 Sensor (Smoke / LPG / Combustible Gas)

```
MQ2 VCC  →  ESP32 5V
MQ2 GND  →  ESP32 GND
MQ2 AO   →  ESP32 GPIO34 (D34)
MQ2 DO   →  (leave unconnected)
```

GPIO34 is **input-only** on the ESP32 — perfect for analog reads.
Only the **AO** pin is used; the onboard comparator DO pin is ignored.

---

### 3.2 MQ135 Sensor (Air Quality / CO₂ / NH₃)

```
MQ135 VCC  →  ESP32 5V
MQ135 GND  →  ESP32 GND
MQ135 AO   →  ESP32 GPIO35 (D35)
MQ135 DO   →  (leave unconnected)
```

GPIO35 is also input-only, wired identically to MQ2.

---

### 3.3 DHT11 Temperature & Humidity

**Using a DHT11 module (3-pin, onboard pull-up):**

```
Module VCC   →  ESP32 5V  (or 3.3V)
Module GND   →  ESP32 GND
Module DATA  →  ESP32 GPIO25 (D25)
```

**Using a bare DHT11 sensor (4-pin):**

```
Pin 1 (VCC)   →  ESP32 3.3V
Pin 2 (DATA)  →  ESP32 GPIO25 (D25)
              →  10kΩ pull-up from DATA to 3.3V
Pin 3         →  (no connection)
Pin 4 (GND)   →  ESP32 GND
```

---

### 3.4 Traffic Light LED Module

The traffic light module has **3 signal pins** (Red, Yellow, Green) and a **GND pin**.
It has built-in current-limiting resistors — connect signal pins **directly** to GPIO.
**No external resistors required.**

```
Traffic module GND  →  ESP32 GND
Traffic module G    →  ESP32 GPIO5  (D5)   ← GREEN  = SAFE
Traffic module Y    →  ESP32 GPIO12 (D12)  ← YELLOW = MINOR / MODERATE
Traffic module R    →  ESP32 GPIO13 (D13)  ← RED    = SEVERE
```

The sketch drives them as standard `digitalWrite(HIGH/LOW)` outputs:

```cpp
#define LED_SAFE   5    // Green
#define LED_WARN  12    // Yellow
#define LED_ALARM 13    // Red
```

---

### 3.5 White LED — Power Indicator

The white LED is **always on** when the board is powered — it just shows the system is live.
It is wired directly off **VIN** (5V from USB), not a GPIO pin.

```
ESP32 VIN  →  220Ω resistor  →  White LED Anode (+)
White LED Cathode (–)  →  GND
```

> VIN on ESP32 DevKit is ~5V when powered by USB.
> 220Ω gives roughly (5 - 3.2) / 220 ≈ 8 mA — safe and bright enough for indication.

No code change needed — the white LED is purely a hardware power indicator.

---

### 3.6 Passive Buzzer

```
Buzzer S (+)   →  ESP32 GPIO4 (D4)
Buzzer GND (–) →  ESP32 GND
```

Uses hardware PWM via `ledcAttach` / `ledcWriteTone` on GPIO4.
A **passive** buzzer is required (active buzzers only click, won't produce tones).

```cpp
ledcAttach(BUZZER_PIN, 2000, 10);  // 2000Hz carrier, 10-bit resolution
```

---

### 3.7 Mute Button

```
Button Leg 1  →  ESP32 GPIO14 (D14)
Button Leg 2  →  GND
```

Configured with `INPUT_PULLUP` — **no external resistor needed**.
- Idle = HIGH
- Pressed = LOW (active LOW triggers 45s mute)

---

## 4. Power Considerations

- Power via **USB** into the ESP32 during development.
- MQ2 and MQ135 heater coils draw ~150–200 mA each — use the **5V (VIN)** pin, not 3.3V.
- For standalone use (no USB), use a **5V regulated supply ≥ 1A**.
- All modules must share **common GND** with the ESP32.

---

## 5. Breadboard Layout Tips

- Place ESP32 DevKit across the centre of a large (830-point) breadboard.
- Place traffic light module at the top — visible from a distance.
- Place white power LED near the ESP32 power pins (VIN / GND).
- Group MQ2 and MQ135 together; run AO wires to D34 and D35.
- DHT11 on the side with a short jumper to D25.
- Use coloured wires: red = VCC, black = GND, yellow/orange = sensor signals, green = LED/buzzer signals.

---

## 6. Quick Wiring Checklist

- [ ] MQ2 AO → D34
- [ ] MQ135 AO → D35
- [ ] DHT11 DATA → D25
- [ ] Traffic light Green → D5
- [ ] Traffic light Yellow → D12
- [ ] Traffic light Red → D13
- [ ] White LED: VIN → 220Ω → LED Anode → GND
- [ ] Buzzer signal → D4, Buzzer GND → GND
- [ ] Button → D14 ↔ GND
- [ ] MQ2 + MQ135 VCC → 5V
- [ ] DHT11 VCC → 5V (or 3.3V)
- [ ] All GND → common ground rail

If all boxes are ticked, the wiring matches the sketch exactly.
