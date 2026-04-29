#include <ArduinoBLE.h>
#include <Wire.h>
#include <math.h>
#include "MAX30105.h"
#include "heartRate.h"

// Ian 3-17-2026:
// Added sleep functionality using a button to sleep and wake up
// Added average system that works for 3 minutes using (old average + new value)/2
// Known issue: the deep sleep whipes the memory, so the Average BPM is lost when it sleeps
// This issue can be remedied with EEPROM, but IDK if this board has EEPROM

// Ian 3-18-2026:
// Removed sleep functionality using a button to sleep and wake up
//     - Subbed with Sydney's "ispaused" routine
// Added average system that works for 3 minutes using (old average + new value)/2
// Known issue: the deep sleep whipes the memory, so the Average BPM is lost when it sleeps
//     - this is no longer an issue due to the fact that the sleep function was removed
// Added sleep functionality using the Arduino NANO's built-in library

// Ian 3-24-2026:
// Fixed 0x2A37 Heart Rate Measurement characteristic to use proper BLE packet format.
// Fixed double writeValue() bug.
// Fixed avgHRChar UUID collision with Bluetooth SIG standard 0x2A38.
// Fixed averageHR logic — replaced exponential moving average with true running average.

// Ian 4-01-2026:
// OUTLIER REJECTION + ROLLING AVERAGE REWORK:
// Replaced single-beat outlier comparison (lastGoodBPM ± 30%) with a z-score filter:
//     - Maintains a rolling window of the last Z_WINDOW_SIZE accepted beats (default 10)
//     - On each new beat, computes the mean and standard deviation of the window
//     - Rejects the beat if it is more than Z_THRESHOLD standard deviations from the mean
//       (default 2.0 SD). This adapts automatically as HR genuinely rises or falls,
//       avoiding the false rejection problem of single-beat comparison.
//     - During warm-up (fewer than Z_WINDOW_SIZE beats collected), the range gate
//       (40–200 BPM) is the only filter applied, since there is not yet enough data
//       to compute a meaningful standard deviation.
// Replaced locked 5-minute average with a perpetual rolling average:
//     - bpmSum and bpmCount accumulate all accepted beats forever — no lock, no window.
//     - averageHR = bpmSum / bpmCount, updated every accepted beat.
//     - The average never locks; it keeps refining as long as the device is worn.
// Alert warm-up period:
//     - The device tracks time since the first accepted beat (firstBeatTime).
//     - averageReady flag is set after WARMUP_MS (2 minutes) of accepted readings.
//     - Before averageReady, the average is transmitted but flagged as not ready,
//       so the dashboard can display it as "warming up" without triggering alerts.
//     - After averageReady, the dashboard alert system activates.

MAX30105 particleSensor;

BLEService heartService("180D");

// Standard Heart Rate Measurement characteristic — flags byte + UINT8 BPM
BLECharacteristic heartRateChar("2A37", BLERead | BLENotify, 2);

// Custom 128-bit UUID for Average HR
BLEUnsignedCharCharacteristic avgHRChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// Second custom characteristic — sends 1 if average is ready (warm-up complete), 0 if not
// Dashboard uses this to know when to activate the alert system
BLEUnsignedCharCharacteristic avgReadyChar("19B10002-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

// ── Z-Score filter ─────────────────────────────────────────────────────────
const int   Z_WINDOW_SIZE = 6;   // Number of recent beats used for SD calculation
const float Z_THRESHOLD   = 4.0;  // Beats beyond this many SDs are rejected
float       zWindow[Z_WINDOW_SIZE];
int         zCount        = 0;    // How many beats are currently in the window
int         zHead         = 0;    // Circular buffer head index

// Compute mean of current window
float windowMean() {
  float sum = 0;
  int   n   = min(zCount, Z_WINDOW_SIZE);
  for (int i = 0; i < n; i++) sum += zWindow[i];
  return sum / n;
}

// Compute standard deviation of current window
float windowSD(float mean) {
  float sumSq = 0;
  int   n     = min(zCount, Z_WINDOW_SIZE);
  for (int i = 0; i < n; i++) {
    float diff = zWindow[i] - mean;
    sumSq += diff * diff;
  }
  return sqrt(sumSq / n);
}

// Returns true if beat is accepted, false if rejected as outlier
bool zScoreAccept(float bpm) {
  // Not enough data yet — accept everything during warm-up of the filter window
  if (zCount < Z_WINDOW_SIZE) {
    zWindow[zHead] = bpm;
    zHead          = (zHead + 1) % Z_WINDOW_SIZE;
    zCount++;
    return true;
  }

  float mean = windowMean();
  float sd   = windowSD(mean);

  // If SD is near zero (very stable HR), accept anything within 5 BPM
  if (sd < 1.0) {
    if (abs(bpm - mean) > 5.0) {
      Serial.print("Z-FILTER REJECTED (low-SD guard): ");
      Serial.print(bpm);
      Serial.println(" BPM");
      return false;
    }
  } else {
    float zScore = abs(bpm - mean) / sd;
    if (zScore > Z_THRESHOLD) {
      Serial.print("Z-FILTER REJECTED (z=");
      Serial.print(zScore, 2);
      Serial.print("): ");
      Serial.print(bpm);
      Serial.println(" BPM");
      return false;
    }
  }

  // Accept — slide the beat into the window
  zWindow[zHead] = bpm;
  zHead          = (zHead + 1) % Z_WINDOW_SIZE;
  return true;
}

// ── Rolling average ────────────────────────────────────────────────────────
float         bpmSum        = 0;
unsigned long bpmCount      = 0;
long          averageHR     = 0;

// ── Warm-up timing ────────────────────────────────────────────────────────
unsigned long firstBeatTime  = 0;
bool          averageReady   = false;
const unsigned long WARMUP_MS = 120000; // 2 minutes

// ── BLE / sensor state ────────────────────────────────────────────────────
unsigned long lastBeat      = 0;
float         beatsPerMinute = 0;

unsigned long noFingerStart  = 0;
const unsigned long TIMEOUT  = 300000;
bool isPaused                = false;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  if (!BLE.begin()) {
    Serial.println("BLE failed");
    while (1);
  }

  BLE.setLocalName("CAREband");
  BLE.setAdvertisedService(heartService);
  heartService.addCharacteristic(heartRateChar);
  heartService.addCharacteristic(avgHRChar);
  heartService.addCharacteristic(avgReadyChar);
  BLE.addService(heartService);
  BLE.advertise();

  Serial.println("CAREBand Active. Alerts arm after 2 minutes of readings.");
}

