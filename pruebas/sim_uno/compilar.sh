#!/bin/sh
# Compila el simulador del Arduino Uno en la PC.
set -e
mkdir -p build
(echo '#include <Arduino.h>'; echo '#line 1 "arduino_uno_telecom.ino"'; cat ../../firmware/arduino_uno_telecom/arduino_uno_telecom.ino) > build/sketch.cpp
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Ishim build/sketch.cpp sim_main.cpp -o build/sim
echo "Simulador listo: build/sim casos.txt"
