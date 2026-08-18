#include <U8g2lib.h>
#include <Wire.h>

#define SDA_PIN 41
#define SCL_PIN 42

// สำหรับจอ 1.3" ชิป SH1106 ขนาด 128x64
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
}

void loop() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 20, "SH1106 Test");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 40, "Heltec V3 External OLED");
  u8g2.sendBuffer();
  delay(2000);
}