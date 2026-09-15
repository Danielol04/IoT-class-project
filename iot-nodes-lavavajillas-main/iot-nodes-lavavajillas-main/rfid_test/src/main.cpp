#include <Arduino.h>
#include <SPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>

// ESP32 pins
#define SS_PIN 5 // SDA/SS of MFRC522

// Create MFRC522 object (v2) with SPI driver
MFRC522DriverPinSimple ss_pin(SS_PIN);
MFRC522DriverSPI driver(ss_pin, SPI);
MFRC522 rfid(driver);

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("Starting RFID MFRC522v2 reader...");

  // Initialize SPI
  SPI.begin(18, 19, 23, SS_PIN); // SCK, MISO, MOSI, CS
  rfid.PCD_Init();

  Serial.println("Hold a card or key fob near the reader.");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;

  // Read card
  if (!rfid.PICC_ReadCardSerial()) return;

  Serial.print("UID detected: ");
  for (uint8_t i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) Serial.print(":");
  }
  Serial.println();

  delay(1000);
}