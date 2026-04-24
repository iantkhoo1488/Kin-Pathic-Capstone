#include <ArduinoBLE.h>
#include <Wire.h>
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
// Fixed 0x2A37 Heart Rate Measurement characteristic to use proper BLE packet format:
//     - Byte 0: Flags (0x00 = UINT8 HR format, no extras)
//     - Byte 1: Heart Rate Value (UINT8)
//     Previously, a bare byte was being sent which caused NRF Connect to report
//     "invalid data syntax" because the flags byte was missing.
// Fixed double writeValue() bug where averageHR was overwriting beatsPerMinute
//     immediately, so NRF Connect only ever saw the averageHR byte with no flags.
// Fixed avgHRChar UUID — 0x2A38 is a reserved Bluetooth SIG UUID (Body Sensor Location)
//     which caused NRF Connect to show "other received". Replaced with a custom 128-bit UUID.
// Fixed averageHR logic — the old (old+new)/2 formula was an exponential moving
//     average that forgot early beats quickly and never actually tracked 5 minutes.
//     Replaced with a true running average (sum / count) over a locked 5-minute window.
//     Once 5 minutes elapses the average locks and stops changing.

// Ian 3-24-2026:
// Added outlier rejection before any beat is accepted:
//     - The first beat is always accepted to seed lastGoodBPM.
//     - Every subsequent beat is compared to lastGoodBPM.
//     - If it deviates more than OUTLIER_THRESHOLD (30%), it is silently discarded.
//       Nothing is updated — not the display, not the running average, not BLE.
//     - This protects the 5-minute average from being poisoned by motion artifacts
//       and prevents the dashboard alert from firing on a single erroneous spike.
//     - The raw BPM display on the dashboard remains instant and unsmoothed;
//       smoothing is NOT applied, only bad readings are dropped.

// Ian 4-01-2026
// changed timeout to be 5 minutes (300000) instead of 10 seconds (10000)

MAX30105 particleSensor;

BLEService heartService("180D");

// Standard Heart Rate Measurement characteristic — requires flags + value packet format
BLECharacteristic heartRateChar("2A37", BLERead | BLENotify, 2);

// Custom 128-bit UUID for Average HR — avoids collision with any Bluetooth SIG standard
BLEUnsignedCharCharacteristic avgHRChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);

unsigned long lastBeat   = 0;
float beatsPerMinute     = 0;
float lastGoodBPM        = 0;  // Tracks last accepted reading for outlier comparison

// Outlier rejection threshold — readings deviating more than this % are discarded
const float OUTLIER_THRESHOLD = 0.30; // 30%

// --- True 5-minute running average ---
float bpmSum                = 0;
int   bpmCount              = 0;
long  averageHR             = 0;
bool  averageLocked         = false;
unsigned long firstBeatTime = 0;
const unsigned long AVG_WINDOW = 300000; // 5 minutes in milliseconds

// --- Sleep / timeout logic ---
unsigned long noFingerStart = 0;
const unsigned long TIMEOUT = 300000; // 10 seconds
bool isPaused = false;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (1);
  }

  // Optimized setup for low power
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
  BLE.addService(heartService);
  BLE.advertise();

  Serial.println("CAREBand Active. System will sleep after 10s of no contact.");
  Serial.println("Average HR will lock after 5 minutes of readings.");
}

