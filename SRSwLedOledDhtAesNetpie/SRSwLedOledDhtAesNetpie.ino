#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ==== OLED display ====
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED,
                           GEOMETRY_128_64, RST_OLED);

// ==== Pins ====
const int SWITCH_PIN = 47;
const int LED_PIN = 48;
const int DHT_PIN = 3;
#define DHTTYPE DHT22

// ==== DHT sensor ====
DHT dht(DHT_PIN, DHTTYPE);

// ==== LoRa config ====
#define RF_FREQUENCY 915000000
#define TX_OUTPUT_POWER 14
#define BUFFER_SIZE 32

// ==== AES key (16 bytes) ====
const char *userKey = "hyhT676#h~_876s";
mbedtls_aes_context aes;

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

bool ledState = false;
bool lora_idle = true;

// ==== Switch debounce ====
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
bool lastSwitchState = HIGH;
bool switchPressed = false;

// ==== LoRa Radio events ====
static RadioEvents_t RadioEvents;

// ==== WiFi + NETPIE Config ====
const char* WIFI_SSID = "IOT24";
const char* WIFI_PASS = "AAAAABBBB1";

const char* NETPIE_SERVER = "broker.netpie.io";
const char* NETPIE_CLIENT_ID = "bed09669-8b0d-47e8-a4be-3fbd7873bdad";
const char* NETPIE_USERNAME  = "99kG2EzkAgPNTECwrsbgr7ZsyJV9LqcL";
const char* NETPIE_PASSWORD  = "bHDjBybfBFmwQtddtpdwsmNbLptr8WU3";

WiFiClient espClient;
PubSubClient client(espClient);

// ---------------------------------------------------------------------
// AES Helper
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// NETPIE WiFi + MQTT
// ---------------------------------------------------------------------
void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.print("Message from NETPIE: ");
  Serial.println(msg);

  // ตัวอย่าง: ควบคุม LED จาก NETPIE
  if (msg == "LEDON") {
    ledState = true;
    digitalWrite(LED_PIN, HIGH);
  } else if (msg == "LEDOFF") {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to NETPIE...");
    if (client.connect(NETPIE_CLIENT_ID, NETPIE_USERNAME, NETPIE_PASSWORD)) {
      Serial.println("connected");
      client.subscribe("@msg/led");   // subscribe สำหรับควบคุม
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

// ---------------------------------------------------------------------
// Setup & Loop
// ---------------------------------------------------------------------
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

  // NETPIE Init
  setup_wifi();
  client.setServer(NETPIE_SERVER, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Check switch
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

// ---------------------------------------------------------------------
// User functions
// ---------------------------------------------------------------------
void toggleLedState() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  Serial.print("LED is now ");
  Serial.println(ledState ? "ON" : "OFF");
}

void sendData() {
  if (!lora_idle) return;

  float tempC = dht.readTemperature();
  if (isnan(tempC)) tempC = 0.0;

  uint8_t buffer[16];
  memset(buffer, 0, sizeof(buffer));

  buffer[0] = ledState ? 1 : 0;
  memcpy(&buffer[1], &tempC, sizeof(float));

  encryptAES(buffer, userKey);
  Radio.Send(buffer, 16);
  lora_idle = false;

  Serial.printf("Sent (LED=%s, Temp=%.1f)\n", ledState ? "ON" : "OFF", tempC);

  String payload = "{\"data\":{\"temp\":" + String(tempC, 1) +
                 ",\"led\":\"" + String(ledState ? "ON" : "OFF") + "\"}}";  // ✅ ใช้ ON/OFF
  client.publish("@shadow/data/update", payload.c_str());
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

void updateDisplayWithReceived(float receivedTemp) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, "RX Temp: " + String(receivedTemp, 1) + " C");
  display.drawString(64, 30, ledState ? "LED ON" : "LED OFF");

  if (ledState) display.fillCircle(64, 55, 10);
  else display.drawCircle(64, 55, 10);
  display.display();
}

// ---------------------------------------------------------------------
// LoRa Callbacks
// ---------------------------------------------------------------------
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
  if (size != 16) {
    Serial.println("Invalid packet size");
    Radio.Rx(0);
    return;
  }

  uint8_t buffer[16];
  memcpy(buffer, payload, 16);
  decryptAES(buffer, userKey);

  ledState = buffer[0] ? true : false;
  float receivedTemp;
  memcpy(&receivedTemp, &buffer[1], sizeof(float));
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);

  Serial.printf("Received LED: %s, Temp: %.1f\n",
                ledState ? "ON" : "OFF", receivedTemp);

  updateDisplayWithReceived(receivedTemp);
  lora_idle = true;
  Radio.Rx(0);

  // ส่งข้อมูล LoRa ที่รับได้ขึ้น NETPIE ด้วย
  String payloadNetpie = "{\"data\":{\"rx_temp\":" + String(receivedTemp, 1) +
                       ",\"led\":\"" + String(ledState ? "ON" : "OFF") + "\"}}";  // ✅ ใช้ ON/OFF
  client.publish("@shadow/data/update", payloadNetpie.c_str());
}
