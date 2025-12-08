/* 
  nRF52832 DK + MAX30102 example
  - Averaged HR filter
  - SpO2 measurement (SparkFun/Maxim algorithm)
  - BLE peripheral (advertise HR/SpO2 via custom characteristic)
  - Battery monitor via ADC (voltage divider)
  - Low-power idle using sd_app_evt_wait() when SoftDevice is active
*/

/* ===========  Libraries  =========== */
#include <Wire.h>
#include "MAX30105.h"                   // SparkFun MAX3010x library
#include "spo2_algorithm.h"             // Maxim algorithm wrapper (from SparkFun examples)
#include <Adafruit_SSD1306.h>           // optional display
#include <Adafruit_GFX.h>

#include <NimBLEDevice.h>               // NimBLE Arduino (if using Bluefruit, adapt accordingly)

#ifdef NRF_POWER
  // SoftDevice sleep helper
  #include "nrf_soc.h"
#endif

/* ===========  Display config (optional) =========== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ===========  Sensor config  =========== */
MAX30105 particleSensor;

/* ===========  Battery pin (ADC) config  =========== */
// Wire battery sense to the analog pin below through an appropriate divider
// If you wire directly to a named AIN pin, update BATTERY_PIN accordingly.
const int BATTERY_PIN = A0;          // change to the ADC pin you wired
const float VDIV_R1 = 100000.0;     // top resistor (ohms) - battery to ADC
const float VDIV_R2 = 47000.0;      // bottom resistor (ohms) - ADC to GND

/* ADC calibration specifics for nRF52 Arduino core */
const float ADC_REF_MV = 3600.0;     // typical ADC ref (mV) - core dependent; adjust if required
const int ADC_MAX = 1023;            // default Arduino ADC resolution for many cores; adjust if your core uses 14-bit

/* ===========  Heart rate averaging/filer =========== */
const int HR_WINDOW = 6;             // moving average window (samples)
int hrBuf[HR_WINDOW];
int hrBufIdx = 0;
bool hrBufFilled = false;

/* ===========  SpO2 buffers for Maxim algorithm =========== */
#define MAX_SAMPLES 100
int32_t irBuffer[MAX_SAMPLES];
int32_t redBuffer[MAX_SAMPLES];
int32_t bufferLength = MAX_SAMPLES;

/* ===========  BLE config  =========== */
const char *deviceName = "nRF52-HR-POX";
static NimBLECharacteristic* pHRChar = nullptr;
static NimBLEServer* pServer = nullptr;

/* Custom simple service/characteristic UUIDs (you can change) */
#define UUID_SERVICE_HR "12345678-1234-5678-1234-56789abcdef0"
#define UUID_CHAR_HR    "12345678-1234-5678-1234-56789abcdef1"

/* ===========  Timing  =========== */
unsigned long lastSampleMs = 0;
const unsigned long sampleIntervalMs = 100;  // sample sensor each 100ms for processing

/* ===========  Setup  =========== */
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("Starting nRF52 MAX30102 HR+SpO2 example");

  Wire.begin();

  // Display init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found, continuing without display.");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("nRF52 HR+SpO2 starting...");
    display.display();
  }

  // MAX30102 init
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring!");
    showMessage("MAX30102 ERROR");
    while (1) { delay(500); } // stop - sensor required
  }
  Serial.println("MAX30102 found");

  // Setup the sensor (values tuned for fingertip)
  particleSensor.setup(); // default setup
  particleSensor.setPulseAmplitudeRed(0x1F);  // LED brightness
  particleSensor.setPulseAmplitudeGreen(0);   // green LED off
  particleSensor.setSampleRate(100);          // 100 samples/sec
  particleSensor.setPulseWidth(411);          // 411us pulse width => 18-bit resolution
  particleSensor.setLEDMode(2);               // red + IR

  // fill HR buffer with zeros
  for (int i = 0; i < HR_WINDOW; ++i) hrBuf[i] = 0;

  // Start BLE
  initBLE();

  // Warm-up/read initial samples into buffer for SpO2 algo
  Serial.println("Collecting initial samples...");
  for (int i = 0; i < bufferLength; i++) {
    while (!particleSensor.checkForBeat()) {
      // pull samples into buffer slots
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i]  = particleSensor.getIR();
      delay(10); // short wait - we will sample until buffer fills
    }
    // If checkForBeat() indicated a beat, still capture the raw values
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i]  = particleSensor.getIR();
    delay(10);
  }
}