void loop() {
  // 1. SLEEP MODE LOGIC
  if (isPaused) {
    particleSensor.wakeUp();
    delay(50);
    if (particleSensor.getIR() > 20000) { // Peek to see if finger returned
      Serial.println("Waking up...");
      isPaused = false;
      noFingerStart = 0;
      BLE.advertise();
    } else {
      // Hard kill LEDs and sleep
      particleSensor.setPulseAmplitudeRed(0);
      particleSensor.setPulseAmplitudeIR(0);
      particleSensor.shutDown();
      delay(2000); // Wait 2 seconds before checking again
      return;
    }
  }

  // 2. ACTIVE MONITORING LOGIC
  BLEDevice central = BLE.central();
  long irValue = particleSensor.getIR();

  // If the sensor is essentially seeing nothing
  if (irValue < 20000) {
    if (noFingerStart == 0) noFingerStart = millis();

    unsigned long idleTime = millis() - noFingerStart;

    // Console feedback every ~1 second
    if (idleTime % 1000 < 20) {
      Serial.print("Sleeping in: ");
      Serial.print(10 - (idleTime / 1000));
      Serial.println("s");
    }

    if (idleTime > TIMEOUT) {
      Serial.println("TIMEOUT: Forcing Power Down.");

      particleSensor.setPulseAmplitudeRed(0); // Physically turn off Red LED
      particleSensor.setPulseAmplitudeIR(0);  // Physically turn off IR LED
      particleSensor.shutDown();              // Put chip in standby (0.7uA)

      if (central) central.disconnect();
      BLE.stopAdvertise();

      isPaused = true;
      noFingerStart = 0;
      return;
    }
  } else {
    noFingerStart = 0; // Reset if finger is back
  }

  // 3. HEART RATE PROCESSING
  if (checkForBeat(irValue)) {
    unsigned long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    /*
    // --- Range gate: only physiologically plausible beats ---
    if (beatsPerMinute > 40 && beatsPerMinute < 200) {

      // --- Outlier rejection ---
      // First beat always accepted to seed lastGoodBPM.
      // After that, discard if deviation from last good reading exceeds 30%.
      if (lastGoodBPM > 0) {
        float deviation = abs(beatsPerMinute - lastGoodBPM) / lastGoodBPM;
        if (deviation > OUTLIER_THRESHOLD) {
          Serial.print("OUTLIER REJECTED: ");
          Serial.print(beatsPerMinute);
          Serial.print(" BPM (");
          Serial.print((int)(deviation * 100));
          Serial.println("% deviation from last good reading)");
          return; // Discard — do not update BLE, average, or display
        }
      }

      // Beat accepted — update last good reading
      lastGoodBPM = beatsPerMinute;
      */

      // --- Start 5-minute window on first accepted beat ---
      if (firstBeatTime == 0) {
        firstBeatTime = millis();
        Serial.println(">>> 5-minute average window started. <<<");
      }

      // --- True running average over a 5-minute window ---
      if (!averageLocked) {
        if (millis() - firstBeatTime < AVG_WINDOW) {
          // Still inside the 5-minute window — accumulate
          bpmSum += beatsPerMinute;
          bpmCount++;
          averageHR = (long)(bpmSum / bpmCount); // True mathematical average
        } else {
          // 5 minutes elapsed — lock the average in
          averageLocked = true;
          Serial.println(">>> Average HR locked in after 5 minutes. <<<");
        }
      }

      // --- Proper 0x2A37 Heart Rate Measurement packet ---
      // Byte 0: Flags = 0x00 (HR value is UINT8, no Energy Expended, no RR interval)
      // Byte 1: Heart Rate Value as UINT8
      uint8_t hrPacket[2];
      hrPacket[0] = 0x00;
      hrPacket[1] = (uint8_t)beatsPerMinute;
      heartRateChar.writeValue(hrPacket, 2);

      // --- Send Average HR on its own characteristic ---
      avgHRChar.writeValue((uint8_t)averageHR);

      // --- Serial output ---
      Serial.print("BPM: ");
      Serial.print(beatsPerMinute);
      Serial.print(" |==||==| Average BPM: ");
      Serial.print(averageHR);
      if (averageLocked) {
        Serial.print(" [LOCKED]");
      } else {
        Serial.print(" [");
        Serial.print((millis() - firstBeatTime) / 1000);
        Serial.print("s / 300s]");
      }
      Serial.println();
    }
  }
//}
