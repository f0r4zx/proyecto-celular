/*
 * =============================================================================
 *  CELULAR BASICO - PROGRAMA DEL ARDUINO UNO (TELECOMUNICACIONES)
 *  Placa: Arduino Uno (ATmega328P) + GPRS Shield SIM900 ("GPRS Shield Arduino UNO")
 * =============================================================================
 *  El Arduino es solo un MEDIADOR: recibe ordenes del ESP32 por UART y las
 *  convierte en comandos AT para la SIM900; y avisa al ESP32 lo que pasa en la
 *  red (llamada entrante, llamada conectada/terminada, SMS recibido, senal).
 *    - Llamar a un numero, colgar, contestar y rechazar llamadas.
 *    - Detectar cuando la llamada se conecta (EN_CURSO) y cuando termina (FIN):
 *      el ESP32 usa esos avisos para su cronometro (aqui NO se mide el tiempo).
 *    - Enviar SMS y recibir SMS (numero, fecha/hora y texto).
 *    - Medir la senal (AT+CSQ) y convertirla a 6 niveles 0..5; solo se avisa al
 *      ESP32 cuando el nivel cambia.
 *    - Volumen de la bocina fijo en 80 % y microfono siempre abierto.
 *  Todo es NO BLOQUEANTE: no hay delay(); un solo comando AT a la vez y cada
 *  respuesta se procesa cuando llega.
 *
 * -----------------------------------------------------------------------------
 *  CONEXIONES
 * -----------------------------------------------------------------------------
 *  GPRS SHIELD SIM900 (ya cableado: D0..D10, A0..A5, GND, 5V, 3.3V, VIN):
 *    D0 (RX) <- TX de la SIM900   |  UART por hardware ("Serial") a 9600 baudios.
 *    D1 (TX) -> RX de la SIM900   |  Poner los 2 jumpers de seleccion de puerto
 *                                    del shield en la posicion de HARDWARE
 *                                    (D0/D1; en algunos shields dice "HW" o "Xduino").
 *    D9      -> encendido por software (PWRKEY) de la SIM900 (pulso de 1.2 s).
 *               Si el shield no tiene esa funcion habilitada, encender con el
 *               boton "PWRKEY" del shield; el programa funciona igual.
 *    Alimentacion de la SIM900: picos de hasta 2 A. Usar la entrada DC del
 *    shield (o una fuente de 5 V / 2 A); el USB del Arduino NO alcanza.
 *  AUDIO (salen del shield, no del Arduino):
 *    Jack/pines SPK de la SIM900 -> entrada IN de la placa LM386 -> parlante 8 ohm.
 *    Microfono electret -> jack/pines MIC de la SIM900 (canal principal, CHFA=0).
 *  ESP32 (a traves del level shifter, ver programa del ESP32):
 *    D11 (RX por software) <- HV1 <- LV1 <- GPIO 17 (TX2 del ESP32)
 *    D12 (TX por software) -> HV2 -> LV2 -> GPIO 16 (RX2 del ESP32)
 *    GND del Arduino = GND del ESP32 (obligatorio).
 *  D13: LED de la placa. Parpadea mientras se configura la SIM900; fijo = lista.
 *
 *  IMPORTANTE AL CARGAR EL PROGRAMA: la SIM900 usa D0/D1, los mismos pines del
 *  USB. Para subir el sketch, quitar los jumpers de puerto del shield (o
 *  apagar el shield) y volverlos a poner despues. No usar Serial.print() para
 *  depurar: todo lo que se imprima le llega a la SIM900.
 * =============================================================================
 */

#include <SoftwareSerial.h>                // UART por software para el ESP32

// ============================ 1. CONFIGURACION ============================
#define SIM Serial                          // UART por hardware (D0/D1) hacia la SIM900
#define BAUD_SIM 9600                       // velocidad de la SIM900 (autobaud con "AT")
#define PIN_ESP_RX 11                       // D11 <- TX del ESP32 (via level shifter)
#define PIN_ESP_TX 12                       // D12 -> RX del ESP32 (via level shifter)
#define BAUD_ESP 19200                      // misma velocidad que en el ESP32
#define PIN_ENCENDIDO_SIM 9                 // D9 -> PWRKEY de la SIM900
#define USAR_PIN_ENCENDIDO 1                // 1 = intenta encender la SIM900 si no responde
#define PIN_LED 13                          // LED de estado de la placa
#define TLF_VOLUMEN_BOCINA 80               // volumen fijo de la bocina en llamada (0-100)
#define TLF_CANAL_AUDIO 0                   // canal de audio SIM900: 0 = principal (SPK/MIC)
#define TLF_GANANCIA_MIC 10                 // ganancia del microfono (0-15)
#define TLF_PERIODO_CSQ_MS 5000UL           // cada cuanto se mide la senal
#define TLF_PERIODO_CLCC_MS 1000UL          // cada cuanto se consulta el estado de la llamada
#define TLF_GRACIA_LLAMADA_MS 2500UL        // espera antes de dar por terminada una llamada que el modulo no lista
#define TLF_ESPERA_CLIP_MS 1500UL           // espera maxima del numero entrante antes de avisar al ESP32
#define TLF_IGNORAR_RING_MS 1500UL          // ignora RING sobrantes justo despues de colgar
#define TLF_LONG_NUM 21                     // tamano maximo de un numero (20 caracteres + fin)
#define TLF_LONG_SMS 160                    // caracteres maximos de un SMS
#define TLF_LONG_LINEA 164                  // tamano del buffer de una linea de la SIM900
#define TLF_COLA_SMS 8                      // SMS nuevos que pueden quedar en espera de lectura
#define ESP_LONG_LINEA 192                  // linea mas larga que puede mandar el ESP32
#define TX_TAM 256                          // buffer de salida hacia el ESP32

// ============================ 2. CONSTANTES INTERNAS ============================
#define TLF_LIBRE 0                         // estado de llamada: sin llamada
#define TLF_MARCANDO 1                      // estado de llamada: llamando a otro
#define TLF_ENTRANTE 2                      // estado de llamada: alguien nos llama
#define TLF_EN_CURSO 3                      // estado de llamada: hablando
#define TLF_RESP_NINGUNA 0                  // respuesta a llamada entrante: aun sin respuesta
#define TLF_RESP_CONTESTAR 1                // respuesta a llamada entrante: contestar
#define TLF_RESP_RECHAZAR 2                 // respuesta a llamada entrante: rechazar
#define TAG_NADA 0                          // comando AT en curso: ninguno
#define TAG_INIT 1                          // comando AT en curso: paso de inicializacion
#define TAG_ATD 2                           // comando AT en curso: marcar
#define TAG_ATA 3                           // comando AT en curso: contestar
#define TAG_ATH 4                           // comando AT en curso: colgar
#define TAG_CLCC 5                          // comando AT en curso: consultar llamada
#define TAG_CSQ 6                           // comando AT en curso: consultar senal
#define TAG_AUDIO 7                         // comando AT en curso: ajuste de audio
#define TAG_CMGS_PROMPT 8                   // comando AT en curso: esperando el '>' para escribir SMS
#define TAG_CMGS_ENVIO 9                    // comando AT en curso: SMS enviandose a la red
#define TAG_CMGR 10                         // comando AT en curso: leyendo un SMS recibido
#define TAG_CMGD 11                         // comando AT en curso: borrando un SMS de la SIM
#define TAG_CREG 12                         // comando AT en curso: registro en la red
#define TLF_ULTIMO_PASO_INIT 10             // ultimo paso de la secuencia de inicializacion
#define TLF_PRIMER_PASO_OPCIONAL 6          // desde este paso, si falla se sigue igual

#define TLF_ES(l, s) (strcmp_P((l), PSTR(s)) == 0)                       // compara linea con texto (texto en flash)
#define TLF_EMPIEZA(l, s) (strncmp_P((l), PSTR(s), sizeof(s) - 1) == 0)  // linea empieza con texto (texto en flash)

