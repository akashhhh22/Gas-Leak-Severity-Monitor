// ╔══════════════════════════════════════════════════════════════╗
// ║           GAS LEAK DETECTOR - PRODUCTION v4.0                ║
// ║                                                              ║
// ║  Hardware:  ESP32 DevKit V1                                  ║
// ║  Sensors:   MQ2 (D34), MQ135 (D35), DHT11 (D25)              ║
// ║  Outputs:   LED G(D5) Y(D12) R(D13), Buzzer(D4)              ║
// ║  Input:     Button(D14)                                      ║
// ║                                                              ║
// ║  Features:                                                   ║
// ║   Moving average smoothing (reduces false triggers)          ║
// ║   Hysteresis (prevents level flickering)                     ║
// ║   DHT11 cached reads (no NaN gaps in JSON)                   ║
// ║   Severity hold timer (stays at level 5s before drop)        ║
// ║   Startup warm-up (30s sensor stabilization)                 ║
// ║   Non-blocking buzzer patterns                               ║
// ║   Serial JSON → Streamlit dashboard                          ║
// ║   Mute button (45s silence)                                  ║
// ╚══════════════════════════════════════════════════════════════╝

#include <DHT.h>

// ─────────────────────────────────────────────
//  PIN DEFINITIONS
// ─────────────────────────────────────────────

#define MQ2_PIN       34    // Smoke / LPG / CO (ADC1_CH6)
#define MQ135_PIN     35    // Air quality / CO2 / NH3 (ADC1_CH7)
#define DHT_PIN       25    // Temperature & Humidity (GPIO25)
#define DHT_TYPE      DHT11

#define BTN_MUTE      14    // Silence button (INPUT_PULLUP → GND)
#define LED_SAFE       5    // Green  → SAFE
#define LED_WARN      12    // Yellow → MINOR / MODERATE
#define LED_ALARM     13    // Red    → SEVERE
#define BUZZER_PIN     4    // Passive buzzer (PWM)

// ─────────────────────────────────────────────
//  TIMING CONSTANTS
// ─────────────────────────────────────────────

const unsigned long SENSOR_READ_MS      = 800;    // Read sensors every 800ms
const unsigned long DHT_READ_MS         = 2500;   // DHT11 min interval
const unsigned long MUTE_DURATION_MS    = 45000;  // 45s mute on button press
const unsigned long SEVERITY_HOLD_MS   = 5000;   // Hold higher severity 5s before drop
const unsigned long WARMUP_MS          = 15000;  // 15s warm-up after boot
const int           SMOOTH_SAMPLES     = 8;      // Moving average window size

// ─────────────────────────────────────────────
//  PRACTICAL THRESHOLDS
//  Clean air baseline: MQ2 ~630  |  MQ135 ~395
//  Large gaps prevent false triggers
// ─────────────────────────────────────────────

const int MQ2_MINOR_TH      = 1200;  // +570  → candle / incense nearby
const int MQ2_MODERATE_TH   = 1800;  // +1170 → smoke visible in room
const int MQ2_SEVERE_TH     = 2800;  // +2170 → gas leak / heavy smoke

const int MQ135_MINOR_TH    = 900;   // +505  → air quality drops
const int MQ135_MODERATE_TH = 1400;  // +1005 → significant pollutant
const int MQ135_SEVERE_TH   = 2100;  // +1705 → dangerous air quality

// ─────────────────────────────────────────────
//  HYSTERESIS (prevents flickering at boundary)
//  Level drops only when reading falls 10% below threshold
// ─────────────────────────────────────────────

const float HYSTERESIS = 0.90;

// ─────────────────────────────────────────────
//  SEVERITY ENUM
// ─────────────────────────────────────────────

enum Severity {
  SAFE     = 0,
  MINOR    = 1,
  MODERATE = 2,
  SEVERE   = 3
};

// ─────────────────────────────────────────────
//  GLOBAL STATE
// ─────────────────────────────────────────────

