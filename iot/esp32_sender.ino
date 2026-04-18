#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <TinyGPSPlus.h>

// ---------------- WIFI ----------------
const char* ssid = "GT20";
const char* password = "12345678";

const char* EVENT_URL = "IP";

// ---------------- MPU ----------------
const int MPU_ADDR = 0x68;
float ax, ay, az;

// ---------------- GPS ----------------
TinyGPSPlus gps;
HardwareSerial gpsSerial(0); // U0R/U0T

// ---------------- PINS ----------------
#define GREEN_LED 12
#define RED_LED   4
#define BUZZER    2
#define BUTTON    13

// ---------------- STATE ----------------
bool alertActive = false;
bool eventSent = false;

unsigned long alertStart = 0;
const unsigned long CANCEL_TIME = 20000;

// ---------------- FILTER ----------------
float filteredImpact = 0;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  digitalWrite(GREEN_LED, HIGH);

  Wire.begin(14, 15);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  gpsSerial.begin(9600);

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
}

// ---------------- MPU ----------------
void readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  ax = (Wire.read() << 8 | Wire.read()) / 16384.0;
  ay = (Wire.read() << 8 | Wire.read()) / 16384.0;
  az = (Wire.read() << 8 | Wire.read()) / 16384.0;
}

// ---------------- IMPACT ----------------
float getImpact() {
  float g = sqrt(ax * ax + ay * ay + az * az);

  // 🔥 FIXED SCALING
  float raw = fabs(g - 1.0) * 20.0;

  filteredImpact = 0.7 * filteredImpact + 0.3 * raw;

  return filteredImpact;
}

// ---------------- SEND ----------------
void sendEvent(float impact, float lat, float lon) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected");
    return;
  }

  Serial.println("🚀 Sending to backend...");

  HTTPClient http;
  http.begin(EVENT_URL);
  http.addHeader("Content-Type", "application/json");

  http.setTimeout(3000);
  http.useHTTP10(true);

  // 🔥 FIXED PAYLOAD
  String payload = "{";
  payload += "\"impact\":" + String(impact,2) + ",";
  payload += "\"speed\":0,";
  payload += "\"lat\":" + String(lat,6) + ",";
  payload += "\"lon\":" + String(lon,6) + ",";
  payload += "\"timestamp\":\"2026-01-01T00:00:00Z\"";
  payload += "}";

  int code = http.POST(payload);

  Serial.print("POST Code: ");
  Serial.println(code);

  http.end();
}

// ---------------- ALERT ----------------
void triggerAlert() {
  alertActive = true;
  eventSent = false;
  alertStart = millis();

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  tone(BUZZER, 2000);

  Serial.println("🚨 ALERT TRIGGERED");
}

// ---------------- RESET ----------------
void resetSystem() {
  alertActive = false;
  eventSent = false;

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  noTone(BUZZER);

  Serial.println("✅ CANCELLED");
}

// ---------------- LOOP ----------------
void loop() {

  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  float lat = gps.location.isValid() ? gps.location.lat() : 0;
  float lon = gps.location.isValid() ? gps.location.lng() : 0;

  readMPU();

  float impact = getImpact();

  // 🔍 DEBUG PRINT
  Serial.println("Impact: " + String(impact));

  // -------- IMPACT DETECTION --------
  if (!alertActive && impact > 20) {
    triggerAlert();
  }

  // -------- SEND EVENT --------
  if (alertActive && !eventSent) {
    sendEvent(impact, lat, lon);
    eventSent = true;
  }

  // -------- CANCEL WINDOW --------
  if (alertActive) {

    if (digitalRead(BUTTON) == LOW) {
      resetSystem();
      delay(300);
      return;
    }

    if (millis() - alertStart > CANCEL_TIME) {
      Serial.println("⏱️ Window ended");
    }
  }

  delay(200);
}
