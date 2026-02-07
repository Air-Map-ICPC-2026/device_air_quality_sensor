#include <WiFi.h>
#include <ThingSpeak.h>
#include <LiquidCrystal_I2C.h>

/* ================== WiFi Settings ================== */
const char* ssid = "";
const char* password = "";

/* ================= ThingSpeak Settings ============== */
unsigned long channelID = 0; 
const char* writeAPIKey = "";

/* ================= PMS7003 Settings ================= */
#define PMS_RX 16
#define PMS_TX 17

HardwareSerial pmsSerial(2);   // ✅ UART2
WiFiClient client;

uint8_t buffer[32];

/* ================= LCD ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ================= Watchdog ================= */
unsigned long lastHealthyTime = 0;
#define AUTO_RESET_TIMEOUT 60000UL   // ✅ 60 seconds

/* ================= WiFi Reconnect ================= */
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;

/* ================= LCD Rotation ================= */
unsigned long lastLCDUpdate = 0;
uint8_t lcdPage = 0;   // 0=PM1, 1=PM2.5, 2=PM10

/* ================= PMS7003 Read ================= */

bool readPMSdata() {
  if (pmsSerial.available() < 32) return false;

  // Resync to frame header
  while (pmsSerial.available() && pmsSerial.read() != 0x42);
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

/* ================= AQI ================= */

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

  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) ESP.restart();
}

/* ================= Setup ================= */

void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);

  lcd.init();
  lcd.backlight();
  lcd.print("Air Quality");
  delay(2000);
  lcd.clear();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) delay(500);

  ThingSpeak.begin(client);
  lastHealthyTime = millis();
}

/* ================= Loop ================= */

void loop() {

  if (millis() - lastHealthyTime > AUTO_RESET_TIMEOUT) ESP.restart();

  if (millis() - lastWiFiCheck > wifiCheckInterval) {
    lastWiFiCheck = millis();
    ensureWiFiConnected();
  }

  static uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
  static int aqi = 0;

  if (readPMSdata()) {
    pm1  = getValue(10);
    pm25 = getValue(12);
    pm10 = getValue(14);
    aqi  = max(AQI_PM25(pm25), AQI_PM10(pm10));

    lastHealthyTime = millis();   // ✅ feed watchdog
  }

  /* ===== LCD ROTATION (5s) ===== */
  if (millis() - lastLCDUpdate > 5000) {
    lastLCDUpdate = millis();
    lcd.clear();

    // ----- Line 1: PM value + unit -----
    lcd.setCursor(0, 0);
    if (lcdPage == 0) {
      lcd.print("PM1.0: ");
      lcd.print(pm1);
      lcd.print(" ug/m3");
    }
    else if (lcdPage == 1) {
      lcd.print("PM2.5: ");
      lcd.print(pm25);
      lcd.print(" ug/m3");
    }
    else {
      lcd.print("PM10: ");
      lcd.print(pm10);
      lcd.print(" ug/m3");
    }

    // ----- Line 2: AQI always -----
    lcd.setCursor(0, 1);
    lcd.print("AQI: ");
    lcd.print(aqi);

    lcdPage = (lcdPage + 1) % 3;
  }


  /* ===== ThingSpeak ===== */
  static unsigned long lastUpload = 0;
  if (millis() - lastUpload > 20000 && WiFi.status() == WL_CONNECTED) {
    lastUpload = millis();

    ThingSpeak.setField(1, pm1);
    ThingSpeak.setField(2, pm25);
    ThingSpeak.setField(3, pm10);
    ThingSpeak.setField(4, aqi);
    ThingSpeak.writeFields(channelID, writeAPIKey);
  }
}