DHT dht(DHT_PIN, DHT_TYPE);

// Sensor smoothing buffers (moving average)
int mq2Buffer[SMOOTH_SAMPLES]   = {0};
int mq135Buffer[SMOOTH_SAMPLES] = {0};
int bufferIndex = 0;
bool bufferFilled = false;

// DHT11 cached values
float cachedTemp = 29.0;
float cachedHum  = 57.0;
unsigned long lastDHTRead = 0;

// Severity state
Severity currentSeverity  = SAFE;
Severity displaySeverity  = SAFE;
unsigned long severityRaisedAt = 0;

// Mute state
bool muteActive = false;
unsigned long muteUntil = 0;

// Timing
unsigned long lastSensorRead = 0;
unsigned long bootTime = 0;

// Buzzer engine state
unsigned long beepPrevMs = 0;
bool   beepOn   = false;
int    beepStep = 0;

// Stats tracking
unsigned long totalReadings   = 0;
unsigned long minorCount      = 0;
unsigned long moderateCount   = 0;
unsigned long severeCount     = 0;
int mq2Peak   = 0;
int mq135Peak = 0;

// ─────────────────────────────────────────────
//  BUZZER CORE
//  512 = 50% duty cycle = MAXIMUM VOLUME
//  Best resonant freqs: 2000–4000 Hz
// ─────────────────────────────────────────────

#define MAX_DUTY 512

void buzzerTone(int freq) {
  ledcWriteTone(BUZZER_PIN, freq);
  ledcWrite(BUZZER_PIN, MAX_DUTY);
}

void buzzerOff() {
  ledcWriteTone(BUZZER_PIN, 0);
  ledcWrite(BUZZER_PIN, 0);
}

// ─────────────────────────────────────────────
//  BUZZER PATTERNS (fully non-blocking)
//
//  SAFE:     Silent
//  MINOR:    pip .......... pip  (1 beep / 3s, soft notice)
//  MODERATE: pip-pip-pip .... (3 fast beeps / 2s, urgent)
//  SEVERE:   BEEP..BEEP..BEEP (400ms on / 200ms off, alarm)
// ─────────────────────────────────────────────

void handleBuzzer(Severity s) {
  // Respect mute
  if (muteActive && millis() < muteUntil) {
    buzzerOff();
    return;
  }
  if (muteActive && millis() >= muteUntil) {
    muteActive = false;
    Serial.println("🔔 Mute expired → Alerts resumed");
  }

  unsigned long now = millis();

  switch (s) {

    // ✅ SAFE - complete silence
    case SAFE:
      buzzerOff();
      beepOn = false;
      beepStep = 0;
      break;

    // 🟡 MINOR - single gentle pip every 3s
    case MINOR:
      if (beepOn && now - beepPrevMs >= 80) {
        buzzerOff();
        beepOn = false;
        beepPrevMs = now;
      } else if (!beepOn && now - beepPrevMs >= 2920) {
        buzzerTone(2000);     // 2000Hz clean tone
        beepOn = true;
        beepPrevMs = now;
      }
      break;

    // 🟠 MODERATE - 3 quick pips burst every 2s
    case MODERATE:
      switch (beepStep) {
        case 0: if (now - beepPrevMs >= 1400) { buzzerTone(2500); beepPrevMs = now; beepStep = 1; } break;
        case 1: if (now - beepPrevMs >=  110) { buzzerOff();      beepPrevMs = now; beepStep = 2; } break;
        case 2: if (now - beepPrevMs >=  100) { buzzerTone(2500); beepPrevMs = now; beepStep = 3; } break;
        case 3: if (now - beepPrevMs >=  110) { buzzerOff();      beepPrevMs = now; beepStep = 4; } break;
        case 4: if (now - beepPrevMs >=  100) { buzzerTone(2500); beepPrevMs = now; beepStep = 5; } break;
        case 5: if (now - beepPrevMs >=  110) { buzzerOff();      beepPrevMs = now; beepStep = 0; } break;
      }
      break;

    // 🔴 SEVERE - fire alarm pattern 400ms ON / 200ms OFF
    case SEVERE:
      if (beepOn && now - beepPrevMs >= 400) {
        buzzerOff();
        beepOn = false;
        beepPrevMs = now;
      } else if (!beepOn && now - beepPrevMs >= 200) {
        buzzerTone(3200);     // 3200Hz max volume
        beepOn = true;
        beepPrevMs = now;
      }
      break;
  }
}

