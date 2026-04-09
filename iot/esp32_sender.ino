#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ---------- Wi-Fi + API ----------
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";
const char* API_URL = "http://YOUR_SERVER_IP:8000/event";
const char* DEVICE_REPORT_URL = "http://YOUR_SERVER_IP:8000/device/report";
const char* DEVICE_COMMAND_URL = "http://YOUR_SERVER_IP:8000/device/esp32cam-1/command";
const char* DEVICE_ACK_URL = "http://YOUR_SERVER_IP:8000/device/esp32cam-1/command/ack";
const char* DEVICE_ID = "esp32cam-1";

// ---------- MPU6050 ----------
const int MPU_ADDR = 0x68;
float accelX, accelY, accelZ;

// ---------- Pins ----------
const int CANCEL_BUTTON_PIN = 4;  // pull-up button to GND
const int BUZZER_PIN = 26;
const int RED_LED_PIN = 2;
const int GREEN_LED_PIN = 33;

// ---------- GPS (NEO-6M) ----------
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);       // RX=16 TX=17

// ---------- Edge decision ----------
const float EDGE_ALERT_THRESHOLD = 20.0;
const float EDGE_EMERGENCY_THRESHOLD = 50.0;
const unsigned long CONFIRM_MS = 20000; // 20 seconds
const int BACKEND_RETRIES = 5;

bool incidentPending = false;
unsigned long pendingSince = 0;
float pendingImpact = 0;
float pendingTilt = 0;
float pendingSpeed = 0;
float pendingLat = 0;
float pendingLon = 0;
int pendingBackendEventId = -1;
unsigned long lastBackendPollMs = 0;
const unsigned long BACKEND_POLL_INTERVAL_MS = 1200;


void setupMPU() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
}

void readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t rawX = Wire.read() << 8 | Wire.read();
  int16_t rawY = Wire.read() << 8 | Wire.read();
  int16_t rawZ = Wire.read() << 8 | Wire.read();

  accelX = rawX / 16384.0;
  accelY = rawY / 16384.0;
  accelZ = rawZ / 16384.0;
}

float calculateImpact() {
  float magnitude = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  return fabs(magnitude - 1.0) * 10.0;
}

float calculateTilt() {
  return fabs(atan2(accelY, accelZ) * 180.0 / PI);
}

float calculateSeverity(float impact, float speed, float tilt) {
  float impactScore = min(100.0, impact * 6.5);
  float speedScore = min(100.0, (speed / 140.0) * 100.0);
  float tiltScore = min(100.0, (tilt / 90.0) * 100.0);
  float score = (impactScore * 0.55) + (speedScore * 0.30) + (tiltScore * 0.15);
  if (speed < 12 && impact < 7) {
    score *= 0.65;
  }
  return score;
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(300);
    attempts++;
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
  } else {
    Serial.println("\nWiFi unavailable");
  }
}

String buildIsoTimestamp() {
  if (gps.date.isValid() && gps.time.isValid()) {
    char buf[30];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    return String(buf);
  }
  return "2026-01-01T00:00:00Z";
}

bool sendEventToBackend(float impact, float tilt, float speed, float lat, float lon) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  String payload = "{";
  payload += "\"impact\":" + String(impact, 2) + ",";
  payload += "\"tilt\":" + String(tilt, 2) + ",";
  payload += "\"speed\":" + String(speed, 2) + ",";
  payload += "\"lat\":" + String(lat, 6) + ",";
  payload += "\"lon\":" + String(lon, 6) + ",";
  payload += "\"timestamp\":\"" + buildIsoTimestamp() + "\"";
  payload += "}";

  for (int i = 1; i <= BACKEND_RETRIES; i++) {
    HTTPClient http;
    http.begin(API_URL);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    String responseBody = http.getString();
    Serial.printf("POST /event try %d => %d\n", i, code);
    if (code > 0 && code < 500) {
      int idx = responseBody.indexOf("\"event_id\":");
      if (idx >= 0) {
        String ev = responseBody.substring(idx + 11);
        pendingBackendEventId = ev.toInt();
      }
      http.end();
      return true;
    }
    http.end();
    delay(400);
  }
  return false;
}