// ============================ 3. ORDENES QUE LLEGAN DEL ESP32 ============================
char tlfNumeroLlamada[TLF_LONG_NUM] = "";   // numero al que se quiere llamar
bool tlfOrdenLlamar = false;                // true -> marca tlfNumeroLlamada
bool tlfOrdenColgar = false;                // true -> corta la llamada (en curso, marcando o sonando)
char tlfNumeroSMS[TLF_LONG_NUM] = "";       // numero destino del SMS
char tlfTextoSMS[TLF_LONG_SMS + 1] = "";    // texto del SMS (Latin-1)
bool tlfOrdenEnviarSMS = false;             // true -> envia el SMS
uint8_t tlfRespuestaEntrante = TLF_RESP_NINGUNA; // respuesta a llamada entrante (1 contestar, 2 rechazar)

// ============================ 4. VARIABLES INTERNAS ============================
SoftwareSerial esp(PIN_ESP_RX, PIN_ESP_TX); // puerto serie por software hacia el ESP32

uint8_t tlfEstadoLlamada = TLF_LIBRE;       // estado actual de la llamada
unsigned long tlfTCambioEstado = 0;         // instante del ultimo cambio de estado
unsigned long tlfTFinLlamada = 0UL - TLF_IGNORAR_RING_MS; // instante en que termino la ultima llamada
char tlfNumeroEntrante[TLF_LONG_NUM] = "";  // numero de quien llama
bool tlfClipRecibido = false;               // true si ya llego el numero de quien llama
bool tlfEntranteAvisado = false;            // true si ya se aviso al ESP32 de la llamada entrante

uint8_t tlfAtTag = TAG_NADA;                // comando AT esperando respuesta
unsigned long tlfAtInicio = 0;              // instante en que se envio ese comando
unsigned long tlfAtTimeout = 0;             // tiempo maximo de espera de ese comando

bool tlfModemListo = false;                 // true cuando la SIM900 ya fue configurada
uint8_t tlfInitPaso = 0;                    // paso actual de la inicializacion
unsigned long tlfInitReintento = 0;         // instante del proximo intento de inicializacion
uint8_t tlfFallosAT = 0;                    // "AT" seguidos sin respuesta
uint8_t tlfPulso = 0;                       // encendido por D9: 0 nada, 1 pulso, 2 esperando arranque
unsigned long tlfTPulso = 0;                // instante del cambio de fase del pulso
uint8_t tlfAudioPaso = 0;                   // ajuste de audio pendiente (0 nada, 1 microfono, 2 volumen)

char tlfLinea[TLF_LONG_LINEA];              // linea que se esta recibiendo de la SIM900
uint8_t tlfLineaLen = 0;                    // cantidad de caracteres de esa linea
bool tlfLineaVaciaPrevia = false;           // true si antes de esta linea hubo una linea vacia

uint8_t tlfColaSms[TLF_COLA_SMS];           // indices (en la SIM) de SMS por leer
uint8_t tlfColaIni = 0;                     // posicion del siguiente indice a leer
uint8_t tlfColaCnt = 0;                     // cantidad de indices en espera
uint8_t tlfSmsIdx = 0;                      // indice del SMS que se esta leyendo
uint8_t tlfSmsBorrar = 0;                   // indice del SMS por borrar (0 = ninguno)
bool tlfCmgrCuerpo = false;                 // true cuando ya llego la cabecera del SMS y falta el cuerpo
bool tlfSmsUcs2 = false;                    // true si el SMS viene en UCS2 (texto en hexadecimal)
uint16_t tlfUcs2Valor = 0;                  // caracter UCS2 que se esta armando
uint8_t tlfUcs2Nibbles = 0;                 // cifras hex reunidas (0..3)
bool tlfGsmEscape = false;                  // el caracter anterior fue el escape GSM 0x1B
bool tlfSmsPorAvisar = false;               // hay un SMS leido esperando ir al ESP32
char tlfSmsRemitente[TLF_LONG_NUM];         // numero de quien envio el SMS
char tlfSmsHora[24];                        // hora del SMS recibido
char tlfSmsCuerpo[TLF_LONG_SMS + 1];        // texto del SMS recibido (Latin-1)
uint8_t tlfSmsCuerpoLen = 0;                // largo del texto recibido
uint8_t tlfSmsTxIdx = 0;                    // posicion del texto que se esta enviando a la SIM900
bool tlfSmsTxActivo = false;                // true mientras se escribe el texto del SMS a la SIM900

bool tlfClccVisto = false;                  // true si AT+CLCC listo una llamada
int8_t tlfClccStat = -1;                    // estado de la llamada segun AT+CLCC
unsigned long tlfTUltCsq = 0;               // instante de la ultima consulta de senal
unsigned long tlfTUltClcc = 0;              // instante de la ultima consulta de llamada
uint8_t tlfNivelSenal = 255;                // ultimo nivel de senal enviado (255 = ninguno aun)
bool tlfRegistrado = false;                 // true si la SIM esta registrada en la red (AT+CREG)
bool tlfCsqPendiente = false;               // tras AT+CREG? toca medir la senal

char espLinea[ESP_LONG_LINEA];              // linea que llega del ESP32
uint8_t espLen = 0;                         // largo de esa linea
bool espDesborde = false;                   // la linea fue demasiado larga (se descarta)
unsigned long espTUltByte = 0;              // ultimo byte recibido del ESP32
uint8_t espUltimoId = 0;                    // ultima orden ejecutada (evita repetirla si llega 2 veces)
uint8_t txBuf[TX_TAM];                      // cola de bytes hacia el ESP32
uint16_t txIni = 0;                         // primer byte de la cola
uint16_t txCnt = 0;                         // bytes en la cola
uint8_t txChk = 0;                          // checksum de la linea que se esta armando

// ============================ 5. SALIDA HACIA EL ESP32 (sin bloquear) ============================
// Cada aviso es una linea "<contenido>*HH\n" (HH = XOR de los bytes en hex).
// Se guarda en una cola y sale de a 1-2 bytes por vuelta de loop(), solo
// cuando el ESP32 no esta transmitiendo (SoftwareSerial no puede hacer las dos
// cosas a la vez).
void txByte(uint8_t c) {                    // agrega un byte a la cola
  if (txCnt >= TX_TAM) return;              // cola llena: se pierde (el checksum lo detecta)
  txBuf[(txIni + txCnt) % TX_TAM] = c;      // lo pone al final
  txCnt++;                                  // un byte mas
}
uint16_t txLibre() { return TX_TAM - txCnt; } // espacio libre en la cola
void lnIni() { txChk = 0; }                 // empieza una linea
void lnCar(char c) { txChk ^= (uint8_t)c; txByte(c); } // agrega un caracter (y lo suma al checksum)
void lnTxt(const char *s) { while (*s) lnCar(*s++); } // agrega un texto en RAM
void lnTxtF(const __FlashStringHelper *f) { // agrega un texto guardado en flash
  PGM_P p = (PGM_P)f;                       // puntero a la flash
  char c;                                   // caracter leido
  while ((c = pgm_read_byte(p++)) != 0) lnCar(c); // copia hasta el fin
}
void lnNum(uint16_t v) { char b[6]; utoa(v, b, 10); lnTxt(b); } // agrega un numero
void lnFin() {                              // cierra la linea con "*HH\n"
  const char hex[] = "0123456789ABCDEF";    // cifras hexadecimales
  txByte('*');                              // separador del checksum
  txByte(hex[txChk >> 4]);                  // cifra alta
  txByte(hex[txChk & 0x0F]);                // cifra baja
  txByte('\n');                             // fin de linea
}
void txAtender() {                          // manda bytes pendientes (maximo 2 por vuelta)
  if (txCnt == 0) return;                   // nada que mandar
  if (espLen > 0 || esp.available() || millis() - espTUltByte < 3) return; // el ESP32 esta hablando: espera
  for (uint8_t k = 0; k < 2 && txCnt > 0; k++) { // 2 bytes = ~1 ms a 19200 baudios
    esp.write(txBuf[txIni]);                // envia el byte
    txIni = (txIni + 1) % TX_TAM;           // avanza la cola
    txCnt--;                                // un byte menos
  }
}

