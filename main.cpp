#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <math.h>

// WiFi credentials
const char* ssid = "Quadcopter";
const char* password = "12341234";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create MPU6050 object
MPU6050 mpu(Wire);

// Motor control pins (connect to MOSFET gates)
const int MOTOR_FL = 32;  // Front Left Motor
const int MOTOR_FR = 23;  // Front Right Motor
const int MOTOR_BL = 27;  // Back Left Motor
const int MOTOR_BR = 2;   // Back Right Motor

// PWM properties
const int FREQ = 5000;
const int RESOLUTION = 8;  // 8-bit resolution (0-255)

// Motor channel assignments
const int MOTOR_FL_CHANNEL = 0;
const int MOTOR_FR_CHANNEL = 1;
const int MOTOR_BL_CHANNEL = 2;
const int MOTOR_BR_CHANNEL = 3;

// Global variables for motor speeds (0-255)
int motorFL_speed = 0;
int motorFR_speed = 0;
int motorBL_speed = 0;
int motorBR_speed = 0;

// Variables to store sensor readings
float pitch = 0;
float roll = 0;
float yaw = 0;

// Safety features
bool armed = false;
unsigned long lastCommandTime = 0;
const unsigned long FAILSAFE_TIMEOUT = 2000; // 2 seconds without commands will trigger failsafe

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Drone Control System Starting...");
  
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // Configure PWM for each motor
  ledcSetup(MOTOR_FL_CHANNEL, FREQ, RESOLUTION);
  ledcSetup(MOTOR_FR_CHANNEL, FREQ, RESOLUTION);
  ledcSetup(MOTOR_BL_CHANNEL, FREQ, RESOLUTION);
  ledcSetup(MOTOR_BR_CHANNEL, FREQ, RESOLUTION);
  
  // Attach channels to GPIO pins
  ledcAttachPin(MOTOR_FL, MOTOR_FL_CHANNEL);
  ledcAttachPin(MOTOR_FR, MOTOR_FR_CHANNEL);
  ledcAttachPin(MOTOR_BL, MOTOR_BL_CHANNEL);
  ledcAttachPin(MOTOR_BR, MOTOR_BR_CHANNEL);
  
  // Initialize all motors to off
  ledcWrite(MOTOR_FL_CHANNEL, 0);
  ledcWrite(MOTOR_FR_CHANNEL, 0);
  ledcWrite(MOTOR_BL_CHANNEL, 0);
  ledcWrite(MOTOR_BR_CHANNEL, 0);
  
  // Initialize MPU6050
  Wire.begin();
  Serial.println("Initializing MPU6050...");
  
  byte status = mpu.begin();
  while(status != 0) {
    Serial.println("Could not find a valid MPU6050 sensor, check wiring!");
    Serial.print("Status: ");
    Serial.println(status);
    delay(500);
    status = mpu.begin();
  }
  
  Serial.println("MPU6050 found!");
  
  // Calibrate gyroscope - keep drone still during calibration
  Serial.println("Calibrating gyroscope. Keep the drone still...");
  delay(1000); // Give user time to keep drone still
  mpu.calcOffsets(); // This calibrates accelerometer and gyro
  Serial.println("Calibration done!");
  
  // Connect to Wi-Fi in Access Point mode
  WiFi.softAP(ssid, password);
  Serial.println("WiFi Access Point started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP()); // Usually 192.168.4.1
  
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  
  // Route to load javascript file
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/script.js", "text/javascript");
  });
  
  // Handle motor control commands
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){
    lastCommandTime = millis(); // Update command time for failsafe
    
    // Process throttle command
    if (request->hasParam("throttle")) {
      int throttle = request->getParam("throttle")->value().toInt();
      throttle = constrain(throttle, 0, 100);
      
      // Only apply throttle if armed
      if (armed) {
        // Convert throttle percentage to PWM value (0-255)
        int pwmValue = map(throttle, 0, 100, 0, 255);
        
        // Apply throttle to all motors (base value)
        motorFL_speed = pwmValue;
        motorFR_speed = pwmValue;
        motorBL_speed = pwmValue;
        motorBR_speed = pwmValue;
      }
    }
    
    // Process pitch, roll, and yaw commands (if armed)
    if (armed) {
      // Pitch control (forward/backward tilt)
      if (request->hasParam("pitch")) {
        int pitchValue = request->getParam("pitch")->value().toInt();
        pitchValue = constrain(pitchValue, -50, 50);
        
        // Positive pitch moves forward (decrease front motors, increase back motors)
        int pitchAdjustment = map(abs(pitchValue), 0, 50, 0, 50);
        
        if (pitchValue > 0) { // Forward
          motorFL_speed -= pitchAdjustment;
          motorFR_speed -= pitchAdjustment;
          motorBL_speed += pitchAdjustment;
          motorBR_speed += pitchAdjustment;
        } else if (pitchValue < 0) { // Backward
          motorFL_speed += pitchAdjustment;
          motorFR_speed += pitchAdjustment;
          motorBL_speed -= pitchAdjustment;
          motorBR_speed -= pitchAdjustment;
        }
      }
      
      // Roll control (left/right tilt)
      if (request->hasParam("roll")) {
        int rollValue = request->getParam("roll")->value().toInt();
        rollValue = constrain(rollValue, -50, 50);
        
        // Positive roll moves right (decrease right motors, increase left motors)
        int rollAdjustment = map(abs(rollValue), 0, 50, 0, 50);
        
        if (rollValue > 0) { // Right
          motorFL_speed += rollAdjustment;
          motorFR_speed -= rollAdjustment;
          motorBL_speed += rollAdjustment;
          motorBR_speed -= rollAdjustment;
        } else if (rollValue < 0) { // Left
          motorFL_speed -= rollAdjustment;
          motorFR_speed += rollAdjustment;
          motorBL_speed -= rollAdjustment;
          motorBR_speed += rollAdjustment;
        }
      }
      
      // Yaw control (rotation)
      if (request->hasParam("yaw")) {
        int yawValue = request->getParam("yaw")->value().toInt();
        yawValue = constrain(yawValue, -50, 50);
        
        // Positive yaw rotates clockwise
        int yawAdjustment = map(abs(yawValue), 0, 50, 0, 50);
        
        if (yawValue > 0) { // Clockwise
          motorFL_speed += yawAdjustment;
          motorFR_speed -= yawAdjustment;
          motorBL_speed -= yawAdjustment;
          motorBR_speed += yawAdjustment;
        } else if (yawValue < 0) { // Counter-clockwise
          motorFL_speed -= yawAdjustment;
          motorFR_speed += yawAdjustment;
          motorBL_speed += yawAdjustment;
          motorBR_speed -= yawAdjustment;
        }
      }
    }
    
    // Arm/disarm command
    if (request->hasParam("arm")) {
      String armCmd = request->getParam("arm")->value();
      if (armCmd == "1" && !armed) {
        armed = true;
        Serial.println("Drone armed");
      } else if (armCmd == "0" && armed) {
        armed = false;
        motorFL_speed = 0;
        motorFR_speed = 0;
        motorBL_speed = 0;
        motorBR_speed = 0;
        Serial.println("Drone disarmed");
      }
    }
    
    // Constrain all motor values to safe range
    motorFL_speed = constrain(motorFL_speed, 0, 255);
    motorFR_speed = constrain(motorFR_speed, 0, 255);
    motorBL_speed = constrain(motorBL_speed, 0, 255);
    motorBR_speed = constrain(motorBR_speed, 0, 255);
    
    // Apply motor speeds via PWM
    if (armed) {
      ledcWrite(MOTOR_FL_CHANNEL, motorFL_speed);
      ledcWrite(MOTOR_FR_CHANNEL, motorFR_speed);
      ledcWrite(MOTOR_BL_CHANNEL, motorBL_speed);
      ledcWrite(MOTOR_BR_CHANNEL, motorBR_speed);
    } else {
      ledcWrite(MOTOR_FL_CHANNEL, 0);
      ledcWrite(MOTOR_FR_CHANNEL, 0);
      ledcWrite(MOTOR_BL_CHANNEL, 0);
      ledcWrite(MOTOR_BR_CHANNEL, 0);
    }
    
    // Return current state as JSON
    String response = "{\"armed\":" + String(armed ? "true" : "false") + 
                     ",\"motorFL\":" + String(motorFL_speed) +
                     ",\"motorFR\":" + String(motorFR_speed) +
                     ",\"motorBL\":" + String(motorBL_speed) +
                     ",\"motorBR\":" + String(motorBR_speed) +
                     ",\"pitch\":" + String(pitch) +
                     ",\"roll\":" + String(roll) +
                     ",\"yaw\":" + String(yaw) + "}";
    
    request->send(200, "application/json", response);
  });
  
  // Route to get sensor data
  server.on("/sensor", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{\"pitch\":" + String(pitch) +
                  ",\"roll\":" + String(roll) +
                  ",\"yaw\":" + String(yaw) + "}";
    request->send(200, "application/json", json);
  });
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Drone control system ready!");
}

