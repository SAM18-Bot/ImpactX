#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <TinyGPSPlus.h>

// ---------------- CONFIG ----------------
const char* ssid     = "GT20";
const char* password = "12345678";

// ⬇ Set this to your PC's local IP (run `ipconfig` on Windows / `ip a` on Linux)
const char* BACKEND_IP = "IP";
const int   BACKEND_PORT = 8000;
const char* DEVICE_ID    = "esp32-001"; // unique ID for your device

// Derived URLs
String EVENT_URL;
String COMMAND_URL;

// ---------------- MPU ----------------
const int MPU_ADDR = 0x68;
float ax, ay, az;

// ---------------- GPS ----------------
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

// ---------------- PINS ----------------
#define GREEN_LED 33
#define RED_LED    2
#define BUZZER    26
#define BUTTON     4

// ---------------- STATE ----------------
bool alertActive    = false;
bool waitingBackend = false;
unsigned long eventTime = 0;

// ---------------- FILTER ----------------
float filteredImpact = 0;
int   rolloverCount  = 0;

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);
  pinMode(BUZZER,    OUTPUT);
  pinMode(BUTTON,    INPUT_PULLUP);

  digitalWrite(GREEN_LED, HIGH);

  Wire.begin(14, 15);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  // Build URLs at runtime
  EVENT_URL   = String("http://") + BACKEND_IP + ":" + BACKEND_PORT + "/event";
  COMMAND_URL = String("http://") + BACKEND_IP + ":" + BACKEND_PORT + "/device/" + DEVICE_ID + "/command";

  Serial.println("EVENT_URL:   " + EVENT_URL);
  Serial.println("COMMAND_URL: " + COMMAND_URL);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
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
  float g   = sqrt(ax * ax + ay * ay + az * az);
  float raw = fabs(g - 1.0) * 5.0;
  filteredImpact = 0.8 * filteredImpact + 0.2 * raw;
  return filteredImpact;
}

// ---------------- ANGLES ----------------
void getAngles(float &pitch, float &roll) {
  pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180 / PI;
  roll  = atan2(ay, sqrt(ax * ax + az * az)) * 180 / PI;
}

// ---------------- SEND EVENT ----------------
void sendEvent(float impact, float speed, float lat, float lon) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(EVENT_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"impact\":"  + String(impact, 2) + ",";
  payload += "\"speed\":"   + String(speed,  2) + ",";
  payload += "\"lat\":"     + String(lat,    6) + ",";
  payload += "\"lon\":"     + String(lon,    6);
  payload += "}";

  int code = http.POST(payload);
  Serial.print("Event POST => "); Serial.println(code);
  http.end();
}

// ---------------- COMMAND POLLING ----------------
void checkCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(COMMAND_URL);

  int code = http.GET();
  if (code == 200) {
    String res = http.getString();
    Serial.print("Command: "); Serial.println(res);

    if (res.indexOf("ALERT") >= 0 || res.indexOf("EMERGENCY") >= 0) triggerAlert();
    if (res.indexOf("STOP")  >= 0 || res.indexOf("CANCEL")    >= 0) resetSystem();
  }
  http.end();
}

// ---------------- ALERT ----------------
void triggerAlert() {
  alertActive = true;
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   HIGH);
  tone(BUZZER, 2000);
  Serial.println("ALERT TRIGGERED");
}

// ---------------- RESET ----------------
void resetSystem() {
  alertActive    = false;
  waitingBackend = false;
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED,   LOW);
  noTone(BUZZER);
  Serial.println("RESET");
}

// ---------------- LOOP ----------------
void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  float speed = gps.speed.isValid()    ? gps.speed.kmph()    : 0;
  float lat   = gps.location.isValid() ? gps.location.lat()  : 0;
  float lon   = gps.location.isValid() ? gps.location.lng()  : 0;

  readMPU();
  float impact = getImpact();

  float pitch, roll;
  getAngles(pitch, roll);

  // -------- ROLLOVER DETECTION --------
  if (abs(pitch) > 60 || abs(roll) > 60) rolloverCount++;
  else rolloverCount = 0;

  if (rolloverCount >= 3 && !alertActive) triggerAlert();

  // -------- IMPACT DETECTION --------
  if (!alertActive && impact > 20) {
    if (WiFi.status() == WL_CONNECTED) {
      sendEvent(impact, speed, lat, lon);
      waitingBackend = true;
      eventTime = millis();
    } else {
      triggerAlert(); // offline fallback
    }
  }

  // -------- BACKEND TIMEOUT --------
  if (waitingBackend && millis() - eventTime > 5000) {
    triggerAlert();
    waitingBackend = false;
  }

  // -------- COMMAND POLL --------
  checkCommand();

  // -------- BUTTON CANCEL --------
  if (alertActive && digitalRead(BUTTON) == LOW) {
    resetSystem();
    delay(300);
  }

  delay(200);
}
