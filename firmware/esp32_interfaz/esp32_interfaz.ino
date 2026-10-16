/*
 * =============================================================================
 *  CELULAR BASICO - PROGRAMA DEL ESP32 (INTERFAZ DE USUARIO)
 *  Placa: ESP32-WROOM-32E (Espressif DevKit de 38 pines)
 * =============================================================================
 *  Que hace este programa:
 *    1. Lee el teclado matricial de 4 filas x 6 columnas (19 teclas).
 *    2. Maneja TODA la logica de menus y navegacion (menu principal, llamadas,
 *       mensajes, bandeja de entrada, llamada entrante).
 *    3. Dibuja los menus en la LCD12864 (controlador ST7920) usando los mapas de
 *       bits del diseno (graficos.h) y solo reenvia a la pantalla las zonas que
 *       cambiaron (numero que se escribe, cronometro, senal, seleccion...).
 *    4. Calcula la duracion de la llamada (cronometro mm:ss).
 *    5. Guarda los SMS recibidos durante 24 horas.
 *    6. Habla con el Arduino Uno por UART (ordenes de llamar, colgar, contestar,
 *       rechazar, enviar SMS; recibe senal, estados de llamada y SMS).
 *  Todo es NO BLOQUEANTE: no hay delay(); cada tarea revisa millis() y sale.
 *
 * -----------------------------------------------------------------------------
 *  CONEXIONES (pin del ESP32 -> pin del otro elemento)
 * -----------------------------------------------------------------------------
 *  TECLADO (conector J1 de la PCB de la matriz):
 *    J1-11 VCC  <- 3V3 del ESP32   (NUNCA 5 V: las filas llegan directo al ESP32)
 *    J1-4  F1   -> GPIO 34   (entrada; pull-up de 10 kOhm R1 en la PCB)
 *    J1-5  F2   -> GPIO 35   (entrada; pull-up R2)
 *    J1-6  F3   -> GPIO 36 / VP  (entrada; pull-up R3)
 *    J1-7  F4   -> GPIO 39 / VN  (entrada; pull-up R4)
 *    J1-8  C1   <- GPIO 13   (salida de barrido)
 *    J1-9  C2   <- GPIO 14   (salida de barrido)
 *    J1-10 C3   <- GPIO 27   (salida de barrido)
 *    J1-3  C4   <- GPIO 26   (salida de barrido)
 *    J1-2  C5   <- GPIO 25   (salida de barrido)
 *    J1-1  C6   <- GPIO 33   (salida de barrido)
 *    -> Las filas estan en ALTO en reposo (por las 4 resistencias de 10 kOhm a
 *       VCC) y leen BAJO cuando se presiona una tecla de la columna activa.
 *       GPIO 34/35/36/39 son "solo entrada" (no tienen pull-up interno), por eso
 *       son ideales para las filas, que ya tienen pull-up en la PCB.
 *    -> NO se usa GPIO 12 (pin de arranque del ESP32: si se presionaba una tecla
 *       al encender, el ESP32 no arrancaba).
 *
 *  PANTALLA LCD12864 (ST7920, 5 V) en modo serie (SPI) - 20 pines:
 *    1 VSS  -> GND
 *    2 VDD  -> 5 V
 *    3 V0   -> sin conectar si el modulo trae potenciometro de contraste
 *    4 RS (CS)  -> 5 V fijo (chip siempre seleccionado; ver nota PIN_LCD_CS)
 *    5 R/W (SID, datos) <- HV4 del level shifter <- LV4 <- GPIO 23 (MOSI)
 *    6 E   (SCLK, reloj) <- HV3 del level shifter <- LV3 <- GPIO 18 (SCK)
 *    15 PSB -> GND        (GND = modo serie; obligatorio)
 *    17 RST -> 5 V        (o sin conectar: tiene pull-up interno)
 *    19 BLA -> 5 V        (luz de fondo +; muchos modulos ya traen resistencia)
 *    20 BLK -> GND        (luz de fondo -)
 *    Pines 7-14 (DB0-DB7), 16, 18: sin conectar.
 *
 *  LEVEL SHIFTER 4 canales (LV = 3.3 V lado ESP32, HV = 5 V lado Arduino/LCD):
 *    LV  -> 3V3 del ESP32        HV  -> 5 V        GND (ambos lados) -> GND comun
 *    LV1 <- GPIO 17 (TX2 ESP32)  HV1 -> D11 del Arduino Uno (RX por software)
 *    LV2 -> GPIO 16 (RX2 ESP32)  HV2 <- D12 del Arduino Uno (TX por software)
 *    LV3 <- GPIO 18 (SCK)        HV3 -> pin 6 (E)   de la LCD
 *    LV4 <- GPIO 23 (MOSI)       HV4 -> pin 5 (R/W) de la LCD
 *
 *  GND del ESP32, del Arduino, de la LCD y de la fuente: TODOS UNIDOS.
 *  Pines libres para futuras funciones: GPIO 4, 5, 19, 21, 22, 32.
 *
 *  Librerias: U8g2 (de olikraus, instalar desde el Gestor de Librerias).
 *  Placa en Arduino IDE: "ESP32 Dev Module" (paquete esp32 de Espressif).
 * =============================================================================
 */

#include <Arduino.h>                       // funciones basicas de Arduino
#include <SPI.h>                           // bus SPI por hardware (para la LCD)
#include <U8g2lib.h>                       // libreria de la pantalla (ST7920)
#include <string.h>                        // strlen, strcpy, memcmp...
#include <stdio.h>                         // snprintf
#include "graficos.h"                      // mapas de bits del diseno (generado)
#include "tipos.h"                         // enums y structs (menus, estados, SMS, ordenes)

/* =============================================================================
 *  BLOQUE 1. CONFIGURACION (pines, tiempos y limites)
 * ============================================================================= */
#define DEPURAR              1             // 1 = imprime eventos por el USB (Monitor Serie 115200)

#define PIN_UART_RX         16             // RX2 del ESP32 <- TX del Arduino (via level shifter)
#define PIN_UART_TX         17             // TX2 del ESP32 -> RX del Arduino (via level shifter)
#define UART_BAUDIOS     19200             // misma velocidad en el Arduino (SoftwareSerial)

#define PIN_LCD_SCK         18             // reloj SPI (VSPI) -> E de la LCD
#define PIN_LCD_MOSI        23             // datos SPI (VSPI) -> R/W de la LCD
#define PIN_LCD_CS   U8X8_PIN_NONE         // RS de la LCD va fijo a 5 V (no quedaban canales del shifter)
#define LCD_RELOJ_HZ    600000UL           // 600 kHz: seguro a traves del level shifter
#define LCD_ACTUALIZACION_PARCIAL 1        // 1 = solo reenvia zonas cambiadas; 0 = pantalla completa siempre

const uint8_t PIN_FILAS[4]    = {34, 35, 36, 39};          // F1..F4 (entradas con pull-up externo)
const uint8_t PIN_COLUMNAS[6] = {13, 14, 27, 26, 25, 33};  // C1..C6 (salidas de barrido)

#define BARRIDO_MS           5UL           // cada cuanto se barre el teclado
#define MUESTRAS_ESTABLES    4             // 4 lecturas iguales (20 ms) = tecla valida (antirrebote)
#define MULTITAP_MS        900UL           // espera para confirmar una letra (se reinicia en cada pulsacion)
#define PARPADEO_MS        500UL           // parpadeo del cursor
#define MANTENER_MS        350UL           // tiempo para considerar "mantener presionada" una flecha
#define DESPLAZAR_MS       160UL           // velocidad de desplazamiento al mantener la flecha
#define DOBLE_BORRAR_MS   3000UL           // tiempo maximo entre las 2 pulsaciones de BORRAR
#define FIN_LLAMADA_MS    1500UL           // cuanto se muestra la llamada terminada
#define AVISO_SMS_MS      1500UL           // cuanto se muestra "ENVIADO" / "ERROR"
#define ESPERA_COLGAR_MS  8000UL           // si el Arduino no confirma el fin, se fuerza
#define ESPERA_SMS_MS    75000UL           // tiempo maximo para el resultado de un SMS
#define CUADRO_MIN_MS       40UL           // como maximo 25 redibujos por segundo
#define REFRESCO_TOTAL_MS 30000UL          // cada 30 s se reenvia la pantalla completa (por si hubo ruido)
#define ACK_ESPERA_MS      400UL           // espera del "ACK" del Arduino antes de reintentar
#define ACK_INTENTOS         6             // reintentos de una orden antes de darla por fallida
#define PURGA_MS         60000UL           // cada minuto se borran SMS de mas de 24 h
#define VIDA_SMS_MS   86400000UL           // 24 horas en milisegundos

#define BANDEJA_MAX         30             // SMS guardados como maximo (el mas viejo se descarta)
#define PREFIJO_PAIS    "+503"             // El Salvador

/* =============================================================================
 *  BLOQUE 2. TECLAS (codigos internos) Y MAPA DE LA MATRIZ
 * ============================================================================= */
#define T_NADA     0                       // ninguna tecla
#define T_ARRIBA  'U'                      // flecha arriba (o derecha si no se puede subir)
#define T_ABAJO   'D'                      // flecha abajo (o izquierda si no se puede bajar)
#define T_OK      'K'                      // OK / seleccionar / confirmar letra
#define T_ATRAS   'A'                      // flecha hacia atras
#define T_BORRAR  'X'                      // borrar ultimo caracter
#define T_LLAMAR  'L'                      // boton verde (llamar / contestar)
#define T_COLGAR  'C'                      // boton rojo (colgar / rechazar)
#define T_MAYUS   'M'                      // bloq mayus
#define T_EXTRA   'E'                      // posiciones libres EXT1..EXT5 (sin uso)

const char MAPA_TECLAS[4][6] = {           // [fila][columna] segun la PCB (pag. 3 y 4)
  {'1', '2', '3', T_EXTRA,  T_ABAJO,  T_EXTRA },   // F1: 1 2 3 EXT1 Abajo EXT2
  {'4', '5', '6', T_LLAMAR, T_OK,     T_COLGAR},   // F2: 4 5 6 Llamar OK Colgar
  {'7', '8', '9', T_ATRAS,  T_ARRIBA, T_BORRAR},   // F3: 7 8 9 Atras Arriba Borrar
  {T_MAYUS, '0', '#', T_EXTRA, T_EXTRA, T_EXTRA}   // F4: Mayus 0 # EXT3 EXT4 EXT5
};

/* =============================================================================
 *  BLOQUE 3. FOCOS (que elemento de la pantalla esta seleccionado)
 *  Los menus (Pantalla), estados de llamada, SMS y ordenes estan en tipos.h
 * ============================================================================= */
