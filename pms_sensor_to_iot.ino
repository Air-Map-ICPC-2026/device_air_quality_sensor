#include <WiFi.h>
#include <ThingSpeak.h>
#include <SoftwareSerial.h>

/* ================== WiFi Settings ================== */
const char* ssid = ""; // Put your wifi name here
const char* password = ""; // PUt your wifi password here

/* ================= ThingSpeak Settings ============== */
unsigned long channelID = 3230888;
const char* writeAPIKey = ""; // Put your API ThingSpeak Api key here

/* ================= PMS7003 Settings ================= */
#define PMS_RX 16
#define PMS_TX 17

SoftwareSerial pmsSerial(PMS_RX, PMS_TX);
WiFiClient client;

uint8_t buffer[32];

/* ================= PMS Functions ================= */

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
  for (int i = 0; i < 30; i++) {
    sum += buffer[i];
  }

  uint16_t checksum = (buffer[30] << 8) | buffer[31];
  return (sum == checksum);
}

uint16_t getValue(int index) {
  return (buffer[index] << 8) | buffer[index + 1];
}

/* ================= Setup ================= */

void setup() {
  Serial.begin(115200);
  delay(1000);

  pmsSerial.begin(9600);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  ThingSpeak.begin(client);
}

/* ================= Loop ================= */

void loop() {
  if (readPMSdata()) {
    uint16_t pm1  = getValue(10);
    uint16_t pm25 = getValue(12);
    uint16_t pm10 = getValue(14);

    Serial.println("---- PMS7003 Data ----");
    Serial.print("PM1.0 : "); Serial.println(pm1);
    Serial.print("PM2.5 : "); Serial.println(pm25);
    Serial.print("PM10  : "); Serial.println(pm10);

    ThingSpeak.setField(1, pm1);
    ThingSpeak.setField(2, pm25);
    ThingSpeak.setField(3, pm10);

    int status = ThingSpeak.writeFields(channelID, writeAPIKey);

    if (status == 200) {
      Serial.println("ThingSpeak update successful");
    } else {
      Serial.print("ThingSpeak error: ");
      Serial.println(status);
    }
  }

  // ThingSpeak minimum update interval is 15 seconds
  delay(20000);
}
