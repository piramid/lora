#include <heltec.h>

void setup() {
  Heltec.begin(true, false, true); // Display, LoRa, Serial
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, "HELTEC OK");
  Heltec.display->display();
}

void loop() {}
