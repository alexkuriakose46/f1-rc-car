#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- PIN CONFIGURATIONS ---

// Motor Driver (e.g. TB6612FNG or L298N)
const int PIN_STBY = 15; // Must be HIGH to enable motor driver (if using TB6612FNG)

// Channel A (Main Motor)
const int PIN_PWMA = 14; // Speed control
const int PIN_AIN1 = 27; // Direction 1
const int PIN_AIN2 = 26; // Direction 2

// Channel B (Front Hinge)
const int PIN_PWMB = 25; // Speed control
const int PIN_BIN1 = 33; // Direction 1
const int PIN_BIN2 = 13; // Direction 2

// Steering Servo
const int PIN_SERVO = 32;

// LEDs
const int PIN_LED_REAR = 2;    // F1 Rain Light / Brake Light (Red)
const int PIN_LED_EXHAUST = 4; // Exhaust Flame (Yellow)

// RGB Headlight
const int PIN_HEADLIGHT = 5;
const int NUM_HEADLIGHT_LEDS = 2; // Assuming 2 LEDs (adjust if needed)
Adafruit_NeoPixel headlight(NUM_HEADLIGHT_LEDS, PIN_HEADLIGHT, NEO_GRB + NEO_KHZ800);

// --- NETWORK CONFIG ---
const char* ssid = "F1_RC_CAR";
const char* password = ""; // Open network
WiFiUDP udp;
const int udpPort = 4210;

// --- STATE VARIABLES ---
int currentGear = 0;
int targetDriveSpeed = 0; // -100 to 100
int targetBrake = 0;      // 0 to 100
float currentSteerAngle = 90.0;
int targetSteerAngle = 90;
int targetHinge = 0;      // -1 (Down), 0 (Stop), 1 (Up)
int targetF1Light = 0;    // 0 = Off, 1 = On (F1 blink mode)
int targetHeadlightColor = 0; // 0=Off, 1=White, 2=Red, 3=Green, 4=Blue, 5=Yellow

unsigned long lastPacketTime = 0;
Servo steerServo;

// --- EXHAUST STATE ---
int previousGear = 0;
bool exhaustFlickering = false;
unsigned long exhaustFlickerStartTime = 0;
const unsigned long EXHAUST_FLICKER_DURATION = 200; // ms

// --- LIGHTING STATE ---
unsigned long previousBlinkMillis = 0;
int blinkState = 0; 
unsigned long previousBrakeBlinkMillis = 0;
bool brakeBlinkState = false;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable Brownout Detector
  setCpuFrequencyMhz(80); // Reduce CPU speed from 240MHz to 80MHz to drastically cut power draw!
  
  Serial.begin(115200);

  // Motor Setup
  pinMode(PIN_STBY, OUTPUT);
  pinMode(PIN_PWMA, OUTPUT);
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  
  pinMode(PIN_PWMB, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  
  digitalWrite(PIN_STBY, HIGH); // Enable Motor Driver

  // LED Setup
  pinMode(PIN_LED_REAR, OUTPUT);
  pinMode(PIN_LED_EXHAUST, OUTPUT);
  
  // Headlight Setup
  headlight.begin();
  headlight.show(); // Initialize all pixels to 'off'
  
  // Servo Setup
  ESP32PWM::allocateTimer(0);
  steerServo.setPeriodHertz(50);
  steerServo.attach(PIN_SERVO, 500, 2400);
  steerServo.write(90);

  // Network Setup
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Lower TX power to prevent voltage dip on 3.7V battery
  WiFi.softAP(ssid, password);
  udp.begin(udpPort);
  Serial.println("F1 AP Started. Listening for UDP...");
}

void setMainMotor(int speed) {
  if (speed == 0) {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, LOW);
    analogWrite(PIN_PWMA, 0);
  } else if (speed > 0) { // Forward
    digitalWrite(PIN_AIN1, HIGH);
    digitalWrite(PIN_AIN2, LOW);
    analogWrite(PIN_PWMA, map(speed, 0, 100, 0, 255));
  } else { // Reverse
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, HIGH);
    analogWrite(PIN_PWMA, map(-speed, 0, 100, 0, 255));
  }
}

void setHingeMotor(int state) {
  // state: 1 = UP, -1 = DOWN, 0 = STOP
  if (state == 1) {
    digitalWrite(PIN_BIN1, HIGH);
    digitalWrite(PIN_BIN2, LOW);
    analogWrite(PIN_PWMB, 255); // Full power
  } else if (state == -1) {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, HIGH);
    analogWrite(PIN_PWMB, 255);
  } else {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, LOW);
    analogWrite(PIN_PWMB, 0);
  }
}

void updateSteering() {
  float smoothingFactor = 0.6; 
  if (abs(targetDriveSpeed) > 50) {
      smoothingFactor = 0.15; // Slower steer at high speeds
  } else if (abs(targetDriveSpeed) > 20) {
      smoothingFactor = 0.3;
  }
  
  currentSteerAngle += (targetSteerAngle - currentSteerAngle) * smoothingFactor;
  
  int newAngle = (int)currentSteerAngle;
  static int lastWrittenAngle = -1;
  if (newAngle != lastWrittenAngle) {
    steerServo.write(newAngle);
    lastWrittenAngle = newAngle;
  }
}