#define F_NINGUNO    0                     // foco: nada seleccionado
#define F_CAMPO      1                     // foco: cuadro de escritura
#define F_X          2                     // foco: boton X / cancelar / rechazar / colgar
#define F_SEGUIR     3                     // foco: flecha de confirmar / enviar / contestar
#define F_REGRESAR   4                     // foco: flecha de regresar
#define F_MENSAJE    5                     // foco (bandeja): un mensaje de la lista

/* =============================================================================
 *  BLOQUE 4. VARIABLES GLOBALES
 * ============================================================================= */
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, PIN_LCD_CS, U8X8_PIN_NONE); // pantalla con buffer completo (1 KB)

// --- teclado ---
uint8_t teclaContador[4][6];               // lecturas seguidas distintas al estado estable
bool teclaEstable[4][6];                   // estado estable (true = presionada)
unsigned long tUltimoBarrido = 0;          // ultimo barrido del teclado
unsigned long tPresionArriba = 0;          // cuando se presiono la flecha arriba
unsigned long tPresionAbajo = 0;           // cuando se presiono la flecha abajo
bool arribaPresionada = false;             // flecha arriba mantenida
bool abajoPresionada = false;              // flecha abajo mantenida
bool huboDesplazamiento = false;           // ya se desplazo el texto con esta pulsacion
bool abajoDesdeTexto = false;              // la flecha abajo se presiono estando en el texto
unsigned long tUltimoDesplazamiento = 0;   // ultimo paso de desplazamiento

// --- escritura (multi-toque) ---
char *edBuf = NULL;                        // texto que se esta editando
uint8_t edMax = 0;                         // largo maximo de ese texto
bool edModoTexto = false;                  // true = letras (mensajes), false = numeros
char edTeclaPend = 0;                      // tecla con letra pendiente de confirmar (0 = ninguna)
uint8_t edIdxPend = 0;                     // posicion dentro del ciclo de esa tecla
unsigned long edLimite = 0;                // instante en que la letra se confirma sola
bool mayusculas = false;                   // bloq mayus activo

// --- datos que escribe el usuario ---
char numeroLlamada[MAX_NUMERO + 1] = "";   // numero a llamar (se conserva hasta salir o colgar)
char numeroSms[MAX_NUMERO + 1] = "";       // numero destino del SMS
char textoSms[MAX_SMS + 1] = "";           // cuerpo del SMS

// --- navegacion ---
Pantalla pantalla = P_MENU;                // menu visible
uint8_t foco = F_NINGUNO;                  // elemento seleccionado en el menu visible
uint8_t iconoMenu = 0;                     // 0 ninguno, 1 llamadas, 2 mensajes, 3 bandeja
Pantalla pantallaPrevia = P_MENU;          // a donde volver tras una llamada entrante
uint8_t focoPrevio = F_NINGUNO;            // foco que tenia esa pantalla
bool llamadaSaliente = false;              // true si la llamada la hicimos nosotros

// --- estado del telefono ---
uint8_t senal = 0;                         // nivel de senal 0..5 (lo manda el Arduino)
EstadoLlamada estadoLlamada = LL_LIBRE;    // estado de la llamada
unsigned long tInicioLlamada = 0;          // instante en que se conecto (cronometro)
unsigned long tFinLlamada = 0;             // instante en que termino
unsigned long segundosMostrados = 0xFFFFFFFFUL; // ultimo segundo dibujado
unsigned long duracionFinal = 0;           // segundos que duro (se congela al terminar)
bool cronometroActivo = false;             // true mientras la llamada esta conectada
bool fueConectada = false;                 // la llamada llego a conectarse (para el fondo)
unsigned long tPedidoColgar = 0;           // cuando se pidio colgar (0 = no se pidio)
char numeroEntrante[MAX_NUMERO + 1] = "";  // quien nos llama

// --- envio de SMS ---
uint8_t estadoEnvio = 0;                   // 0 nada, 1 enviando, 2 enviado, 3 error
unsigned long tEstadoEnvio = 0;            // cuando cambio estadoEnvio

// --- bandeja de entrada (indice 0 = el mas reciente = "1") ---
Sms bandeja[BANDEJA_MAX];                  // mensajes guardados (~6 KB de RAM)
uint8_t nSms = 0;                          // cuantos hay
uint8_t selMsg = 0;                        // mensaje seleccionado en la bandeja
uint8_t primeraFila = 0;                   // mensaje que se ve en la casilla de arriba
bool regresoDesdeUltimo = false;           // la flecha de regresar se alcanzo desde el ultimo
uint8_t msgAbierto = 0;                    // mensaje que se esta leyendo
uint8_t lineaLeer = 0;                     // primera linea visible al leer
bool borrarArmado = false;                 // primera pulsacion de BORRAR ya hecha
unsigned long tBorrarArmado = 0;           // cuando se hizo
unsigned long tUltimaPurga = 0;            // ultima limpieza de SMS viejos

// --- pantalla ---
bool redibujar = true;                     // hay que volver a dibujar
bool envioCompleto = true;                 // proximo envio: pantalla completa
unsigned long tUltimoCuadro = 0;           // ultimo envio a la LCD
unsigned long tUltimoTotal = 0;            // ultimo envio completo
uint8_t sombra[1024];                      // copia de lo que ya tiene la LCD
bool cursorVisible = true;                 // fase del parpadeo
unsigned long tParpadeo = 0;               // ultimo cambio de parpadeo

// --- enlace UART con el Arduino ---
#define COLA_ORDENES 4                     // ordenes en espera como maximo
Orden cola[COLA_ORDENES];                  // cola circular de ordenes
uint8_t colaIni = 0;                       // primera orden de la cola
uint8_t colaCnt = 0;                       // cantidad en la cola
uint8_t siguienteId = 1;                   // proximo numero de orden
char rxLinea[260];                         // linea que llega del Arduino
uint16_t rxLen = 0;                        // largo actual de esa linea
unsigned long tUltimoRx = 0;               // ultimo byte recibido del Arduino

/* =============================================================================
 *  BLOQUE 5. UTILIDADES DE TEXTO
 * ============================================================================= */
void depurar(const char *a, const char *b) { // mensaje de depuracion por USB (a + b)
#if DEPURAR                                 // solo si se depura
  Serial.print(a);                         // primera parte
  Serial.println(b);                       // segunda parte y salto de linea
#endif
}

void copiar(char *dst, const char *src, size_t tam) { // copia segura (siempre termina en 0)
  strncpy(dst, src, tam - 1);              // copia lo que quepa
  dst[tam - 1] = 0;                        // asegura el fin de texto
}

char aMayuscula(char c) {                  // convierte una letra a mayuscula (incluye la ñ)
  if (c >= 'a' && c <= 'z') return c - 32; // a..z -> A..Z
  if ((uint8_t)c == 0xF1) return (char)0xD1; // ñ -> Ñ (Latin-1)
  return c;                                // lo demas no cambia
}

bool soloDigitos(const char *s) {          // true si el texto solo tiene 0..9
  if (!*s) return false;                   // vacio no cuenta
  for (; *s; s++) if (*s < '0' || *s > '9') return false; // algo que no es digito
  return true;                             // todo son digitos
}

// Convierte lo que escribio el usuario al formato que entiende la SIM900:
//  - "7777 7777" (8 digitos, sin +)  -> "+50377777777" (prefijo de El Salvador)
//  - "+1 555..." (empieza con +)     -> se marca tal cual (otro pais)
//  - otros (ej. 911, 123)            -> tal cual
// Se quitan espacios y guiones (solo son visuales).
bool formatearNumero(const char *entrada, char *salida, size_t tam) { // devuelve false si es demasiado corto
  char limpio[MAX_NUMERO + 1];             // numero sin espacios ni guiones
  uint8_t n = 0;                           // largo de "limpio"
  for (const char *p = entrada; *p && n < MAX_NUMERO; p++) { // recorre lo escrito
    if (*p == ' ' || *p == '-') continue;  // separadores visuales: se ignoran
    limpio[n++] = *p;                      // guarda el caracter
  }
  limpio[n] = 0;                           // fin de texto
  uint8_t digitos = 0;                     // cuenta digitos reales
  for (uint8_t i = 0; i < n; i++) if (limpio[i] >= '0' && limpio[i] <= '9') digitos++; // cuenta los digitos
  if (digitos < 3) return false;           // demasiado corto para ser un numero
  if (limpio[0] != '+' && n == 8 && soloDigitos(limpio)) { // numero local de 8 digitos
    snprintf(salida, tam, "%s%s", PREFIJO_PAIS, limpio);   // agrega +503
  } else {
    copiar(salida, limpio, tam);           // internacional o especial: tal cual
  }
  return true;                             // numero listo
}

// Version para mostrar: "77777777" se ve como "7777 7777" mientras se escribe.
void numeroVisible(const char *num, char *salida, size_t tam) { // formato para la pantalla
  size_t n = strlen(num);                  // largo del numero
  if (n > 4 && n <= 8 && soloDigitos(num)) { // numero local en progreso
    snprintf(salida, tam, "%.4s %s", num, num + 4); // inserta el espacio despues de 4 digitos
  } else {
    copiar(salida, num, tam);              // se muestra tal cual
  }
}

/* =============================================================================
 *  BLOQUE 6. ENLACE UART CON EL ARDUINO UNO
 *  Formato de cada linea:  <contenido>*HH\n   (HH = XOR de los bytes, en hex)
 *  ESP32 -> Arduino:  "<id>,LLAMAR,<num>"  "<id>,COLGAR"  "<id>,CONTESTAR"
 *                     "<id>,RECHAZAR"  "<id>,SMS,<num>,<texto>"  "<id>,ESTADO"
 *                     (el Arduino responde "ACK,<id>"; si no llega, se reintenta)
 *  Arduino -> ESP32:  "LISTO"  "SENAL,<0-5>"  "LLAMANDO"  "EN_CURSO"  "FIN"
 *                     "ENTRANTE,<num>"  "SMS,<num>,<fecha>,<texto>"
 *                     "SMS_OK"  "SMS_ERROR"  "ACK,<id>"
 * ============================================================================= */
uint8_t checksum(const char *s) {          // XOR de todos los caracteres
  uint8_t x = 0;                           // acumulador
  while (*s) x ^= (uint8_t)*s++;           // mezcla cada byte
  return x;                                // resultado
}

void transmitirOrden(Orden &o) {           // envia (o reenvia) una orden por la UART
  char cola3[5];                           // "*HH" + fin
  snprintf(cola3, sizeof(cola3), "*%02X", checksum(o.linea)); // calcula el checksum
  Serial2.print(o.linea);                  // contenido
  Serial2.print(cola3);                    // checksum
  Serial2.print('\n');                     // fin de linea
  o.intentos++;                            // cuenta el intento
  o.tEnvio = millis();                     // anota cuando se envio
  depurar("[ESP->UNO] ", o.linea);             // depuracion
}

