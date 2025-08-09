
/*
This is a simple code to display text on the OLED display
WiFi LoRa 32 V3 ESP32 module
Written by Ahmad Shamshiri 02 April 2025
Watch full video explanation https://youtu.be/WkyQMXkQhE8
Resources page https://robojax.com/tutorial_view.php?id=387
*/
#include <Wire.h>               
#include "HT_SSD1306Wire.h"


static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst


void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  VextON();
  delay(100);

  // Initialising the UI will init the display too.
  display.init();

  display.setFont(ArialMT_Plain_10);

}


void displayTemperature(double temperature, int unit) {
    display.clear();  // Clear display before new content
    
    // Line 1: "Temperature:" in 16pt font
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "Temperature:");

    // Line 2: Temperature value in 24pt font
    display.setFont(ArialMT_Plain_24);
    
    // Format temperature with correct unit symbol
    String tempString = String(temperature, 1); // 1 decimal place
    switch(unit) {
        case 1:  tempString += "�C"; break;  // Celsius
        case 2:  tempString += "�F"; break;  // Fahrenheit
        default: tempString += "�U"; break;  // Unknown unit
    }
    
    display.drawString(0, 20, tempString);  // Display at Y=20 (below label)
    display.display();  // Update OLED
}



void VextON(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
}

void VextOFF(void) //Vext default OFF
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}



void loop() {
  // clear the display
  display.clear();
  displayTemperature(23.5, 1); // Displays "23.5�C" /1
  delay(2000);

}