void evSimple(const __FlashStringHelper *t) { lnIni(); lnTxtF(t); lnFin(); } // aviso sin datos
void evSenal(uint8_t nivel) { lnIni(); lnTxtF(F("SENAL,")); lnNum(nivel); lnFin(); } // SENAL,<0-5>
void evEntrante() { lnIni(); lnTxtF(F("ENTRANTE,")); lnTxt(tlfNumeroEntrante); lnFin(); } // ENTRANTE,<num>
void evAck(uint8_t id) { lnIni(); lnTxtF(F("ACK,")); lnNum(id); lnFin(); } // ACK,<id>

bool evSmsRecibido() {                      // SMS,<remitente>,<hora>,<texto>  (true si cupo)
  uint16_t largo = 12 + strlen(tlfSmsRemitente) + strlen(tlfSmsHora) + tlfSmsCuerpoLen; // tamano de la linea
  if (txLibre() < largo + 8) return false;  // no cabe aun: se intenta en la siguiente vuelta
  lnIni();                                  // empieza la linea
  lnTxtF(F("SMS,"));                        // nombre del aviso
  lnTxt(tlfSmsRemitente);                   // quien lo envio
  lnCar(',');                               // separador
  lnTxt(tlfSmsHora);                        // hora "aa/mm/dd hh:mm:ss"
  lnCar(',');                               // separador
  lnTxt(tlfSmsCuerpo);                      // texto (al final: puede tener comas)
  lnFin();                                  // cierra la linea
  return true;                              // enviado a la cola
}

// ============================ 6. UTILIDADES ============================
bool tlfNumeroValido(const char *n) {       // acepta solo digitos, + * #
  if (n[0] == 0) return false;              // vacio no es valido
  for (; *n; n++) {                         // revisa cada caracter
    if (!((*n >= '0' && *n <= '9') || *n == '+' || *n == '*' || *n == '#')) return false; // caracter no permitido
  }
  return true;                              // todo correcto
}

bool tlfCampoComillas(const char *s, uint8_t n, char *dst, uint8_t max) { // copia el texto entre comillas numero n
  uint8_t q = 0;                            // cuenta de comillas vistas
  const char *ini = NULL;                   // inicio del campo buscado
  for (; *s; s++) {                         // recorre la linea
    if (*s != '"') continue;                // solo interesan las comillas
    q++;                                    // una comilla mas
    if (q == (uint8_t)(2 * n - 1)) {        // comilla de apertura del campo n
      ini = s + 1;                          // el campo empieza despues
    } else if (q == (uint8_t)(2 * n)) {     // comilla de cierre del campo n
      uint8_t len = s - ini;                // largo del campo
      if (len >= max) len = max - 1;        // recorta si no cabe
      memcpy(dst, ini, len);                // copia el campo
      dst[len] = 0;                         // fin de texto
      return true;                          // encontrado
    }
  }
  dst[0] = 0;                               // no encontrado: texto vacio
  return false;                             // sin campo
}

uint8_t tlfCsqANivel(int csq) {             // convierte CSQ (0-31, 99) a nivel 0-5
  if (csq <= 0 || csq > 31) return 0;       // 0 o 99 (desconocido) = sin senal
  return (uint8_t)(((csq - 1) * 5) / 31 + 1); // reparte 1-31 en 5 rangos iguales (1-7,8-13,14-19,20-25,26-31)
}

char gsmALatin(uint8_t c) {                 // alfabeto GSM 7 bits -> Latin-1 (lo que dibuja el ESP32)
  switch (c) {                              // solo los que no coinciden con ASCII
    case 0x80: return '@';                  // 0x00 (se guarda como 0x80 para no cortar el texto)
    case 0x01: return (char)0xA3;           // libra
    case 0x02: return '$';                  // dolar
    case 0x04: return (char)0xE8;           // e grave
    case 0x05: return (char)0xE9;           // e aguda
    case 0x06: return (char)0xF9;           // u grave
    case 0x07: return (char)0xEC;           // i grave
    case 0x08: return (char)0xF2;           // o grave
    case 0x09: return (char)0xC7;           // C cedilla
    case 0x11: return '_';                  // guion bajo
    case 0x1F: return (char)0xC9;           // E aguda
    case 0x40: return (char)0xA1;           // abre exclamacion
    case 0x5B: return (char)0xC4;           // A dieresis
    case 0x5C: return (char)0xD6;           // O dieresis
    case 0x5D: return (char)0xD1;           // enie mayuscula
    case 0x5E: return (char)0xDC;           // U dieresis
    case 0x60: return (char)0xBF;           // abre interrogacion
    case 0x7B: return (char)0xE4;           // a dieresis
    case 0x7C: return (char)0xF6;           // o dieresis
    case 0x7D: return (char)0xF1;           // enie minuscula
    case 0x7E: return (char)0xFC;           // u dieresis
    case 0x7F: return (char)0xE0;           // a grave
    default: return (c < 0x20 || c > 0x7F) ? ' ' : (char)c; // control -> espacio; el resto es ASCII
  }
}

char gsmEscapeALatin(uint8_t c) {           // caracteres que en GSM van precedidos de 0x1B
  switch (c) {                              // tabla de extension GSM
    case 0x28: return '{';                  // llave abre
    case 0x29: return '}';                  // llave cierra
    case 0x2F: return '\\';                 // barra invertida
    case 0x3C: return '[';                  // corchete abre
    case 0x3D: return '~';                  // virgulilla
    case 0x3E: return ']';                  // corchete cierra
    case 0x40: return '|';                  // barra vertical
    case 0x14: return '^';                  // circunflejo
    case 0x65: return 'E';                  // euro (no existe en Latin-1)
    default: return ' ';                    // desconocido
  }
}

uint8_t latinAGsm(uint8_t c) {              // Latin-1 (del ESP32) -> alfabeto GSM para enviar
  if (c == 0xF1) return 0x7D;               // enie minuscula
  if (c == 0xD1) return 0x5D;               // enie mayuscula
  if (c == 0x1A || c == 0x1B) return ' ';   // caracteres de control del SMS: prohibidos
  if (c >= 0x80) return '?';                // otro caracter sin equivalente
  return c;                                 // letras, numeros, espacio, # + -: iguales
}

void tlfAgregarCaracter(char c) {           // agrega un caracter al cuerpo del SMS recibido
  if (tlfSmsCuerpoLen >= TLF_LONG_SMS) return; // ya esta lleno
  tlfSmsCuerpo[tlfSmsCuerpoLen++] = c;      // lo guarda
  tlfSmsCuerpo[tlfSmsCuerpoLen] = 0;        // fin de texto
}

void tlfUcs2Cifra(char c) {                 // arma caracteres UCS2 a partir de cifras hex (SMS con acentos)
  uint8_t v;                                // valor de la cifra
  if (c >= '0' && c <= '9') v = c - '0';    // 0-9
  else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; // A-F
  else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; // a-f
  else return;                              // no es cifra hex: se ignora
  tlfUcs2Valor = (tlfUcs2Valor << 4) | v;   // agrega la cifra
  if (++tlfUcs2Nibbles < 4) return;         // faltan cifras
  uint16_t u = tlfUcs2Valor;                // caracter completo
  tlfUcs2Nibbles = 0;                       // empieza el siguiente
  tlfUcs2Valor = 0;                         // decodificador UCS2 limpio
  if (u == 0x0A || u == 0x0D) tlfAgregarCaracter(' '); // salto de linea -> espacio
  else if (u < 0x20) return;                // otros controles: se ignoran
  else if (u < 0x100) tlfAgregarCaracter((char)u); // Latin-1 directo (a, e, i, o, u con tilde, enie...)
  else if (u >= 0xD800 && u <= 0xDFFF) { if (u < 0xDC00) tlfAgregarCaracter('?'); } // emoji: un solo '?'
  else tlfAgregarCaracter('?');             // sin equivalente
}

