/* LoRa AES Receiver (Heltec LoRa 32 V3)
   - Expects packet length = N*16
   - First byte after decrypt = payload length
*/

#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"

// ---------- OLED ----------
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ---------- AES key (same as sender) ----------
const char *userKey = "hyhT676#h~_876s";

// ---------- License if required ----------
// uint32_t license[4] = {...};
// Mcu.setlicense(license, HELTEC_BOARD);

#define RF_FREQUENCY 915000000UL
#define MAX_BUF 64
#define SIGNAL_TIMEOUT 5000UL
#define MIN_RSSI -120
#define MAX_RSSI -50

static RadioEvents_t RadioEvents;
bool lora_idle = true;
unsigned long lastRxTime = 0;

void decryptAES_blocks(uint8_t *data, size_t len, const char *key) {
  mbedtls_aes_context aes;
  uint8_t processedKey[16] = {0};
  size_t klen = strlen(key); if (klen > 16) klen = 16;
  memcpy(processedKey, key, klen);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, processedKey, 128);
  for (size_t off = 0; off < len; off += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + off, data + off);
  }
  mbedtls_aes_free(&aes);
}

void displaySignal(int16_t rssi) {
  int percent = map(constrain(rssi, MIN_RSSI, MAX_RSSI), MIN_RSSI, MAX_RSSI, 0, 100);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  String strength = String(percent) + "% [";
  for (int i = 0; i < 5; i++) {
      strength += (percent > (i * 20)) ? "|" : " ";
  }
  strength += "]";
  display.drawString(128, 50, strength);
}


// Radio callback
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  lastRxTime = millis();

  Serial.printf("RX got size=%d RSSI=%d\n", size, rssi);

  if (size == 0 || (size % 16) != 0 || size > MAX_BUF) {
    Serial.println("Invalid packet size");
    lora_idle = true;
    return;
  }

  uint8_t buf[MAX_BUF];
  memset(buf, 0, sizeof(buf));
  memcpy(buf, payload, size);

  // debug HEX (optional)
  Serial.print("HEX: ");
  for (int i=0;i<size;i++) Serial.printf("%02X ", buf[i]);
  Serial.println();

  // decrypt all blocks
  decryptAES_blocks(buf, size, userKey);

  uint8_t msgLen = buf[0];
  if (msgLen > size - 1) msgLen = size - 1;
  char msg[MAX_BUF];
  memset(msg, 0, sizeof(msg));
  memcpy(msg, buf + 1, msgLen);
  msg[msgLen] = '\0';

  Serial.printf("Decrypted len=%d msg=%s\n", msgLen, msg);

  // display on OLED
  String message = String(msg);

  displayMainData(message, rssi);

  lora_idle = true;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // if needed: Mcu.setlicense(license, HELTEC_BOARD);

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0,0,"LoRa AES Receiver");
  display.display();

  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);

  // enter receive
  Radio.Rx(0);
  lastRxTime = millis();
}

void displayMainData(String message, int16_t rssi) {
  display.clear();  
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  display.setFont(ArialMT_Plain_10); // ใช้ฟอนต์ขนาด 10pt เล็กลงจาก 16pt
  display.drawString(0, 0, "LoRa AES Receiver");

  display.setFont(ArialMT_Plain_10); // ข้อความหลักก็ใช้ขนาด 10pt ให้พอดีกว่า
  // ถ้าข้อความยาวเกิน ให้ตัดความยาวไม่เกิน 20 ตัวอักษร (ประมาณ)
  if (message.length() > 20) {
    message = message.substring(0, 20) + "...";
  }
  display.drawString(0, 15, message);

  displaySignal(rssi);  // แสดงแถบสัญญาณ
  
  display.display();
}

void noSignalCheck()
{
    // Automatic "No Signal" after timeout
    if (millis() - lastRxTime > SIGNAL_TIMEOUT) {
        display.clear();
        display.setTextAlignment(TEXT_ALIGN_CENTER);  // ตั้งให้ข้อความอยู่กึ่งกลางแนวนอน
        display.setFont(ArialMT_Plain_16);             // ตั้งฟอนต์ขนาดเหมาะสม
        display.drawString(64, 32 - 8, "NO SIGNAL");  // 32 คือกลางจอแนวตั้ง, ลบ 8 เพื่อเลื่อนขึ้นเล็กน้อย (เพราะข้อความสูงประมาณ 16)
        display.display();
    }
}


void loop() {
  // ถ้าไม่มีสัญญาณนานเกินไป ให้แสดง No Signal
  if (millis() - lastRxTime > SIGNAL_TIMEOUT) {
    noSignalCheck();
  }

  Radio.IrqProcess();
  delay(5);
}