void enviarOrden(const char *orden) {      // pone una orden en la cola (no bloquea)
  if (colaCnt >= COLA_ORDENES) {           // cola llena: se descarta la mas vieja
    colaIni = (colaIni + 1) % COLA_ORDENES; // descarta la orden mas vieja
    colaCnt--;                              // una orden menos
  }
  Orden &o = cola[(colaIni + colaCnt) % COLA_ORDENES]; // casilla libre al final
  o.id = siguienteId;                      // numero de esta orden
  siguienteId = (siguienteId % 99) + 1;    // 1..99 y vuelve a empezar
  snprintf(o.linea, sizeof(o.linea), "%u,%s", o.id, orden); // "<id>,<orden>"
  o.intentos = 0;                          // aun no se envia
  o.tEnvio = 0;                            // aun no se envia
  colaCnt++;                               // una orden mas
  if (colaCnt == 1) transmitirOrden(o);    // si es la unica, sale de inmediato
}

void ordenFallida(const char *linea);      // (definida en el bloque 9)

void atenderColaOrdenes() {                // reintenta la orden sin ACK (no bloquea)
  if (colaCnt == 0) return;                // nada pendiente
  Orden &o = cola[colaIni];                // orden mas vieja
  if (o.intentos == 0) { transmitirOrden(o); return; } // aun no salio: se envia
  if (millis() - o.tEnvio < ACK_ESPERA_MS) return; // aun hay tiempo para el ACK
  if (o.intentos < ACK_INTENTOS) { transmitirOrden(o); return; } // reintento
  depurar("[ESP] Orden sin respuesta: ", o.linea); // el Arduino no respondio
  char copia[200];                         // copia para avisar despues de sacarla
  copiar(copia, o.linea, sizeof(copia));    // copia el texto de la orden
  colaIni = (colaIni + 1) % COLA_ORDENES;  // se saca de la cola
  colaCnt--;                                // una orden menos
  ordenFallida(copia);                     // la interfaz reacciona (ej. SMS con error)
}

void recibirAck(uint8_t id) {              // llego "ACK,<id>"
  if (colaCnt == 0 || cola[colaIni].id != id) return; // no es la orden esperada
  colaIni = (colaIni + 1) % COLA_ORDENES;  // la orden ya llego: se saca
  colaCnt--;                                // una orden menos
  if (colaCnt > 0) transmitirOrden(cola[colaIni]); // envia la siguiente
}

void procesarEvento(char *ev);             // (definida en el bloque 9)

void leerUart() {                          // junta bytes del Arduino en lineas (no bloquea)
  if (rxLen > 0 && !Serial2.available() && millis() - tUltimoRx > 100UL) rxLen = 0; // linea cortada: se descarta
  while (Serial2.available() > 0) {        // procesa solo lo que ya llego
    char c = (char)Serial2.read();         // un byte
    tUltimoRx = millis();                  // ultimo byte recibido
    if (c == '\r') continue;               // ignora retorno de carro
    if (c != '\n') {                       // aun no termina la linea
      if (rxLen < sizeof(rxLinea) - 1) rxLinea[rxLen++] = c; // guarda si cabe
      continue;                             // sigue juntando bytes
    }
    rxLinea[rxLen] = 0;                    // fin de texto
    uint16_t len = rxLen;                  // largo de la linea completa
    rxLen = 0;                             // buffer listo para la siguiente
    if (len < 4 || rxLinea[len - 3] != '*') continue; // sin checksum: se descarta
    rxLinea[len - 3] = 0;                  // separa contenido y checksum
    uint8_t esperado = (uint8_t)strtoul(&rxLinea[len - 2], NULL, 16); // checksum recibido
    if (checksum(rxLinea) != esperado) { depurar("[UNO] checksum malo", ""); continue; } // linea danada
    depurar("[UNO->ESP] ", rxLinea);           // depuracion
    procesarEvento(rxLinea);               // la interpreta
  }
}

/* =============================================================================
 *  BLOQUE 7. TECLADO MATRICIAL (barrido + antirrebote, sin bloquear)
 * ============================================================================= */
void teclaPresionada(char t);              // (bloque 10)
void teclaSoltada(char t);                 // (bloque 10)

void iniciarTeclado() {                     // configura los pines del teclado
  for (uint8_t f = 0; f < 4; f++) pinMode(PIN_FILAS[f], INPUT); // filas: entradas (pull-up en la PCB)
  for (uint8_t c = 0; c < 6; c++) pinMode(PIN_COLUMNAS[c], INPUT); // columnas en alta impedancia
  memset(teclaContador, 0, sizeof(teclaContador)); // sin cambios pendientes
  memset(teclaEstable, 0, sizeof(teclaEstable));   // ninguna tecla presionada
}

void barrerTeclado() {                      // lee el teclado (no bloquea)
  if (millis() - tUltimoBarrido < BARRIDO_MS) return; // aun no toca barrer
  tUltimoBarrido = millis();               // anota este barrido
  for (uint8_t c = 0; c < 6; c++) {        // una columna a la vez
    pinMode(PIN_COLUMNAS[c], OUTPUT);      // la columna activa pasa a salida...
    digitalWrite(PIN_COLUMNAS[c], LOW);    // ...en BAJO
    delayMicroseconds(4);                  // 4 us para que se estabilice (no afecta a nada)
    for (uint8_t f = 0; f < 4; f++) {      // lee las 4 filas
      bool leida = (digitalRead(PIN_FILAS[f]) == LOW); // BAJO = tecla presionada
      if (leida == teclaEstable[f][c]) {   // igual al estado conocido
        teclaContador[f][c] = 0;           // no hay cambio en curso
        continue;                           // sigue con la siguiente fila
      }
      if (++teclaContador[f][c] < MUESTRAS_ESTABLES) continue; // aun puede ser rebote
      teclaContador[f][c] = 0;             // cambio confirmado
      teclaEstable[f][c] = leida;          // nuevo estado
      char t = MAPA_TECLAS[f][c];          // que tecla es
      if (t == T_EXTRA) continue;          // posiciones libres: se ignoran
      if (leida) teclaPresionada(t);       // flanco: se presiono
      else teclaSoltada(t);                // flanco: se solto
    }
    pinMode(PIN_COLUMNAS[c], INPUT);       // la columna vuelve a alta impedancia (nunca ALTO)
  }
}

/* =============================================================================
 *  BLOQUE 8. ESCRITURA MULTI-TOQUE (como celular antiguo)
 *  1 pulsacion = numero; 2,3,4... = letras; OK confirma; tras MULTITAP_MS se
 *  confirma solo. En modo NUMERO las teclas 0-9 escriben el digito directo y
 *  solo la tecla "#+-" usa multi-toque.
 * ============================================================================= */
const char *cicloTecla(char t) {           // caracteres de cada tecla (modo texto)
  switch (t) {                              // segun la tecla
    case '1': return "1abc";                // tecla 1
    case '2': return "2def";                // tecla 2
    case '3': return "3ghi";                // tecla 3
    case '4': return "4jkl";                // tecla 4
    case '5': return "5mn\xF1o";           // \xF1 = ñ (Latin-1)
    case '6': return "6pqr";                // tecla 6
    case '7': return "7stu";                // tecla 7
    case '8': return "8vwx";                // tecla 8
    case '9': return "9yz";                 // tecla 9
    case '0': return "0 ";                 // 2 pulsaciones = espacio
    case '#': return "#+-";                // numeral, mas, menos
    default:  return "";                    // otras teclas: sin letras
  }
}

void editorIniciar(char *buf, uint8_t max, bool texto) { // elige que texto se edita
  edBuf = buf;                             // destino
  edMax = max;                             // largo maximo
  edModoTexto = texto;                     // letras o numeros
  edTeclaPend = 0;                         // sin letra pendiente
}

bool editorConfirmar() {                   // fija la letra pendiente; true si habia una
  if (!edTeclaPend || !edBuf) return false; // no habia nada pendiente
  size_t n = strlen(edBuf);                // largo actual
  if (!edModoTexto && n > 1 && edBuf[n - 1] == '+') edBuf[n - 1] = 0; // "+" solo vale al inicio
  edTeclaPend = 0;                         // ya no hay pendiente
  redibujar = true;                        // quita el resaltado
  return true;                              // habia una letra pendiente
}

void editorTecla(char t) {                 // llega una tecla 0-9 o #
  if (!edBuf) return;                      // no hay campo activo
  size_t n = strlen(edBuf);                // largo actual
  bool multitoque = edModoTexto || t == '#'; // numeros: solo # usa multi-toque
  if (!multitoque) {                       // digito directo (modo numero)
    editorConfirmar();                     // fija una letra pendiente anterior
    n = strlen(edBuf);                      // largo actualizado
    if (n < edMax) { edBuf[n] = t; edBuf[n + 1] = 0; } // agrega el digito
    redibujar = true;                       // hay que redibujar
    return;                                 // listo
  }
  const char *ciclo = cicloTecla(t);       // opciones de esta tecla
  uint8_t largo = strlen(ciclo);           // cuantas opciones
  if (edTeclaPend == t && n > 0) {         // misma tecla antes del tiempo: siguiente opcion
    edIdxPend = (edIdxPend + 1) % largo;   // avanza en el ciclo
    char c = ciclo[edIdxPend];             // nuevo caracter
    edBuf[n - 1] = mayusculas ? aMayuscula(c) : c; // reemplaza el ultimo
  } else {                                 // otra tecla: nueva letra
    editorConfirmar();                     // fija la anterior
    n = strlen(edBuf);                      // largo actualizado
    if (n >= edMax) return;                // ya no cabe nada
    edIdxPend = 0;                         // primera opcion (el numero)
    edBuf[n] = ciclo[0];                   // escribe el numero
    edBuf[n + 1] = 0;                       // fin de texto
    edTeclaPend = t;                       // queda pendiente
  }
  edLimite = millis() + MULTITAP_MS;       // el tiempo se reinicia con cada pulsacion
  redibujar = true;                         // hay que redibujar
}

void editorBorrar() {                      // tecla BORRAR
  if (!edBuf) return;                      // no hay campo activo
  size_t n = strlen(edBuf);                // largo actual
  edTeclaPend = 0;                         // cancela la letra pendiente
  if (n > 0) edBuf[n - 1] = 0;             // quita el ultimo caracter
  redibujar = true;                         // hay que redibujar
}

void editorTiempo() {                      // confirma sola la letra al vencer el tiempo
  if (edTeclaPend && (long)(millis() - edLimite) >= 0) editorConfirmar(); // vencio el tiempo: se fija la letra
}

/* =============================================================================
 *  BLOQUE 9. LOGICA DEL TELEFONO (llamadas, SMS, bandeja)
 * ============================================================================= */
