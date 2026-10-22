/*
 * =============================================================================
 *  PRUEBA DE LLAMADA AUTOMATICA - Arduino Uno + GPRS Shield SIM900
 * =============================================================================
 *  A los 4 segundos de encender, revisa paso a paso la SIM900 y la tarjeta SIM
 *  y, si todo esta bien, llama a +503 7624-1413.
 *  El LED integrado (D13) indica el resultado:
 *
 *    LED ENCENDIDO FIJO ..... la llamada salio (el otro telefono debe sonar)
 *    LED APAGADO ............ la llamada termino (despues de haber salido)
 *    2 parpadeos + pausa .... la SIM900 no responde (apagada, D0/D1, jumpers,
 *                             alimentacion)
 *    3 parpadeos + pausa .... la tarjeta SIM pide PIN (quitarlo en otro celular)
 *    4 parpadeos + pausa .... no se puede leer la tarjeta SIM (mal colocada,
 *                             sucia, al reves o dañada)
 *    5 parpadeos + pausa .... no hay senal / no se registra en la red en 60 s
 *                             (antena, cobertura 2G, alimentacion)
 *    6 parpadeos + pausa .... la red rechazo el registro de la SIM (linea
 *                             suspendida o sin servicio)
 *    7 parpadeos + pausa .... la red no dejo hacer la llamada (ERROR, NO
 *                             CARRIER, NO DIALTONE, BUSY al marcar; saldo, etc.)
 *    Parpadeo rapido continuo: esperando (los primeros 4 s y mientras revisa)
 *
 *  CONEXIONES (las mismas del proyecto):
 *    D0 (RX) <- TX de la SIM900 | jumpers de puerto del shield en D0/D1 (HW)
 *    D1 (TX) -> RX de la SIM900 |
 *    D9      -> PWRKEY (encendido). Solo funciona si los pads "PWR" del shield
 *               estan soldados; si no, encender con el boton PWRKEY (2 s).
 *    5V      -> 5V del shield (interruptor de alimentacion del shield en la
 *               posicion "Arduino"). OJO: la SIM900 pide picos de 2 A; si el
 *               Arduino va por USB puede reiniciarse al registrarse (error 5).
 *    GND     -> GND del shield.
 *
 *  AL SUBIR EL PROGRAMA: quitar los jumpers de puerto del shield (D0/D1 son
 *  los mismos pines del USB) y volverlos a poner despues; luego presionar
 *  RESET del Arduino.
 * =============================================================================
 */

#define NUMERO "+50376241413"               // numero a llamar (+503 7624-1413)
#define PIN_LED 13                          // LED integrado del Arduino
#define PIN_ENCENDIDO 9                     // D9 -> PWRKEY de la SIM900
#define ESPERA_INICIAL_MS 4000UL            // 4 s despues de encender
#define ESPERA_RED_MS 60000UL               // tiempo maximo para registrarse en la red

#define ERR_NO_RESPONDE 2                   // codigos de error = numero de parpadeos
#define ERR_SIM_PIN 3
#define ERR_SIM_NO_LEIDA 4
#define ERR_SIN_RED 5
#define ERR_RED_RECHAZA 6
#define ERR_LLAMADA 7

char resp[128];                             // ultima respuesta de la SIM900
uint8_t respLen = 0;                        // largo de la respuesta

// ---------------------------------------------------------------- LED
void parpadeoRapido(unsigned long ms) {     // parpadeo rapido de "esperando" durante ms
  unsigned long t0 = millis();              // inicio
  while (millis() - t0 < ms) {              // durante el tiempo pedido
    digitalWrite(PIN_LED, (millis() / 100) & 1); // cambia cada 100 ms
  }
  digitalWrite(PIN_LED, LOW);               // termina apagado
}

void error(uint8_t n) {                     // repite para siempre: n parpadeos + pausa
  while (true) {                            // nunca sale (hay que reiniciar)
    for (uint8_t i = 0; i < n; i++) {       // n parpadeos
      digitalWrite(PIN_LED, HIGH);          // enciende
      delay(300);                           // 0.3 s
      digitalWrite(PIN_LED, LOW);           // apaga
      delay(300);                           // 0.3 s
    }
    delay(2000);                            // pausa de 2 s para poder contar
  }
}

// ---------------------------------------------------------------- comandos AT
void limpiarEntrada() {                     // descarta lo que haya llegado antes
  while (Serial.available()) Serial.read(); // vacia el buffer
}

// Espera la respuesta hasta ver uno de los textos finales o hasta timeout.
// Devuelve true si llego algo final (OK, ERROR, etc.); el texto queda en resp.
bool leerRespuesta(unsigned long timeout) {
  respLen = 0;                              // respuesta vacia
  resp[0] = 0;
  unsigned long t0 = millis();              // inicio de la espera
  while (millis() - t0 < timeout) {         // mientras haya tiempo
    while (Serial.available()) {            // lee lo que llego
      char c = Serial.read();               // un byte
      if (respLen < sizeof(resp) - 1) { resp[respLen++] = c; resp[respLen] = 0; } // lo guarda
    }
    if (strstr(resp, "OK\r") || strstr(resp, "ERROR") || strstr(resp, "NO CARRIER") ||
        strstr(resp, "NO DIALTONE") || strstr(resp, "BUSY") || strstr(resp, "NO ANSWER")) {
      delay(20);                            // deja llegar el resto de la linea
      while (Serial.available() && respLen < sizeof(resp) - 1) { resp[respLen++] = Serial.read(); resp[respLen] = 0; }
      return true;                          // respuesta completa
    }
  }
  return false;                             // no respondio a tiempo
}