void updateLighting() {
  unsigned long currentMillis = millis();
  
  // --- EXHAUST FLAME ---
  // Trigger flicker on gear change up or down (excluding neutral/reverse)
  if (currentGear != previousGear) {
    if (currentGear > 0) {
      exhaustFlickering = true;
      exhaustFlickerStartTime = currentMillis;
    }
    previousGear = currentGear;
  }

  if (exhaustFlickering) {
    if (currentMillis - exhaustFlickerStartTime < 1000) { // Solid 1 second bright flash
      digitalWrite(PIN_LED_EXHAUST, HIGH); 
    } else {
      exhaustFlickering = false;
      digitalWrite(PIN_LED_EXHAUST, LOW);
    }
  } else {
    digitalWrite(PIN_LED_EXHAUST, LOW);
  }

  // --- REAR F1 LIGHT ---
  if (targetBrake > 5) { // Braking applied
    // Fast blink (10Hz -> 50ms ON/OFF)
    if (currentMillis - previousBrakeBlinkMillis >= 50) {
      previousBrakeBlinkMillis = currentMillis;
      brakeBlinkState = !brakeBlinkState;
      digitalWrite(PIN_LED_REAR, brakeBlinkState ? HIGH : LOW);
    }
    blinkState = 0; // Reset F1 blink sequence for when brake is released
  } else if (targetF1Light == 1) { 
    // F1 Rain Light mode: ON 70ms, OFF 70ms, ON 70ms, OFF 450ms
    if (blinkState == 0) {
      digitalWrite(PIN_LED_REAR, HIGH);
      if (currentMillis - previousBlinkMillis >= 70) { blinkState = 1; previousBlinkMillis = currentMillis; }
    } else if (blinkState == 1) {
      digitalWrite(PIN_LED_REAR, LOW);
      if (currentMillis - previousBlinkMillis >= 70) { blinkState = 2; previousBlinkMillis = currentMillis; }
    } else if (blinkState == 2) {
      digitalWrite(PIN_LED_REAR, HIGH);
      if (currentMillis - previousBlinkMillis >= 70) { blinkState = 3; previousBlinkMillis = currentMillis; }
    } else if (blinkState == 3) {
      digitalWrite(PIN_LED_REAR, LOW);
      if (currentMillis - previousBlinkMillis >= 450) { blinkState = 0; previousBlinkMillis = currentMillis; }
    }
  } else {
    digitalWrite(PIN_LED_REAR, LOW);
  }

  // --- RGB HEADLIGHT ---
  uint32_t color = headlight.Color(0, 0, 0); // Default Off
  switch(targetHeadlightColor) {
    case 1: color = headlight.Color(255, 255, 255); break; // White
    case 2: color = headlight.Color(255, 0, 0); break; // Red
    case 3: color = headlight.Color(0, 255, 0); break; // Green
    case 4: color = headlight.Color(0, 0, 255); break; // Blue
    case 5: color = headlight.Color(255, 255, 0); break; // Yellow
  }
  
  static uint32_t lastColor = 999; // Force update on first run
  if (color != lastColor) {
    for(int i=0; i<NUM_HEADLIGHT_LEDS; i++) {
      headlight.setPixelColor(i, color);
    }
    headlight.show();
    lastColor = color;
  }
}

void loop() {
  char packetBuffer[255];
  bool gotCommand = false;

  // 1A. Process Serial (USB Wired Testing)
  if (Serial.available()) {
    String serialData = Serial.readStringUntil('\n');
    serialData.toCharArray(packetBuffer, 255);
    gotCommand = true;
  }
  
  // 1B. Process Network Packets (Wi-Fi)
  int packetSize = udp.parsePacket();
  if (packetSize && !gotCommand) {
    int len = udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    gotCommand = true;
  }

  // 1C. Parse the command
  // Format: G:1;T:100;B:0;S:90;H:0;L:1;C:0
  if (gotCommand) {
    int inGear=0, inThrot=0, inBrake=0, inSteer=0, inHinge=0, inLight=0, inColor=0;
    if (sscanf(packetBuffer, "G:%d;T:%d;B:%d;S:%d;H:%d;L:%d;C:%d", 
               &inGear, &inThrot, &inBrake, &inSteer, &inHinge, &inLight, &inColor) == 7) {
      
      currentGear = inGear;
      targetBrake = inBrake;
      targetHinge = inHinge;
      targetF1Light = inLight;
      targetHeadlightColor = inColor;
      
      if (inGear == 0) {
        targetDriveSpeed = 0;
      } else if (inGear == -1) {
        targetDriveSpeed = -inThrot; 
      } else {
        targetDriveSpeed = inThrot;
      }

      if (inBrake > 5) {
        targetDriveSpeed = 0; // Cut throttle on brake
      }

      // Map steer input (-100 to 100) to servo angle (60 to 120)
      targetSteerAngle = map(inSteer, -100, 100, 60, 120);
      
      lastPacketTime = millis();
    }
  }

  // 2. Safety Watchdog
  if (millis() - lastPacketTime > 500) {
    targetDriveSpeed = 0;
    targetSteerAngle = 90;
    targetHinge = 0;
    targetBrake = 0;
  }

  // 3. Actuate Hardware
  setMainMotor(targetDriveSpeed);
  setHingeMotor(targetHinge);
  updateSteering();
  updateLighting();
  
  delay(10); 
}
