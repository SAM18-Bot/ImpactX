#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "esp_camera.h"

// ---------- Wi-Fi + API ----------
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";
const char* API_URL = "http://YOUR_SERVER_IP:8000/event/camera";

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
const float EDGE_ALERT_THRESHOLD = 30.0;
const float EDGE_EMERGENCY_THRESHOLD = 70.0;
const unsigned long CONFIRM_MS = 20000; // 20 seconds
const int BACKEND_RETRIES = 5;

bool incidentPending = false;
unsigned long pendingSince = 0;
float pendingImpact = 0;
float pendingTilt = 0;
float pendingSpeed = 0;
float pendingLat = 0;
float pendingLon = 0;

// ---------- ESP32-CAM AI Thinker pins ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

bool cameraReady = false;

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

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  cameraReady = (err == ESP_OK);
  Serial.println(cameraReady ? "Camera initialized" : "Camera init failed");
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
  if (WiFi.status() != WL_CONNECTED || !cameraReady) {
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }

  for (int i = 1; i <= BACKEND_RETRIES; i++) {
    HTTPClient http;
    http.begin(API_URL);
    String boundary = "----ImpactXBoundary";
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    String head = "";
    auto addField = [&](const String& name, const String& value) {
      head += "--" + boundary + "\r\n";
      head += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
      head += value + "\r\n";
    };
    addField("impact", String(impact, 2));
    addField("tilt", String(tilt, 2));
    addField("speed", String(speed, 2));
    addField("lat", String(lat, 6));
    addField("lon", String(lon, 6));
    addField("timestamp", buildIsoTimestamp());
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"image\"; filename=\"crash.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";
    int totalLength = head.length() + fb->len + tail.length();
    uint8_t *body = (uint8_t*)malloc(totalLength);
    if (!body) {
      Serial.println("OOM for multipart payload");
      http.end();
      break;
    }
    memcpy(body, head.c_str(), head.length());
    memcpy(body + head.length(), fb->buf, fb->len);
    memcpy(body + head.length() + fb->len, tail.c_str(), tail.length());

    int code = http.POST(body, totalLength);
    free(body);
    Serial.printf("POST /event/camera try %d => %d\n", i, code);
    if (code > 0 && code < 500) {
      http.end();
      esp_camera_fb_return(fb);
      return true;
    }
    http.end();
    delay(400);
  }
  esp_camera_fb_return(fb);
  return false;
}

void triggerLocalAlert(const String& msg) {
  digitalWrite(RED_LED_PIN, HIGH);
  tone(BUZZER_PIN, 1800, 600);
  Serial.println(msg);
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
  setupCamera();
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  connectWiFi();
}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  readMPU();
  float impact = calculateImpact();
  float tilt = calculateTilt();
  float speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;

  if (!incidentPending) {
    float score = calculateSeverity(impact, speed, tilt);
    if (score >= EDGE_ALERT_THRESHOLD) {
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
      digitalWrite(RED_LED_PIN, LOW);
      noTone(BUZZER_PIN);
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
      digitalWrite(RED_LED_PIN, LOW);
      noTone(BUZZER_PIN);
    }
  }
  delay(300);
}