bool comando(const char *cmd, unsigned long timeout) { // envia un comando y espera respuesta
  limpiarEntrada();                         // sin restos de antes
  Serial.print(cmd);                        // comando
  Serial.print("\r\n");                     // fin de linea
  return leerRespuesta(timeout);            // espera la respuesta
}

bool ok() { return strstr(resp, "OK") != NULL && strstr(resp, "ERROR") == NULL; } // la respuesta fue OK

// ---------------------------------------------------------------- pasos de la prueba
bool simResponde() {                        // la SIM900 contesta "OK" a "AT"?
  for (uint8_t i = 0; i < 5; i++) {         // hasta 5 intentos (tambien ajusta la velocidad)
    if (comando("AT", 1000) && ok()) return true; // respondio
  }
  return false;                             // no respondio
}

void encenderConD9() {                      // "presiona" PWRKEY 1.2 s
  digitalWrite(PIN_ENCENDIDO, HIGH);        // presiona
  parpadeoRapido(1200);                     // 1.2 s
  digitalWrite(PIN_ENCENDIDO, LOW);         // suelta
  parpadeoRapido(8000);                     // la SIM900 tarda en arrancar
}

void revisarModulo() {                      // paso 1: que la SIM900 este encendida y conteste
  if (simResponde()) return;                // ya estaba encendida
  encenderConD9();                          // intenta encenderla
  if (simResponde()) return;                // ahora si contesta
  error(ERR_NO_RESPONDE);                   // no hay comunicacion
}

void revisarSim() {                         // paso 2: que la tarjeta SIM se pueda leer
  for (uint8_t i = 0; i < 10; i++) {        // hasta ~10 s (la SIM tarda en leerse al arrancar)
    comando("AT+CPIN?", 3000);              // estado de la SIM
    if (strstr(resp, "READY")) return;      // SIM lista
    if (strstr(resp, "SIM PIN") || strstr(resp, "SIM PUK")) error(ERR_SIM_PIN); // pide PIN o PUK
    parpadeoRapido(1000);                   // espera 1 s y reintenta
  }
  error(ERR_SIM_NO_LEIDA);                  // nunca estuvo lista: no se lee la SIM
}

void revisarRed() {                         // paso 3: senal y registro en la red
  unsigned long t0 = millis();              // inicio
  while (millis() - t0 < ESPERA_RED_MS) {   // hasta 60 s
    comando("AT+CREG?", 3000);              // +CREG: <n>,<estado>
    char *p = strstr(resp, "+CREG:");       // busca la respuesta
    if (p) p = strchr(p, ',');              // el estado va despues de la coma
    if (p) {
      int estado = atoi(p + 1);             // 1 = red local, 5 = roaming, 2 = buscando, 3 = rechazada
      if (estado == 1 || estado == 5) return; // registrada: se puede llamar
      if (estado == 3) error(ERR_RED_RECHAZA); // la red no acepta la SIM
    }
    parpadeoRapido(2000);                   // espera 2 s y vuelve a preguntar
  }
  error(ERR_SIN_RED);                       // no se registro a tiempo
}

void llamar() {                             // paso 4: marcar
  comando("AT+CLVL=80", 2000);              // volumen de la bocina 80 % (si falla no importa)
  comando("AT+CMUT=0", 2000);               // microfono abierto
  limpiarEntrada();                         // sin restos
  Serial.print("ATD");                      // marcar...
  Serial.print(NUMERO);                     // ...este numero...
  Serial.print(";\r\n");                    // ...como llamada de voz
  if (!leerRespuesta(20000) || !ok()) error(ERR_LLAMADA); // ERROR, NO CARRIER, NO DIALTONE, BUSY o sin respuesta
  digitalWrite(PIN_LED, HIGH);              // la llamada salio: LED fijo
}

// ---------------------------------------------------------------- programa
void setup() {
  pinMode(PIN_LED, OUTPUT);                 // LED integrado
  pinMode(PIN_ENCENDIDO, OUTPUT);           // PWRKEY
  digitalWrite(PIN_ENCENDIDO, LOW);         // sin presionar
  Serial.begin(9600);                       // UART con la SIM900 (D0/D1)
  parpadeoRapido(ESPERA_INICIAL_MS);        // espera 4 s desde el encendido

  revisarModulo();                          // 1. la SIM900 responde?
  comando("ATE0", 1000);                    // sin eco (respuestas mas limpias)
  revisarSim();                             // 2. la tarjeta SIM se lee?
  revisarRed();                             // 3. hay red?
  llamar();                                 // 4. llama a +503 7624-1413
}

void loop() {                               // durante la llamada
  if (Serial.available()) {                 // llego algo de la SIM900
    if (leerRespuesta(500) && (strstr(resp, "NO CARRIER") || strstr(resp, "BUSY") || strstr(resp, "NO ANSWER"))) {
      digitalWrite(PIN_LED, LOW);           // la llamada termino: LED apagado
    }
  }
}