void triggerLocalAlert(const String& msg) {
  digitalWrite(RED_LED_PIN, HIGH);
  tone(BUZZER_PIN, 1800, 600);
  Serial.println(msg);
}

void clearLocalAlert() {
  digitalWrite(RED_LED_PIN, LOW);
  noTone(BUZZER_PIN);
}

void reportDeviceStatus(bool falseAlarm = false, int eventId = -1) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(DEVICE_REPORT_URL);
  http.addHeader("Content-Type", "application/json");
  String body = "{";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"connected\":true,";
  body += "\"false_alarm\":" + String(falseAlarm ? "true" : "false");
  if (eventId >= 0) {
    body += ",\"event_id\":" + String(eventId);
  }
  body += "}";
  http.POST(body);
  http.end();
}

void ackCommand() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(DEVICE_ACK_URL);
  http.POST("");
  http.end();
}

void pollBackendCommand() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastBackendPollMs < BACKEND_POLL_INTERVAL_MS) return;
  lastBackendPollMs = millis();

  HTTPClient http;
  http.begin(DEVICE_COMMAND_URL);
  int code = http.GET();
  if (code <= 0) {
    http.end();
    return;
  }
  String body = http.getString();
  http.end();
  if (body.indexOf("\"command\":\"ALERT\"") >= 0 || body.indexOf("\"command\":\"EMERGENCY\"") >= 0) {
    triggerLocalAlert("Backend commanded emergency output.");
    delay(2500);
    clearLocalAlert();
    ackCommand();
  } else if (body.indexOf("\"command\":\"CANCEL\"") >= 0) {
    clearLocalAlert();
    ackCommand();
  }
}

void startPendingIncident(float impact, float tilt, float speed, float lat, float lon) {
  incidentPending = true;
  pendingSince = millis();
  pendingImpact = impact;
  pendingTilt = tilt;
  pendingSpeed = speed;
  pendingLat = lat;
  pendingLon = lon;

  Serial.println("Edge crash candidate detected. Waiting 20s for physical cancel button...");
}

void setup() {
  Serial.begin(115200);
  pinMode(CANCEL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH); // green stays always ON
  digitalWrite(RED_LED_PIN, LOW);

  setupMPU();
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  connectWiFi();
  reportDeviceStatus(false, -1);
}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  pollBackendCommand();

  readMPU();
  float impact = calculateImpact();
  float tilt = calculateTilt();
  float speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;

  if (!incidentPending) {
    float score = calculateSeverity(impact, speed, tilt);
    if (impact > 20.0 && score >= EDGE_ALERT_THRESHOLD) {
      float lat = gps.location.isValid() ? gps.location.lat() : 0.0;
      float lon = gps.location.isValid() ? gps.location.lng() : 0.0;

      bool delivered = sendEventToBackend(impact, tilt, speed, lat, lon);
      if (!delivered) {
        Serial.println("Backend unavailable after retries.");
      }

      startPendingIncident(impact, tilt, speed, lat, lon);
    }
  }

  if (incidentPending) {
    if (digitalRead(CANCEL_BUTTON_PIN) == LOW) {
      incidentPending = false;
      clearLocalAlert();
      reportDeviceStatus(true, pendingBackendEventId);
      pendingBackendEventId = -1;
      Serial.println("Physical cancel button pressed. False alarm cleared.");
      delay(350);
    } else if (millis() - pendingSince >= CONFIRM_MS) {
      incidentPending = false;
      float score = calculateSeverity(pendingImpact, pendingSpeed, pendingTilt);
      String level = score >= EDGE_EMERGENCY_THRESHOLD ? "EMERGENCY" : "ALERT";
      String msg = "ImpactX " + level + "! Location: https://maps.google.com/?q=" +
                   String(pendingLat, 6) + "," + String(pendingLon, 6);
      triggerLocalAlert(msg);
      Serial.println("No cancel in 20s -> local emergency response triggered.");
      delay(2500);
      clearLocalAlert();
    }
  }
  delay(300);
}