void irA(Pantalla p, uint8_t f) {          // cambia de menu
  pantalla = p;                            // nuevo menu
  foco = f;                                // foco inicial
  edBuf = NULL;                            // por defecto no se escribe nada
  edTeclaPend = 0;                          // sin letra pendiente
  if (p == P_LLAMADAS) editorIniciar(numeroLlamada, MAX_NUMERO, false); // escribe numero
  if (p == P_MANDAR) editorIniciar(numeroSms, MAX_NUMERO, false);       // escribe numero
  if (p == P_MENSAJE) editorIniciar(textoSms, MAX_SMS, true);           // escribe texto
  redibujar = true;                         // hay que redibujar
}

void volverAlMenu() {                      // regresa al menu principal limpiando datos
  numeroLlamada[0] = 0;                    // el numero se borra al salir
  numeroSms[0] = 0;                         // borra el numero del SMS
  textoSms[0] = 0;                          // borra el texto del SMS
  iconoMenu = 0;                           // ningun icono seleccionado
  irA(P_MENU, F_NINGUNO);                   // muestra el menu principal
}

// ---- llamadas ----
void iniciarLlamada() {                    // boton verde en "LLAMADAS"
  char num[MAX_NUMERO + 6];                // numero ya formateado
  if (!formatearNumero(numeroLlamada, num, sizeof(num))) return; // numero incompleto
  char orden[40];                           // texto de la orden
  snprintf(orden, sizeof(orden), "LLAMAR,%s", num); // orden para el Arduino
  enviarOrden(orden);                       // la manda al Arduino
  estadoLlamada = LL_MARCANDO;             // llamando...
  llamadaSaliente = true;                  // la iniciamos nosotros
  cronometroActivo = false;                // el tiempo NO corre hasta que contesten
  fueConectada = false;                    // titulo "LLAMANDO..."
  duracionFinal = 0;                       // muestra 00:00
  tPedidoColgar = 0;                        // no se ha pedido colgar
  irA(P_LLAMADA, F_NINGUNO);                // muestra LLAMANDO...
}

void colgar() {                            // colgar (o rechazar) desde la interfaz
  if (estadoLlamada == LL_LIBRE || estadoLlamada == LL_TERMINADA) return; // no hay llamada
  if (estadoLlamada == LL_ENTRANTE) enviarOrden("RECHAZAR"); // rechaza la entrante
  else enviarOrden("COLGAR");              // corta la llamada
  if (!tPedidoColgar) tPedidoColgar = millis(); // espera la confirmacion del Arduino
}

void contestar() {                         // aceptar llamada entrante
  if (estadoLlamada != LL_ENTRANTE) return; // solo si esta sonando
  enviarOrden("CONTESTAR");                // el cronometro arranca con "EN_CURSO"
}

void llamadaConectada() {                  // el Arduino confirmo que contestaron
  estadoLlamada = LL_EN_CURSO;             // llamada en curso
  cronometroActivo = true;                 // empieza el cronometro...
  fueConectada = true;                     // ahora el titulo es "LLAMADA EN CURSO"
  tInicioLlamada = millis();               // ...desde 00:00
  segundosMostrados = 0xFFFFFFFFUL;        // fuerza dibujar el tiempo
  tPedidoColgar = 0;                        // ya no se espera el colgado
  if (pantalla != P_LLAMADA) irA(P_LLAMADA, F_NINGUNO); // muestra LLAMADA EN CURSO
  redibujar = true;                         // hay que redibujar
}

void llamadaTerminada() {                  // el Arduino aviso el fin (o se forzo)
  if (estadoLlamada == LL_LIBRE || estadoLlamada == LL_TERMINADA) return; // no habia llamada
  if (cronometroActivo) duracionFinal = (millis() - tInicioLlamada) / 1000UL; // congela el tiempo
  cronometroActivo = false;                // el cronometro se detiene
  tPedidoColgar = 0;                        // ya no se espera el colgado
  if (estadoLlamada == LL_ENTRANTE) {      // rechazada o perdida: vuelve a donde estaba
    estadoLlamada = LL_LIBRE;               // sin llamada
    irA(pantallaPrevia, focoPrevio);        // vuelve a donde estaba
    if (pantallaPrevia == P_MENU) iconoMenu = 0; // en el menu: sin icono marcado
    return;                                 // listo
  }
  estadoLlamada = LL_TERMINADA;            // se muestra un momento en pantalla
  tFinLlamada = millis();                   // anota cuando termino
  if (pantalla != P_LLAMADA) irA(P_LLAMADA, F_NINGUNO); // muestra la llamada terminada
  redibujar = true;                         // hay que redibujar
}

void tareaLlamada() {                      // tiempos de la llamada (se llama en cada loop)
  if (estadoLlamada == LL_EN_CURSO && cronometroActivo) { // cronometro
    unsigned long s = (millis() - tInicioLlamada) / 1000UL; // segundos transcurridos
    if (s != segundosMostrados) { segundosMostrados = s; redibujar = true; } // cambia el numero
  }
  if (tPedidoColgar && millis() - tPedidoColgar > ESPERA_COLGAR_MS) llamadaTerminada(); // sin respuesta: se fuerza
  if (estadoLlamada == LL_TERMINADA && millis() - tFinLlamada > FIN_LLAMADA_MS) { // ya se mostro
    estadoLlamada = LL_LIBRE;               // sin llamada
    if (llamadaSaliente) {                 // llamada nuestra: el numero se borra
      llamadaSaliente = false;              // la llamada ya no es nuestra
      volverAlMenu();                       // menu principal (borra el numero)
    } else {                               // llamada recibida: vuelve a donde estaba
      irA(pantallaPrevia, focoPrevio);      // vuelve a donde estaba
      if (pantallaPrevia == P_MENU) iconoMenu = 0; // en el menu: sin icono marcado
    }
  }
}

void llamadaEntrante(const char *num) {    // el Arduino aviso una llamada entrante
  copiar(numeroEntrante, num, sizeof(numeroEntrante)); // guarda el numero
  if (estadoLlamada == LL_LIBRE) {         // solo si no hay otra llamada
    pantallaPrevia = (pantalla == P_LLAMADA || pantalla == P_ENTRANTE) ? P_MENU : pantalla; // recuerda donde estaba
    focoPrevio = (pantallaPrevia == P_MENU) ? F_NINGUNO : foco; // y que tenia seleccionado
    editorConfirmar();                     // fija una letra pendiente (no se pierde)
  }
  estadoLlamada = LL_ENTRANTE;              // esta sonando
  llamadaSaliente = false;                  // nos llaman a nosotros
  cronometroActivo = false;                 // el cronometro no corre aun
  fueConectada = false;                     // titulo LLAMANDO hasta conectar
  duracionFinal = 0;                        // tiempo en 00:00
  irA(P_ENTRANTE, F_NINGUNO);               // muestra LLAMADA ENTRANTE
}

// ---- SMS saliente ----
void enviarSms() {                         // flecha de enviar en "MENSAJES"
  editorConfirmar();                       // fija la ultima letra
  char num[MAX_NUMERO + 6];                // numero formateado
  if (!textoSms[0] || !formatearNumero(numeroSms, num, sizeof(num))) return; // faltan datos
  static char orden[MAX_SMS + 40];         // "SMS,<num>,<texto>"
  snprintf(orden, sizeof(orden), "SMS,%s,%s", num, textoSms); // arma la orden
  enviarOrden(orden);                       // la manda al Arduino
  estadoEnvio = 1;                         // "ENVIANDO..."
  tEstadoEnvio = millis();                  // anota cuando empezo
  edBuf = NULL;                            // no se escribe mientras se envia
  redibujar = true;                         // hay que redibujar
}

void resultadoSms(bool ok) {               // llego SMS_OK / SMS_ERROR
  if (estadoEnvio != 1) return;            // no se estaba enviando nada
  estadoEnvio = ok ? 2 : 3;                // enviado o error
  tEstadoEnvio = millis();                  // anota cuando cambio
  redibujar = true;                         // hay que redibujar
}

void tareaSms() {                          // tiempos del envio de SMS
  if (estadoEnvio == 1 && millis() - tEstadoEnvio > ESPERA_SMS_MS) resultadoSms(false); // sin respuesta
  if (estadoEnvio >= 2 && millis() - tEstadoEnvio > AVISO_SMS_MS) { // aviso ya mostrado
    bool ok = (estadoEnvio == 2);           // recuerda si salio bien
    estadoEnvio = 0;                        // termina el aviso
    if (pantalla != P_MENSAJE) return;     // (p. ej. entro una llamada)
    if (ok) volverAlMenu();                // enviado: al menu principal
    else { editorIniciar(textoSms, MAX_SMS, true); foco = F_CAMPO; redibujar = true; } // error: se puede reintentar
  }
}

void ordenFallida(const char *linea) {     // el Arduino nunca confirmo una orden
  const char *orden = strchr(linea, ',');  // salta el id
  if (!orden) return;                       // formato inesperado
  orden++;                                  // salta la coma
  if (strncmp(orden, "SMS,", 4) == 0) resultadoSms(false); // el SMS no salio
  if (strncmp(orden, "LLAMAR,", 7) == 0) llamadaTerminada(); // no se pudo marcar
  if (strncmp(orden, "COLGAR", 6) == 0 || strncmp(orden, "RECHAZAR", 8) == 0) llamadaTerminada(); // no se pudo colgar: se cierra igual
}

// ---- bandeja de entrada ----
void ajustarBandeja();                     // (definida mas abajo)

void guardarSms(const char *num, const char *fecha, const char *texto) { // SMS nuevo
  if (nSms == BANDEJA_MAX) nSms--;         // llena: se descarta el mas viejo (el ultimo)
  memmove(&bandeja[1], &bandeja[0], nSms * sizeof(Sms)); // corre todos una posicion
  Sms &m = bandeja[0];                     // el nuevo queda como "1"
  copiar(m.numero, num, sizeof(m.numero));  // remitente
  // la SIM900 manda "aa/mm/dd hh:mm:ss" -> se guarda "dd/mm/aa hh:mm"
  if (strlen(fecha) >= 14) snprintf(m.fecha, sizeof(m.fecha), "%.2s/%.2s/%.2s %.5s", fecha + 6, fecha + 3, fecha, fecha + 9); // fecha reordenada
  else copiar(m.fecha, fecha, sizeof(m.fecha)); // formato desconocido: tal cual
  copiar(m.texto, texto, sizeof(m.texto));  // texto
  m.recibido = millis();                   // para borrarlo a las 24 h
  nSms++;                                   // un mensaje mas
  if (pantalla == P_BANDEJA && foco == F_MENSAJE && selMsg + 1 < nSms) selMsg++; // sigue marcando el mismo
  if (pantalla == P_LEER && ++msgAbierto >= nSms) irA(P_BANDEJA, F_NINGUNO); // el que se lee se corrio (o se descarto)
  ajustarBandeja();                        // la lista sigue mostrando la seleccion
  redibujar = true;                         // hay que redibujar
}

