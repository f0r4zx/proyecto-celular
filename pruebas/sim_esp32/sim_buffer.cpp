#include <Arduino.h>
#include <U8g2lib.h>
extern U8G2_ST7920_128X64_F_HW_SPI u8g2;
extern "C" uint8_t *sim_buffer() { return u8g2.getBufferPtr(); }