void loop() {
  // 1. SLEEP MODE LOGIC
  if (isPaused) {
    particleSensor.wakeUp();
    delay(50);
    if (particleSensor.getIR() > 20000) {
      Serial.println("Waking up...");
      isPaused      = false;
      noFingerStart = 0;
      BLE.advertise();
    } else {
      particleSensor.setPulseAmplitudeRed(0);
      particleSensor.setPulseAmplitudeIR(0);
      particleSensor.shutDown();
      delay(2000);
      return;
    }
  }

  // 2. ACTIVE MONITORING LOGIC
  BLEDevice central = BLE.central();
  long irValue = particleSensor.getIR();

  if (irValue < 20000) {
    if (noFingerStart == 0) noFingerStart = millis();
    unsigned long idleTime = millis() - noFingerStart;

    if (idleTime % 1000 < 20) {
      Serial.print("Sleeping in: ");
      Serial.print(10 - (idleTime / 1000));
      Serial.println("s");
    }

    if (idleTime > TIMEOUT) {
      Serial.println("TIMEOUT: Forcing Power Down.");
      particleSensor.setPulseAmplitudeRed(0);
      particleSensor.setPulseAmplitudeIR(0);
      particleSensor.shutDown();
      if (central) central.disconnect();
      BLE.stopAdvertise();
      isPaused      = true;
      noFingerStart = 0;
      return;
    }
  } else {
    noFingerStart = 0;
  }

  // 3. HEART RATE PROCESSING
  if (checkForBeat(irValue)) {
    unsigned long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    // Range gate — physiologically plausible beats only
    if (beatsPerMinute < 40 || beatsPerMinute > 200) return;

    // Z-score filter — reject statistical outliers
    if (!zScoreAccept(beatsPerMinute)) return;

    // ── Beat accepted ──────────────────────────────────────────────────────

    // Start warm-up timer on very first accepted beat
    if (firstBeatTime == 0) {
      firstBeatTime = millis();
      Serial.println(">>> Warm-up started. Alerts arm in 2 minutes. <<<");
    }

    // Check if warm-up period has elapsed
    if (!averageReady && (millis() - firstBeatTime >= WARMUP_MS)) {
      averageReady = true;
      Serial.println(">>> 2-minute warm-up complete. Alert system now active. <<<");
    }

    // Update rolling average
    bpmSum   += beatsPerMinute;
    bpmCount++;
    averageHR = (long)(bpmSum / bpmCount);

    // Transmit live BPM — proper 0x2A37 packet format
    uint8_t hrPacket[2];
    hrPacket[0] = 0x00; // Flags: UINT8, no extras
    hrPacket[1] = (uint8_t)beatsPerMinute;
    heartRateChar.writeValue(hrPacket, 2);

    // Transmit rolling average
    avgHRChar.writeValue((uint8_t)averageHR);

    // Transmit ready flag (1 = alerts active, 0 = still warming up)
    avgReadyChar.writeValue((uint8_t)(averageReady ? 1 : 0));

    // Serial output
    unsigned long elapsed = (millis() - firstBeatTime) / 1000;
    Serial.print("BPM: ");
    Serial.print(beatsPerMinute);
    Serial.print(" | Avg: ");
    Serial.print(averageHR);
    Serial.print(" | Samples: ");
    Serial.print(bpmCount);
    if (!averageReady) {
      Serial.print(" | Warming up [");
      Serial.print(elapsed);
      Serial.print("s / 120s]");
    } else {
      Serial.print(" | ALERT SYSTEM ACTIVE");
    }
    Serial.println();
  }
}