void borrarSms(uint8_t i) {                // elimina el mensaje i
  if (i >= nSms) return;                    // indice invalido
  memmove(&bandeja[i], &bandeja[i + 1], (nSms - i - 1) * sizeof(Sms)); // cierra el hueco
  nSms--;                                   // un mensaje menos
}

void ajustarBandeja() {                    // mantiene la seleccion dentro de la lista
  if (nSms == 0) { if (foco == F_MENSAJE) foco = F_NINGUNO; primeraFila = 0; return; } // vacia: nada que seleccionar
  if (selMsg >= nSms) selMsg = nSms - 1;   // no pasar del ultimo
  if (selMsg < primeraFila) primeraFila = selMsg; // que el seleccionado se vea
  if (selMsg > primeraFila + 1) primeraFila = selMsg - 1; // que el seleccionado quede en la casilla de abajo
  if (primeraFila + 2 > nSms) primeraFila = (nSms >= 2) ? nSms - 2 : 0; // no dejar casillas vacias al final
}

void purgarSms() {                         // borra mensajes de mas de 24 horas
  if (millis() - tUltimaPurga < PURGA_MS) return; // aun no toca revisar
  tUltimaPurga = millis();                  // anota esta revision
  bool cambio = false;                      // aun no se borro nada
  for (int i = nSms - 1; i >= 0; i--) {    // del mas viejo al mas nuevo
    if (millis() - bandeja[i].recibido < VIDA_SMS_MS) continue; // aun vigente
    if (pantalla == P_LEER && i == msgAbierto) irA(P_BANDEJA, F_NINGUNO); // se esta leyendo: se cierra
    else if (pantalla == P_LEER && i < msgAbierto) msgAbierto--; // el que se lee se corre
    borrarSms(i);                           // lo elimina
    cambio = true;                          // hubo cambios
  }
  if (cambio) { ajustarBandeja(); redibujar = true; } // acomoda la lista y redibuja
}

// ---- eventos que llegan del Arduino ----
void procesarEvento(char *ev) {             // interpreta un aviso del Arduino
  char *datos = strchr(ev, ',');           // separa nombre y datos
  if (datos) *datos++ = 0;                 // "NOMBRE\0datos"
  if (!strcmp(ev, "ACK")) { if (datos) recibirAck((uint8_t)atoi(datos)); return; } // confirmacion de una orden
  if (!strcmp(ev, "SENAL")) {              // nivel de senal 0..5
    uint8_t s = datos ? (uint8_t)atoi(datos) : 0; // nivel recibido
    if (s > 5) s = 5;                       // maximo 5
    if (s != senal) { senal = s; redibujar = true; } // solo redibuja si cambio
    return;                                 // listo
  }
  if (!strcmp(ev, "LISTO")) return;        // la SIM900 termino de configurarse
  if (!strcmp(ev, "LLAMANDO")) {           // el Arduino esta marcando
    if (estadoLlamada == LL_LIBRE) {       // (resincronizacion tras reiniciar el ESP32)
      estadoLlamada = LL_MARCANDO; llamadaSaliente = true; cronometroActivo = false; fueConectada = false; duracionFinal = 0; // estado de llamada saliente
      irA(P_LLAMADA, F_NINGUNO);            // muestra LLAMANDO...
    }
    return;                                 // listo
  }
  if (!strcmp(ev, "EN_CURSO")) { llamadaConectada(); return; } // contestaron: cronometro
  if (!strcmp(ev, "FIN")) { llamadaTerminada(); return; }      // la llamada termino
  if (!strcmp(ev, "ENTRANTE")) { llamadaEntrante(datos ? datos : ""); return; } // llamada entrante
  if (!strcmp(ev, "SMS_OK")) { resultadoSms(true); return; } // SMS enviado
  if (!strcmp(ev, "SMS_ERROR")) { resultadoSms(false); return; } // SMS no enviado
  if (!strcmp(ev, "SMS") && datos) {       // SMS,<num>,<fecha>,<texto>
    char *fecha = strchr(datos, ',');      // fin del numero
    if (!fecha) return;                     // formato inesperado
    *fecha++ = 0;                           // corta el numero
    char *texto = strchr(fecha, ',');      // fin de la fecha (el texto puede tener comas)
    if (!texto) return;                     // formato inesperado
    *texto++ = 0;                           // corta la fecha
    guardarSms(datos, fecha, texto);        // lo guarda en la bandeja
  }
}

/* =============================================================================
 *  BLOQUE 10. NAVEGACION: QUE HACE CADA TECLA EN CADA MENU
 * ============================================================================= */
void abrirIconoMenu() {                    // OK en el menu principal
  if (iconoMenu == 1) irA(P_LLAMADAS, F_CAMPO);   // telefono -> llamadas
  if (iconoMenu == 2) irA(P_MANDAR, F_CAMPO);     // carta -> mandar mensaje
  if (iconoMenu == 3) { selMsg = 0; primeraFila = 0; irA(P_BANDEJA, F_NINGUNO); } // bandeja
}

void teclaMenu(char t) {                   // MENU PRINCIPAL
  if (t == T_ARRIBA) iconoMenu = (iconoMenu % 3) + 1;          // arriba = a la derecha (circular)
  if (t == T_ABAJO) iconoMenu = (iconoMenu <= 1) ? 3 : iconoMenu - 1; // abajo = a la izquierda
  if (t == T_OK) abrirIconoMenu();          // OK abre el icono marcado
  redibujar = true;                         // hay que redibujar
}

void teclaLlamadas(char t) {               // LLAMADAS (ingresar numero)
  if (foco == F_CAMPO) {                    // esta en el cuadro del numero
    if ((t >= '0' && t <= '9') || t == '#') editorTecla(t); // escribe
    else if (t == T_BORRAR) editorBorrar();                 // borra el ultimo
    else if (t == T_OK) editorConfirmar();                  // confirma #/+/-
    else if (t == T_ARRIBA || t == T_ATRAS) { editorConfirmar(); foco = F_X; } // sube a la X
    else if (t == T_LLAMAR) { editorConfirmar(); iniciarLlamada(); }          // boton verde
  } else {                                 // foco en la X
    if (t == T_ABAJO) foco = F_CAMPO;      // vuelve al cuadro (el numero sigue ahi)
    else if (t == T_OK || t == T_ATRAS) volverAlMenu(); // OK o 2a vez ATRAS: sale y borra
    else if (t == T_LLAMAR) iniciarLlamada(); // boton verde tambien llama
  }
  redibujar = true;                         // hay que redibujar
}

void teclaLlamada(char t) {                // LLAMANDO / LLAMADA EN CURSO
  if (estadoLlamada == LL_TERMINADA) return; // ya termino: se espera
  if (t == T_COLGAR) { colgar(); return; } // boton rojo: cuelga de inmediato
  if (t == T_ABAJO || t == T_ATRAS) foco = F_X;   // selecciona "COLGAR"
  else if (t == T_ARRIBA) foco = F_NINGUNO;       // quita la seleccion
  else if (t == T_OK && foco == F_X) colgar();    // OK sobre "COLGAR"
  redibujar = true;                         // hay que redibujar
}

void teclaEntrante(char t) {               // LLAMADA ENTRANTE
  if (t == T_LLAMAR) { contestar(); return; }    // boton verde
  if (t == T_COLGAR) { colgar(); return; }       // boton rojo = rechazar
  if (t == T_ARRIBA) foco = (foco == F_SEGUIR) ? F_X : F_SEGUIR;  // por defecto el cheque, luego alterna
  else if (t == T_ABAJO) foco = (foco == F_X) ? F_SEGUIR : F_X;   // por defecto la X, luego alterna
  else if (t == T_ATRAS) { if (foco == F_X) colgar(); else foco = F_X; } // atras marca la X; otra vez rechaza
  else if (t == T_OK) {                     // OK sobre un boton
    if (foco == F_SEGUIR) contestar();     // cheque = contestar
    if (foco == F_X) colgar();             // X = rechazar
  }
  redibujar = true;                         // hay que redibujar
}

void teclaMandar(char t) {                 // MANDAR MENSAJE (numero destino)
  if (foco == F_CAMPO) {                    // esta en el cuadro del numero
    if ((t >= '0' && t <= '9') || t == '#') editorTecla(t); // escribe
    else if (t == T_BORRAR) editorBorrar(); // borra el ultimo
    else if (t == T_OK) { if (!editorConfirmar() && numeroSms[0]) foco = F_SEGUIR; } // OK: confirma "+" o va a la flecha
    else if (t == T_ARRIBA) { editorConfirmar(); foco = F_SEGUIR; } // flecha derecha (confirmar)
    else if (t == T_ABAJO) { editorConfirmar(); foco = F_REGRESAR; } // flecha curva (regresar)
    else if (t == T_ATRAS) { editorConfirmar(); foco = F_REGRESAR; } // atras marca la flecha de regresar
  } else {
    if (t == T_ARRIBA) foco = F_CAMPO;     // vuelve al cuadro del numero
    else if (t == T_ABAJO) foco = (foco == F_REGRESAR) ? F_SEGUIR : F_REGRESAR; // alterna flechas
    else if (t == T_ATRAS) { if (foco == F_REGRESAR) volverAlMenu(); else foco = F_REGRESAR; } // 2a vez atras: regresa
    else if (t == T_OK) {                   // OK sobre una flecha
      if (foco == F_REGRESAR) volverAlMenu();             // regresa al menu
      else if (numeroSms[0]) { textoSms[0] = 0; mayusculas = false; irA(P_MENSAJE, F_CAMPO); } // a escribir
    }
  }
  redibujar = true;                         // hay que redibujar
}

void teclaMensaje(char t) {                // MENSAJES (escribir el texto)
  if (estadoEnvio) return;                 // enviando: no se acepta nada
  if (foco == F_CAMPO) {                    // esta escribiendo el texto
    if ((t >= '0' && t <= '9') || t == '#') editorTecla(t); // escribe (multi-toque)
    else if (t == T_BORRAR) editorBorrar(); // borra el ultimo
    else if (t == T_MAYUS) { editorConfirmar(); mayusculas = !mayusculas; } // bloq mayus
    else if (t == T_OK) editorConfirmar(); // OK SOLO confirma la letra (nunca envia)
    else if (t == T_ARRIBA) { editorConfirmar(); foco = F_SEGUIR; } // flecha de enviar
    else if (t == T_ABAJO || t == T_ATRAS) { editorConfirmar(); foco = F_X; } // X de cancelar
  } else {
    if (t == T_ARRIBA) foco = F_CAMPO;     // vuelve a escribir
    else if (t == T_ABAJO) foco = (foco == F_X) ? F_SEGUIR : F_X; // alterna X / enviar
    else if (t == T_ATRAS) { if (foco == F_X) volverAlMenu(); else foco = F_X; } // 2a vez atras: cancela
    else if (t == T_OK) {                   // OK sobre un boton
      if (foco == F_SEGUIR) enviarSms();   // envia
      else volverAlMenu();                 // X: cancela el envio
    }
  }
  redibujar = true;                         // hay que redibujar
}

