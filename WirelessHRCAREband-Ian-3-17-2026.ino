#include <ArduinoBLE.h>
#include <Wire.h>
#include <avr/sleep.h> //This is the library for the "Sleep" functionality on the Arduino
#include "MAX30105.h"
#include "heartRate.h"

// Ian 3-17-2026:
// Added sleep functionality using a button to sleep and wake up
// Added average system that works for 3 minutes using (old average + new value)/2
// Known issue: the deep sleep whipes the memory, so the Average BPM is lost when it sleeps
// This issue can be remedied with EEPROM, but IDK if this board has EEPROM


MAX30105 particleSensor;

BLEService heartService("180D"); // Heart Rate Service
BLEUnsignedCharCharacteristic heartRateChar("2A37", BLERead | BLENotify);

long lastBeat = 0;
float beatsPerMinute;
float beatsPerMinutePrev; //this is used once in order to get the initial measurment of the average
long initialAverage = 0;
long averageHR = 0;
int sleeptimer = 0; //Variable to indicate when to put the ardino to sleep, currently set to: 3 Seconds
//-----------------------------------------------------------------------------------------------------------------
const int wakeUpPin = AddPinNumberHere; // Pin __ connects to the pushbutton
//-----------------------------------------------------------------------------------------------------------------

void wakeUp() {
  Serial.print("Waking up... I could've used 5 more minutes");
  //Interrupt service routine (can add to this if needed, nothing needed as of now)
}

void goToSleep() {
  sleep_enable(); // Enables the sleep bit in the mcucr register
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), wakeUp, LOW); // Wake on low level
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // set to Deep sleep mode
  
  sleep_cpu(); // Arduino sleeps here
  
  // Wakes up here
  sleep_disable(); // Disable sleep
  detachInterrupt(digitalPinToInterrupt(wakeUpPin)); // Remove interrupt
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(wakeUpPin, INPUT_PULLUP); // setting the pin for the wake-up

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  if (!BLE.begin()) {
    Serial.println("BLE failed");
    while (1);
  }

  BLE.setLocalName("CAREband");
  BLE.setAdvertisedService(heartService);

  heartService.addCharacteristic(heartRateChar);
  BLE.addService(heartService);

  BLE.advertise();

  Serial.println("BLE Heart Rate Monitor Ready");
}

void loop() {

  BLEDevice central = BLE.central();

  // Wait until a device connects
  if (central) {

    Serial.print("Connected to: ");
    Serial.println(central.address());

    while (central.connected()) {

      long irValue = particleSensor.getIR();

      if (checkForBeat(irValue)) {

        long delta = millis() - lastBeat;
        lastBeat = millis();

        // This is for the sleep cycle
        if(lastBeat == 0){
          sleeptimer += 1; //adds 1 to the timer if there is 0 heartbeat
        }
        else {
          sleeptimer = 0; //if there is a heartbeat, reset the sleep timer
        }
        
        //This is the time it takes to sleep; 60000 is approximtely 1 minute
        if(sleeptimer >= 60000 || digitalRead(wakeUpPin) == HIGH) { //if the timer reaches a minute OR if the button is pushed
          delay(1000);
          Serial.print("Going to sleep... in case I don't see ya, good afternoon, good evening, and goodnight");
          goToSleep();
        }

        beatsPerMinute = 60 / (delta / 1000.0);

        //average heartrate formula
        if (initialAverage <= 2) {
          beatsPerMinutePrev = 60 / (lastBeat / 1000.0);
          averageHR = (beatsPerMinute + beatsPerMinutePrev) / 2;
          initialAverage++;
          sleeptimer = 0;
        }
        else if (initialAverage <= 300000 && initialAverage > 2) { //the 300000 miliseconds is 5 minutes
          averageHR = (averageHR + beatsPerMinute) / 2;
          initialAverage++;
          sleeptimer = 0;
        }
        else {
          averageHR = averageHR;
        }

        if (beatsPerMinute > 40 && beatsPerMinute < 200) {

          heartRateChar.writeValue((byte)beatsPerMinute);
          heartRateChar.writeValue((byte)averageHR);

          Serial.print("BPM: ");
          Serial.print(beatsPerMinute);
          Serial.print(" |==||==| ");
          Serial.print("Average BPM: ");
          Serial.print(averageHR);
          Serial.print(" |==||==| ");
          Serial.print("Time until sleep: ");
          Serial.print(sleeptimer);
          Serial.println(" %");

        }
      }
    }

    Serial.println("Device disconnected");
  }
}