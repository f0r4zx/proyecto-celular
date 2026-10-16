#pragma once
#include <Arduino.h>
struct SoftwareSerial : Puerto { SoftwareSerial(int, int) {} };
extern SoftwareSerial *sim_esp;
