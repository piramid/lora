#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#include "mbedtls/aes.h"

// ------------------ กำหนดตัวเครื่อง ------------------
#define MY_ID   'ฺA'      // เปลี่ยนเป็น 'A' หรือ 'B'
#define PEER_ID 'B'      // คู่เครื่องตรงข้าม

// ------------------ OLED ------------------
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED,
                           GEOMETRY_128_64, RST_OLED);

// ------------------ I/O ------------------
#define SWITCH_PIN 47
#define LED_PIN    48

// ------------------ LoRa ------------------
#define RF_FREQUENCY 915000000UL
#define TX_POWER_DBM 14
#define LORA_BW      0
#define LORA_SF      7
#define LORA_CR      1
#define LORA_PREAM   8
#define LORA_IQ_INV  false

#define PACKET_SIZE 32
#define AES_BLOCK   16
#define SIGNAL_TIMEOUT 5000UL
#define MIN_RSSI -120
#define MAX_RSSI -50

// ------------------ AES ------------------
const char *AES_KEY = "hyhT676#h~_876s";
mbedtls_aes_context aes;

// ------------------ ตัวแปรสถานะ ------------------
bool ledLocal = false;  // LED เครื่องนี้
bool ledPeer  = false;  // LED เครื่องคู่
bool lastBtn  = false;

static RadioEvents_t RadioEvents;
volatile bool lora_idle = true;
unsigned long lastRxMs = 0;

// ------------------ AES helpers ------------------
static void makeKey16(const char *k, uint8_t out[16]) {
  memset(out, 0, 16);
  size_t n = strlen(k); if(n>16) n=16;
  memcpy(out, k, n);
}

static void aesEncryptBlocks(uint8_t *buf, size_t len, const char *key) {
  uint8_t k16[16]; makeKey16(key, k16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, k16, 128);
  for(size_t off=0; off<len; off+=16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, buf+off, buf+off);
  mbedtls_aes_free(&aes);
}

static void aesDecryptBlocks(uint8_t *buf, size_t len, const char *key) {
  uint8_t k16[16]; makeKey16(key, k16);
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, k16, 128);
  for(size_t off=0; off<len; off+=16)
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, buf+off, buf+off);
  mbedtls_aes_free(&aes);
}

// ------------------ Packet ------------------
static void buildPayload(char id, bool led, uint8_t out[PACKET_SIZE]) {
  char msg[30]; snprintf(msg,sizeof(msg),"ID:%c;LED:%d;",id,led?1:0);
  size_t n=strlen(msg); if(n>PACKET_SIZE-1)n=PACKET_SIZE-1;
  memset(out,0,PACKET_SIZE);
  out[0]=(uint8_t)n;
  memcpy(out+1,msg,n);
}

static bool parsePayload(uint8_t in[PACKET_SIZE], char &id, bool &led) {
  uint8_t n=in[0]; if(n==0||n>PACKET_SIZE-1) return false;
  char s[PACKET_SIZE]; memset(s,0,sizeof(s));
  memcpy(s,in+1,n); s[n]='\0';
  char idc=0; int lv=0;
  if(sscanf(s,"ID:%c;LED:%d;",&idc,&lv)==2) { id=idc; led=(lv!=0); return true; }
  return false;
}

// ------------------ OLED ------------------
static void drawSignal(int16_t rssi) {
  int percent = map(constrain(rssi,MIN_RSSI,MAX_RSSI),MIN_RSSI,MAX_RSSI,0,100);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.setFont(ArialMT_Plain_10);
  String bars=String(percent)+"% ["; for(int i=0;i<5;i++) bars += (percent>i*20)?"|":" "; bars+="]";
  display.drawString(128,52,bars);
}

static void drawScreen(int16_t rssi, bool showNoSignal=false) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0,0,String("2-Way LoRa AES ID: ")+MY_ID);

  if(showNoSignal) {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64,22,"NO SIGNAL");
    display.setTextAlignment(TEXT_ALIGN_LEFT);
  } else {
    display.setFont(ArialMT_Plain_16);
    display.drawString(0,18,"Me  : "+String(ledLocal?"ON":"OFF"));
    display.drawString(0,35,"Peer: "+String(ledPeer?"ON":"OFF"));
    display.drawCircle(110,26,8); if(ledLocal) display.fillCircle(110,26,7);
    display.drawCircle(110,43,8); if(ledPeer) display.fillCircle(110,43,7);
  }

  drawSignal(rssi);
  display.display();
}

