/* LoRa AES Sender (Heltec LoRa 32 V3)
   - AES-128-ECB (mbedtls)
   - Buffer format: [len(1)][payload bytes][padding... to 16*n]
   - Safe: supports message up to MAX_MSG (default 47)
*/

#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"
#include "DHT.h"

// ---------- OLED ----------
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ---------- DHT ----------
#define DHTPIN 3        // ปรับขาให้ถูกต้อง
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ---------- AES key (must be identical on receiver) ----------
const char *userKey = "hyhT676#h~_876s"; // <= ใช้ key เดียวกันทั้งคู่

// ---------- License (ถ้าจำเป็น) ----------
// uint32_t license[4] = {0x...,0x...,0x...,0x...};
// Mcu.setlicense(license, HELTEC_BOARD);

// ---------- LoRa / buffer config ----------
#define RF_FREQUENCY    915000000UL
#define TX_OUTPUT_POWER 5
#define MAX_MSG         47   // max user message bytes (1 + 47 = 48 -> multiple of 16)
#define MAX_BUF         64   // must be >= padded length (here 48 or 64 are OK)

static RadioEvents_t RadioEvents;
bool lora_idle = true;

// ---------- AES helpers ----------
void encryptAES_blocks(uint8_t *data, size_t len, const char *key) {
  mbedtls_aes_context aes;
  uint8_t processedKey[16] = {0};
  size_t klen = strlen(key); if (klen > 16) klen = 16;
  memcpy(processedKey, key, klen);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, processedKey, 128);
  for (size_t off = 0; off < len; off += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data + off, data + off);
  }
  mbedtls_aes_free(&aes);
}

// ---------- Radio callbacks ----------
void OnTxDone(void) {
  Serial.println("[TX] Done");
  lora_idle = true;
}
void OnTxTimeout(void) {
  Serial.println("[TX] Timeout");
  lora_idle = true;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // If needed: set license here before Mcu.begin
  // Mcu.setlicense(license, HELTEC_BOARD);

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // OLED init
  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0,0,"LoRa AES Sender");
  display.display();

  // DHT init
  dht.begin();

  // Radio init
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);
}

unsigned long lastSend = 0;
void loop() {
  // send every 5 seconds
  if (millis() - lastSend < 5000) {
    Radio.IrqProcess();
    delay(5);
    return;
  }
  lastSend = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("DHT read fail");
    display.clear();
    display.drawString(0,0,"DHT read fail");
    display.display();
    return;
  }

  char message[MAX_MSG+1];
  snprintf(message, sizeof(message), "Temp:%.1fC Hum:%.1f%%", t, h);
  size_t msgLen = strlen(message);
  if (msgLen > MAX_MSG) msgLen = MAX_MSG;

  // prepare buffer: [len][msg]
  uint8_t buf[MAX_BUF];
  memset(buf, 0, sizeof(buf));
  buf[0] = (uint8_t)msgLen;
  memcpy(buf + 1, message, msgLen);

  size_t total = 1 + msgLen;
  size_t bufLen = ((total + 15) / 16) * 16; // padded up to 16-block

  // encrypt
  encryptAES_blocks(buf, bufLen, userKey);

  // show on OLED (we show original cleartext so user-friendly)
  display.clear();
  display.drawString(0, 0, "Sending:");
  display.drawString(0, 12, String(message));
  display.display();

  // send encrypted blocks
  Radio.Send(buf, bufLen);
  lora_idle = false;

  // process IRQs until TX done
  unsigned long t0 = millis();
  while (!lora_idle && millis() - t0 < 5000) {
    Radio.IrqProcess();
    delay(5);
  }
}