// ============================ 7. ESTADO DE LA LLAMADA ============================
void tlfSetEstado(uint8_t e) {              // cambia el estado de la llamada
  tlfEstadoLlamada = e;                     // guarda el nuevo estado
  tlfTCambioEstado = millis();              // anota cuando cambio
}

void tlfFinalizarLlamada() {                // la llamada termino (por cualquier motivo)
  if (tlfEstadoLlamada == TLF_LIBRE) return; // ya estaba libre
  bool avisar = !(tlfEstadoLlamada == TLF_ENTRANTE && !tlfEntranteAvisado); // no avisa fin de una llamada que nunca se aviso
  tlfSetEstado(TLF_LIBRE);                  // vuelve a libre
  tlfTFinLlamada = millis();                // anota cuando termino
  tlfRespuestaEntrante = TLF_RESP_NINGUNA;  // descarta respuestas pendientes
  tlfClipRecibido = false;                  // limpia datos de llamada entrante
  tlfEntranteAvisado = false;               // limpia aviso de llamada entrante
  if (avisar) evSimple(F("FIN"));           // avisa al ESP32 (detiene su cronometro)
}

void tlfPasarEnCurso() {                    // la llamada se conecto
  if (tlfEstadoLlamada == TLF_EN_CURSO) return; // ya estaba en curso
  tlfSetEstado(TLF_EN_CURSO);               // estado: hablando
  tlfAudioPaso = 1;                         // reasegura microfono abierto y volumen fijo
  evSimple(F("EN_CURSO"));                  // avisa al ESP32 (arranca su cronometro en 00:00)
}

void tlfActualizarSenal(uint8_t nivel) {    // envia el nivel solo si cambio
  if (nivel == tlfNivelSenal) return;       // sin cambio: no satura al ESP32
  tlfNivelSenal = nivel;                    // guarda el nuevo nivel
  evSenal(nivel);                           // avisa al ESP32
}

void evEstadoCompleto() {                   // respuesta a "ESTADO" (el ESP32 se reinicio)
  if (tlfModemListo) evSimple(F("LISTO"));  // la SIM900 esta lista
  evSenal(tlfNivelSenal == 255 ? 0 : tlfNivelSenal); // senal actual
  if (tlfEstadoLlamada == TLF_MARCANDO) evSimple(F("LLAMANDO")); // estaba marcando
  if (tlfEstadoLlamada == TLF_EN_CURSO) evSimple(F("EN_CURSO")); // estaba hablando
  if (tlfEstadoLlamada == TLF_ENTRANTE && tlfEntranteAvisado) evEntrante(); // estaba sonando
}

// ============================ 8. AVISOS NO SOLICITADOS DE LA SIM900 ============================
void tlfAvisoRing() {                       // la SIM900 detecto una llamada entrante
  if (tlfEstadoLlamada != TLF_LIBRE) return; // ya hay llamada: se ignora
  if (millis() - tlfTFinLlamada < TLF_IGNORAR_RING_MS) return; // RING sobrante de una llamada ya cortada
  tlfSetEstado(TLF_ENTRANTE);               // estado: llamada entrante
  tlfNumeroEntrante[0] = 0;                 // aun no se conoce el numero
  tlfClipRecibido = false;                  // aun no llega el numero
  tlfEntranteAvisado = false;               // aun no se avisa al ESP32
  tlfRespuestaEntrante = TLF_RESP_NINGUNA;  // sin respuesta todavia
}

void tlfAvisoClip(char *l) {                // llego el numero de quien llama
  if (tlfEstadoLlamada == TLF_LIBRE) tlfAvisoRing(); // si el RING se perdio, arranca igual
  if (tlfEstadoLlamada != TLF_ENTRANTE) return; // solo importa si esta sonando
  tlfCampoComillas(l, 1, tlfNumeroEntrante, TLF_LONG_NUM); // guarda el numero
  tlfClipRecibido = true;                   // ya se tiene el numero
}

void tlfAvisoFinLlamada() {                 // NO CARRIER / BUSY / NO ANSWER / NO DIALTONE
  if (tlfAtTag == TAG_ATD || tlfAtTag == TAG_ATA) tlfAtTag = TAG_NADA; // era la respuesta final de ese comando
  tlfFinalizarLlamada();                    // cierra la llamada
}

void tlfAvisoSmsNuevo(char *l) {            // +CMTI: "SM",<indice>   o   +CMGL: <indice>,...
  char *c = strrchr(l, ',');                // +CMTI: el indice va tras la ultima coma
  if (TLF_EMPIEZA(l, "+CMGL:")) c = l + 5;  // +CMGL: el indice va justo despues de los dos puntos
  if (!c) return;                           // formato inesperado
  uint8_t idx = (uint8_t)atoi(c + 1);       // indice del SMS en la SIM
  if (idx == 0 || tlfColaCnt >= TLF_COLA_SMS) return; // invalido o cola llena
  for (uint8_t i = 0; i < tlfColaCnt; i++)  // evita repetir un indice ya en cola
    if (tlfColaSms[(tlfColaIni + i) % TLF_COLA_SMS] == idx) return; // ya estaba en cola: no se repite
  tlfColaSms[(tlfColaIni + tlfColaCnt) % TLF_COLA_SMS] = idx; // lo pone en la cola
  tlfColaCnt++;                             // un SMS mas por leer
}

void tlfAvisoReinicio() {                   // la SIM900 se reinicio o se apago
  tlfFinalizarLlamada();                    // una llamada en curso se perdio
  if (tlfSmsTxActivo || tlfAtTag == TAG_CMGS_PROMPT || tlfAtTag == TAG_CMGS_ENVIO) evSimple(F("SMS_ERROR")); // SMS a medias
  tlfSmsTxActivo = false;                   // deja de escribir texto
  tlfAtTag = TAG_NADA;                      // olvida el comando en curso
  tlfModemListo = false;                    // hay que configurarla otra vez
  tlfInitPaso = 0;                          // desde el primer paso
  tlfInitReintento = millis() + 1000UL;     // en 1 s
  tlfRegistrado = false;                    // hay que volver a registrarse
  tlfActualizarSenal(0);                    // sin modem = sin senal
}

// ============================ 9. RESPUESTAS A COMANDOS AT ============================
void tlfAtIniciar(uint8_t tag, unsigned long timeout) { // marca un comando como "esperando respuesta"
  tlfAtTag = tag;                           // que comando es
  tlfAtInicio = millis();                   // cuando se envio
  tlfAtTimeout = timeout;                   // cuanto se espera
}

void tlfResolverClcc() {                    // fin de la respuesta de AT+CLCC
  if (tlfEstadoLlamada == TLF_LIBRE) return; // la llamada ya termino por otro aviso
  bool sinLlamada = (!tlfClccVisto || tlfClccStat == 6); // el modulo no lista llamada o esta colgando
  if (sinLlamada) {                         // la llamada ya no existe
    if (millis() - tlfTCambioEstado >= TLF_GRACIA_LLAMADA_MS) tlfFinalizarLlamada(); // la da por terminada tras la gracia
  } else if (tlfClccStat == 0 && tlfEstadoLlamada != TLF_EN_CURSO) { // llamada activa (contestada)
    tlfPasarEnCurso();                      // pasa a llamada en curso
  }
}