/* ===========  BLE init function  =========== */
void initBLE() {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // set transmit power (value range depends on port); optional

  pServer = NimBLEDevice::createServer();
  NimBLEService *pService = pServer->createService(UUID_SERVICE_HR);

  // create characteristic with read + notify
  pHRChar = pService->createCharacteristic(
    UUID_CHAR_HR,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  // Start service and advertising
  pService->start();

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(UUID_SERVICE_HR);
  pAdv->setScanResponse(true);
  pAdv->start();

  Serial.println("BLE advertising started");
}

/* ===========  Loop  =========== */
void loop() {
  unsigned long now = millis();

  // Sample the sensor periodically
  if (now - lastSampleMs >= sampleIntervalMs) {
    lastSampleMs = now;

    // collect one sensor reading (red + ir)
    int32_t red = particleSensor.getRed();
    int32_t ir  = particleSensor.getIR();

    // Push into circular SpO2 buffer (shift older values)
    pushSample(red, ir);

    // Attempt to compute HR/SpO2 once per bufferLength samples
    static int samplesCollected = 0;
    samplesCollected++;
    if (samplesCollected >= bufferLength) {
      samplesCollected = 0;
      int spo2 = 0;
      int heartRate = 0;
      int valid_spo2 = 0;
      int valid_hr   = 0;

      // Use Maxim algorithm (wrapper used by SparkFun examples)
      maxim_heart_rate_and_oxygen_saturation(
        (int32_t *)irBuffer,
        (int32_t *)redBuffer,
        bufferLength,
        &spo2,
        &valid_spo2,
        &heartRate,
        &valid_hr
      );

      // Heart rate smoothing: push into average buffer
      int hrToUse = (valid_hr ? heartRate : 0);
      int avgHR = addHRandGetAverage(hrToUse);

      // Read battery voltage
      float battV = readBatteryVoltage();

      // Show on display + Serial
      Serial.print("HR: ");
      Serial.print(heartRate);
      Serial.print(" (avg ");
      Serial.print(avgHR);
      Serial.print(") SPO2: ");
      Serial.print(spo2);
      Serial.print(" valid? ");
      Serial.print(valid_spo2);
      Serial.print("  Batt: ");
      Serial.print(battV, 3);
      Serial.println(" V");

      // Display (if present)
      showHRandSpO2(avgHR, spo2, valid_spo2, battV);

      // Notify over BLE (simple CSV string, apps can parse)
      if (pHRChar) {
        char payload[64];
        snprintf(payload, sizeof(payload), "HR:%d,AVG:%d,SpO2:%d,Batt:%.3f", heartRate, avgHR, spo2, battV);
        pHRChar->setValue((uint8_t*)payload, strlen(payload));
        pHRChar->notify();
      }
    }
  }

  // Low power idle while BLE / SoftDevice active
  // sd_app_evt_wait() puts the CPU to sleep until an app event occurs (advertising/BLE events keep working)
  // Only call if SoftDevice is present. Wrap with ifdef to keep compatibility.
  #ifdef NRF_POWER
    // call SVC to wait for next event (safe if SoftDevice is present)
    sd_app_evt_wait();
  #else
    // fallback
    delay(10);
  #endif
}

/* ===========  Helpers  =========== */

void pushSample(int32_t red, int32_t ir) {
  // shift left and append at end (simple ring buffer would be more efficient)
  for (int i = 0; i < bufferLength - 1; ++i) {
    redBuffer[i] = redBuffer[i + 1];
    irBuffer[i]  = irBuffer[i + 1];
  }
  redBuffer[bufferLength - 1] = red;
  irBuffer[bufferLength - 1]  = ir;
}

int addHRandGetAverage(int newHR) {
  hrBuf[hrBufIdx] = newHR;
  hrBufIdx++;
  if (hrBufIdx >= HR_WINDOW) {
    hrBufIdx = 0;
    hrBufFilled = true;
  }
  // compute average over non-zero samples (avoid zeros when invalid)
  int sum = 0;
  int count = 0;
  for (int i = 0; i < HR_WINDOW; ++i) {
    if (hrBuf[i] > 0) {
      sum += hrBuf[i];
      count++;
    }
  }
  if (count == 0) return 0;
  return (sum + (count/2)) / count;
}

float readBatteryVoltage() {
  // Read ADC and compute battery voltage using divider
  int raw = analogRead(BATTERY_PIN);
  // Some Arduino cores use different ADC resolution; try to detect / adjust if needed
  float adcMax = ADC_MAX;
  float vAdc = (raw / adcMax) * ADC_REF_MV; // mV seen at ADC pin
  float vBattery = vAdc * ((VDIV_R1 + VDIV_R2) / VDIV_R2) / 1000.0; // convert to V
  return vBattery;
}

/* Display helpers */
void showMessage(const char *txt) {
  if (!display.display()) return;
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(txt);
  display.display();
}

void showHRandSpO2(int avgHR, int spo2, int valid_spo2, float battV) {
  if (!display.display()) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HR(avg): ");
  display.println(avgHR);
  display.print("SpO2: ");
  display.print(spo2);
  display.print(valid_spo2 ? "%" : " (bad)");
  display.setCursor(0, 40);
  display.print("Batt: ");
  display.print(battV, 3);
  display.println(" V");
  display.display();
}