// ------------------ LoRa callbacks ------------------
static void OnTxDone(void) { lora_idle=true; Radio.Rx(0); }
static void OnTxTimeout(void) { lora_idle=true; Radio.Rx(0); }
static void OnRxDone(uint8_t *payload,uint16_t size,int16_t rssi,int8_t snr){
  lastRxMs=millis();
  if(size!=PACKET_SIZE){ lora_idle=true; Radio.Rx(0); return; }
  uint8_t buf[PACKET_SIZE]; memcpy(buf,payload,PACKET_SIZE);
  aesDecryptBlocks(buf,PACKET_SIZE,AES_KEY);
  char rxId=0; bool rxLed=false;
  if(parsePayload(buf,rxId,rxLed)){
    if(rxId==PEER_ID){
      ledPeer=rxLed;
      // ไม่ mirror LED ของตัวเอง แต่โชว์สถานะ peer
      drawScreen(rssi,false);
    }
  }
  lora_idle=true; Radio.Rx(0);
}

// ------------------ ส่ง LED ------------------
static void sendLedState(bool state){
  if(!lora_idle) return;
  uint8_t pkt[PACKET_SIZE]; buildPayload(MY_ID,state,pkt);
  aesEncryptBlocks(pkt,PACKET_SIZE,AES_KEY);
  lora_idle=false; Radio.Send(pkt,PACKET_SIZE);
}

// ------------------ SETUP ------------------
void setup(){
  Serial.begin(115200);
  pinMode(SWITCH_PIN,INPUT_PULLUP);
  pinMode(LED_PIN,OUTPUT); digitalWrite(LED_PIN,LOW);
  display.init(); drawScreen(-120,true);
  Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);

  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone    = OnRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA,TX_POWER_DBM,0,LORA_BW,LORA_SF,LORA_CR,LORA_PREAM,false,
                    true,0,0,LORA_IQ_INV,3000);
  Radio.SetRxConfig(MODEM_LORA,LORA_BW,LORA_SF,LORA_CR,0,LORA_PREAM,0,false,
                    0,true,0,0,LORA_IQ_INV,true);

  Radio.Rx(0); lastRxMs=millis();
}

// ------------------ LOOP ------------------
#define HEARTBEAT_INTERVAL 2000UL  // ส่ง LED ทุก 2 วินาที

void loop() {
  static bool lastBtn = false;
  static bool lastNoSignal = false;
  static unsigned long lastHeartbeat = 0;

  // debounce ปุ่ม
  bool pressed = (digitalRead(SWITCH_PIN) == LOW);
  static uint32_t lastChange = 0;
  static bool lastRaw = pressed;
  if (pressed != lastRaw) { lastRaw = pressed; lastChange = millis(); }

  if ((millis() - lastChange) > 30) {
    static bool lastStable = HIGH;
    if (pressed != lastStable) {
      lastStable = pressed;
      if (pressed == LOW) {
        // toggle LED ของตัวเอง
        ledLocal = !ledLocal;
        digitalWrite(LED_PIN, ledLocal ? HIGH : LOW);
        sendLedState(ledLocal);
        drawScreen(-60, false);  // รีเฟรช OLED
      }
    }
  }

  // heartbeat: ส่ง LED ของตัวเองทุกช่วงเวลา
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    sendLedState(ledLocal);  // ส่งสถานะ LED แม้ไม่ได้กด
  }

  // ตรวจสอบ NO SIGNAL
  bool noSignal = (millis() - lastRxMs > SIGNAL_TIMEOUT);
  if (noSignal != lastNoSignal) {
    drawScreen(noSignal ? -120 : -60, noSignal);
    lastNoSignal = noSignal;
  }

  Radio.IrqProcess();
  delay(5);
}