void tlfCompletarAT(bool ok) {              // llego OK (true) o ERROR (false) del comando en curso
  uint8_t t = tlfAtTag;                     // comando que termina
  tlfAtTag = TAG_NADA;                      // libera el canal AT
  switch (t) {                              // accion segun el comando
    case TAG_INIT:                          // paso de inicializacion
      if (tlfInitPaso == 0) tlfFallosAT = 0; // la SIM900 respondio
      if (ok || tlfInitPaso >= TLF_PRIMER_PASO_OPCIONAL) { // funciono (o era opcional)
        tlfInitPaso++;                      // siguiente paso
        tlfInitReintento = millis();        // sin esperar
        if (tlfInitPaso > TLF_ULTIMO_PASO_INIT) { // termino la inicializacion
          tlfModemListo = true;             // SIM900 lista
          tlfAudioPaso = 1;                 // fija microfono y volumen
          tlfTUltCsq = millis() - TLF_PERIODO_CSQ_MS; // primera lectura de senal de inmediato
          evSimple(F("LISTO"));             // avisa al ESP32
        }
      } else {                              // el paso fallo (p. ej. SIM aun no lista)
        tlfInitReintento = millis() + 2000UL; // reintenta en 2 s
      }
      break;                                // fin del caso
    case TAG_ATD:                           // marcar
      if (!ok) tlfFinalizarLlamada();       // error al marcar: no hay llamada
      break;                                // fin del caso
    case TAG_ATA:                           // contestar
      if (ok) tlfPasarEnCurso();            // contesto: llamada en curso
      else tlfFinalizarLlamada();           // no se pudo contestar
      break;                                // fin del caso
    case TAG_ATH:                           // colgar / rechazar
      tlfFinalizarLlamada();                // llamada terminada
      break;                                // fin del caso
    case TAG_CLCC:                          // estado de llamada
      if (ok) tlfResolverClcc();            // decide segun lo que listo el modulo
      break;                                // fin del caso
    case TAG_AUDIO:                         // ajuste de audio
      tlfAudioPaso = (ok && tlfAudioPaso == 1) ? 2 : 0; // tras el microfono sigue el volumen; luego termina
      break;                                // fin del caso
    case TAG_CSQ:                           // senal
      if (!ok) tlfActualizarSenal(0);       // si falla se considera sin senal
      break;                                // fin del caso
    case TAG_CMGS_PROMPT:                   // OK/ERROR antes del '>' = no se pudo iniciar el SMS
      evSimple(F("SMS_ERROR"));             // avisa al ESP32
      break;                                // fin del caso
    case TAG_CMGS_ENVIO:                    // resultado final del envio
      tlfSmsTxActivo = false;               // deja de escribir texto
      if (ok) evSimple(F("SMS_OK"));        // avisa: enviado
      else evSimple(F("SMS_ERROR"));        // avisa: error
      break;                                // fin del caso
    case TAG_CMGR:                          // fin de la lectura de un SMS
      if (ok && tlfCmgrCuerpo) tlfSmsPorAvisar = true; // listo para mandarlo al ESP32
      tlfCmgrCuerpo = false;                // termina la lectura
      break;                                // fin del caso
    default:                                // TAG_CMGD u otros
      break;                                // nada que hacer
  }
}

void tlfLeerCabeceraSms(char *l) {          // +CMGR: "estado","remitente","","aa/mm/dd,hh:mm:ss+zz",tooa,fo,pid,dcs,...
  tlfCampoComillas(l, 2, tlfSmsRemitente, TLF_LONG_NUM); // campo 2 = remitente
  tlfCampoComillas(l, 4, tlfSmsHora, sizeof(tlfSmsHora)); // campo 4 = fecha y hora
  if (strlen(tlfSmsHora) >= 17) {           // formato esperado aa/mm/dd,hh:mm:ss
    tlfSmsHora[17] = 0;                     // quita la zona horaria
    tlfSmsHora[8] = ' ';                    // cambia la coma por espacio (las comas separan campos)
  }
  tlfSmsUcs2 = false;                       // por defecto texto GSM normal
  uint8_t q = 0;                            // comillas vistas
  char *p = l;                              // recorre la cabecera
  for (; *p && q < 8; p++) if (*p == '"') q++; // salta hasta cerrar el campo 4
  for (uint8_t k = 0; k < 4 && p; k++) {    // tras la fecha: ,tooa ,fo ,pid ,dcs
    p = strchr(p, ',');                     // siguiente coma
    if (p) p++;                             // salta la coma
  }
  if (p && *p >= '0' && *p <= '9') tlfSmsUcs2 = ((atoi(p) & 0x0C) == 0x08); // dcs: UCS2 = texto en hexadecimal
  tlfSmsCuerpo[0] = 0;                      // cuerpo vacio
  tlfSmsCuerpoLen = 0;                      // largo cero
  tlfUcs2Valor = 0;                         // decodificador UCS2 limpio
  tlfUcs2Nibbles = 0;                       // sin cifras hex pendientes
  tlfGsmEscape = false;                     // sin escape pendiente
  tlfCmgrCuerpo = true;                     // ahora vienen las lineas del cuerpo
}

void tlfAgregarCuerpo(const char *l) {      // anade una linea (alfabeto GSM) al cuerpo del SMS
  if (tlfSmsCuerpoLen > 0) tlfAgregarCaracter(' '); // une lineas con espacio
  for (; *l; l++) {                         // cada caracter
    uint8_t c = (uint8_t)*l;                // byte recibido
    if (tlfGsmEscape) { tlfAgregarCaracter(gsmEscapeALatin(c)); tlfGsmEscape = false; continue; } // caracter extendido
    if (c == 0x1B) { tlfGsmEscape = true; continue; } // escape: el significado lo da el siguiente
    tlfAgregarCaracter(gsmALatin(c));       // convierte a Latin-1
  }
}

void tlfProcesarLinea(char *l) {            // interpreta una linea completa de la SIM900
  if (tlfAtTag == TAG_CMGR && tlfCmgrCuerpo) { // leyendo cuerpo de SMS: tiene prioridad
    if (TLF_ES(l, "OK") && tlfLineaVaciaPrevia) tlfCompletarAT(true); // el OK final va tras una linea vacia
    else if (!tlfSmsUcs2) tlfAgregarCuerpo(l); // texto GSM (el UCS2 ya se decodifico al llegar)
    return;                                 // nada mas que hacer
  }
  if (TLF_ES(l, "RING")) { tlfAvisoRing(); return; } // llamada entrante
  if (TLF_EMPIEZA(l, "+CLIP:")) { tlfAvisoClip(l); return; } // numero de quien llama
  if (TLF_ES(l, "NO CARRIER") || TLF_ES(l, "BUSY") || TLF_ES(l, "NO ANSWER") || TLF_ES(l, "NO DIALTONE")) { tlfAvisoFinLlamada(); return; } // llamada terminada
  if (TLF_ES(l, "MO CONNECTED")) { if (tlfEstadoLlamada == TLF_MARCANDO) tlfPasarEnCurso(); return; } // contestaron (AT+MORING=1)
  if (TLF_ES(l, "MO RING")) return;         // el otro telefono esta sonando
  if (TLF_EMPIEZA(l, "+CMTI:") || TLF_EMPIEZA(l, "+CMGL:")) { tlfAvisoSmsNuevo(l); return; } // SMS por leer
  if (TLF_ES(l, "Call Ready")) { if (tlfModemListo) tlfAvisoReinicio(); return; } // modulo reiniciado
  if (strstr_P(l, PSTR("POWER DOWN"))) { tlfAvisoReinicio(); return; } // modulo apagado (bateria baja, etc.)
  if (tlfAtTag == TAG_NADA) return;         // no se espera respuesta: se ignora
  if (TLF_ES(l, "OK")) { tlfCompletarAT(true); return; } // comando exitoso
  if (TLF_ES(l, "ERROR") || TLF_EMPIEZA(l, "+CME ERROR") || TLF_EMPIEZA(l, "+CMS ERROR")) { tlfCompletarAT(false); return; } // comando fallido
  if (TLF_EMPIEZA(l, "+CSQ:")) {            // respuesta de senal
    tlfActualizarSenal(tlfRegistrado ? tlfCsqANivel(atoi(l + 5)) : 0); // 0-5 (0 si no hay red); envia si cambio
  } else if (TLF_EMPIEZA(l, "+CREG:")) {    // respuesta de registro: +CREG: <n>,<stat>
    char *p = strchr(l, ',');               // el estado va despues de la coma
    uint8_t stat = (uint8_t)atoi(p ? p + 1 : l + 6); // 1 = red local, 5 = roaming
    tlfRegistrado = (stat == 1 || stat == 5); // registrada?
  } else if (TLF_EMPIEZA(l, "+CLCC:")) {    // respuesta de estado de llamada
    char *p = strchr(l, ',');               // salta el indice
    if (p) p = strchr(p + 1, ',');          // salta la direccion
    if (p) {                                // tercer campo = estado
      tlfClccStat = (int8_t)atoi(p + 1);    // guarda el estado
      tlfClccVisto = true;                  // hay llamada listada
    }
  } else if (TLF_EMPIEZA(l, "+CMGR:")) {    // cabecera de SMS leido
    tlfLeerCabeceraSms(l);                  // extrae remitente, hora y codificacion
  }
}