void loop() {
  // Update MPU6050 sensor data
  mpu.update();
  
  // Get orientation data
  pitch = mpu.getAngleX();
  roll = mpu.getAngleY();
  yaw = mpu.getAngleZ();
  
  // Failsafe check - if no commands for a while, disarm
  if (armed && (millis() - lastCommandTime > FAILSAFE_TIMEOUT)) {
    Serial.println("FAILSAFE TRIGGERED - No commands received");
    armed = false;
    motorFL_speed = 0;
    motorFR_speed = 0;
    motorBL_speed = 0;
    motorBR_speed = 0;
    
    // Turn off all motors
    ledcWrite(MOTOR_FL_CHANNEL, 0);
    ledcWrite(MOTOR_FR_CHANNEL, 0);
    ledcWrite(MOTOR_BL_CHANNEL, 0);
    ledcWrite(MOTOR_BR_CHANNEL, 0);
  }
  
  // Print motor values for debugging every second
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 1000) {
    lastDebugTime = millis();
    
    if (armed) {
      Serial.print("Motors: FL=");
      Serial.print(motorFL_speed);
      Serial.print(" FR=");
      Serial.print(motorFR_speed);
      Serial.print(" BL=");
      Serial.print(motorBL_speed);
      Serial.print(" BR=");
      Serial.println(motorBR_speed);
      
      Serial.print("Orientation: Pitch=");
      Serial.print(pitch);
      Serial.print(" Roll=");
      Serial.print(roll);
      Serial.print(" Yaw=");
      Serial.println(yaw);
    }
  }
  
  // Small delay to ensure stable loop timing
  delay(10);
}