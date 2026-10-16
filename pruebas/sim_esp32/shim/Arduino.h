// Simulador en PC: version minima de Arduino.h para compilar el sketch del ESP32.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <deque>
#define PROGMEM
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define LOW 0
#define HIGH 1
#define SERIAL_8N1 0
extern unsigned long sim_ms;                     // reloj virtual
inline unsigned long millis() { return sim_ms; }
inline void delayMicroseconds(unsigned) {}
void pinMode(uint8_t p, uint8_t m);
void digitalWrite(uint8_t p, uint8_t v);
int digitalRead(uint8_t p);
struct SimSerial {                                // USB (consola)
  bool silencio = true;
  void begin(unsigned long) {}
  void print(const char *s) { if (!silencio) fputs(s, stdout); }
  void println(const char *s) { if (!silencio) { fputs(s, stdout); fputc('\n', stdout); } }
};
struct SimSerial2 {                               // UART hacia el Arduino
  std::deque<uint8_t> rx;                         // lo que manda el "Arduino"
  std::string tx;                                 // lo que manda el ESP32
  void begin(unsigned long, int, int, int) {}
  void setRxBufferSize(size_t) {}
  int available() { return (int)rx.size(); }
  int read() { int c = rx.front(); rx.pop_front(); return c; }
  void print(const char *s) { tx += s; }
  void print(char c) { tx += c; }
};
extern SimSerial Serial;
extern SimSerial2 Serial2;
struct SimSPI { void begin(int, int, int, int) {} };
extern SimSPI SPI;