// ============================ 10. LECTURA DE LA SIM900 (sin bloquear) ============================
void tlfLeerSIM() {                         // procesa solo lo que ya llego
  while (SIM.available() > 0) {             // mientras haya bytes
    char c = SIM.read();                    // lee un byte
    if (c == '\r') continue;                // ignora retorno de carro
    if (c == '\n') {                        // fin de linea
      if (tlfLineaLen > 0) {                // linea con contenido
        tlfLinea[tlfLineaLen] = 0;          // fin de texto
        tlfLineaLen = 0;                    // buffer libre para la siguiente
        tlfProcesarLinea(tlfLinea);         // la interpreta
        tlfLineaVaciaPrevia = false;        // ya no hay linea vacia previa
      } else {                              // linea vacia
        tlfLineaVaciaPrevia = true;         // la recuerda (separa el cuerpo del OK final)
      }
      continue;                             // siguiente byte
    }
    if (c == '>' && tlfLineaLen == 0 && tlfAtTag == TAG_CMGS_PROMPT) { // la SIM900 pide el texto del SMS
      tlfSmsTxIdx = 0;                      // empieza desde el primer caracter
      tlfSmsTxActivo = true;                // se escribe el texto poco a poco
      tlfAtIniciar(TAG_CMGS_ENVIO, 60000UL); // ahora espera la confirmacion de la red
      continue;                             // siguiente byte
    }
    if (tlfAtTag == TAG_CMGR && tlfCmgrCuerpo && tlfSmsUcs2) tlfUcs2Cifra(c); // UCS2: se decodifica al vuelo (puede pasar de 164)
    if (c == 0) c = (char)0x80;             // '@' en GSM es 0x00: se marca para no cortar el texto
    if (c == ' ' && tlfLineaLen == 0 && !(tlfAtTag == TAG_CMGR && tlfCmgrCuerpo)) continue; // ignora espacios al inicio (no en el SMS)
    if (tlfLineaLen < TLF_LONG_LINEA - 1) tlfLinea[tlfLineaLen++] = c; // guarda el caracter si cabe
  }
}

void tlfTransmitirTextoSms() {              // escribe el SMS a la SIM900 sin esperar
  if (!tlfSmsTxActivo) return;              // nada que escribir
  while (tlfSmsTxIdx < TLF_LONG_SMS && tlfTextoSMS[tlfSmsTxIdx] != 0 && SIM.availableForWrite() > 0) { // mientras quepa en el buffer
    SIM.write(latinAGsm((uint8_t)tlfTextoSMS[tlfSmsTxIdx++])); // siguiente caracter (convertido a GSM)
  }
  if ((tlfSmsTxIdx >= TLF_LONG_SMS || tlfTextoSMS[tlfSmsTxIdx] == 0) && SIM.availableForWrite() > 0) { // texto completo
    SIM.write((uint8_t)0x1A);               // Ctrl+Z: la SIM900 envia el SMS
    tlfSmsTxActivo = false;                 // termina la escritura
  }
}

void tlfVerificarTimeout() {                // da por perdido un comando que no responde
  if (tlfAtTag == TAG_NADA) return;         // no hay comando en espera
  if (millis() - tlfAtInicio < tlfAtTimeout) return; // aun hay tiempo
  uint8_t t = tlfAtTag;                     // comando que no respondio
  tlfAtTag = TAG_NADA;                      // libera el canal AT
  switch (t) {                              // accion segun el comando
    case TAG_INIT:                          // paso de inicializacion
      if (tlfInitPaso == 0) tlfFallosAT++;  // "AT" sin respuesta: quiza la SIM900 esta apagada
      if (tlfInitPaso >= TLF_PRIMER_PASO_OPCIONAL) { // paso opcional: se sigue con el siguiente
        tlfAtTag = TAG_INIT;                // tlfCompletarAT necesita saber que comando era
        tlfCompletarAT(false);              // lo da por fallido (y avanza)
      } else {
        tlfInitReintento = millis();        // obligatorio: reintenta de inmediato
      }
      break;                                // fin del caso
    case TAG_ATD:                           // marcar
    case TAG_ATA:                           // contestar
    case TAG_ATH:                           // colgar
      tlfOrdenColgar = true;                // estado incierto: fuerza colgar
      break;                                // fin del caso
    case TAG_CSQ:                           // senal
      tlfActualizarSenal(0);                // sin respuesta = sin senal
      break;                                // fin del caso
    case TAG_AUDIO:                         // audio
      tlfAudioPaso = 0;                     // se reintenta en la proxima llamada
      break;                                // fin del caso
    case TAG_CMGS_PROMPT:                   // nunca llego el '>'
    case TAG_CMGS_ENVIO:                    // la red no confirmo
      tlfSmsTxActivo = false;               // deja de escribir texto
      SIM.write((uint8_t)0x1B);             // ESC: cancela el SMS en la SIM900
      evSimple(F("SMS_ERROR"));             // avisa al ESP32
      break;                                // fin del caso
    case TAG_CMGR:                          // lectura de SMS
      tlfCmgrCuerpo = false;                // descarta la lectura
      break;                                // fin del caso
    default:                                // otros comandos
      break;                                // nada que hacer
  }
}

// ============================ 11. PLANIFICADOR DE COMANDOS AT (uno a la vez) ============================
void tlfEnviarPasoInit() {                  // envia el paso actual de la inicializacion
  switch (tlfInitPaso) {                    // segun el paso
    case 0: SIM.println(F("AT")); break;    // sincroniza velocidad / comprueba que responde
    case 1: SIM.println(F("ATE0")); break;  // desactiva el eco
    case 2: SIM.println(F("AT+CMGF=1")); break; // SMS en modo texto
    case 3: SIM.println(F("AT+CSCS=\"GSM\"")); break; // juego de caracteres GSM (para la enie)
    case 4: SIM.println(F("AT+CNMI=2,1,0,0,0")); break; // avisa +CMTI cuando llega un SMS
    case 5: SIM.println(F("AT+CLIP=1")); break; // muestra el numero de quien llama
    case 6: SIM.println(F("AT+CSDH=1")); break; // cabecera completa del SMS (para saber si viene en UCS2)
    case 7: SIM.println(F("AT+MORING=1")); break; // avisa "MO CONNECTED" al contestar una llamada nuestra
    case 8: SIM.print(F("AT+CHFA=")); SIM.println(TLF_CANAL_AUDIO); break; // canal de audio (SPK/MIC principal)
    case 9: SIM.print(F("AT+CMIC=")); SIM.print(TLF_CANAL_AUDIO); SIM.print(','); SIM.println(TLF_GANANCIA_MIC); break; // ganancia del microfono
    default: SIM.println(F("AT+CMGL=\"ALL\"")); break; // SMS que llegaron con el celular apagado
  }
  tlfAtIniciar(TAG_INIT, tlfInitPaso == 0 ? 1000UL : (tlfInitPaso == TLF_ULTIMO_PASO_INIT ? 10000UL : 3000UL)); // espera su respuesta
}

