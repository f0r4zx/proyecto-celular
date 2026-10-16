#!/bin/sh
# Compila el simulador. Uso: ./compilar.sh <ruta a U8g2_Arduino/src>
set -e
U8=${1:?ruta a U8g2_Arduino/src}
mkdir -p build capturas
[ -f build/libu8g2.a ] || { for f in $U8/clib/*.c; do gcc -O1 -c -I$U8/clib $f -o build/$(basename $f).o; done; ar rcs build/libu8g2.a build/*.c.o; }
(echo '#include <Arduino.h>'; echo '#line 1 "esp32_interfaz.ino"'; cat ../../firmware/esp32_interfaz/esp32_interfaz.ino) > build/sketch.cpp
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Ishim -I$U8/clib -I../../firmware/esp32_interfaz \
  build/sketch.cpp sim_main.cpp sim_buffer.cpp build/libu8g2.a -o build/sim
echo "Simulador listo: build/sim casos.txt"
