#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"

// OLED config
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// AES key (เหมือนกับฝั่งส่ง)
const char *userKey = "hyhT676#h~_876s";

mbedtls_aes_context aes;

#define RF_FREQUENCY 915000000
#define BUFFER_SIZE 32
#define SIGNAL_TIMEOUT 5000UL
#define MIN_RSSI -120
#define MAX_RSSI -50

// Pins (ปรับตามบอร์ด)
#define SWITCH_PIN 47
#define LED_PIN 48

// ตัวแปรสถานะ
bool ledState = false;
bool switchState = false;
bool lastSwitchState = false;

static RadioEvents_t RadioEvents;
bool lora_idle = true;
unsigned long lastRxTime = 0;

void processKey(const char *userKey, uint8_t *processedKey, size_t keySize) {
  memset(processedKey, 0, keySize);
  size_t len = strlen(userKey);
  if (len > keySize) len = keySize;
  memcpy(processedKey, userKey, len);
}

void decryptAES(uint8_t *data, const char *key) {
  uint8_t processedKey[16];
  processKey(key, processedKey, 16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, processedKey, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data, data);
  mbedtls_aes_free(&aes);
}

void encryptAES(uint8_t *data, const char *key) {
  uint8_t processedKey[16];
  processKey(key, processedKey, 16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, processedKey, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, data, data);
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

void displayMainData(float temp, float humid, bool led, int16_t rssi) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);

  display.drawString(0, 0, "LoRa AES Receiver");
  display.drawString(0, 15, "Temp: " + String(temp, 1) + " C");
  display.drawString(0, 30, "Humidity: " + String(humid, 1) + " %");
  display.drawString(0, 45, "LED: " + String(led ? "ON" : "OFF"));

  displaySignal(rssi);

  display.drawCircle(110, 40, 8);
  if (led) {
    display.fillCircle(110, 40, 7);
  }
  display.display();
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  lastRxTime = millis();

  if (size != BUFFER_SIZE) {
    Serial.println("Invalid packet size");
    lora_idle = true;
    return;
  }

  uint8_t buf[BUFFER_SIZE];
  memcpy(buf, payload, size);

  decryptAES(buf, userKey);

  String msg = String((char *)buf);
  msg.trim();

  Serial.print("Decrypted message: ");
  Serial.println(msg);

  float temp = 0;
  float humid = 0;
  int ledVal = 0;

  int idxT = msg.indexOf("T:");
  int idxH = msg.indexOf("H:");
  int idxL = msg.indexOf("L:");

  if (idxT != -1 && idxH != -1 && idxL != -1) {
    temp = msg.substring(idxT + 2, idxH - 1).toFloat();
    humid = msg.substring(idxH + 2, idxL - 1).toFloat();
    ledVal = msg.substring(idxL + 2).toInt();
  }

  ledState = (ledVal == 1);
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);

  displayMainData(temp, humid, ledState, rssi);

  lora_idle = true;
}

// ** ฟังก์ชันนี้จะถูกเรียกเมื่อส่งข้อมูลเสร็จสิ้น **
void OnTxDone() {
  Serial.println("TX done");
  lora_idle = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "LoRa AES Receiver");
  display.display();

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxDone = OnTxDone;   // เพิ่ม callback นี้ด้วย
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);

  Radio.Rx(0);
  lastRxTime = millis();
}

void loop() {
  // อ่านสวิตช์และส่งสถานะ LED กลับไปฝั่งส่งเมื่อเปลี่ยนแปลง
  switchState = !digitalRead(SWITCH_PIN);
  static bool lastSwitchStateLocal = false;

  if (switchState != lastSwitchStateLocal && switchState == true) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);

    char feedback[BUFFER_SIZE] = {0};
    snprintf(feedback, BUFFER_SIZE, "T:0.0,H:0.0,L:%d", ledState ? 1 : 0);

    uint8_t data[BUFFER_SIZE] = {0};
    strncpy((char *)data, feedback, BUFFER_SIZE - 1);
    encryptAES(data, userKey);

    if (lora_idle) {
      lora_idle = false;
      Radio.Send(data, BUFFER_SIZE);
      Serial.printf("Sent feedback: %s\n", feedback);
    }
  }
  lastSwitchStateLocal = switchState;

  // ตรวจสอบ timeout สัญญาณ
  if (millis() - lastRxTime > SIGNAL_TIMEOUT) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 28, "NO SIGNAL");
    display.display();
  }

  Radio.IrqProcess();
  delay(100);
}