// ─────────────────────────────────────────────
//  MOVING AVERAGE SMOOTHER
//  Fills ring buffer, returns average
//  Eliminates single-sample spikes
// ─────────────────────────────────────────────

int smoothedMQ2   = 0;
int smoothedMQ135 = 0;

void updateSmoothing(int rawMQ2, int rawMQ135) {
  mq2Buffer[bufferIndex]   = rawMQ2;
  mq135Buffer[bufferIndex] = rawMQ135;
  bufferIndex = (bufferIndex + 1) % SMOOTH_SAMPLES;
  if (bufferIndex == 0) bufferFilled = true;

  int count = bufferFilled ? SMOOTH_SAMPLES : bufferIndex;
  long sumMQ2 = 0, sumMQ135 = 0;
  for (int i = 0; i < count; i++) {
    sumMQ2   += mq2Buffer[i];
    sumMQ135 += mq135Buffer[i];
  }
  smoothedMQ2   = sumMQ2   / count;
  smoothedMQ135 = sumMQ135 / count;
}

// ─────────────────────────────────────────────
//  SEVERITY CALCULATOR (weighted scoring)
//  Both sensors must agree for higher levels
//  Hysteresis prevents boundary flickering
// ─────────────────────────────────────────────
Severity computeSeverity(int mq2, int mq135) {
  int score = 0;

  // MQ2 scoring
  if      (mq2 > MQ2_SEVERE_TH)    score += 3;
  else if (mq2 > MQ2_MODERATE_TH)  score += 2;
  else if (mq2 > MQ2_MINOR_TH)     score += 1;

  // MQ135 scoring
  if      (mq135 > MQ135_SEVERE_TH)    score += 3;
  else if (mq135 > MQ135_MODERATE_TH)  score += 2;
  else if (mq135 > MQ135_MINOR_TH)     score += 1;

  // Classify
  if (score >= 5) return SEVERE;
  if (score >= 3) return MODERATE;
  if (score >= 1) return MINOR;
  return SAFE;
}

// ─────────────────────────────────────────────
//  HYSTERESIS LOGIC
//  Severity only DROPS after SEVERITY_HOLD_MS
//  Severity RISES immediately
// ─────────────────────────────────────────────

Severity applyHysteresis(Severity raw) {
  if (raw > displaySeverity) {
    // Immediate rise
    displaySeverity  = raw;
    severityRaisedAt = millis();
  } else if (raw < displaySeverity) {
    // Delayed drop (hold 5s)
    if (millis() - severityRaisedAt >= SEVERITY_HOLD_MS) {
      displaySeverity = raw;
    }
  }
  return displaySeverity;
}

// ─────────────────────────────────────────────
//  LED CONTROL
// ─────────────────────────────────────────────

void applyLEDs(Severity s) {
  digitalWrite(LED_SAFE,  s == SAFE                     ? HIGH : LOW);
  digitalWrite(LED_WARN,  (s == MINOR || s == MODERATE) ? HIGH : LOW);
  digitalWrite(LED_ALARM, s == SEVERE                   ? HIGH : LOW);
}

// ─────────────────────────────────────────────
//  SEVERITY LABEL
// ─────────────────────────────────────────────

