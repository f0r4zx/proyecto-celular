// =============================================================================
//  SIMULADOR EN PC DEL ESP32 (solo para pruebas; no se sube a ninguna placa)
//  - Compila el sketch real (esp32_interfaz.ino) con la libreria C real de U8g2.
//  - Emula el teclado matricial a nivel de pines (filas/columnas).
//  - Decodifica los bytes SPI que recibiria la ST7920 y reconstruye su memoria
//    grafica (GDRAM): asi se comprueba que el envio parcial deja la LCD igual
//    al buffer dibujado.
//  - Lee un guion de pruebas (ver casos.txt) y guarda capturas .pbm.
// =============================================================================
#include <Arduino.h>
#include "u8g2.h"
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

unsigned long sim_ms = 0;
SimSerial Serial;
SimSerial2 Serial2;
SimSPI SPI;
extern void setup();
extern void loop();
extern class U8G2_ST7920_128X64_F_HW_SPI u8g2;
extern "C" uint8_t *sim_buffer();

// ---------------- teclado ----------------
static const int FILAS[4] = {34, 35, 36, 39};
static const int COLS[6] = {13, 14, 27, 26, 25, 33};
static const char MAPA[4][6] = {{'1','2','3','E','D','E'},{'4','5','6','L','K','C'},{'7','8','9','A','U','X'},{'M','0','#','E','E','E'}};
static int modo[64], valor[64];
static char teclaAbajo = 0;
void pinMode(uint8_t p, uint8_t m) { modo[p] = m; }
void digitalWrite(uint8_t p, uint8_t v) { valor[p] = v; }
int digitalRead(uint8_t p) {
  for (int f = 0; f < 4; f++) {
    if (FILAS[f] != p) continue;
    for (int c = 0; c < 6; c++)
      if (MAPA[f][c] == teclaAbajo && modo[COLS[c]] == OUTPUT && valor[COLS[c]] == LOW) return LOW;
    return HIGH;                                   // pull-up de 10k
  }
  return HIGH;
}

// ---------------- ST7920 virtual ----------------
static uint8_t gdram[32][32];
static int cmdData = 0, nib = 0, alto = 0, ay = 0, ax = 0, byteEnPalabra = 0, addrFase = 0;
static long bytesSpi = 0;
static void st7920_valor(uint8_t v) {
  if (cmdData == 0) {                              // comando
    if ((v & 0xE0) == 0x20) { addrFase = 0; return; } // function set
    if (v & 0x80) {                                // direccion GDRAM (modo extendido)
      if (addrFase == 0) { ay = v & 0x3F; addrFase = 1; }
      else { ax = v & 0x0F; addrFase = 0; byteEnPalabra = 0; }
    }
    return;
  }
  int col = ax * 2 + byteEnPalabra;                // dato
  if (ay < 32 && col < 32) gdram[ay][col] = v;
  if (++byteEnPalabra == 2) { byteEnPalabra = 0; ax++; }
}
extern "C" uint8_t sim_byte_cb(u8x8_t *, uint8_t msg, uint8_t n, void *p) {
  if (msg != U8X8_MSG_BYTE_SEND) return 1;
  uint8_t *b = (uint8_t *)p;
  for (int i = 0; i < n; i++) {
    uint8_t v = b[i];
    bytesSpi++;
    if (v & 0x0F) { cmdData = (v == 0xFA); nib = 0; continue; } // byte de sincronia
    if (nib == 0) { alto = v & 0xF0; nib = 1; }
    else { st7920_valor(alto | (v >> 4)); nib = 0; }
  }
  return 1;
}
extern "C" uint8_t sim_gpio_cb(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
static int pixelLcd(int x, int y) {
  int fila = y < 32 ? y : y - 32;
  int col = (y < 32 ? 0 : 16) + x / 8;
  return (gdram[fila][col] >> (7 - (x % 8))) & 1;
}

// ---------------- enlace UART simulado (hace de Arduino) ----------------
static bool autoAck = true;
static std::vector<std::string> enviados;          // ordenes recibidas del ESP32
static uint8_t chk(const std::string &s) { uint8_t x = 0; for (char c : s) x ^= (uint8_t)c; return x; }
static void haciaEsp(const std::string &s) {
  char t[8]; snprintf(t, sizeof t, "*%02X\n", chk(s));
  for (char c : s + t) Serial2.rx.push_back((uint8_t)c);
}
static void revisarTx() {
  size_t pos;
  while ((pos = Serial2.tx.find('\n')) != std::string::npos) {
    std::string l = Serial2.tx.substr(0, pos);
    Serial2.tx.erase(0, pos + 1);
    std::string cuerpo = l.substr(0, l.size() - 3);
    unsigned v = strtoul(l.c_str() + l.size() - 2, NULL, 16);
    if (chk(cuerpo) != v) { printf("  !! checksum malo en orden: %s\n", l.c_str()); continue; }
    printf("  [t=%6lu] ESP32->UNO: %s\n", sim_ms, cuerpo.c_str());
    enviados.push_back(cuerpo);
    if (autoAck) haciaEsp("ACK," + cuerpo.substr(0, cuerpo.find(',')));
  }
}

static std::string latin1(const std::string &u) {  // el guion esta en UTF-8; el enlace usa Latin-1
  std::string r;
  for (size_t i = 0; i < u.size(); i++) {
    uint8_t c = u[i];
    if (c >= 0xC0 && i + 1 < u.size()) { r += (char)(((c & 0x1F) << 6) | (u[++i] & 0x3F)); }
    else r += (char)c;
  }
  return r;
}
static int fallos = 0;
static void correr(unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) { loop(); revisarTx(); sim_ms++; }
}
static void captura(const std::string &nombre) {
  correr(60);                                      // deja que termine de dibujar
  uint8_t *buf = sim_buffer();
  int dif = 0;
  for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) {
    int fb = (buf[y * 16 + x / 8] >> (7 - x % 8)) & 1; // buffer horizontal: bit 7 = izquierda
    if (fb != pixelLcd(x, y)) dif++;
  }
  if (dif) { printf("  !! %s: la LCD difiere del buffer en %d pixeles\n", nombre.c_str(), dif); fallos++; }
  std::ofstream f("capturas/" + nombre + ".pbm");
  f << "P1\n128 64\n";
  for (int y = 0; y < 64; y++) { for (int x = 0; x < 128; x++) f << pixelLcd(x, y) << ' '; f << '\n'; }
  printf("  captura %s (bytes SPI acumulados: %ld)\n", nombre.c_str(), bytesSpi);
}

