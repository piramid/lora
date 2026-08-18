#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <U8g2lib.h>
#include <Wire.h>

/* ---------- ตั้งค่าจอ OLED ภายนอก (SH1106, 1.3", ต่อผ่าน I2C) ---------- */
#define OLED_SDA_PIN 41
#define OLED_SCL_PIN 42
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

/* ---------- พารามิเตอร์ LoRa ---------- */
#define RF_FREQUENCY               923000000 // Hz (ไทยใช้ย่าน 920-925 MHz)
#define TX_OUTPUT_POWER             14        // dBm
#define LORA_BANDWIDTH               0        // 0: 125 kHz
#define LORA_SPREADING_FACTOR         7        // SF7..SF12
#define LORA_CODINGRATE                1        // 4/5
#define LORA_PREAMBLE_LENGTH            8
#define LORA_SYMBOL_TIMEOUT              0
#define LORA_FIX_LENGTH_PAYLOAD_ON   false
#define LORA_IQ_INVERSION_ON         false

#define RX_TIMEOUT_VALUE  1000
#define BUFFER_SIZE         30

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;
int16_t txNumber = 0;
bool isMaster = true;

// ตัวแปรเก็บข้อมูลล่าสุดไว้แสดงบนจอ
String lastStatus = "";
int16_t lastRssi = 0;
int8_t  lastSnr  = 0;

void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void updateDisplay(String line1, String line2, String line3 = "");

void setup() {
  Serial.begin(115200);
  delay(1500);

  /* เริ่มจอก่อน จะได้แสดงสถานะการตั้งค่าทุกขั้นตอน */
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  u8g2.begin();
  updateDisplay("Heltec LoRa32 V3", "Initializing...", "");

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  pinMode(0, INPUT_PULLUP);

  Serial.println("Hold PRG button now to select TX mode...");
  updateDisplay("Hold PRG now", "for TX mode", "(3 sec)");

  isMaster = false;
  for (int i = 0; i < 30; i++) {
    if (digitalRead(0) == LOW) {
      isMaster = true;
      break;
    }
    delay(100);
  }

  txNumber = 0;

  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone    = OnRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                     LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                     LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                     true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                     LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                     LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                     0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  Serial.println(isMaster ? "== TX Mode ==" : "== RX Mode ==");
  updateDisplay(isMaster ? "== TX Mode ==" : "== RX Mode ==", "Ready", "");

  if (!isMaster) {
    Radio.Rx(0);
  }
}

void loop() {
  if (isMaster) {
    sprintf(txpacket, "Hello LoRa #%d", txNumber++);
    Serial.printf("Sending: %s\n", txpacket);
    updateDisplay("== TX Mode ==", "Sent:", String(txpacket));
    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
    delay(3000);
  }
  Radio.IrqProcess();
}

void OnTxDone(void) {
  Serial.println("TX done!");
}

void OnTxTimeout(void) {
  Radio.Sleep();
  Serial.println("TX Timeout!");
  updateDisplay("== TX Mode ==", "TX Timeout!", "");
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';
  lastRssi = rssi;
  lastSnr  = snr;
  Serial.printf("Received: \"%s\"  RSSI: %d  SNR: %d\n", rxpacket, rssi, snr);

  String info = "RSSI:" + String(rssi) + " SNR:" + String(snr);
  updateDisplay("== RX Mode ==", String(rxpacket), info);

  Radio.Rx(0);
}

/* ---------- ฟังก์ชันช่วยเขียนข้อความ 3 บรรทัดลงจอ ---------- */
void updateDisplay(String line1, String line2, String line3) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, line1.c_str());
  u8g2.drawLine(0, 16, 128, 16);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 32, line2.c_str());
  u8g2.drawStr(0, 46, line3.c_str());
  u8g2.sendBuffer();
}