#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genera docs/diagrama_conexiones.svg: como se conectan todas las placas y
componentes del celular, con los pines tal como estan definidos en el codigo
(firmware/esp32_interfaz/esp32_interfaz.ino y firmware/arduino_uno_telecom/...).
Uso: python3 herramientas/generar_diagrama.py
"""
import os

SALIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "diagrama_conexiones.svg")
W, H = 1820, 1260
PASO = 30                                   # separacion entre pines
svg = []

COL = {                                     # colores de cada tipo de senal
    "fila": "#1f6fd1", "col": "#1a9a4a", "uart": "#e07b00", "lcd": "#8a3fd1",
    "3v3": "#d1a000", "5v": "#d12a2a", "gnd": "#222222", "audio": "#8b5a2b", "ctl": "#0a8f8f",
}


def t(x, y, s, size=13, anchor="start", weight="normal", color="#111", italic=False):
    st = ' font-style="italic"' if italic else ""
    svg.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" '
               f'font-weight="{weight}" fill="{color}"{st}>{s}</text>')


def bloque(x, y, w, titulo, sub, izq, der, fondo, alto_min=0):
    """Dibuja una placa con pines a la izquierda y derecha. Devuelve {nombre: (x, y)}."""
    n = max(len(izq), len(der))
    h = max(70 + n * PASO, alto_min)
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fondo}" stroke="#333" stroke-width="2"/>')
    t(x + w / 2, y + 24, titulo, 16, "middle", "bold")
    t(x + w / 2, y + 42, sub, 11, "middle", color="#444")
    pines = {}
    for i, (clave, etiqueta) in enumerate(izq):
        py = y + 66 + i * PASO
        svg.append(f'<circle cx="{x}" cy="{py}" r="4" fill="#333"/>')
        t(x + 10, py + 4, etiqueta, 12)
        pines[clave] = (x, py)
    for i, (clave, etiqueta) in enumerate(der):
        py = y + 66 + i * PASO
        svg.append(f'<circle cx="{x + w}" cy="{py}" r="4" fill="#333"/>')
        t(x + w - 10, py + 4, etiqueta, 12, "end")
        pines[clave] = (x + w, py)
    return pines, h


def cable(a, b, color, ancho=2.5, xm=None, etiqueta=None):
    """Cable con codos en angulo recto de a hasta b (xm = columna del tramo vertical)."""
    (x1, y1), (x2, y2) = a, b
    if xm is None:
        xm = (x1 + x2) / 2
    svg.append(f'<polyline points="{x1},{y1} {xm},{y1} {xm},{y2} {x2},{y2}" fill="none" '
               f'stroke="{color}" stroke-width="{ancho}" stroke-linejoin="round"/>')
    if etiqueta:
        t((x1 + xm) / 2, y1 - 5, etiqueta, 10, "middle", color=color)


def red(p, nombre, lado):
    """Etiqueta de red (5V, GND...): todas las del mismo nombre estan unidas."""
    x, y = p
    color = COL["5v"] if nombre == "5V" else COL["gnd"]
    largo = 26
    x2 = x + largo if lado == "der" else x - largo
    svg.append(f'<line x1="{x}" y1="{y}" x2="{x2}" y2="{y}" stroke="{color}" stroke-width="2.5"/>')
    ax = x2 if lado == "der" else x2 - 44
    svg.append(f'<rect x="{ax}" y="{y - 10}" width="44" height="20" rx="4" fill="{color}"/>')
    t(ax + 22, y + 4, nombre, 11, "middle", "bold", "#fff")


# ----------------------------------------------------------------- titulo
svg.append(f'<rect width="{W}" height="{H}" fill="#fbfaf6"/>')
t(40, 40, "Celular básico — diagrama de conexiones", 26, weight="bold")
t(40, 64, "Pines tomados del código: esp32_interfaz.ino y arduino_uno_telecom.ino. "
          "Las etiquetas 5V y GND del mismo nombre van todas unidas (tierra común obligatoria).", 13, color="#444")

# ----------------------------------------------------------------- teclado
tec, h_tec = bloque(40, 110, 230, "TECLADO (PCB matriz)", "conector J1 · R1–R4 = 10 kΩ pull-up",
                    [], [("vcc", "J1-11  VCC"), ("f1", "J1-4  F1"), ("f2", "J1-5  F2"), ("f3", "J1-6  F3"),
                         ("f4", "J1-7  F4"), ("c1", "J1-8  C1"), ("c2", "J1-9  C2"), ("c3", "J1-10  C3"),
                         ("c4", "J1-3  C4"), ("c5", "J1-2  C5"), ("c6", "J1-1  C6")], "#eef4ff")
t(155, 110 + h_tec + 18, "Filas con pull-up → en reposo ALTO,", 11, "middle", color="#444", italic=True)
t(155, 110 + h_tec + 32, "BAJO al presionar con su columna activa.", 11, "middle", color="#444", italic=True)
t(155, 110 + h_tec + 46, "¡VCC a 3.3 V, nunca a 5 V!", 11, "middle", "bold", COL["5v"])

# ----------------------------------------------------------------- ESP32
esp, h_esp = bloque(470, 110, 230, "ESP32-WROOM-32E", "DevKit 38 pines · lógica 3.3 V",
                    [("3v3a", "3V3"), ("34", "GPIO 34"), ("35", "GPIO 35"), ("36", "GPIO 36 (VP)"),
                     ("39", "GPIO 39 (VN)"), ("13", "GPIO 13"), ("14", "GPIO 14"), ("27", "GPIO 27"),
                     ("26", "GPIO 26"), ("25", "GPIO 25"), ("33", "GPIO 33")],
                    [("3v3b", "3V3"), ("17", "GPIO 17 (TX2)"), ("16", "GPIO 16 (RX2)"), ("18", "GPIO 18 (SCK)"),
                     ("23", "GPIO 23 (MOSI)"), ("gnd", "GND"), ("vin", "VIN (5 V)")], "#fff4e6")
t(585, 110 + h_esp + 18, "No usar GPIO 12 (pin de arranque).", 11, "middle", color="#444", italic=True)
t(585, 110 + h_esp + 32, "Libres: GPIO 4, 5, 19, 21, 22, 32.", 11, "middle", color="#444", italic=True)

# teclado -> ESP32 (lineas rectas: mismo orden de pines)
cable(tec["vcc"], esp["3v3a"], COL["3v3"], etiqueta="3.3 V")
for k, g in [("f1", "34"), ("f2", "35"), ("f3", "36"), ("f4", "39")]:
    cable(tec[k], esp[g], COL["fila"], etiqueta="fila " + k[1])
for k, g in [("c1", "13"), ("c2", "14"), ("c3", "27"), ("c4", "26"), ("c5", "25"), ("c6", "33")]:
    cable(tec[k], esp[g], COL["col"], etiqueta="col " + k[1])

# ----------------------------------------------------------------- level shifter
ls, h_ls = bloque(830, 110, 220, "LEVEL SHIFTER", "4 canales bidireccional · 3.3 V ↔ 5 V",
                  [("lv", "LV"), ("lv1", "LV1"), ("lv2", "LV2"), ("lv3", "LV3"), ("lv4", "LV4"), ("gl", "GND")],
                  [("hv", "HV"), ("hv1", "HV1"), ("hv2", "HV2"), ("hv3", "HV3"), ("hv4", "HV4"), ("gh", "GND")],
                  "#f2f2f2")
cable(esp["3v3b"], ls["lv"], COL["3v3"], etiqueta="3.3 V")
cable(esp["17"], ls["lv1"], COL["uart"], etiqueta="TX2 →")
cable(esp["16"], ls["lv2"], COL["uart"], etiqueta="← RX2")
cable(esp["18"], ls["lv3"], COL["lcd"], etiqueta="SCK")
cable(esp["23"], ls["lv4"], COL["lcd"], etiqueta="MOSI")
cable(esp["gnd"], ls["gl"], COL["gnd"])
red(esp["vin"], "5V", "der")
red(ls["hv"], "5V", "der")
red(ls["gh"], "GND", "der")

# ----------------------------------------------------------------- LCD
lcd, h_lcd = bloque(1300, 110, 250, "LCD12864 (ST7920)", "128×64 · 5 V · modo serie (SPI)",
                    [("e", "6  E (SCLK)"), ("rw", "5  R/W (SID)"), ("rs", "4  RS (CS)"), ("vdd", "2  VDD"),
                     ("bla", "19  BLA (luz +)"), ("rst", "17  RST"), ("vss", "1  VSS"), ("psb", "15  PSB"),
                     ("blk", "20  BLK (luz −)")], [], "#eefbf0")
t(1425, 110 + h_lcd + 18, "V0 (3): sin conectar si trae potenciómetro.", 11, "middle", color="#444", italic=True)
t(1425, 110 + h_lcd + 32, "7–14, 16, 18: sin conectar. PSB a GND = modo serie.", 11, "middle", color="#444", italic=True)
cable(ls["hv3"], lcd["e"], COL["lcd"], xm=1150)
t(1225, lcd["e"][1] - 6, "SCK 5 V", 10, "middle", color=COL["lcd"])
cable(ls["hv4"], lcd["rw"], COL["lcd"], xm=1170)
t(1235, lcd["rw"][1] - 6, "DATOS 5 V", 10, "middle", color=COL["lcd"])
for k in ("rs", "vdd", "bla", "rst"):
    red(lcd[k], "5V", "izq")
for k in ("vss", "psb", "blk"):
    red(lcd[k], "GND", "izq")

# ----------------------------------------------------------------- Arduino Uno
uno, h_uno = bloque(1160, 560, 230, "ARDUINO UNO", "ATmega328P · lógica 5 V",
                    [("d11", "D11 (RX soft)"), ("d12", "D12 (TX soft)"), ("5v", "5V"), ("gnd", "GND")],
                    [("d0", "D0 (RX)"), ("d1", "D1 (TX)"), ("d9", "D9"), ("d2", "D2–D8, D10"), ("a", "A0–A5"),
                     ("u5", "5V"), ("u33", "3.3V"), ("ug", "GND"), ("uvin", "VIN")], "#eaf6fb")
t(1275, 560 + h_uno + 18, "D13 = LED de estado (interno): parpadea", 11, "middle", color="#444", italic=True)
t(1275, 560 + h_uno + 32, "configurando la SIM900, fijo = lista.", 11, "middle", color="#444", italic=True)
cable(ls["hv1"], uno["d11"], COL["uart"], xm=1135)
cable(ls["hv2"], uno["d12"], COL["uart"], xm=1125)
t(1145, 515, "UART 19200 baudios", 12, "start", "bold", COL["uart"])
t(1145, 531, "HV1 → D11 · D12 → HV2", 11, "start", color=COL["uart"])
red(uno["5v"], "5V", "izq")
red(uno["gnd"], "GND", "izq")

# ----------------------------------------------------------------- Shield SIM900
sh, h_sh = bloque(1520, 560, 260, "GPRS SHIELD SIM900", "montado sobre el Uno · chip Tigo (2G)",
                  [("d0", "D0 ← TXD SIM900"), ("d1", "D1 → RXD SIM900"), ("d9", "D9 → PWRKEY"), ("d2", "D2–D8, D10"),
                   ("a", "A0–A5"), ("s5", "5V"), ("s33", "3.3V"), ("sg", "GND"), ("svin", "VIN")],
                  [("spk", "SPK (jack)"), ("mic", "MIC (jack)"), ("dc", "DC IN")], "#fdeef0")
cable(uno["d0"], sh["d0"], COL["uart"])
cable(uno["d1"], sh["d1"], COL["uart"])
cable(uno["d9"], sh["d9"], COL["ctl"], etiqueta="encendido")
for k in ("d2", "a"):
    cable(uno[k], sh[k], "#999999", 2)
cable(uno["u5"], sh["s5"], COL["5v"])
cable(uno["u33"], sh["s33"], COL["3v3"])
cable(uno["ug"], sh["sg"], COL["gnd"])
cable(uno["uvin"], sh["svin"], COL["5v"], 2)
t(1650, 560 + h_sh + 18, "Jumpers de puerto en HW (D0/D1).", 11, "middle", "bold", "#444")
t(1650, 560 + h_sh + 32, "Quitarlos al subir el programa al Uno.", 11, "middle", color="#444", italic=True)
t(1650, 560 + h_sh + 46, "DC IN: fuente capaz de 2 A (picos).", 11, "middle", "bold", COL["5v"])
t(1460, 560 + h_sh + 64, "AT a 9600 baudios · volumen fijo 80 % · micrófono abierto", 11, "middle", color="#444", italic=True)

# ----------------------------------------------------------------- Audio
amp, h_amp = bloque(1450, 1000, 230, "AMPLIFICADOR LM386", "módulo 5–12 V",
                    [("o1", "OUT +"), ("o2", "OUT −")], [("in", "IN"), ("ag", "GND"), ("vcc", "VCC")], "#f7f0e6")
red(amp["ag"], "GND", "der")
red(amp["vcc"], "5V", "der")
# SPK de la SIM900 -> IN del LM386 (baja por la derecha)
svg.append(f'<polyline points="{sh["spk"][0]},{sh["spk"][1]} 1795,{sh["spk"][1]} 1795,{amp["in"][1]} {amp["in"][0]},{amp["in"][1]}" '
           f'fill="none" stroke="{COL["audio"]}" stroke-width="2.5" stroke-linejoin="round"/>')
t(1740, amp["in"][1] - 6, "SPK", 10, "middle", color=COL["audio"])

# parlante (a la izquierda del amplificador)
px, py = 1330, amp["o1"][1] + 15
svg.append(f'<rect x="{px}" y="{py - 18}" width="18" height="36" fill="#ddd" stroke="#333" stroke-width="2"/>')
svg.append(f'<polygon points="{px},{py - 18} {px - 26},{py - 36} {px - 26},{py + 36} {px},{py + 18}" fill="#ddd" stroke="#333" stroke-width="2"/>')
t(px - 4, py + 58, "Parlante 8 Ω · 0.5 W", 12, "middle", "bold")
cable(amp["o1"], (px + 18, py - 8), COL["audio"], xm=1400)
cable(amp["o2"], (px + 18, py + 8), COL["audio"], xm=1415)

# microfono (abajo a la derecha)
mx, my = 1740, 1212
svg.append(f'<circle cx="{mx}" cy="{my}" r="20" fill="#ddd" stroke="#333" stroke-width="2"/>')
svg.append(f'<line x1="{mx - 12}" y1="{my + 26}" x2="{mx + 12}" y2="{my + 26}" stroke="#333" stroke-width="3"/>')
t(mx - 80, my + 2, "Micrófono electret", 12, "end", "bold")
t(mx - 80, my + 16, "(+) punta · (−) malla", 11, "end", color="#444")
svg.append(f'<polyline points="{sh["mic"][0]},{sh["mic"][1]} 1808,{sh["mic"][1]} 1808,{my} {mx + 20},{my}" fill="none" '
           f'stroke="{COL["audio"]}" stroke-width="2.5" stroke-linejoin="round"/>')
t(1770, my - 6, "MIC", 10, "middle", color=COL["audio"])

# ----------------------------------------------------------------- fuente
fu, h_fu = bloque(40, 640, 330, "ALIMENTACIÓN", "18650 + TP4056 + MT3608 / LM2596",
                  [], [("f5", "+5 V"), ("fg", "GND")], "#fff0f0")
t(205, 640 + h_fu + 18, "Configuración interna según el diseño de la fuente", 11, "middle", color="#444", italic=True)
t(205, 640 + h_fu + 32, "del equipo (no la define el código).", 11, "middle", color="#444", italic=True)
red(fu["f5"], "5V", "der")
red(fu["fg"], "GND", "der")

# ----------------------------------------------------------------- leyenda
lx, ly = 40, 860
svg.append(f'<rect x="{lx}" y="{ly}" width="560" height="330" rx="10" fill="#ffffff" stroke="#bbb"/>')
t(lx + 16, ly + 28, "Leyenda", 16, weight="bold")
items = [("fila", "Filas del teclado (entradas del ESP32)"), ("col", "Columnas del teclado (salidas de barrido)"),
         ("uart", "UART: ESP32 ↔ Uno (19200) y Uno ↔ SIM900 (9600)"), ("lcd", "SPI hacia la LCD (pasa por el level shifter)"),
         ("ctl", "Control: D9 enciende la SIM900"), ("audio", "Audio analógico"), ("3v3", "3.3 V (sale del ESP32)"),
         ("5v", "5 V"), ("gnd", "GND (tierra común)")]
for i, (k, d) in enumerate(items):
    yy = ly + 56 + i * 26
    svg.append(f'<line x1="{lx + 18}" y1="{yy}" x2="{lx + 60}" y2="{yy}" stroke="{COL[k]}" stroke-width="4"/>')
    t(lx + 72, yy + 4, d, 13)
yy = ly + 56 + len(items) * 26
svg.append(f'<line x1="{lx + 18}" y1="{yy}" x2="{lx + 60}" y2="{yy}" stroke="#999" stroke-width="3"/>')
t(lx + 72, yy + 4, "Conexión Uno–shield sin uso en el programa", 13)

svg.insert(0, f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
              f'font-family="DejaVu Sans, Arial, sans-serif">')
svg.append("</svg>")
with open(SALIDA, "w", encoding="utf-8") as f:
    f.write("\n".join(svg))
print("Generado:", os.path.normpath(SALIDA))
