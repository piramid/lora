#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"
#include <DHT.h>

// OLED config
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// DHT22 config
#define DHTPIN 3
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Pins
#define SWITCH_PIN 47
#define LED_PIN 48

// AES key
const char *userKey = "hyhT676#h~_876s";

mbedtls_aes_context aes;

#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 14

// LoRa parameters ตามเดิม
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

#define BUFFER_SIZE 32
char txpacket[BUFFER_SIZE];

bool ledState = false;
bool switchState = false;
bool lastSwitchState = false;

static RadioEvents_t RadioEvents;
bool lora_idle = true;

void processKey(const char *userKey, uint8_t *processedKey, size_t keySize) {
  memset(processedKey, 0, keySize);
  size_t len = strlen(userKey);
  if (len > keySize) len = keySize;
  memcpy(processedKey, userKey, len);
}

void encryptAES(uint8_t *data, const char *key) {
  uint8_t processedKey[16];
  processKey(key, processedKey, 16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, processedKey, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data, data);
  mbedtls_aes_free(&aes);
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  display.init();
  display.setFont(ArialMT_Plain_10);

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.TxDone = []() {
    Serial.println("TX done");
    lora_idle = true;
  };
  RadioEvents.TxTimeout = []() {
    Serial.println("TX timeout");
    lora_idle = true;
  };

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  // ใช้ฟังก์ชันแบบเดิมที่ไม่ error
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

  Radio.Rx(0);
}

void loop() {
  // อ่านสวิตช์และควบคุม LED
  switchState = !digitalRead(SWITCH_PIN); // active low switch
  if (switchState != lastSwitchState && switchState == true) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
  lastSwitchState = switchState;

  // อ่านค่า DHT22
  float tempC = dht.readTemperature();
  float humidity = dht.readHumidity();

  // สร้างข้อมูลส่ง (format: "T:25.3,H:60,L:1")
  char dataStr[BUFFER_SIZE];
  snprintf(dataStr, BUFFER_SIZE, "T:%.1f,H:%.1f,L:%d", tempC, humidity, ledState ? 1 : 0);

  // เข้ารหัส AES
  uint8_t data[BUFFER_SIZE] = {0};
  strncpy((char *)data, dataStr, BUFFER_SIZE - 1);
  encryptAES(data, userKey);

  if (lora_idle) {
    lora_idle = false;
    Radio.Send(data, BUFFER_SIZE);
    Serial.printf("Sent: %s\n", dataStr);
  }
  Radio.IrqProcess();

  // แสดงผล OLED
  display.clear();
  display.drawString(0, 0, "Sender Side");
  display.drawString(0, 15, String("Temp: ") + String(tempC, 1) + " C");
  display.drawString(0, 30, String("Humidity: ") + String(humidity, 1) + " %");
  display.drawString(0, 45, String("LED: ") + (ledState ? "ON" : "OFF"));
  display.display();

  delay(200);
}
