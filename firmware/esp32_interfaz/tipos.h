// =============================================================================
//  tipos.h - Tipos de datos del programa del ESP32 (menus, estados, mensajes).
//  Estan en un .h aparte porque el IDE de Arduino genera prototipos automaticos
//  de las funciones del .ino y los coloca ARRIBA del archivo: si estos tipos
//  estuvieran en el .ino, esos prototipos no los conocerian y no compilaria.
// =============================================================================
#pragma once
#include <Arduino.h>                       // tipos uint8_t, etc.

#define MAX_NUMERO          20             // caracteres de un numero de telefono
#define MAX_SMS            160             // caracteres de un SMS (1 mensaje GSM de 7 bits)

enum Pantalla : uint8_t {                  // menus del celular
  P_MENU,                                  // menu principal
  P_LLAMADAS,                              // ingresar numero a llamar
  P_LLAMADA,                               // llamando... / llamada en curso
  P_ENTRANTE,                              // llamada entrante
  P_MANDAR,                                // mandar mensaje: ingresar numero
  P_MENSAJE,                               // escribir el texto del mensaje
  P_BANDEJA,                               // mensajes recibidos
  P_LEER                                   // leer un mensaje
};

enum EstadoLlamada : uint8_t {             // estado de la llamada visto por el ESP32
  LL_LIBRE,                                // sin llamada
  LL_MARCANDO,                             // llamando a otro (aun no contesta)
  LL_ENTRANTE,                             // alguien llama
  LL_EN_CURSO,                             // conectados (corre el cronometro)
  LL_TERMINADA                             // termino: se muestra un momento
};

struct Sms {                                // un mensaje recibido
  char numero[MAX_NUMERO + 1];             // remitente
  char fecha[15];                          // "dd/mm/aa hh:mm"
  char texto[MAX_SMS + 1];                 // cuerpo
  unsigned long recibido;                  // millis() al llegar (para borrarlo a las 24 h)
};

struct Orden {                             // orden que espera su "ACK"
  char linea[200];                         // "<id>,<ORDEN>,<datos>"
  uint8_t id;                              // numero de orden (1..99)
  uint8_t intentos;                        // veces enviada
  unsigned long tEnvio;                    // ultimo envio
};
