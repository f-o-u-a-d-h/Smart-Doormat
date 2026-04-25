//Updated main code:
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <HX711.h>
#include "BluetoothSerial.h"

//package
bool packagePresent = false;
float packageThreshold = 1.0; // grams NEEDS ADJUSTING AFTER CALIBRATION
//Bluetooth
BluetoothSerial SerialBT;
unsigned long lastBTTime = 0;
const unsigned long btInterval = 3000; // every 3 seconds
// ================= OLED (SH1106) =================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ================= RFID =================
#define SS_PIN 5
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ================= DHT =================
#define DHTPIN 21
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ================= LDR =================
const int ldrPin = 34;
int ldrThreshold = 1500;
String lightStatus = "Bright";
float luxValue = 0.0;  // NEW

// ================= FSR =================
const int fsr1Pin = 35;   // outside
const int fsr2Pin = 27;   // inside
int fsrThreshold = 1500;

int directionState = 0;
unsigned long firstTriggerTime = 0;
unsigned long timeoutWindow = 2000;

int peopleCount = 0;

// ================= Proximity Sensor =================
#define PROX_PIN 4

String proximityStatus = "No Object";
unsigned long lastProxTime = 0;
const unsigned long proxInterval = 300;

// ================= HX711 Load Cell =================
#define HX711_DT  33
#define HX711_SCK 32
HX711 scale;
float calibrationFactor = 7050.0;
float mass = 0.0;
unsigned long lastScaleTime = 0;
const unsigned long scaleInterval = 500;

// ================= Sensor values =================
float temperature = 0;
float humidity = 0;
String lastCardUID = "None";

// ================= Timers =================
unsigned long lastDHTTime = 0;
unsigned long lastLDRTime = 0;
unsigned long lastOLEDTime = 0;
unsigned long lastCardDisplayTime = 0;

const unsigned long dhtInterval = 2000;
const unsigned long ldrInterval = 500;
const unsigned long oledInterval = 500;
const unsigned long cardMessageDuration = 3000;


// ================= RFID Card Database =================
struct Card {
  byte uid[7];
  byte uidSize;
  const char* name;
};

Card knownCards[] = {
  {{0x04, 0x38, 0x0F, 0xF2, 0xCE, 0x76, 0x80}, 7, "Vera"},
  {{0x04, 0x7B, 0x74, 0xDA, 0xCE, 0x76, 0x80}, 7, "Serena"},
  {{0x04, 0x59, 0x5C, 0x5A, 0x33, 0x7B, 0x80}, 7, "FOuad"},
  {{0x04, 0x75, 0x55, 0xDA, 0xCE, 0x76, 0x80}, 7, "Abed"}
};
const int NUM_CARDS = sizeof(knownCards) / sizeof(knownCards[0]);

const char* getCardName(byte* uid, byte uidSize) {
  for (int i = 0; i < NUM_CARDS; i++) {
    if (uidSize == knownCards[i].uidSize &&
        memcmp(uid, knownCards[i].uid, uidSize) == 0) {
      return knownCards[i].name;
    }
  }
  return nullptr;
}

String lastCardName = "";


// ================= OLED update =================
void updateOLED() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.drawStr(0, 8,  "People:");
  char peopleBuffer[10];
  sprintf(peopleBuffer, "%d", peopleCount);
  u8g2.drawStr(60, 8, peopleBuffer);

  u8g2.drawStr(0, 18, "Temp:");
  char tempBuffer[16];
  dtostrf(temperature, 4, 1, tempBuffer);
  u8g2.drawStr(60, 18, tempBuffer);
  u8g2.drawStr(100, 18, "C");

  u8g2.drawStr(0, 28, "Hum:");
  char humBuffer[16];
  dtostrf(humidity, 4, 1, humBuffer);
  u8g2.drawStr(60, 28, humBuffer);
  u8g2.drawStr(100, 28, "%");

  // NEW: show both status and lux on same line
  u8g2.drawStr(0, 38, "Light:");
  u8g2.drawStr(40, 38, lightStatus.c_str());
  char luxBuffer[16];
  dtostrf(luxValue, 5, 0, luxBuffer);
  u8g2.drawStr(75, 38, luxBuffer);
  u8g2.drawStr(110, 38, "lx");

  u8g2.drawStr(0, 48, "Prox:");
  u8g2.drawStr(60, 48, proximityStatus.c_str());

  u8g2.drawStr(0, 58, "Mass:");
  char massBuffer[16];
  dtostrf(mass, 6, 1, massBuffer);
  u8g2.drawStr(60, 58, massBuffer);
  u8g2.drawStr(110, 58, "g");

  if (millis() - lastCardDisplayTime < cardMessageDuration) {
    u8g2.setFont(u8g2_font_5x8_tr);
    if (lastCardName == "Unknown") {
      u8g2.drawStr(0, 64, "Unknown Card");
    } else {
      String welcome = "Hi, " + lastCardName + "!";
      u8g2.drawStr(0, 64, welcome.c_str());
    }
  }

  u8g2.sendBuffer();
}
String getWeatherAdvice() {
  if (temperature < 15) return "Wear a jacket";
  else if (temperature > 30) return "Stay hydrated";
  else if (humidity > 80) return "Bring an umbrella";
  else return "Weather is nice";
}
void setup() {
  SerialBT.begin("SmartDoormat"); // Bluetooth name shown on phone
  Serial.println("Bluetooth started. Pair with 'SmartDoormat'");
  Serial.begin(115200);

  Wire.begin(25, 26);
  u8g2.begin();

  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  dht.begin();

  pinMode(PROX_PIN, INPUT);

  scale.begin(HX711_DT, HX711_SCK);
  scale.set_scale(calibrationFactor);
  scale.tare();
  Serial.println("Load cell ready. Tared.");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 25, "Starting...");
  u8g2.drawStr(10, 45, "Smart Doormat");
  u8g2.sendBuffer();

  Serial.println("System started");
}

