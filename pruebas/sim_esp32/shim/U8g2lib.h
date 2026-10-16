// Simulador en PC: envoltura minima de la clase U8G2 sobre la libreria C real de U8g2.
#pragma once
#include <Arduino.h>
#include "u8g2.h"
extern "C" uint8_t sim_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
extern "C" uint8_t sim_gpio_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
class U8G2 {
 public:
  u8g2_t u8g2;
  u8x8_t *getU8x8() { return u8g2_GetU8x8(&u8g2); }
  void setBusClock(uint32_t) {}
  void begin() { u8g2_InitDisplay(&u8g2); u8g2_ClearDisplay(&u8g2); u8g2_SetPowerSave(&u8g2, 0); }
  void setBitmapMode(uint8_t m) { u8g2_SetBitmapMode(&u8g2, m); }
  void setFontMode(uint8_t m) { u8g2_SetFontMode(&u8g2, m); }
  void clearBuffer() { u8g2_ClearBuffer(&u8g2); }
  void sendBuffer() { u8g2_SendBuffer(&u8g2); }
  uint8_t *getBufferPtr() { return u8g2_GetBufferPtr(&u8g2); }
  void setDrawColor(uint8_t c) { u8g2_SetDrawColor(&u8g2, c); }
  void drawXBMP(int x, int y, int w, int h, const uint8_t *b) { u8g2_DrawXBMP(&u8g2, x, y, w, h, b); }
  void drawBox(int x, int y, int w, int h) { u8g2_DrawBox(&u8g2, x, y, w, h); }
  void drawFrame(int x, int y, int w, int h) { u8g2_DrawFrame(&u8g2, x, y, w, h); }
  void drawVLine(int x, int y, int h) { u8g2_DrawVLine(&u8g2, x, y, h); }
  void drawPixel(int x, int y) { u8g2_DrawPixel(&u8g2, x, y); }
  void setFont(const uint8_t *f) { u8g2_SetFont(&u8g2, f); }
  void setFontPosTop() { u8g2_SetFontPosTop(&u8g2); }
  int drawStr(int x, int y, const char *s) { return u8g2_DrawStr(&u8g2, x, y, s); }
  int getStrWidth(const char *s) { return u8g2_GetStrWidth(&u8g2, s); }
};
class U8G2_ST7920_128X64_F_HW_SPI : public U8G2 {
 public:
  U8G2_ST7920_128X64_F_HW_SPI(const u8g2_cb_t *r, uint8_t, uint8_t) {
    u8g2_Setup_st7920_s_128x64_f(&u8g2, r, sim_byte_cb, sim_gpio_cb);
  }
};