bool tlfTareaEncendido() {                  // enciende la SIM900 con D9 si no responde (true = ocupado)
#if USAR_PIN_ENCENDIDO                      // solo si se usa el pin D9
  unsigned long ahora = millis();           // instante actual
  if (tlfPulso == 1) {                      // pulso en curso
    if (ahora - tlfTPulso < 1200UL) return true; // mantiene D9 en ALTO 1.2 s
    digitalWrite(PIN_ENCENDIDO_SIM, LOW);   // suelta el "boton"
    tlfPulso = 2;                           // espera a que arranque
    tlfTPulso = ahora;                      // desde ahora
    return true;                            // ocupado
  }
  if (tlfPulso == 2) {                      // esperando el arranque
    if (ahora - tlfTPulso < 6000UL) return true; // la SIM900 tarda unos segundos
    tlfPulso = 0;                           // listo para volver a probar "AT"
    tlfFallosAT = 0;                        // reinicia el conteo
    return false;                           // sigue la inicializacion
  }
  if (tlfFallosAT >= 4) {                   // 4 "AT" sin respuesta: probablemente apagada
    digitalWrite(PIN_ENCENDIDO_SIM, HIGH);  // "presiona" PWRKEY
    tlfPulso = 1;                           // pulso en curso
    tlfTPulso = ahora;                      // desde ahora
    return true;                            // ocupado
  }
#endif
  return false;                             // nada que hacer
}

void tlfPlanificarAT() {                    // decide cual es el proximo comando AT
  if (tlfAtTag != TAG_NADA) return;         // hay un comando en curso: espera
  unsigned long ahora = millis();           // instante actual
  if (!tlfModemListo) {                     // aun falta inicializar
    if (tlfTareaEncendido()) return;        // encendiendo la SIM900
    if ((long)(ahora - tlfInitReintento) >= 0) tlfEnviarPasoInit(); // envia el paso si ya toca
    return;                                 // nada mas hasta estar lista
  }
  if (tlfOrdenColgar) {                     // orden de colgar
    tlfOrdenColgar = false;                 // consume la orden
    if (tlfEstadoLlamada != TLF_LIBRE) {    // solo si hay llamada
      SIM.println(F("ATH"));                // corta la llamada
      tlfAtIniciar(TAG_ATH, 5000UL);        // espera respuesta
      return;                               // un comando por vez
    }
  }
  if (tlfRespuestaEntrante != TLF_RESP_NINGUNA) { // hay respuesta a llamada entrante
    if (tlfEstadoLlamada != TLF_ENTRANTE) {  // ya no esta sonando
      tlfRespuestaEntrante = TLF_RESP_NINGUNA; // descarta la respuesta
    } else if (tlfEntranteAvisado) {        // ya se aviso al ESP32, la respuesta es valida
      uint8_t r = tlfRespuestaEntrante;     // copia la respuesta
      tlfRespuestaEntrante = TLF_RESP_NINGUNA; // consume la respuesta
      if (r == TLF_RESP_CONTESTAR) {        // aprobada
        SIM.println(F("ATA"));              // contesta la llamada
        tlfAtIniciar(TAG_ATA, 10000UL);     // espera respuesta
      } else {                              // rechazada
        SIM.println(F("ATH"));              // corta la llamada entrante
        tlfAtIniciar(TAG_ATH, 5000UL);      // espera respuesta
      }
      return;                               // un comando por vez
    }
  }
  if (tlfAudioPaso == 1) {                  // ajuste de audio: microfono
    SIM.println(F("AT+CMUT=0"));            // microfono siempre abierto (sin silencio)
    tlfAtIniciar(TAG_AUDIO, 3000UL);        // espera respuesta
    return;                                 // un comando por vez
  }
  if (tlfAudioPaso == 2) {                  // ajuste de audio: volumen
    SIM.print(F("AT+CLVL="));               // volumen de la bocina
    SIM.println(TLF_VOLUMEN_BOCINA);        // nivel fijo (80 %)
    tlfAtIniciar(TAG_AUDIO, 3000UL);        // espera respuesta
    return;                                 // un comando por vez
  }
  if (tlfOrdenLlamar) {                     // orden de llamar
    tlfOrdenLlamar = false;                 // consume la orden
    if (tlfEstadoLlamada == TLF_LIBRE && tlfNumeroValido(tlfNumeroLlamada)) { // libre y numero valido
      SIM.print(F("ATD"));                  // marcar
      SIM.print(tlfNumeroLlamada);          // numero
      SIM.println(';');                     // ';' = llamada de voz
      tlfSetEstado(TLF_MARCANDO);           // estado: llamando
      tlfAtIniciar(TAG_ATD, 15000UL);       // espera respuesta
      evSimple(F("LLAMANDO"));              // avisa al ESP32
      return;                               // un comando por vez
    } else if (tlfEstadoLlamada == TLF_LIBRE) { // numero invalido
      evSimple(F("FIN"));                   // avisa que no hay llamada
    }
  }
  if (tlfEstadoLlamada != TLF_LIBRE && ahora - tlfTUltClcc >= TLF_PERIODO_CLCC_MS) { // toca revisar la llamada
    tlfTUltClcc = ahora;                    // anota la consulta
    tlfClccVisto = false;                   // aun no se lista llamada
    tlfClccStat = -1;                       // estado desconocido
    SIM.println(F("AT+CLCC"));              // lista llamadas actuales
    tlfAtIniciar(TAG_CLCC, 3000UL);         // espera respuesta
    return;                                 // un comando por vez
  }
  if (tlfOrdenEnviarSMS) {                  // orden de enviar SMS
    tlfOrdenEnviarSMS = false;              // consume la orden
    if (tlfNumeroValido(tlfNumeroSMS) && tlfTextoSMS[0] != 0) { // hay destino y texto
      SIM.print(F("AT+CMGS=\""));           // inicia el envio
      SIM.print(tlfNumeroSMS);              // numero destino
      SIM.println('"');                     // cierra comillas
      tlfAtIniciar(TAG_CMGS_PROMPT, 5000UL); // espera el '>'
      return;                               // un comando por vez
    } else {                                // faltan datos
      evSimple(F("SMS_ERROR"));             // avisa al ESP32
    }
  }
  if (tlfSmsBorrar != 0) {                  // hay un SMS ya entregado por borrar de la SIM
    SIM.print(F("AT+CMGD="));               // borra de la SIM
    SIM.println(tlfSmsBorrar);              // indice a borrar
    tlfSmsBorrar = 0;                       // ya no queda pendiente
    tlfAtIniciar(TAG_CMGD, 5000UL);         // espera respuesta
    return;                                 // un comando por vez
  }
  if (tlfColaCnt > 0 && !tlfSmsPorAvisar) { // hay SMS nuevos por leer (y el anterior ya salio)
    tlfSmsIdx = tlfColaSms[tlfColaIni];     // toma el mas antiguo
    tlfColaIni = (tlfColaIni + 1) % TLF_COLA_SMS; // avanza la cola
    tlfColaCnt--;                           // uno menos en espera
    tlfCmgrCuerpo = false;                  // aun no llega la cabecera
    SIM.print(F("AT+CMGR="));               // lee el SMS
    SIM.println(tlfSmsIdx);                 // indice a leer
    tlfAtIniciar(TAG_CMGR, 5000UL);         // espera respuesta
    return;                                 // un comando por vez
  }
  if (tlfCsqPendiente) {                    // ya se reviso el registro: ahora la senal
    tlfCsqPendiente = false;                // consume el pendiente
    SIM.println(F("AT+CSQ"));               // pide la calidad de senal
    tlfAtIniciar(TAG_CSQ, 3000UL);          // espera respuesta
    return;                                 // un comando por vez
  }
  if (ahora - tlfTUltCsq >= TLF_PERIODO_CSQ_MS) { // toca medir la senal
    tlfTUltCsq = ahora;                     // anota la consulta
    SIM.println(F("AT+CREG?"));             // primero: esta registrada en la red? (la SIM900 es solo 2G)
    tlfAtIniciar(TAG_CREG, 3000UL);         // espera respuesta
    tlfCsqPendiente = true;                 // despues se mide la senal
  }
}

