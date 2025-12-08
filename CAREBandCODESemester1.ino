//this is for the heartrate monitor
//libraries required:
//SparkFun MAX3010x Pulse and Proximity Sensor Library
//Adafruit SSD1306
//Adafruit GFX

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SparkFun_MAX3010x.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MAX30102 object
MAX30105 particleSensor;

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("Initializing I2C...");
  Wire.begin();

  // ---------------------------
  // Initialize OLED
  // ---------------------------
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ---------------------------
  // Initialize MAX30102
  // ---------------------------
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring!");
    display.setCursor(0, 0);
    display.println("MAX30102 ERROR");
    display.display();
    while (1);
  }
  Serial.println("MAX30102 initialized!");

  particleSensor.setup();  
  particleSensor.setPulseAmplitudeRed(0x1F);  
  particleSensor.setPulseAmplitudeGreen(0);  
  particleSensor.setSampleRate(100);  
  particleSensor.setPulseWidth(411);
}

void loop() {
  long irValue = particleSensor.getIR();

  if (irValue < 50000) {
    Serial.println("No finger detected.");
    showOnDisplay("Place finger...");
    delay(500);
    return;
  }

  uint8_t hr, confidence;
  bool validHR = particleSensor.checkForBeat(irValue);

  static uint32_t beatCount = 0;
  static uint32_t lastBeat = 0;

  if (validHR) {
    uint32_t now = millis();
    uint32_t dt = now - lastBeat;
    lastBeat = now;

    uint32_t bpm = 60000 / dt;

    // Print to serial
    Serial.print("BPM: ");
    Serial.println(bpm);

    // Print to OLED
    showHeartRate(bpm);
  }

  delay(10);
}

// ------------------------------
// Helper functions
// ------------------------------

void showOnDisplay(const char *msg) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(msg);
  display.display();
}

void showHeartRate(int bpm) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 10);
  display.print("Heart Rate:");
  display.setCursor(0, 40);
  display.setTextSize(3);
  display.print(bpm);
  display.print(" BPM");
  display.display();
}