void teclaBandeja(char t) {                // MENSAJES RECIBIDOS
  if (t == T_ABAJO) {                       // flecha abajo
    if (foco == F_REGRESAR) { if (nSms) { foco = F_MENSAJE; selMsg = regresoDesdeUltimo ? nSms - 1 : 0; } } // desde regresar: vuelve al extremo de donde vino
    else if (foco == F_NINGUNO) { if (nSms) { foco = F_MENSAJE; selMsg = 0; } else { foco = F_REGRESAR; regresoDesdeUltimo = false; } } // sin seleccion: primer mensaje (o regresar si no hay)
    else if (selMsg + 1 < nSms) selMsg++;  // siguiente mensaje
    else { foco = F_REGRESAR; regresoDesdeUltimo = true; } // despues del ultimo: flecha de regresar
  } else if (t == T_ARRIBA) {               // flecha arriba
    if (foco == F_REGRESAR) { if (nSms) { foco = F_MENSAJE; selMsg = regresoDesdeUltimo ? nSms - 1 : 0; } } // desde regresar: vuelve al extremo de donde vino
    else if (foco == F_MENSAJE && selMsg > 0) selMsg--; // mensaje anterior
    else { foco = F_REGRESAR; regresoDesdeUltimo = false; } // arriba del primero: flecha de regresar
  } else if (t == T_ATRAS) {                // flecha atras
    if (foco == F_REGRESAR) volverAlMenu(); else { foco = F_REGRESAR; regresoDesdeUltimo = false; } // 1a vez marca regresar; 2a vez regresa
  } else if (t == T_OK) {                   // OK
    if (foco == F_REGRESAR) volverAlMenu(); // regresa al menu
    else if (foco == F_MENSAJE) { msgAbierto = selMsg; lineaLeer = 0; borrarArmado = false; irA(P_LEER, F_CAMPO); } // abre el mensaje
  }
  ajustarBandeja();                         // mantiene visible la seleccion
  redibujar = true;                         // hay que redibujar
}

void teclaLeer(char t) {                   // LEER UN MENSAJE
  if (t != T_BORRAR) borrarArmado = false; // otra tecla cancela el borrado
  if (t == T_BORRAR) {                     // 1a vez: nada; 2a vez: se elimina
    if (borrarArmado && millis() - tBorrarArmado < DOBLE_BORRAR_MS) { // 2a pulsacion a tiempo
      borrarArmado = false;                 // ya no esta armado
      borrarSms(msgAbierto);                // elimina el mensaje
      selMsg = msgAbierto;                  // se marca la posicion que ocupaba
      irA(P_BANDEJA, nSms ? F_MENSAJE : F_NINGUNO); // vuelve a la bandeja
      ajustarBandeja();                     // acomoda la lista
    } else { borrarArmado = true; tBorrarArmado = millis(); } // 1a pulsacion: solo se arma (no pasa nada visible)
    return;                                 // listo
  }
  if (foco == F_REGRESAR) {                 // esta en la flecha de regresar
    if (t == T_ARRIBA || t == T_ABAJO) foco = F_CAMPO; // vuelve al texto
    else if (t == T_OK || t == T_ATRAS) { selMsg = msgAbierto; irA(P_BANDEJA, F_MENSAJE); ajustarBandeja(); } // regresa a la bandeja
  } else {
    if (t == T_ATRAS) foco = F_REGRESAR;   // 1a vez marca la flecha
    // ARRIBA/ABAJO en el texto se resuelven al soltar o al mantener (bloque 11)
  }
  redibujar = true;                         // hay que redibujar
}

void teclaPresionada(char t) {             // reparte la tecla segun el menu visible
  char nombre[2] = {t, 0};                 // tecla como texto (para depurar)
  depurar("[TECLA] ", nombre);              // depuracion
  cursorVisible = true; tParpadeo = millis(); // el cursor se ve mientras se escribe
  if (t == T_ARRIBA) { arribaPresionada = true; tPresionArriba = millis(); huboDesplazamiento = false; } // empieza a contar el tiempo mantenida
  if (t == T_ABAJO) { abajoPresionada = true; tPresionAbajo = millis(); huboDesplazamiento = false; } // empieza a contar el tiempo mantenida
  if (pantalla == P_LEER) abajoDesdeTexto = (t == T_ABAJO && foco == F_CAMPO); // clic en el texto
  if (t == T_MAYUS && pantalla != P_MENSAJE) return; // bloq mayus solo sirve al escribir mensajes
  switch (pantalla) {                       // segun el menu visible
    case P_MENU:     teclaMenu(t); break;   // menu principal
    case P_LLAMADAS: teclaLlamadas(t); break; // ingresar numero
    case P_LLAMADA:  teclaLlamada(t); break; // llamando / en curso
    case P_ENTRANTE: teclaEntrante(t); break; // llamada entrante
    case P_MANDAR:   teclaMandar(t); break; // numero del SMS
    case P_MENSAJE:  teclaMensaje(t); break; // texto del SMS
    case P_BANDEJA:  teclaBandeja(t); break; // mensajes recibidos
    case P_LEER:     teclaLeer(t); break;   // leer mensaje
  }
}

/* =============================================================================
 *  BLOQUE 11. FLECHAS MANTENIDAS (desplazar el texto al leer un mensaje)
 * ============================================================================= */
uint8_t lineasDelMensaje(uint8_t idx, char lineas[][23], uint8_t maxLineas); // (bloque 12)

void desplazarLectura(int8_t paso) {       // mueve el texto una linea
  static char tmp[24][23];                 // lineas del mensaje
  uint8_t total = lineasDelMensaje(msgAbierto, tmp, 24); // cuantas lineas tiene
  int maxPrimera = (total > 5) ? total - 5 : 0; // 5 lineas visibles
  int nueva = (int)lineaLeer + paso;        // nueva primera linea
  if (nueva < 0) nueva = 0;                 // no antes del inicio
  if (nueva > maxPrimera) nueva = maxPrimera; // no despues del final
  if (nueva != lineaLeer) { lineaLeer = (uint8_t)nueva; redibujar = true; } // redibuja si se movio
}

void teclaSoltada(char t) {                 // se solto una tecla
  if (t == T_ARRIBA) arribaPresionada = false; // ya no se mantiene
  if (t == T_ABAJO) abajoPresionada = false; // ya no se mantiene
  if (pantalla != P_LEER || foco != F_CAMPO) return; // solo importa al leer, en el texto
  if (t == T_ABAJO && abajoDesdeTexto && !huboDesplazamiento) { foco = F_REGRESAR; redibujar = true; } // un clic: flecha de regresar
  if (t == T_ABAJO) abajoDesdeTexto = false; // termina el clic
}

void tareaFlechasMantenidas() {            // al mantener ARRIBA/ABAJO se desplaza el texto
  if (pantalla != P_LEER || foco != F_CAMPO) return; // solo al leer, en el texto
  unsigned long ahora = millis();           // instante actual
  bool bajar = abajoPresionada && ahora - tPresionAbajo >= MANTENER_MS; // abajo mantenida lo suficiente
  bool subir = arribaPresionada && ahora - tPresionArriba >= MANTENER_MS; // arriba mantenida lo suficiente
  if (!bajar && !subir) return;             // ninguna mantenida
  if (huboDesplazamiento && ahora - tUltimoDesplazamiento < DESPLAZAR_MS) return; // velocidad
  huboDesplazamiento = true;                // ya no cuenta como clic
  tUltimoDesplazamiento = ahora;            // anota este paso
  desplazarLectura(bajar ? 1 : -1);         // una linea
}

/* =============================================================================
 *  BLOQUE 12. DIBUJO DE LOS MENUS (en RAM; luego se envia solo lo que cambio)
 * ============================================================================= */
#define FUENTE_TEXTO u8g2_font_5x7_tf      // 5x7 con acentos y ñ (Latin-1)
#define FUENTE_PEQ   u8g2_font_4x6_tf      // 4x6 para textos chicos

void fondo(const unsigned char *bmp) {     // dibuja un fondo completo de 128x64
  u8g2.setDrawColor(1);                     // pixeles negros
  u8g2.drawXBMP(0, 0, 128, 64, bmp);        // imagen completa
}

#define PIEZA(N) pieza(N##_X, N##_Y, N##_W, N##_H, N) // atajo para dibujar una pieza de graficos.h
void pieza(int x, int y, int w, int h, const unsigned char *bmp) { // dibuja una pieza opaca
  u8g2.setDrawColor(0); u8g2.drawBox(x, y, w, h);   // borra lo que habia debajo
  u8g2.setDrawColor(1); u8g2.drawXBMP(x, y, w, h, bmp); // dibuja la pieza
}

void barrasSenal(int x, int yBase) {       // 5 barras de senal (alto 1..5)
  u8g2.setDrawColor(1);                     // pixeles negros
  for (uint8_t i = 0; i < 5; i++) {         // 5 barras
    int bx = x + i * 2;                    // una barra cada 2 pixeles
    if (i < senal) u8g2.drawVLine(bx, yBase - i, i + 1); // barra encendida
    else u8g2.drawPixel(bx, yBase);        // barra apagada: solo un punto en la base
  }
}

// Digitos de 4x5 con el mismo estilo del "00:00" del diseno (cada fila: 4 bits, bit 3 = izquierda)
const uint8_t DIG4x5[12][5] = {             // tabla de digitos
  {6, 9, 9, 9, 6}, {4, 12, 4, 4, 14}, {14, 1, 6, 8, 15}, {14, 1, 6, 1, 14}, {9, 9, 15, 1, 1}, // 0 1 2 3 4
  {15, 8, 14, 1, 14}, {6, 8, 14, 9, 6}, {15, 1, 2, 4, 4}, {6, 9, 6, 9, 6}, {6, 9, 7, 1, 6}, // 5 6 7 8 9
  {0, 4, 14, 4, 0} /* + */, {0, 8, 0, 8, 0} /* : */
};
int digito4x5(int x, int y, uint8_t d) {   // dibuja un digito y devuelve el avance
  uint8_t ancho = (d == 11) ? 1 : 4;       // ":" mide 1 pixel
  for (uint8_t f = 0; f < 5; f++)           // 5 filas
    for (uint8_t c = 0; c < ancho; c++)     // columnas del digito
      if (DIG4x5[d][f] & (8 >> c)) u8g2.drawPixel(x + c, y + f); // pixel encendido
  return ancho + 1;                        // ancho + 1 pixel de separacion
}

