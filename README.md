# ESP32 Gas Leak Detector (MQ2 + MQ135 + DHT11)

## 🔧 Hardware Setup

![Circuit](images/circuit.jpg)

This project is a production-grade gas leak and air-quality monitor built on an ESP32 DevKit V1.
It uses MQ2 and MQ135 gas sensors plus a DHT11 temperature/humidity sensor and indicates the
severity via a traffic light LED module, a patterned buzzer alarm, and JSON logs on the serial port.

---

## 🚀 Features

- MQ2 (smoke / LPG / combustible gas) and MQ135 (air quality / CO2 / NH3)
- DHT11 temperature & humidity sensing
- Moving-average smoothing (noise reduction)
- Hysteresis + stability control (no flicker)
- Smart buzzer patterns (severity-based)
- 15s sensor warm-up handling
- Mute button (45s silence mode)
- JSON output for dashboards (Streamlit, etc.)

---

## 📸 Real Hardware

This is the actual working prototype:

![Circuit](images/circuit.jpg)

---

## 🧠 Gas Severity Logic

| Level    | LED Indicator | Description              |
|----------|--------------|--------------------------|
| SAFE     | 🟢 Green     | Normal air               |
| MINOR    | 🟡 Yellow    | Slight pollution         |
| MODERATE | 🟡 Yellow    | Noticeable gas presence  |
| SEVERE   | 🔴 Red       | Dangerous gas levels     |

---

## 🔌 Pin Mapping

| Function      | GPIO |
|--------------|------|
| MQ2 AO       | 34   |
| MQ135 AO     | 35   |
| DHT11 DATA   | 25   |
| Green LED    | 5    |
| Yellow LED   | 12   |
| Red LED      | 13   |
| Buzzer       | 4    |
| Button       | 14   |

---

## 🛠️ Setup

1. Install Arduino IDE
2. Install ESP32 Board Package
3. Install DHT Library (Adafruit)
4. Upload `GasLeakv2.ino`
5. Open Serial Monitor (115200 baud)

---

## 📂 Project Structure

```
.
├── GasLeakv2.ino
├── README.md
├── CIRCUIT.md
└── images/
    └── circuit.jpg
```

---

## ⚠️ Safety Note

This is an educational project and should not replace certified gas detection systems.