void loop() {
  unsigned long now = millis();

  // ================= RFID =================
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    lastCardUID = "Card: ";
    Serial.print("Card UID: ");

    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) { Serial.print("0"); lastCardUID += "0"; }
      Serial.print(mfrc522.uid.uidByte[i], HEX);
      Serial.print(" ");
      lastCardUID += String(mfrc522.uid.uidByte[i], HEX);
      if (i < mfrc522.uid.size - 1) lastCardUID += " ";
    }
    Serial.println();

    const char* name = getCardName(mfrc522.uid.uidByte, mfrc522.uid.size);
    if (name) {
      lastCardName = String(name);
      Serial.printf("Welcome, %s!\n", name);
      String msg = String(name) + " entered the house\n";
      SerialBT.print(msg);
    } else {
      lastCardName = "Unknown";
      Serial.println("Unknown card.");
      SerialBT.print("Unknown person tried to enter\n");
    }

    lastCardDisplayTime = now;
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  // ================= DHT =================
  if (now - lastDHTTime >= dhtInterval) {
    lastDHTTime = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;

      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.print(" C | Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
  }

  // ================= LDR =================
  if (now - lastLDRTime >= ldrInterval) {
    lastLDRTime = now;

    int ldrValue = analogRead(ldrPin);

    // Convert raw ADC value to lux (approximate formula for common LDR + 10k resistor divider)
    float voltage = ldrValue * (3.3 / 4095.0);
    float resistance = (3.3 - voltage) == 0 ? 1 : (10000.0 * voltage) / (3.3 - voltage);
    luxValue = 500.0 / (resistance / 1000.0);  // NEW: approximate lux

    if (ldrValue > ldrThreshold) {
      lightStatus = "Bright";
    } else {
      lightStatus = "Dark";
    }

    Serial.print("LDR: ");
    Serial.print(ldrValue);
    Serial.print(" | Lux: ");
    Serial.print(luxValue, 0);
    Serial.print(" | ");
    Serial.println(lightStatus);
  }

  // ================= Proximity Sensor =================
  if (now - lastProxTime >= proxInterval) {
    lastProxTime = now;

    int proxValue = digitalRead(PROX_PIN);

    Serial.print("Proximity pin reading: ");
    Serial.println(proxValue);

    if (proxValue == 1) {
      proximityStatus = "Object";
    } else {
      proximityStatus = "No Object";
    }
  }

  // ================= HX711 Load Cell =================
  if (now - lastScaleTime >= scaleInterval) {
    lastScaleTime = now;

    if (scale.is_ready()) {
      mass = scale.get_units(3);

      Serial.print("Mass: ");
      Serial.print(mass, 1);
      Serial.println(" g");

  // Detect package placed
    if (!packagePresent && mass > packageThreshold) {
      packagePresent = true;

      String msg = "Package detected: " + String(mass, 1) + " g\n";
      SerialBT.print(msg);
  }

  // Detect package removed
    if (packagePresent && mass < 1.0) {//CHANGE AFTER CALIBRATION
      packagePresent = false;

      SerialBT.print("Package removed\n");
  }

} else {
  Serial.println("HX711 not ready");
}
  }

  // ================= FSR direction =================
  int fsr1Value = analogRead(fsr1Pin);
  int fsr2Value = analogRead(fsr2Pin);

  bool fsr1Pressed = (fsr1Value > fsrThreshold);
  bool fsr2Pressed = (fsr2Value > fsrThreshold);

  if (directionState == 0) {
    if (fsr1Pressed) {
      directionState = 1;
      firstTriggerTime = now;
    }
    else if (fsr2Pressed) {
      directionState = 2;
      firstTriggerTime = now;
    }
  }

  else if (directionState == 1) {
    if (fsr2Pressed) {
      peopleCount++;

      Serial.print("ENTERED | People count: ");
      Serial.println(peopleCount);

      directionState = 0;
      delay(300);
    }
    else if (now - firstTriggerTime > timeoutWindow) {
      directionState = 0;
    }
  }

  else if (directionState == 2) {
    if (fsr1Pressed) {
      if (peopleCount > 0) {
        peopleCount--;
      }

      Serial.print("LEFT | People count: ");
      Serial.println(peopleCount);

      directionState = 0;
      delay(300);
    }
    else if (now - firstTriggerTime > timeoutWindow) {
      directionState = 0;
    }
  }
  if (millis() - lastBTTime >= btInterval) {
  lastBTTime = millis();

  String message = "";
  message += "Temp: " + String(temperature) + " C\n";
  message += "Humidity: " + String(humidity) + " %\n";
  message += "Advice: " + getWeatherAdvice() + "\n";
  message += "People: " + String(peopleCount) + "\n";
  message += "----------------------\n";

  SerialBT.print(message);   // send to phone
  Serial.print(message);     // also show in Serial Monitor
}
  // ================= OLED =================
  if (now - lastOLEDTime >= oledInterval) {
    lastOLEDTime = now;
    updateOLED();
  }
}