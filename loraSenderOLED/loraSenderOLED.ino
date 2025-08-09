#include "LoRaWan_APP.h"
#include "Arduino.h"

#define RF_FREQUENCY                                915000000 // Hz
#define TX_OUTPUT_POWER                             5        // dBm
#define LORA_BANDWIDTH                              0         // [0: 125 kHz]
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5]
#define LORA_PREAMBLE_LENGTH                        8
#define LORA_SYMBOL_TIMEOUT                         0
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false
#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 30

char txpacket[BUFFER_SIZE];
double txNumber;
bool lora_idle = true;

// Radio event functions
static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);

void setup() {
  Serial.begin(115200);

  // Initialize LoRa and OLED display
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  txNumber = 0;

  // Setup LoRa events and config
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

  // OLED initial message
  Display.setFont(ArialMT_Plain_10);
  Display.clear();
  Display.drawString(0, 0, "LoRa Sender Ready");
  Display.display();
}

void loop() {
  if (lora_idle == true) {
    delay(1000);

    txNumber += 0.01;
    sprintf(txpacket, "Hello world number %0.2f", txNumber);

    Serial.printf("\r\nsending packet \"%s\" , length %d\r\n", txpacket, strlen(txpacket));

    // ==== OLED Display update ====
    Display.clear();
    Display.setTextAlignment(TEXT_ALIGN_LEFT);
    Display.setFont(ArialMT_Plain_10);
    Display.drawString(0, 0, "Sending packet:");
    Display.drawString(0, 12, txpacket);
    Display.display();

    // Send LoRa packet
    Radio.Send((uint8_t *)txpacket, strlen(txpacket));
    lora_idle = false;
  }

  Radio.IrqProcess();
}

void OnTxDone(void) {
  Serial.println("TX done......");
  lora_idle = true;

  // Show TX Done on OLED
  Display.clear();
  Display.setTextAlignment(TEXT_ALIGN_LEFT);
  Display.drawString(0, 0, "TX Done!");
  Display.display();
}

void OnTxTimeout(void) {
  Radio.Sleep();
  Serial.println("TX Timeout......");
  lora_idle = true;

  // Show TX Timeout on OLED
  Display.clear();
  Display.setTextAlignment(TEXT_ALIGN_LEFT);
  Display.drawString(0, 0, "TX Timeout!");
  Display.display();
}