// ============================ 12. TAREAS PERIODICAS HACIA EL ESP32 ============================
void tlfTareaEntrante() {                   // avisa la llamada entrante una sola vez
  if (tlfEstadoLlamada != TLF_ENTRANTE || tlfEntranteAvisado) return; // nada que avisar
  if (!tlfClipRecibido && millis() - tlfTCambioEstado < TLF_ESPERA_CLIP_MS) return; // espera un poco el numero
  tlfEntranteAvisado = true;                // ya avisado
  evEntrante();                             // avisa al ESP32 (espera contestar o rechazar)
}

void tlfTareaSmsRecibido() {                // manda al ESP32 el SMS leido (cuando cabe en la cola)
  if (!tlfSmsPorAvisar) return;             // nada pendiente
  if (!evSmsRecibido()) return;             // aun no cabe: se reintenta
  tlfSmsPorAvisar = false;                  // ya salio
  tlfSmsBorrar = tlfSmsIdx;                 // ahora si se borra de la SIM
}

void tareaLed() {                           // LED D13: parpadea = configurando, fijo = lista
  digitalWrite(PIN_LED, tlfModemListo ? HIGH : ((millis() / 250) & 1)); // estado visible
}

// ============================ 13. ORDENES QUE LLEGAN DEL ESP32 ============================
void ejecutarOrden(char *orden) {           // "LLAMAR,<num>" "COLGAR" "CONTESTAR" "RECHAZAR" "SMS,<num>,<texto>" "ESTADO"
  char *datos = strchr(orden, ',');         // separa nombre y datos
  if (datos) *datos++ = 0;                  // "NOMBRE\0datos"
  if (!tlfModemListo && TLF_ES(orden, "LLAMAR")) { evSimple(F("FIN")); return; } // SIM900 aun no lista: no se marca
  if (!tlfModemListo && TLF_ES(orden, "SMS")) { evSimple(F("SMS_ERROR")); return; } // ni se envia
  if (TLF_ES(orden, "LLAMAR") && datos) {   // marcar un numero
    strncpy(tlfNumeroLlamada, datos, TLF_LONG_NUM - 1); // guarda el numero
    tlfNumeroLlamada[TLF_LONG_NUM - 1] = 0; // fin de texto
    tlfOrdenLlamar = true;                  // el planificador marcara
  } else if (TLF_ES(orden, "COLGAR")) {     // cortar
    tlfOrdenColgar = true;                  // el planificador colgara
  } else if (TLF_ES(orden, "CONTESTAR")) {  // aceptar la llamada entrante
    tlfRespuestaEntrante = TLF_RESP_CONTESTAR; // ATA
  } else if (TLF_ES(orden, "RECHAZAR")) {   // rechazar la llamada entrante
    tlfRespuestaEntrante = TLF_RESP_RECHAZAR; // ATH
  } else if (TLF_ES(orden, "SMS") && datos) { // enviar SMS
    char *texto = strchr(datos, ',');       // separa numero y texto
    if (!texto || tlfSmsTxActivo || tlfAtTag == TAG_CMGS_PROMPT || tlfAtTag == TAG_CMGS_ENVIO) { evSimple(F("SMS_ERROR")); return; } // mal formado u ocupado
    *texto++ = 0;                           // "numero\0texto"
    strncpy(tlfNumeroSMS, datos, TLF_LONG_NUM - 1); // guarda el destino
    tlfNumeroSMS[TLF_LONG_NUM - 1] = 0;     // fin de texto
    strncpy(tlfTextoSMS, texto, TLF_LONG_SMS); // guarda el texto
    tlfTextoSMS[TLF_LONG_SMS] = 0;          // fin de texto
    tlfOrdenEnviarSMS = true;               // el planificador lo enviara
  } else if (TLF_ES(orden, "ESTADO")) {     // el ESP32 pide todo el estado
    evEstadoCompleto();                     // responde
  }
}

void procesarLineaEsp() {                   // valida una linea "<id>,<orden>*HH"
  if (espLen < 5 || espLinea[espLen - 3] != '*') return; // sin checksum: se descarta
  espLinea[espLen - 3] = 0;                 // separa contenido y checksum
  uint8_t esperado = (uint8_t)strtoul(&espLinea[espLen - 2], NULL, 16); // checksum recibido
  uint8_t x = 0;                            // checksum calculado
  for (char *p = espLinea; *p; p++) x ^= (uint8_t)*p; // XOR de todo el contenido
  if (x != esperado) return;                // linea danada: el ESP32 la reenviara
  char *orden = strchr(espLinea, ',');      // separa id y orden
  if (!orden) return;                       // formato inesperado
  *orden++ = 0;                             // "id\0orden"
  uint8_t id = (uint8_t)atoi(espLinea);     // numero de orden
  if (id != espUltimoId || TLF_ES(orden, "ESTADO")) { // orden nueva (si se repite, solo se confirma; ESTADO siempre)
    espUltimoId = id;                       // la recuerda
    ejecutarOrden(orden);                   // la ejecuta
  }
  evAck(id);                                // confirma al ESP32
}

void leerEsp() {                            // junta bytes del ESP32 en lineas (no bloquea)
  if (espLen > 0 && !esp.available() && millis() - espTUltByte > 100UL) { // linea cortada a la mitad
    espLen = 0;                             // se descarta (el ESP32 la reenviara)
    espDesborde = false;                    // sin desborde
  }
  while (esp.available() > 0) {             // solo lo que ya llego
    char c = esp.read();                    // un byte
    espTUltByte = millis();                 // el ESP32 esta transmitiendo
    if (c == '\r') continue;                // ignora retorno de carro
    if (c == '\n') {                        // fin de linea
      espLinea[espLen] = 0;                 // fin de texto
      if (!espDesborde) procesarLineaEsp(); // la procesa si estaba completa
      espLen = 0;                           // buffer listo
      espDesborde = false;                  // sin desborde
      continue;                             // siguiente byte
    }
    if (espLen < ESP_LONG_LINEA - 1) espLinea[espLen++] = c; // guarda si cabe
    else espDesborde = true;                // demasiado larga: se descartara
  }
}

// ============================ 14. SETUP Y LOOP ============================
void setup() {                              // se ejecuta una vez
  pinMode(PIN_LED, OUTPUT);                 // LED de estado
#if USAR_PIN_ENCENDIDO                      // solo si se usa el pin D9
  pinMode(PIN_ENCENDIDO_SIM, OUTPUT);       // PWRKEY de la SIM900
  digitalWrite(PIN_ENCENDIDO_SIM, LOW);     // en reposo
#endif
  SIM.begin(BAUD_SIM);                      // UART por hardware hacia la SIM900
  esp.begin(BAUD_ESP);                      // UART por software hacia el ESP32
  tlfInitReintento = millis() + 500UL;      // empieza a configurar la SIM900 en 0.5 s
}

void loop() {                               // se repite siempre (nada aqui espera)
  leerEsp();                                // 1. ordenes del ESP32
  tlfLeerSIM();                             // 2. lo que mando la SIM900
  tlfVerificarTimeout();                    // 3. comandos AT sin respuesta
  tlfTransmitirTextoSms();                  // 4. texto del SMS saliente, poco a poco
  tlfPlanificarAT();                        // 5. siguiente comando AT si el canal esta libre
  tlfTareaEntrante();                       // 6. aviso de llamada entrante
  tlfTareaSmsRecibido();                    // 7. SMS recibido hacia el ESP32
  txAtender();                              // 8. bytes pendientes hacia el ESP32
  tareaLed();                               // 9. LED de estado
}