int main(int argc, char **argv) {
  Serial.silencio = !(argc > 2);
  setup();
  std::ifstream g(argv[1]);
  std::string linea;
  while (std::getline(g, linea)) {
    if (linea.empty() || linea[0] == ';') continue;
    std::istringstream ss(linea);
    std::string cmd; ss >> cmd;
    if (cmd == "titulo") { printf("\n== %s\n", linea.substr(7).c_str()); continue; }
    if (cmd == "esperar") { unsigned long ms; ss >> ms; correr(ms); continue; }
    if (cmd == "teclas") {                         // varias teclas seguidas (clics rapidos)
      std::string ts; ss >> ts;
      for (char t : ts) { teclaAbajo = t; correr(60); teclaAbajo = 0; correr(90); }
      continue;
    }
    if (cmd == "mantener") { std::string t; unsigned long ms; ss >> t >> ms; teclaAbajo = t[0]; correr(ms); teclaAbajo = 0; correr(60); continue; }
    if (cmd == "uno") { std::string resto; std::getline(ss, resto); haciaEsp(latin1(resto.substr(1))); correr(20); continue; }
    if (cmd == "saltar") { unsigned long ms; ss >> ms; sim_ms += ms; continue; } // adelanta el reloj
    if (cmd == "uno_crudo") { std::string resto; std::getline(ss, resto); for (char c : resto.substr(1) + "\n") Serial2.rx.push_back(c); correr(20); continue; }
    if (cmd == "ack") { std::string v; ss >> v; autoAck = (v == "si"); continue; }
    if (cmd == "captura") { std::string n; ss >> n; captura(n); continue; }
    if (cmd == "espera_orden") {                   // comprueba que el ESP32 envio esta orden
      std::string pat; std::getline(ss, pat); pat = latin1(pat.substr(1));
      bool ok = false;
      for (auto &e : enviados) if (e.find(pat) != std::string::npos) ok = true;
      printf("  %s orden que contiene \"%s\"\n", ok ? "OK " : "!! FALTA", pat.c_str());
      if (!ok) fallos++;
      enviados.clear();
      continue;
    }
    if (cmd == "sin_ordenes") {
      if (!enviados.empty()) { printf("  !! no se esperaban ordenes y hubo %zu\n", enviados.size()); fallos++; }
      else printf("  OK  sin ordenes\n");
      enviados.clear(); continue;
    }
  }
  printf("\nRESULTADO: %d fallos\n", fallos);
  return fallos ? 1 : 0;
}