void cronometro(int x, int y) {            // tiempo de llamada mm:ss (max 99:59, luego "+")
  unsigned long s = cronometroActivo ? (millis() - tInicioLlamada) / 1000UL : duracionFinal; // segundos a mostrar
  bool excedido = s > 99UL * 60UL + 59UL;  // mas de 99:59
  if (excedido) s = 99UL * 60UL + 59UL;     // tope 99:59
  uint8_t m = s / 60, sg = s % 60;          // minutos y segundos
  u8g2.setDrawColor(1);                     // pixeles negros
  x += digito4x5(x, y, m / 10);            // decenas de minuto
  x += digito4x5(x, y, m % 10);            // minutos
  x += digito4x5(x, y, 11);                // ":"
  x += digito4x5(x, y, sg / 10);           // decenas de segundo
  x += digito4x5(x, y, sg % 10);           // segundos
  if (excedido) digito4x5(x, y, 10);       // "+"
}

// Texto con cursor: el caracter pendiente (multi-toque) se ve invertido y el
// cursor es una barra que parpadea. 'desde' = primer caracter visible.
void textoConCursor(int x, int y, const char *s, size_t desde, size_t hasta, bool activo, bool conCursor, int altoCursor) { // dibuja un texto editable
  u8g2.setFont(FUENTE_TEXTO);               // fuente 5x7
  u8g2.setFontPosTop();                     // y = borde superior
  size_t n = strlen(s);                     // largo del texto
  for (size_t i = desde; i < hasta && i < n; i++) { // caracteres visibles
    int cx = x + (int)(i - desde) * 5;     // 5 pixeles por caracter
    char c[2] = {s[i], 0};                  // un caracter
    bool pend = activo && edTeclaPend && i == n - 1; // letra aun sin confirmar
    if (pend) { u8g2.setDrawColor(1); u8g2.drawBox(cx - 1, y - 1, 6, 9); u8g2.setDrawColor(0); } // fondo negro, letra blanca
    else u8g2.setDrawColor(1);              // letra negra
    u8g2.drawStr(cx, y, c);                 // dibuja el caracter
  }
  u8g2.setDrawColor(1);                     // vuelve a negro
  if (activo && conCursor && cursorVisible && !edTeclaPend) { // barra del cursor al final del texto
    int cx = x + (int)(n - desde) * 5;      // posicion del cursor
    u8g2.drawVLine(cx, y - (altoCursor - 8) / 2 - 1, altoCursor); // barra vertical
  }
}

void dibujarMenu() {                       // MENU PRINCIPAL
  fondo(FONDO_MENU);                        // fondo del menu
  if (iconoMenu == 1) PIEZA(SEL_MENU_LLAMADA);   // telefono invertido
  if (iconoMenu == 2) PIEZA(SEL_MENU_MENSAJE);   // carta invertida
  if (iconoMenu == 3) PIEZA(SEL_MENU_BANDEJA);   // bandeja invertida (flecha sigue negra)
  barrasSenal(111, 6);                      // senal arriba a la derecha
}

void dibujarCampoNumero(int x, int y, int maxCar, const char *num, bool activo) { // numero en un cuadro
  char vis[MAX_NUMERO + 4];                 // numero con espacio visual
  numeroVisible(num, vis, sizeof(vis));    // "7777 7777"
  size_t n = strlen(vis);                   // largo visible
  size_t desde = (n + 1 > (size_t)maxCar) ? n + 1 - maxCar : 0; // se ven los ULTIMOS digitos
  if (edTeclaPend && activo) desde = (n > (size_t)maxCar) ? n - maxCar : 0; // sin cursor: cabe un caracter mas
  textoConCursor(x, y, vis, desde, n, activo, true, 12); // dibuja el numero
}

void dibujarLlamadas() {                   // LLAMADAS: ingresar numero
  fondo(FONDO_LLAMADAS);                    // fondo de LLAMADAS
  if (foco == F_X) PIEZA(SEL_LLAMADAS_X);  // X invertida
  dibujarCampoNumero(32, 40, 12, numeroLlamada, foco == F_CAMPO); // numero en el cuadro (12 caracteres)
}

void dibujarLlamada() {                    // LLAMANDO... / LLAMADA EN CURSO
  bool conectada = fueConectada;          // titulo "LLAMADA EN CURSO" o "LLAMANDO..."
  fondo(conectada ? FONDO_EN_CURSO : FONDO_LLAMANDO); // fondo segun si ya contestaron
  if (foco == F_X && estadoLlamada != LL_TERMINADA) { // COLGAR seleccionado
    if (conectada) PIEZA(SEL_EN_CURSO_COLGAR); else PIEZA(SEL_LLAMANDO_COLGAR); // "COLGAR" invertido
  }
  cronometro(51, 37);                      // 00:00 en la misma posicion del diseno
  barrasSenal(117, 5);                      // senal arriba a la derecha
  if (estadoLlamada == LL_TERMINADA) {     // aviso de fin
    u8g2.setFont(FUENTE_PEQ); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente chica
    u8g2.drawStr(46, 46, "FINALIZADA");     // aviso de fin
  } else if (tPedidoColgar) {               // se pidio colgar
    u8g2.setFont(FUENTE_PEQ); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente chica
    u8g2.drawStr(46, 46, "COLGANDO...");    // aviso de colgando
  }
}

void dibujarEntrante() {                   // LLAMADA ENTRANTE
  fondo(FONDO_ENTRANTE);                    // fondo de LLAMADA ENTRANTE
  if (foco == F_SEGUIR) PIEZA(SEL_ENTRANTE_CONTESTAR); // cheque invertido
  if (foco == F_X) PIEZA(SEL_ENTRANTE_RECHAZAR);       // X invertida
  u8g2.setFont(FUENTE_TEXTO); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente 5x7
  char vis[19];                            // se muestran los PRIMEROS 18 caracteres
  copiar(vis, numeroEntrante[0] ? numeroEntrante : "DESCONOCIDO", sizeof(vis)); // numero o DESCONOCIDO
  u8g2.drawStr(19, 20, vis);                // dentro del recuadro
}

void dibujarMandar() {                     // MANDAR MENSAJE: numero destino
  fondo(FONDO_MANDAR);                      // fondo de MANDAR MENSAJE
  if (foco == F_REGRESAR) PIEZA(SEL_MANDAR_REGRESAR); // flecha curva invertida
  if (foco == F_SEGUIR) PIEZA(SEL_MANDAR_SEGUIR);     // flecha recta invertida
  dibujarCampoNumero(32, 39, 13, numeroSms, foco == F_CAMPO); // numero en el cuadro (13 caracteres)
}

void dibujarMensaje() {                    // MENSAJES: escribir el texto
  fondo(FONDO_MENSAJE);                     // fondo de MENSAJES
  if (foco == F_SEGUIR) PIEZA(SEL_MENSAJE_ENVIAR);    // flecha de enviar invertida
  if (foco == F_X) PIEZA(SEL_MENSAJE_CANCELAR);       // X invertida
  u8g2.setFont(FUENTE_PEQ); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente chica
  char vis[12];                            // primeros 11 caracteres del numero destino
  char num[MAX_NUMERO + 4];                 // numero con espacio visual
  numeroVisible(numeroSms, num, sizeof(num)); // formato visual
  copiar(vis, num, sizeof(vis));            // recorta
  u8g2.drawStr(55, 9, vis);                 // despues de ENVIANDO A:
  if (estadoEnvio) {                       // aviso de envio dentro del cuadro
    u8g2.setFont(FUENTE_TEXTO);             // fuente 5x7
    const char *msg = (estadoEnvio == 1) ? "ENVIANDO..." : (estadoEnvio == 2) ? "MENSAJE ENVIADO" : "ERROR AL ENVIAR"; // texto del aviso
    u8g2.drawStr(64 - u8g2.getStrWidth(msg) / 2, 31, msg); // centrado en el cuadro
    return;                                 // no se dibuja el texto
  }
  // Saltos de linea SOLO visuales: 22 caracteres por fila, 4 filas visibles.
  const uint8_t porFila = 22, filas = 4;    // 22 por fila, 4 filas
  size_t n = strlen(textoSms);              // largo del texto
  size_t filaCursor = n / porFila;         // fila donde esta el cursor
  size_t primera = (filaCursor >= filas) ? filaCursor - (filas - 1) : 0; // la de arriba desaparece
  for (uint8_t f = 0; f < filas; f++) {     // cada fila visible
    size_t ini = (primera + f) * porFila;  // primer caracter de la fila
    if (ini > n) break;                     // fila vacia: termina
    textoConCursor(9, 20 + f * 8, textoSms, ini, ini + porFila, foco == F_CAMPO, primera + f == filaCursor, 9); // dibuja la fila (cursor solo en su fila)
    if (ini + porFila > n) break;          // el cursor ya se dibujo en esta fila
  }
  char cnt[16];                            // contador de caracteres y bloq mayus
  snprintf(cnt, sizeof(cnt), "%s%u/%u", mayusculas ? "MAY " : "", (unsigned)n, (unsigned)MAX_SMS); // texto del contador
  u8g2.setFont(FUENTE_PEQ);                 // fuente chica
  u8g2.drawStr(64 - u8g2.getStrWidth(cnt) / 2, 56, cnt); // centrado abajo
}

void circuloNumero(int x, int y, int w, int h, const unsigned char *bmp, uint8_t num, bool lleno) { // circulo con numero del mensaje
  u8g2.setDrawColor(1);                     // pixeles negros
  u8g2.drawXBMP(x, y, w, h, bmp);          // contorno (o circulo relleno)
  char t[4];                                // numero como texto
  snprintf(t, sizeof(t), "%u", num);       // numero del mensaje (1 = el mas reciente)
  u8g2.setFont(num < 10 ? FUENTE_TEXTO : FUENTE_PEQ); u8g2.setFontPosTop(); // 1 digito: 5x7; 2 digitos: 4x6
  int tw = u8g2.getStrWidth(t);             // ancho del numero
  u8g2.setDrawColor(lleno ? 0 : 1);        // numero blanco si el circulo esta relleno
  u8g2.setFontMode(1);                     // texto transparente
  u8g2.drawStr(x + (w - tw) / 2 + (num < 10 ? 1 : 0), y + (h - (num < 10 ? 7 : 6)) / 2, t); // centrado en el circulo
  u8g2.setFontMode(0);                      // vuelve al modo normal
  u8g2.setDrawColor(1);                     // vuelve a negro
}

