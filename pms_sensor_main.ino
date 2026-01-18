#include <WiFi.h>
#include <ThingSpeak.h>
#include <SoftwareSerial.h>

/* ================== WiFi Settings ================== */
const char* ssid = "";
const char* password = "";

/* ================= ThingSpeak Settings ============== */
unsigned long channelID = 0;
const char* writeAPIKey = "";

/* ================= PMS7003 Settings ================= */
#define PMS_RX 16
#define PMS_TX 17

SoftwareSerial pmsSerial(PMS_RX, PMS_TX);
WiFiClient client;

uint8_t buffer[32];

/* ================= Software Watchdog ================= */
unsigned long lastHealthyTime = 0;
#define AUTO_RESET_TIMEOUT 120000UL   // 🔧 2 minutes (milliseconds)

/* ================= WiFi Reconnect =================== */
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;

/* ================= PMS7003 Read ================= */

bool readPMSdata() {
  if (pmsSerial.available() < 32) return false;

  if (pmsSerial.read() != 0x42) return false;
  if (pmsSerial.read() != 0x4D) return false;

  buffer[0] = 0x42;
  buffer[1] = 0x4D;

  for (int i = 2; i < 32; i++) {
    buffer[i] = pmsSerial.read();
  }

  uint16_t sum = 0;
  for (int i = 0; i < 30; i++) sum += buffer[i];

  uint16_t checksum = (buffer[30] << 8) | buffer[31];
  return (sum == checksum);
}

uint16_t getValue(int index) {
  return (buffer[index] << 8) | buffer[index + 1];
}

/* ================= AQI FUNCTIONS ================= */

int calcAQI(float C, float Cl, float Ch, int Il, int Ih) {
  return (int)((Ih - Il) * (C - Cl) / (Ch - Cl) + Il);
}

int AQI_PM25(float pm25) {
  if (pm25 <= 12.0)   return calcAQI(pm25, 0.0, 12.0, 0, 50);
  if (pm25 <= 35.4)   return calcAQI(pm25, 12.1, 35.4, 51, 100);
  if (pm25 <= 55.4)   return calcAQI(pm25, 35.5, 55.4, 101, 150);
  if (pm25 <= 150.4)  return calcAQI(pm25, 55.5, 150.4, 151, 200);
  if (pm25 <= 250.4)  return calcAQI(pm25, 150.5, 250.4, 201, 300);
  if (pm25 <= 350.4)  return calcAQI(pm25, 250.5, 350.4, 301, 400);
  return calcAQI(pm25, 350.5, 500.4, 401, 500);
}

int AQI_PM10(float pm10) {
  if (pm10 <= 54)    return calcAQI(pm10, 0, 54, 0, 50);
  if (pm10 <= 154)   return calcAQI(pm10, 55, 154, 51, 100);
  if (pm10 <= 254)   return calcAQI(pm10, 155, 254, 101, 150);
  if (pm10 <= 354)   return calcAQI(pm10, 255, 354, 151, 200);
  if (pm10 <= 424)   return calcAQI(pm10, 355, 424, 201, 300);
  if (pm10 <= 504)   return calcAQI(pm10, 425, 504, 301, 400);
  return calcAQI(pm10, 505, 604, 401, 500);
}

/* ================= WiFi Helper ================= */

void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi disconnected, reconnecting...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 30000) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed — restarting ESP32");
    delay(100);
    ESP.restart();
  }
}

/* ================= Setup ================= */

void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  ThingSpeak.begin(client);

  lastHealthyTime = millis();   // initialize watchdog timer
}

/* ================= Loop ================= */

void loop() {

  /* 🔴 SOFTWARE WATCHDOG */
  if (millis() - lastHealthyTime > AUTO_RESET_TIMEOUT) {
    Serial.println("System inactive too long — restarting ESP32");
    delay(100);
    ESP.restart();
  }

  if (millis() - lastWiFiCheck > wifiCheckInterval) {
    lastWiFiCheck = millis();
    ensureWiFiConnected();
  }

  if (readPMSdata()) {

    float pm1  = getValue(10);
    float pm25 = getValue(12);
    float pm10 = getValue(14);

    int aqi = max(AQI_PM25(pm25), AQI_PM10(pm10));

    Serial.println("---- Air Quality ----");
    Serial.print("PM1.0  : "); Serial.print(pm1);  Serial.println(" µg/m³");
    Serial.print("PM2.5  : "); Serial.print(pm25); Serial.println(" µg/m³");
    Serial.print("PM10   : "); Serial.print(pm10); Serial.println(" µg/m³");
    Serial.print("AQI    : "); Serial.println(aqi);

    if (WiFi.status() == WL_CONNECTED) {
      ThingSpeak.setField(1, pm1);
      ThingSpeak.setField(2, pm25);
      ThingSpeak.setField(3, pm10);
      ThingSpeak.setField(4, aqi);

      
      int status = ThingSpeak.writeFields(channelID, writeAPIKey);
      Serial.println(status == 200 ? "ThingSpeak update OK" : "ThingSpeak update FAILED");


      lastHealthyTime = millis();   // ✅ system is healthy
    }
  }

  delay(20000);   // ThingSpeak rate limit
}
