// =============================================================================
//  SIMULADOR EN PC DEL ARDUINO UNO (solo para pruebas)
//  Compila el sketch real (arduino_uno_telecom.ino) y lo conecta a:
//    - una SIM900 FALSA que responde a los comandos AT como el modulo real;
//    - un "ESP32" que manda ordenes (con id y checksum) y recibe los avisos.
//  El guion casos.txt dice que pasa en la red (llamadas, SMS, senal).
// =============================================================================
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>

unsigned long sim_ms = 0;
Puerto Serial;
extern SoftwareSerial esp;
extern void setup();
extern void loop();
static int pinD9 = 0;
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t p, uint8_t v) { if (p == 9) { if (v && !pinD9) printf("  [t=%7lu] D9 -> PULSO DE ENCENDIDO\n", sim_ms); pinD9 = v; } }

// ---------------- SIM900 falsa ----------------
struct SmsGuardado { std::string num, fecha, cuerpo; int dcs; };
static std::map<int, SmsGuardado> simSms;          // mensajes guardados en la SIM
static bool encendido = true;
static int csq = 18;
static int registro = 1;                          // +CREG: 1 = registrada, 0/2/3 = sin red
static int llamada = -1;                          // -1 nada; si no, stat de +CLCC (0 activa, 2 marcando, 3 alertando, 4 entrante)
static std::string numLlamada;
static bool modoTextoSms = false;                 // recibiendo el texto despues de '>'
static std::string textoSms, lineaAt;
static std::vector<std::string> comandosAt;       // todo lo que recibio la SIM900
static std::string ultimoSmsEnviado;
static void responder(const std::string &s) { for (char c : s) Serial.rx.push_back((uint8_t)c); }
static void atender(const std::string &c) {
  comandosAt.push_back(c);
  if (!encendido) return;                          // apagada: no responde
  if (c.rfind("ATD", 0) == 0) { llamada = 2; numLlamada = c.substr(3, c.size() - 4); responder("\r\nOK\r\n"); return; }
  if (c == "ATA") { if (llamada == 4) llamada = 0; responder("\r\nOK\r\n"); return; }
  if (c == "ATH") { llamada = -1; responder("\r\nOK\r\n"); return; }
  if (c == "AT+CREG?") { responder("\r\n+CREG: 0," + std::to_string(registro) + "\r\n\r\nOK\r\n"); return; }
  if (c == "AT+CSQ") { responder("\r\n+CSQ: " + std::to_string(csq) + ",0\r\n\r\nOK\r\n"); return; }
  if (c == "AT+CLCC") {
    if (llamada >= 0) responder("\r\n+CLCC: 1," + std::string(llamada == 4 ? "1" : "0") + "," + std::to_string(llamada) + ",0,0,\"" + numLlamada + "\",145,\"\"\r\n");
    responder("\r\nOK\r\n"); return;
  }
  if (c.rfind("AT+CMGR=", 0) == 0) {
    int i = atoi(c.c_str() + 8);
    if (!simSms.count(i)) { responder("\r\nOK\r\n"); return; }
    auto &m = simSms[i];
    responder("\r\n+CMGR: \"REC UNREAD\",\"" + m.num + "\",\"\",\"" + m.fecha + "\",145,4,0," + std::to_string(m.dcs) + ",\"+50379999999\",145," + std::to_string(m.cuerpo.size()) + "\r\n" + m.cuerpo + "\r\n\r\nOK\r\n");
    return;
  }
  if (c.rfind("AT+CMGD=", 0) == 0) { simSms.erase(atoi(c.c_str() + 8)); responder("\r\nOK\r\n"); return; }
  if (c.rfind("AT+CMGL", 0) == 0) {
    for (auto &p : simSms) responder("\r\n+CMGL: " + std::to_string(p.first) + ",\"REC UNREAD\",\"" + p.second.num + "\",\"\",\"" + p.second.fecha + "\",145,5\r\n" + p.second.cuerpo);
    responder("\r\n\r\nOK\r\n"); return;
  }
  if (c.rfind("AT+CMGS=", 0) == 0) { modoTextoSms = true; textoSms.clear(); responder("\r\n> "); return; }
  responder("\r\nOK\r\n");                         // el resto de comandos: OK
}
static void simRecibeByte(uint8_t b) {
  if (modoTextoSms) {
    if (b == 0x1A) { modoTextoSms = false; ultimoSmsEnviado = textoSms; responder("\r\n+CMGS: 7\r\n\r\nOK\r\n"); }
    else if (b == 0x1B) { modoTextoSms = false; }
    else textoSms += (char)b;
    return;
  }
  if (b == '\r') return;
  if (b == '\n') { if (!lineaAt.empty()) atender(lineaAt); lineaAt.clear(); return; }
  lineaAt += (char)b;
}