void dibujarBandeja() {                    // MENSAJES RECIBIDOS
  fondo(FONDO_BANDEJA);                     // fondo de MENSAJES RECIBIDOS
  if (foco == F_REGRESAR) PIEZA(SEL_BANDEJA_REGRESAR); // flecha de regresar invertida
  if (nSms == 0) {                         // bandeja vacia
    u8g2.setFont(FUENTE_TEXTO); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente 5x7
    u8g2.drawStr(26, 32, "SIN MENSAJES");   // aviso centrado
    return;                                 // no hay casillas
  }
  for (uint8_t k = 0; k < 2; k++) {        // 2 casillas visibles
    uint8_t i = primeraFila + k;           // mensaje en esta casilla
    if (i >= nSms) break;                  // si solo hay 1 mensaje, solo 1 casilla
    int y = 16 + k * 26;                   // fila superior de la casilla (16 y 42)
    bool sel = (foco == F_MENSAJE && selMsg == i); // esta seleccionado?
    if (sel) circuloNumero(CIRCULO_BANDEJA_LLENO_X, y + 1, CIRCULO_BANDEJA_LLENO_W, CIRCULO_BANDEJA_LLENO_H, CIRCULO_BANDEJA_LLENO, i + 1, true); // circulo negro, numero blanco
    else circuloNumero(CIRCULO_BANDEJA_X, y + 1, CIRCULO_BANDEJA_W, CIRCULO_BANDEJA_H, CIRCULO_BANDEJA, i + 1, false); // circulo blanco, numero negro
    u8g2.drawPixel(17, y + 6); u8g2.drawPixel(17, y + 10); // los dos puntos ":"
    u8g2.drawFrame(20, y, 95, 18);         // recuadro del mensaje
    u8g2.setFont(FUENTE_TEXTO); u8g2.setFontPosTop(); // fuente 5x7
    char linea[19];                         // primeros 18 caracteres
    copiar(linea, bandeja[i].numero, sizeof(linea)); // remitente
    u8g2.drawStr(23, y + 2, linea);        // remitente
    u8g2.setFont(FUENTE_PEQ);               // fuente chica
    char prev[23];                          // primeros 22 caracteres
    copiar(prev, bandeja[i].texto, sizeof(prev)); // inicio del texto
    u8g2.drawStr(23, y + 10, prev);        // inicio del texto
  }
  if (nSms > 2) {                          // flechas solo si hay mas de 2 mensajes
    if (primeraFila > 0) PIEZA(FLECHA_ARRIBA_ACTIVA); else PIEZA(FLECHA_ARRIBA);   // negra = se puede subir
    if (primeraFila + 2 < nSms) PIEZA(FLECHA_ABAJO_ACTIVA); else PIEZA(FLECHA_ABAJO); // negra = se puede bajar
  }
}

// Parte el mensaje en lineas de 22 caracteres (respetando palabras) y agrega
// al final una linea vacia (el "enter") y la hora en que se recibio.
uint8_t lineasDelMensaje(uint8_t idx, char lineas[][23], uint8_t maxLineas) { // devuelve cuantas lineas salieron
  if (idx >= nSms) return 0;                // indice invalido
  const char *s = bandeja[idx].texto;       // texto del mensaje
  uint8_t total = 0;                        // lineas armadas
  while (*s && total < maxLineas - 2) {     // deja lugar al enter y la hora
    size_t largo = strlen(s);               // lo que falta
    size_t corte = largo > 22 ? 22 : largo; // maximo 22 por linea
    if (largo > 22) {                      // busca un espacio para no partir palabras
      for (size_t k = 22; k > 10; k--) if (s[k] == ' ') { corte = k; break; } // corta en el ultimo espacio
    }
    memcpy(lineas[total], s, corte);        // copia la linea
    lineas[total][corte] = 0;               // fin de texto
    total++;                                // una linea mas
    s += corte;                             // avanza
    while (*s == ' ') s++;                 // la siguiente linea no empieza con espacio
  }
  lineas[total++][0] = 0;                  // el "enter"
  snprintf(lineas[total++], 23, "%s", bandeja[idx].fecha); // hora de recepcion
  return total;                             // total de lineas
}

void dibujarLeer() {                       // LEER MENSAJE
  fondo(FONDO_LEER);                        // fondo de MENSAJE DE
  if (foco == F_REGRESAR) PIEZA(SEL_LEER_REGRESAR); // flecha de regresar invertida
  if (msgAbierto >= nSms) return;           // no existe (seguridad)
  circuloNumero(CIRCULO_LEER_X, CIRCULO_LEER_Y, CIRCULO_LEER_W, CIRCULO_LEER_H, CIRCULO_LEER, msgAbierto + 1, false); // numero del mensaje en el circulo
  u8g2.setFont(FUENTE_PEQ); u8g2.setFontPosTop(); u8g2.setDrawColor(1); // fuente chica
  char rem[14];                             // remitente recortado
  copiar(rem, bandeja[msgAbierto].numero, sizeof(rem)); // primeros 13 caracteres del remitente
  u8g2.drawStr(70, 3, rem);                 // despues de MENSAJE DE:
  static char lineas[24][23];               // lineas del mensaje
  uint8_t total = lineasDelMensaje(msgAbierto, lineas, 24); // parte el texto
  if (lineaLeer + 5 > total) lineaLeer = (total > 5) ? total - 5 : 0; // no pasar del final
  u8g2.setFont(FUENTE_TEXTO);               // fuente 5x7
  for (uint8_t f = 0; f < 5 && lineaLeer + f < total; f++) // 5 lineas visibles
    u8g2.drawStr(7, 16 + f * 8, lineas[lineaLeer + f]); // una linea
}

/* =============================================================================
 *  BLOQUE 13. ENVIO A LA LCD (solo las zonas que cambiaron)
 *  La ST7920 guarda la imagen en bloques de 16 pixeles de ancho por fila. Se
 *  compara el buffer nuevo con la copia "sombra" de lo que ya tiene la LCD y,
 *  por cada franja de 8 filas, se envian solo las columnas que cambiaron.
 * ============================================================================= */
void enviarPantalla() {                     // envia el buffer a la LCD
  uint8_t *buf = u8g2.getBufferPtr();      // 1024 bytes: fila y, byte x -> buf[y*16 + x]
  bool total = envioCompleto || !LCD_ACTUALIZACION_PARCIAL || millis() - tUltimoTotal > REFRESCO_TOTAL_MS; // toca envio completo?
  if (total) {                             // pantalla completa
    u8g2.sendBuffer();                      // envia los 1024 bytes
    memcpy(sombra, buf, sizeof(sombra));    // la LCD ya tiene esto
    envioCompleto = false;                  // proximo: parcial
    tUltimoTotal = millis();                // anota el envio completo
    return;                                 // listo
  }
  for (uint8_t ty = 0; ty < 8; ty++) {     // 8 franjas de 8 filas
    int8_t x0 = -1, x1 = -1;               // primera y ultima columna (byte) con cambios
    for (uint8_t l = 0; l < 8; l++) {       // cada fila de la franja
      const uint8_t *a = buf + (ty * 8 + l) * 16; // fila nueva
      const uint8_t *b = sombra + (ty * 8 + l) * 16; // fila que ya tiene la LCD
      for (uint8_t x = 0; x < 16; x++) {    // 16 bytes por fila
        if (a[x] == b[x]) continue;         // igual: sin cambio
        if (x0 < 0 || x < x0) x0 = x;       // primera columna cambiada
        if (x > x1) x1 = x;                 // ultima columna cambiada
      }
    }
    if (x0 < 0) continue;                  // franja sin cambios
    x0 &= ~1;                              // la ST7920 trabaja de 16 en 16 pixeles
    x1 |= 1;                                // termina en impar
    uint8_t cnt = x1 - x0 + 1;             // bytes por fila a enviar (par)
    uint8_t tmp[8 * 16];                    // zona a enviar
    for (uint8_t l = 0; l < 8; l++) {      // copia la zona cambiada
      memcpy(tmp + l * cnt, buf + (ty * 8 + l) * 16 + x0, cnt); // copia la zona
      memcpy(sombra + (ty * 8 + l) * 16 + x0, buf + (ty * 8 + l) * 16 + x0, cnt); // actualiza la sombra
    }
    u8x8_DrawTile(u8g2.getU8x8(), x0, ty, cnt, tmp); // envia solo esa zona
  }
}

void tareaPantalla() {                      // dibuja solo si algo cambio
  if (millis() - tParpadeo >= PARPADEO_MS) { // parpadeo del cursor
    tParpadeo = millis();                   // anota el cambio
    cursorVisible = !cursorVisible;         // invierte el cursor
    if (edBuf && foco == F_CAMPO) redibujar = true; // solo si hay un cuadro de escritura activo
  }
  if (millis() - tUltimoTotal > REFRESCO_TOTAL_MS) redibujar = true; // refresco de seguridad
  if (!redibujar || millis() - tUltimoCuadro < CUADRO_MIN_MS) return; // nada nuevo o muy pronto
  redibujar = false;                        // se va a dibujar
  tUltimoCuadro = millis();                 // anota el cuadro
  u8g2.clearBuffer();                      // dibuja todo en RAM (rapido)
  switch (pantalla) {                       // dibuja el menu visible
    case P_MENU:     dibujarMenu(); break;  // menu principal
    case P_LLAMADAS: dibujarLlamadas(); break; // ingresar numero
    case P_LLAMADA:  dibujarLlamada(); break; // llamando / en curso
    case P_ENTRANTE: dibujarEntrante(); break; // llamada entrante
    case P_MANDAR:   dibujarMandar(); break; // numero del SMS
    case P_MENSAJE:  dibujarMensaje(); break; // texto del SMS
    case P_BANDEJA:  dibujarBandeja(); break; // mensajes recibidos
    case P_LEER:     dibujarLeer(); break;  // leer mensaje
  }
  enviarPantalla();                        // manda solo lo que cambio
}

/* =============================================================================
 *  BLOQUE 14. SETUP Y LOOP
 * ============================================================================= */
void setup() {                              // se ejecuta una vez al encender
#if DEPURAR                                 // solo si se depura
  Serial.begin(115200);                    // USB para depurar
#endif
  Serial2.setRxBufferSize(1024);           // buffer grande: no se pierden datos del Arduino
  Serial2.begin(UART_BAUDIOS, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX); // UART con el Arduino
  iniciarTeclado();                         // pines del teclado
  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_MOSI, -1); // SPI solo con reloj y datos
  u8g2.setBusClock(LCD_RELOJ_HZ);          // velocidad segura para el level shifter
  u8g2.begin();                            // inicia la ST7920
  u8g2.setBitmapMode(1);                   // bitmaps transparentes (las piezas borran su zona antes)
  envioCompleto = true;                     // el primer envio es completo
  enviarOrden("ESTADO");                   // pide senal y estado de llamada al Arduino
  depurar("[ESP32] Celular listo", "");         // depuracion
}

void loop() {                               // se repite siempre (nada aqui espera)
  leerUart();                              // 1. datos del Arduino
  atenderColaOrdenes();                    // 2. reintentos de ordenes sin ACK
  barrerTeclado();                         // 3. teclado
  editorTiempo();                          // 4. confirma letras pendientes
  tareaFlechasMantenidas();                // 5. desplazamiento al leer
  tareaLlamada();                          // 6. cronometro y fin de llamada
  tareaSms();                              // 7. resultado del envio de SMS
  purgarSms();                             // 8. borra SMS de mas de 24 h
  tareaPantalla();                         // 9. dibuja si algo cambio
}
