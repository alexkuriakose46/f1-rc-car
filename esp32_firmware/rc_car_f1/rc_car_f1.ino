#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>

// --- PIN CONFIGURATIONS ---
// You can easily edit these constants if you plug things into different pins!

// TB6612FNG Motor Driver
const int PIN_STBY = 15; // Must be HIGH to enable motor driver
const int PIN_PWMA = 14; // Speed control
const int PIN_AIN1 = 27; // Direction 1
const int PIN_AIN2 = 26; // Direction 2

// Steering Servo (SG90)
const int PIN_SERVO = 32;

// LEDs
const int PIN_LED_REAR = 2;   // F1 Rain Light (Red)
const int PIN_LED_EXHAUST = 4; // Exhaust Flame (Yellow)


// --- NETWORK CONFIG ---
const char* ssid = "F1_RC_CAR";
const char* password = ""; // Open network
WiFiUDP udp;
const int udpPort = 4210;

// --- STATE VARIABLES ---
int targetDriveSpeed = 0;
float currentSteerAngle = 90.0;
int targetSteerAngle = 90;
int lightTrigger = 0; // 0 = off, 1 = exhaust flame ON
unsigned long lastPacketTime = 0;

Servo steerServo;

// --- LIGHTING STATE ---
unsigned long previousBlinkMillis = 0;
int blinkState = 0; // 0=ON(70ms), 1=OFF(70ms), 2=ON(70ms), 3=OFF(450ms)


void setup() {
  Serial.begin(115200);

  // Motor Setup
  pinMode(PIN_STBY, OUTPUT);
  pinMode(PIN_PWMA, OUTPUT);
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  digitalWrite(PIN_STBY, HIGH); // Enable TB6612FNG

  // LED Setup
  pinMode(PIN_LED_REAR, OUTPUT);
  pinMode(PIN_LED_EXHAUST, OUTPUT);
  
  // Servo Setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  steerServo.setPeriodHertz(50);
  steerServo.attach(PIN_SERVO, 500, 2400);
  steerServo.write(90);

  // Network Setup
  WiFi.softAP(ssid, password);
  udp.begin(udpPort);
  Serial.println("F1 AP Started. Listening for UDP...");
}

void setMotor(int speed) {
  // speed expects -100 to 100
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

void updateSteering() {
  // Speed-dependent smoothing factor
  // Lower factor = slower, smoother steering
  float smoothingFactor = 0.6; 
  
  if (abs(targetDriveSpeed) > 50) {
      smoothingFactor = 0.15; // Slow steering reaction at high speed
  } else if (abs(targetDriveSpeed) > 20) {
      smoothingFactor = 0.3; // Medium smoothing at medium speed
  }
  
  // Low-pass filter interpolation
  currentSteerAngle += (targetSteerAngle - currentSteerAngle) * smoothingFactor;
  steerServo.write((int)currentSteerAngle);
}

void updateLighting() {
  unsigned long currentMillis = millis();
  
  // --- EXHAUST FLAME (Yellow LED) ---
  if (lightTrigger == 1 && targetDriveSpeed > 0) {
    // Random flicker to simulate fire
    if (random(0, 10) > 4) {
      analogWrite(PIN_LED_EXHAUST, random(100, 255));
    } else {
      analogWrite(PIN_LED_EXHAUST, 0);
    }
  } else {
    analogWrite(PIN_LED_EXHAUST, 0);
  }

  // --- REAR F1 RAIN LIGHT (Red LED) ---
  if (targetDriveSpeed < 0) {
    // Solid ON while in Reverse
    digitalWrite(PIN_LED_REAR, HIGH);
    blinkState = 0; // Reset blink pattern for when we return to forward
  } else {
    // F1 Pattern: ON 70ms, OFF 70ms, ON 70ms, OFF 450ms
    if (blinkState == 0) {
      digitalWrite(PIN_LED_REAR, HIGH);
      if (currentMillis - previousBlinkMillis >= 70) {
        blinkState = 1;
        previousBlinkMillis = currentMillis;
      }
    } else if (blinkState == 1) {
      digitalWrite(PIN_LED_REAR, LOW);
      if (currentMillis - previousBlinkMillis >= 70) {
        blinkState = 2;
        previousBlinkMillis = currentMillis;
      }
    } else if (blinkState == 2) {
      digitalWrite(PIN_LED_REAR, HIGH);
      if (currentMillis - previousBlinkMillis >= 70) {
        blinkState = 3;
        previousBlinkMillis = currentMillis;
      }
    } else if (blinkState == 3) {
      digitalWrite(PIN_LED_REAR, LOW);
      if (currentMillis - previousBlinkMillis >= 450) {
        blinkState = 0;
        previousBlinkMillis = currentMillis;
      }
    }
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
  if (gotCommand) {
    // Expect format: THROTTLE:100;STEER:90;LIGHT:1
    int inThrot = 0, inSteer = 90, inLight = 0;
    if (sscanf(packetBuffer, "THROTTLE:%d;STEER:%d;LIGHT:%d", &inThrot, &inSteer, &inLight) == 3) {
      targetDriveSpeed = inThrot;
      targetSteerAngle = inSteer;
      lightTrigger = inLight;
      lastPacketTime = millis();
    }
  }

  // 2. Safety Watchdog
  if (millis() - lastPacketTime > 500) {
    targetDriveSpeed = 0;
    targetSteerAngle = 90;
    lightTrigger = 0;
  }

  // 3. Actuate Hardware non-blockingly
  setMotor(targetDriveSpeed);
  updateSteering();
  updateLighting();
  
  // Tiny delay to keep loop fast but stable
  delay(10); 
}
