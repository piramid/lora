#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"

// ==== OLED Display ====
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

#define RF_FREQUENCY 915000000
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 64

// ==== Encryption Key ====
const uint8_t key = 0x5A;

char rxpacket[BUFFER_SIZE];
static RadioEvents_t RadioEvents;

void decryptData(char *data, int len) {
  for (int i = 0; i < len; i++) {
    data[i] ^= key;
  }
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  memcpy(rxpacket, payload, size);
  rxpacket[size] = '\0';

  decryptData(rxpacket, size); // ถอดรหัส

  Serial.printf("Received: %s RSSI: %d\n", rxpacket, rssi);

  display.clear();
  display.drawString(0, 0, "Received:");
  display.drawString(0, 12, rxpacket);
  display.drawString(0, 24, "RSSI: " + String(rssi));
  display.display();

  Radio.Rx(0);
}

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa Receiver Init...");
  display.display();

  RadioEvents.RxDone = OnRxDone;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    0, LORA_FIX_LENGTH_PAYLOAD_ON, 0, true,
                    0, 0, LORA_IQ_INVERSION_ON, true);

  Radio.Rx(0);
}

void loop() {
  Radio.IrqProcess();
}
