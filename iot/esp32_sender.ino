#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ---------- Wi-Fi + API ----------
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";
const char* API_URL = "http://YOUR_SERVER_IP:8000/event";

// ---------- MPU6050 ----------
const int MPU_ADDR = 0x68;
float accelX, accelY, accelZ;

// ---------- GPS (NEO-6M) ----------
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

void setupMPU() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0); // wake up MPU6050
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
  return fabs(magnitude - 1.0) * 10.0; // simple shock proxy
}

float calculateTilt() {
  return atan2(accelY, accelZ) * 180.0 / PI;
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void setup() {
  Serial.begin(115200);
  setupMPU();
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17); // RX=16 TX=17
  connectWiFi();
}

void loop() {
  readMPU();

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (!gps.location.isValid()) {
    Serial.println("GPS location not fixed yet, skipping POST...");
    delay(2000);
    return;
  }

  float lat = gps.location.lat();
  float lon = gps.location.lng();
  float lat = gps.location.isValid() ? gps.location.lat() : 18.5204;
  float lon = gps.location.isValid() ? gps.location.lng() : 73.8567;
  float speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;

  float impact = calculateImpact();
  float tilt = fabs(calculateTilt());

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(API_URL);
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"impact\":" + String(impact, 2) + ",";
    payload += "\"tilt\":" + String(tilt, 2) + ",";
    payload += "\"speed\":" + String(speed, 2) + ",";
    payload += "\"lat\":" + String(lat, 6) + ",";
    payload += "\"lon\":" + String(lon, 6) + ",";
    payload += "\"timestamp\":\"2026-03-27T10:30:00Z\"";
    payload += "}";

    int code = http.POST(payload);
    Serial.printf("POST /event => %d\n", code);
    if (code > 0) {
      Serial.println(http.getString());
    }
    http.end();
  }

  delay(2000);
}
