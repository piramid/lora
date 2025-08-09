#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"
#include <DHT.h>

// OLED display
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// Pins
const int SWITCH_PIN = 47;
const int LED_PIN = 48;
const int DHT_PIN = 3;
#define DHTTYPE DHT22

// DHT sensor
DHT dht(DHT_PIN, DHTTYPE);

// LoRa config
#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 14
#define BUFFER_SIZE 32

// AES key (16 bytes)
const char *userKey = "hyhT676#h~_876s";

mbedtls_aes_context aes;

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

bool ledState = false;
bool lora_idle = true;

// Debounce variables
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastSwitchState = HIGH;
bool switchPressed = false;

// Radio events
static RadioEvents_t RadioEvents;

void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

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

void decryptAES(uint8_t *data, const char *key) {
  uint8_t processedKey[16];
  processKey(key, processedKey, 16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, processedKey, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data, data);
  mbedtls_aes_free(&aes);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dht.begin();

  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.display();

  // LoRa Init
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, 0,
                    7, 1, 8, false,
                    true, 0, 0, false, 3000);
  Radio.Rx(0);

  updateDisplay();
}

void loop() {
  bool currentSwitchState = digitalRead(SWITCH_PIN);
  if (currentSwitchState != lastSwitchState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentSwitchState == LOW && !switchPressed) {
      switchPressed = true;
      toggleLedState();
      sendData();
      updateDisplay();
    }
    if (currentSwitchState == HIGH) {
      switchPressed = false;
    }
  }
  lastSwitchState = currentSwitchState;

  Radio.IrqProcess();
  delay(10);
}

void toggleLedState() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  Serial.print("LED is now ");
  Serial.println(ledState ? "ON" : "OFF");
}

void sendData() {
  if (!lora_idle) return;

  float tempC = dht.readTemperature();
  if (isnan(tempC)) tempC = 0.0; // กรณีอ่านไม่ได้
  // เตรียมข้อมูลส่ง: 1 ไบต์ LED + 6 ไบต์ temp float เป็น string (ตัวอย่าง)
  String payloadStr = String(ledState ? '1' : '0') + String(tempC, 1); // e.g. "134.5"

  memset(txpacket, 0, BUFFER_SIZE);
  strncpy(txpacket, payloadStr.c_str(), BUFFER_SIZE - 1);

  encryptAES((uint8_t *)txpacket, userKey);

  Radio.Send((uint8_t *)txpacket, 16); // AES เข้ารหัส 16 ไบต์
  lora_idle = false;

  Serial.print("Sent encrypted data: ");
  Serial.println(payloadStr);
}

void updateDisplay() {
  float tempC = dht.readTemperature();
  if (isnan(tempC)) tempC = 0.0;

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, "Temp: " + String(tempC, 1) + " C");
  display.drawString(64, 30, ledState ? "LED ON" : "LED OFF");

  if (ledState) display.fillCircle(64, 55, 10);
  else display.drawCircle(64, 55, 10);

  display.display();
}

void OnTxDone() {
  Serial.println("TX done");
  lora_idle = true;
  Radio.Rx(0);
}

void OnTxTimeout() {
  Serial.println("TX timeout");
  lora_idle = true;
  Radio.Rx(0);
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < 16) {
    Serial.println("Invalid packet size");
    Radio.Rx(0);
    return;
  }

  memcpy(rxpacket, payload, size);
  decryptAES((uint8_t *)rxpacket, userKey);
  rxpacket[size] = 0;

  // แยกข้อมูล
  char receivedLed = rxpacket[0];
  String tempStr = String((char *)&rxpacket[1]);
  float receivedTemp = tempStr.toFloat();

  ledState = (receivedLed == '1');
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);

  Serial.print("Received LED: ");
  Serial.print(ledState ? "ON" : "OFF");
  Serial.print(", Temp: ");
  Serial.println(receivedTemp);

  updateDisplay();

  lora_idle = true;
  Radio.Rx(0);
}
