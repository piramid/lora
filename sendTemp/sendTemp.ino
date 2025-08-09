#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "DHT.h"

// ==== OLED Display ====
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ==== DHT Sensor ====
#define DHTPIN 3       // ขาเชื่อม DHT22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ==== LoRa Config ====
#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 5
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 64

// ==== Encryption Key ====
const uint8_t key = 0x5A;  // XOR key

char txpacket[BUFFER_SIZE];
bool lora_idle = true;
static RadioEvents_t RadioEvents;

void encryptData(char *data) {
  for (int i = 0; i < strlen(data); i++) {
    data[i] ^= key;
  }
}

void OnTxDone(void) {
  Serial.println("TX done");
  lora_idle = true;
}

void OnTxTimeout(void) {
  Serial.println("TX Timeout");
  lora_idle = true;
}

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  dht.begin();

  display.init();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa Sender Init...");
  display.display();

  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
}

void loop() {
  if (lora_idle) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
      Serial.println("DHT Read Error!");
      return;
    }

    sprintf(txpacket, "T:%.2fC H:%.2f%%", temp, hum);

    encryptData(txpacket); // เข้ารหัส

    Serial.println("Sending Encrypted Data...");
    display.clear();
    display.drawString(0, 0, "Sending:");
    display.drawString(0, 12, txpacket); // จะเป็นตัวอักษรแปลกๆเพราะเข้ารหัสแล้ว
    display.display();

    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
    lora_idle = false;
  }
  Radio.IrqProcess();
}
