#include <Arduino_GFX_Library.h>

#include "graphics.h"

const char SCLK_OLED =  14; //SCLK
const char MOSI_OLED =  13; //MOSI (Master Output Slave Input)
const char MISO_OLED =  12; // (Master Input Slave Output)
const char CS_OLED = 15;
const char DC_OLED =  16; //OLED DC(Data/Command)
const char RST_OLED =  4; //OLED Reset

 //#define DISABLE_GFX 1

//SSD1331Extended display(SCLK_OLED, MISO_OLED, MOSI_OLED, CS_OLED, DC_OLED, RST_OLED);

Arduino_DataBus *bus = new Arduino_ESP32SPI(DC_OLED /* DC */, CS_OLED /* CS */, SCLK_OLED /* SCK */, MOSI_OLED /* MOSI */, MISO_OLED /* MISO */);
//Arduino_DataBus *bus = new Arduino_HWSPI(16 /* DC */, 15 /* CS */);
Arduino_GFX *gfx = new Arduino_SSD1331(bus, RST_OLED /* RST */);

static int s_labelWidths[] = {5, 1, 2, 1}; // Hardcode for now.

void graphics_init() {
  #ifdef DISABLE_GFX
  return;
  #endif
  gfx->begin();
  graphics_clear();
}

void graphics_clear() {
  #ifdef DISABLE_GFX
  return;
  #endif
    gfx->fillScreen(RGB565_BLACK);
}

void graphics_showLabel(int slot, uint16_t color, String str) {
  #ifdef DISABLE_GFX
  return;
  #endif
  int x = 0;
  for (int i=0; i<slot; i++) {
    x+= s_labelWidths[i]*8+1;
  }
  const int w = s_labelWidths[slot]*8;
  gfx->setCursor(x+2, 2);
  gfx->setTextColor(color);
  gfx->fillRect(x+1, 1, w-2, 11, RGB565_BLACK);
  gfx->drawRoundRect(x, 0, w, 12, 3, color);
  gfx->println(str);
}

void graphics_println(String str) {
  #ifdef DISABLE_GFX
    Serial.println(str);
    return;
  #endif
  if (str.length() == 0 || str == "\n") {
    return;
  }

  static int yPos;
  const int numLines = 5;
  static char textBuffer[numLines][17];
  strncpy(textBuffer[yPos], str.c_str(), 16);
  int bufIndex = (yPos+1) % numLines;
  gfx->fillRect(0, 12, gfx->width(), gfx->height()-12, RGB565_BLACK);
  for (int i=0; i<numLines; i++) {
    gfx->setCursor(0, i*10+12);
    gfx->setTextColor(RGB565_WHITE);
    gfx->println(textBuffer[bufIndex]);
    bufIndex++;
    bufIndex %= numLines;
  }
  yPos++;
  yPos %= numLines;
}