// ---------------- ESP32 falso ----------------
static std::vector<std::string> eventos;           // lineas recibidas por el "ESP32"
static std::string rxEsp;
static int idOrden = 1, fallos = 0;
static uint8_t chk(const std::string &s) { uint8_t x = 0; for (char c : s) x ^= (uint8_t)c; return x; }
static void ordenEsp(const std::string &o, int id = -1) {
  std::string l = std::to_string(id < 0 ? idOrden++ : id) + "," + o;
  char t[8]; snprintf(t, sizeof t, "*%02X\n", chk(l));
  for (char c : l + t) esp.rx.push_back((uint8_t)c);
}
static std::string legible(const std::string &s) { std::string r; for (unsigned char c : s) { if (c >= 0x80) { char b[8]; snprintf(b, 8, "<%02X>", c); r += b; } else r += c; } return r; }
static void bombear() {
  for (char c : Serial.tx) simRecibeByte((uint8_t)c);
  Serial.tx.clear();
  for (char c : esp.tx) {
    if (c != '\n') { rxEsp += c; continue; }
    std::string cuerpo = rxEsp.substr(0, rxEsp.size() - 3);
    unsigned v = strtoul(rxEsp.c_str() + rxEsp.size() - 2, NULL, 16);
    if (rxEsp.size() < 4 || rxEsp[rxEsp.size() - 3] != '*' || chk(cuerpo) != v) { printf("  !! linea con checksum malo: %s\n", rxEsp.c_str()); fallos++; }
    else { printf("  [t=%7lu] UNO->ESP32: %s\n", sim_ms, legible(cuerpo).c_str()); eventos.push_back(cuerpo); }
    rxEsp.clear();
  }
  esp.tx.clear();
}
static void correr(unsigned long ms) { for (unsigned long i = 0; i < ms; i++) { loop(); bombear(); sim_ms++; } }
static std::string latin1(const std::string &u) {
  std::string r;
  for (size_t i = 0; i < u.size(); i++) { uint8_t c = u[i]; if (c >= 0xC0 && i + 1 < u.size()) r += (char)(((c & 0x1F) << 6) | (u[++i] & 0x3F)); else r += (char)c; }
  return r;
}
static void verificar(bool ok, const std::string &que) { printf("  %s %s\n", ok ? "OK " : "!! FALLA", que.c_str()); if (!ok) fallos++; }

int main(int, char **argv) {
  setup();
  std::ifstream g(argv[1]);
  std::string linea;
  while (std::getline(g, linea)) {
    if (linea.empty() || linea[0] == ';') continue;
    std::istringstream ss(linea);
    std::string cmd; ss >> cmd;
    std::string resto; std::getline(ss, resto); if (!resto.empty()) resto = resto.substr(1);
    if (cmd == "titulo") { printf("\n== %s\n", resto.c_str()); eventos.clear(); continue; }
    if (cmd == "esperar") { correr(std::stoul(resto)); continue; }
    if (cmd == "orden") { ordenEsp(latin1(resto)); correr(300); continue; }
    if (cmd == "orden_repetida") { ordenEsp(latin1(resto), idOrden - 1); correr(300); continue; }
    if (cmd == "csq") { csq = std::stoi(resto); continue; }
    if (cmd == "registro") { registro = std::stoi(resto); continue; }
    if (cmd == "apagar") { encendido = false; continue; }
    if (cmd == "apagado_urc") { responder("\r\nNORMAL POWER DOWN\r\n"); correr(50); encendido = false; continue; }
    if (cmd == "encender") { encendido = true; continue; }
    if (cmd == "conecta_sin_aviso") { llamada = 0; continue; }     // contestan pero sin "MO CONNECTED"
    if (cmd == "ring") { llamada = 4; numLlamada = resto; responder("\r\nRING\r\n\r\n+CLIP: \"" + resto + "\",145,\"\",0,\"\",0\r\n"); correr(100); continue; }
    if (cmd == "contesta_remoto") { llamada = 0; responder("\r\nMO CONNECTED\r\n"); correr(100); continue; }
    if (cmd == "suena_remoto") { llamada = 3; responder("\r\nMO RING\r\n"); correr(100); continue; }
    if (cmd == "cuelga_remoto") { llamada = -1; responder("\r\nNO CARRIER\r\n"); correr(100); continue; }
    if (cmd == "deja_de_sonar") { llamada = -1; continue; }   // llamada perdida: solo deja de listarse
    if (cmd == "sms_llega" || cmd == "sms_en_sim") {          // <idx> <num> <dcs> <texto>
      std::istringstream r(resto); int idx, dcs; std::string num; r >> idx >> num >> dcs;
      std::string txt; std::getline(r, txt); txt = txt.substr(1);
      simSms[idx] = {num, "26/10/16,14:05:09-24", txt, dcs};
      if (cmd == "sms_llega") { responder("\r\n+CMTI: \"SM\"," + std::to_string(idx) + "\r\n"); correr(100); }
      continue;
    }
    if (cmd == "espera_evento") {
      std::string pat = latin1(resto); bool ok = false;
      for (size_t i = 0; i < eventos.size() && !ok; i++)
        if (eventos[i].find(pat) != std::string::npos) { ok = true; eventos.erase(eventos.begin(), eventos.begin() + i + 1); }
      verificar(ok, "evento al ESP32 que contiene \"" + legible(pat) + "\"");
      continue;
    }
    if (cmd == "sin_evento") {
      bool ok = true; for (auto &e : eventos) if (e.find(resto) != std::string::npos) ok = false;
      verificar(ok, "no hubo evento \"" + resto + "\""); continue;
    }
    if (cmd == "espera_at") {
      bool ok = false; for (auto &c : comandosAt) if (c.find(resto) != std::string::npos) ok = true;
      verificar(ok, "la SIM900 recibio \"" + resto + "\""); comandosAt.clear(); continue;
    }
    if (cmd == "cuenta_at") {                                 // <texto> <veces>
      std::istringstream r(resto); std::string t; int n; r >> t >> n; int k = 0;
      for (auto &c : comandosAt) if (c.find(t) != std::string::npos) k++;
      verificar(k == n, "la SIM900 recibio \"" + t + "\" " + std::to_string(k) + " veces (esperado " + std::to_string(n) + ")");
      comandosAt.clear(); continue;
    }
    if (cmd == "sms_enviado") { verificar(ultimoSmsEnviado == latin1(resto), "texto que llego a la SIM900: \"" + legible(ultimoSmsEnviado) + "\""); continue; }
    if (cmd == "limpiar") { eventos.clear(); comandosAt.clear(); continue; }
  }
  printf("\nRESULTADO: %d fallos\n", fallos);
  return fallos ? 1 : 0;
}