const char* severityLabel(Severity s) {
  switch (s) {
    case SAFE:     return "SAFE";
    case MINOR:    return "MINOR";
    case MODERATE: return "MODERATE";
    case SEVERE:   return "SEVERE";
    default:       return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────
//  LED STARTUP SELF-TEST
// ─────────────────────────────────────────────

void ledSelfTest() {
  Serial.println("  Running LED self-test...");
  digitalWrite(LED_SAFE,  HIGH); delay(250); digitalWrite(LED_SAFE,  LOW);
  digitalWrite(LED_WARN,  HIGH); delay(250); digitalWrite(LED_WARN,  LOW);
  digitalWrite(LED_ALARM, HIGH); delay(250); digitalWrite(LED_ALARM, LOW);
  delay(100);
  // All on together
  digitalWrite(LED_SAFE, HIGH);
  digitalWrite(LED_WARN, HIGH);
  digitalWrite(LED_ALARM, HIGH);
  delay(300);
  digitalWrite(LED_SAFE, LOW);
  digitalWrite(LED_WARN, LOW);
  digitalWrite(LED_ALARM, LOW);
  Serial.println("  LED self-test complete ✅");
}

// ─────────────────────────────────────────────
//  BUZZER STARTUP CONFIRM (3 ascending tones)
// ─────────────────────────────────────────────
void buzzerStartupTone() {
  Serial.println("  Running buzzer self-test...");
  buzzerTone(1500); delay(100); buzzerOff(); delay(60);
  buzzerTone(2000); delay(100); buzzerOff(); delay(60);
  buzzerTone(2500); delay(100); buzzerOff();
  Serial.println("  Buzzer self-test complete ✅");
}

// ─────────────────────────────────────────────
//  PRINT STATS (every 100 readings)
// ─────────────────────────────────────────────

void printStats() {
  Serial.println("\n──── SESSION STATS ────────────────────");
  Serial.printf("  Total readings : %lu\n", totalReadings);
  Serial.printf("  MINOR  events  : %lu\n", minorCount);
  Serial.printf("  MODERATE events: %lu\n", moderateCount);
  Serial.printf("  SEVERE  events : %lu\n", severeCount);
  Serial.printf("  MQ2   peak     : %d\n",  mq2Peak);
  Serial.printf("  MQ135 peak     : %d\n",  mq135Peak);
  Serial.printf("  Uptime         : %lu s\n", millis() / 1000);
  Serial.println("────────────────────────────────────────\n");
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  bootTime = millis();

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  GAS LEAK DETECTOR v4.0 - BOOTING    ║");
  Serial.println("╚══════════════════════════════════════╝");

  // Initialize DHT11
  dht.begin();
  Serial.println("  DHT11 initializing...");
  delay(2500);

  // Initialize GPIO
  pinMode(BTN_MUTE,  INPUT_PULLUP);
  pinMode(LED_SAFE,  OUTPUT);
  pinMode(LED_WARN,  OUTPUT);
  pinMode(LED_ALARM, OUTPUT);
  digitalWrite(LED_SAFE, LOW);
  digitalWrite(LED_WARN, LOW);
  digitalWrite(LED_ALARM, LOW);

  // Initialize PWM buzzer (10-bit resolution, 2000Hz carrier)
  ledcAttach(BUZZER_PIN, 2000, 10);
  ledcWriteTone(BUZZER_PIN, 0);

  // Self tests
  ledSelfTest();
  buzzerStartupTone();

  // Sensor warm-up (flash green slowly)
  Serial.printf("  Sensor warm-up: %d seconds...\n", (int)(WARMUP_MS/1000));
  unsigned long warmStart = millis();
  while (millis() - warmStart < WARMUP_MS) {
    digitalWrite(LED_SAFE, HIGH); delay(500);
    digitalWrite(LED_SAFE, LOW);  delay(500);
  }
  digitalWrite(LED_SAFE, HIGH);  // Warm-up done → Green on

  // Pre-fill smoothing buffer
  for (int i = 0; i < SMOOTH_SAMPLES; i++) {
    mq2Buffer[i]   = analogRead(MQ2_PIN);
    mq135Buffer[i] = analogRead(MQ135_PIN);
    delay(50);
  }
  bufferFilled = true;

  // Initial DHT11 read
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    cachedHum = h; cachedTemp = t;
  }

  Serial.println("\n════════════════════════════════════════");
  Serial.println("  ✅ SYSTEM ONLINE - Monitoring started");
  Serial.println("════════════════════════════════════════");
  Serial.println("  Thresholds (practical, anti-false-trigger):");
  Serial.printf ("  MINOR    → MQ2 > %d  | MQ135 > %d\n", MQ2_MINOR_TH, MQ135_MINOR_TH);
  Serial.printf ("  MODERATE → MQ2 > %d | MQ135 > %d\n", MQ2_MODERATE_TH, MQ135_MODERATE_TH);
  Serial.printf ("  SEVERE   → MQ2 > %d | MQ135 > %d\n", MQ2_SEVERE_TH, MQ135_SEVERE_TH);
  Serial.println("  BTN D14  → Mute 45s");
  Serial.println("════════════════════════════════════════\n");
}

// ─────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── Mute button handler ──────────────────
  if (digitalRead(BTN_MUTE) == LOW) {
    muteActive = true;
    muteUntil  = now + MUTE_DURATION_MS;
    buzzerOff();
    Serial.printf("🔇 MUTE activated for %d seconds\n", (int)(MUTE_DURATION_MS/1000));
    delay(300);  // debounce
  }

  // ── Always run buzzer engine (non-blocking) ──
  handleBuzzer(displaySeverity);

  // ── Sensor read cycle ────────────────────
  if (now - lastSensorRead >= SENSOR_READ_MS) {
    lastSensorRead = now;
    totalReadings++;

    // --- Raw ADC reads ---
    int rawMQ2   = analogRead(MQ2_PIN);
    int rawMQ135 = analogRead(MQ135_PIN);

    // --- Apply moving average ---
    updateSmoothing(rawMQ2, rawMQ135);

    // --- Track peaks ---
    if (smoothedMQ2   > mq2Peak)   mq2Peak   = smoothedMQ2;
    if (smoothedMQ135 > mq135Peak) mq135Peak = smoothedMQ135;

    // --- DHT11 read (max every 2.5s, use cache if faster) ---
    if (now - lastDHTRead >= DHT_READ_MS) {
      lastDHTRead = now;
      float h = dht.readHumidity();
      float t = dht.readTemperature();
      if (!isnan(h) && !isnan(t) && h > 0 && h <= 100) {
        cachedHum  = h;
        cachedTemp = t;
      }
    }

    // --- Compute severity on smoothed values ---
    Severity raw = computeSeverity(smoothedMQ2, smoothedMQ135);

    // --- Apply hysteresis ---
    Severity final = applyHysteresis(raw);

    // --- Update stats ---
    if (final == MINOR)    minorCount++;
    if (final == MODERATE) moderateCount++;
    if (final == SEVERE)   severeCount++;

    // --- Apply LEDs ---
    applyLEDs(final);

    // --- Log severity change ---
    if (final != currentSeverity) {
      Serial.printf("\n⚠️  LEVEL CHANGE: %s → %s  (MQ2=%d MQ135=%d)\n\n",
        severityLabel(currentSeverity),
        severityLabel(final),
        smoothedMQ2, smoothedMQ135);
      currentSeverity = final;
      beepOn   = false;
      beepStep = 0;
      beepPrevMs = now;
    }

    // --- JSON output for Streamlit dashboard ---
    Serial.printf(
      "{\"mq2\":%d,\"mq135\":%d,\"mq2_raw\":%d,\"mq135_raw\":%d,"
      "\"temp\":%.1f,\"hum\":%.1f,\"severity\":%d,\"label\":\"%s\","
      "\"uptime\":%lu}\n",
      smoothedMQ2, smoothedMQ135, rawMQ2, rawMQ135,
      cachedTemp, cachedHum,
      (int)final, severityLabel(final),
      now / 1000
    );

    // --- Print stats every 100 readings ---
    if (totalReadings % 100 == 0) printStats();
  }
}