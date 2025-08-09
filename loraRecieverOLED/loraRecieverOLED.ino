#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "images.h"
#include "LoRaWan_APP.h"
#include "Arduino.h"

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26
#define LORA_BAND 915E6  // เปลี่ยนตามความถี่ที่ใช้งาน เช่น 868E6, 433E6

#define SDA_OLED 21
#define SCL_OLED 22
#define RST_OLED -1

// OLED Display setup
SSD1306Wire display(0x3C, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ฟังก์ชันเปิดไฟเลี้ยงจอ (Vext)
void VextON() {
  pinMode(21, OUTPUT); // Vext pin สำหรับบางบอร์ด
  digitalWrite(21, LOW);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // เปิดไฟเลี้ยงจอ OLED
  VextON();
  delay(100);

  // OLED init
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "LoRa Receiver Initializing...");
  display.display();

  // LoRa pins init
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  // Start LoRa
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Starting LoRa failed!");
    display.drawString(0, 16, "LoRa Init Failed!");
    display.display();
    while (1);  
  }

  Serial.println("LoRa Receiver Ready");
  display.clear();
  display.drawString(0, 0, "LoRa Receiver Ready");
  display.display();
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    Serial.print("Received: ");
    Serial.println(incoming);
    Serial.print("RSSI: ");
    Serial.println(rssi);

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "Received:");
    display.drawString(0, 12, incoming);
    display.drawString(0, 28, "RSSI: " + String(rssi));
    display.display();
  }
}
