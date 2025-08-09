#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"

// OLED display setup
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// GPIO pins
const int SWITCH_PIN = 47;   // ปุ่มกด
const int LED_PIN = 48;      // LED

// LoRa settings
#define RF_FREQUENCY 915000000 // Hz
#define TX_OUTPUT_POWER 14     // dBm

// LoRa parameters
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

#define BUFFER_SIZE 16

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

bool ledState = false;      // สถานะ LED
bool lora_idle = true;

// Debounce variables
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;  // 50 ms debounce
bool lastSwitchState = HIGH;  // assume pull-up
bool switchPressed = false;

// Radio Events
static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Init OLED
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.display();

  // LoRa init
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

  // Start LoRa receive mode
  Radio.Rx(0);

  updateDisplay();
}

void loop() {
  // Debounce switch reading
  bool currentSwitchState = digitalRead(SWITCH_PIN);
  if (currentSwitchState != lastSwitchState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentSwitchState == LOW && !switchPressed) { // กดปุ่มลง
      switchPressed = true;
      toggleLedState();
      sendLedState();
      updateDisplay();
    }
    if (currentSwitchState == HIGH) { // ปล่อยปุ่ม
      switchPressed = false;
    }
  }
  lastSwitchState = currentSwitchState;

  // Process LoRa IRQs
  Radio.IrqProcess();

  delay(10);
}

void toggleLedState() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  Serial.print("LED is now ");
  Serial.println(ledState ? "ON" : "OFF");
}

void sendLedState() {
  if (!lora_idle) return; // กำลังส่งอยู่

  memset(txpacket, 0, BUFFER_SIZE);
  txpacket[0] = ledState ? '1' : '0';

  Radio.Send((uint8_t *)txpacket, 1);
  lora_idle = false;

  Serial.print("Sending LED state: ");
  Serial.println(txpacket[0]);
}

void updateDisplay() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 20, ledState ? "LED ON" : "LED OFF");

  // วาดวงกลมทึบ/วงกลมใส
  if (ledState) {
    display.fillCircle(64, 55, 10);
  } else {
    display.drawCircle(64, 55, 10);
  }

  display.display();
}

// Radio callbacks

void OnTxDone(void) {
  Serial.println("TX done");
  lora_idle = true;
  Radio.Rx(0);
}

void OnTxTimeout(void) {
  Serial.println("TX timeout");
  lora_idle = true;
  Radio.Rx(0);
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < 1) return;

  char received = (char)payload[0];

  Serial.print("Received LED state: ");
  Serial.println(received);

  ledState = (received == '1');
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);

  updateDisplay();

  lora_idle = true;
  Radio.Rx(0);
}
