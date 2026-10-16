// Simulador en PC: version minima de Arduino.h (AVR) para compilar el sketch del Uno.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <deque>
class __FlashStringHelper;
#define F(s) ((const __FlashStringHelper *)(s))
#define PSTR(s) (s)
#define PGM_P const char *
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strstr_P strstr
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
inline char *utoa(unsigned v, char *b, int) { sprintf(b, "%u", v); return b; }
extern unsigned long sim_ms;
inline unsigned long millis() { return sim_ms; }
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
struct Puerto {                                   // puerto serie simulado
  std::deque<uint8_t> rx; std::string tx;
  void begin(unsigned long) {}
  int available() { return (int)rx.size(); }
  int read() { int c = rx.front(); rx.pop_front(); return c; }
  int availableForWrite() { return 63; }
  size_t write(uint8_t c) { tx += (char)c; return 1; }
  void print(const __FlashStringHelper *s) { tx += (const char *)s; }
  void print(const char *s) { tx += s; }
  void print(char c) { tx += c; }
  void print(int v) { tx += std::to_string(v); }
  void println(const __FlashStringHelper *s) { print(s); tx += "\r\n"; }
  void println(const char *s) { print(s); tx += "\r\n"; }
  void println(char c) { tx += c; tx += "\r\n"; }
  void println(int v) { print(v); tx += "\r\n"; }
};
extern Puerto Serial;
