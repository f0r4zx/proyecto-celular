#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GENERADOR DE GRAFICOS PARA LA PANTALLA LCD12864 (ST7920)
=========================================================
Lee las 31 capturas de 128x64 pixeles del documento "INTERFAZ-TELEFONO"
(carpeta pantallas_png/) y produce firmware/esp32_interfaz/graficos.h.

Por que existe este script:
  * El archivo Interfaz-Telefono-Bits.txt venia en formato SSD1306 (paginas
    verticales), con los bits INVERTIDOS (1 = pixel apagado), con nombres
    repetidos y le faltaban pantallas (mensajes, subir, leer...).
  * Ademas guardaba pantallas completas "fijas" (solo el mensaje 1 y 2, etc.),
    mientras que el celular necesita partes que cambian (numero del mensaje,
    flechas, senal, cronometro, texto).
  * Aqui se toma cada captura (la fuente correcta y completa del diseno) y se
    separa en: FONDOS (lo estatico de cada menu) y PIEZAS (lo que se invierte al
    seleccionar o lo que aparece/desaparece), en formato XBM que dibuja U8g2
    con drawXBMP().

Uso:  python3 herramientas/generar_graficos.py
Requiere: Pillow (pip install pillow)
"""
import os
from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))                       # carpeta herramientas/
PNG = os.path.join(AQUI, "pantallas_png")                                # capturas del diseno
SALIDA = os.path.join(AQUI, "..", "firmware", "esp32_interfaz", "graficos.h")  # archivo generado


def cargar(nombre):
    """Devuelve la captura como matriz [y][x] con 1 = pixel encendido (negro)."""
    im = Image.open(os.path.join(PNG, nombre + ".png")).convert("RGBA")  # abre la imagen
    fondo = Image.new("RGBA", im.size, (255, 255, 255, 255))             # fondo blanco (las PNG tienen transparencia)
    fondo.alpha_composite(im)                                            # aplana la transparencia
    g = fondo.convert("L")                                               # escala de grises
    assert g.size == (128, 64), nombre                                   # todas deben ser 128x64
    return [[1 if g.getpixel((x, y)) < 128 else 0 for x in range(128)] for y in range(64)]


def limpiar(m, x, y, w, h):
    """Apaga un rectangulo (zona que el programa dibuja en tiempo real)."""
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            m[yy][xx] = 0


def a_xbm(m, x0, y0, w, h):
    """Convierte una region a bytes XBM (filas horizontales, bit 0 = pixel izquierdo)."""
    bpf = (w + 7) // 8                                                    # bytes por fila
    datos = []
    for y in range(y0, y0 + h):
        for b in range(bpf):
            v = 0
            for bit in range(8):
                x = x0 + b * 8 + bit
                if b * 8 + bit < w and m[y][x]:
                    v |= 1 << bit                                         # XBM: LSB primero
            datos.append(v)
    return datos


def caja_de(pixeles):
    """Rectangulo minimo (x, y, w, h) que contiene una lista de pixeles."""
    xs = [p[0] for p in pixeles]
    ys = [p[1] for p in pixeles]
    return min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1


def pieza_por_diferencia(var, base, region):
    """Recorta de 'var' la zona donde difiere de 'base' (dentro de 'region')."""
    rx, ry, rw, rh = region
    dif = [(x, y) for y in range(ry, ry + rh) for x in range(rx, rx + rw) if var[y][x] != base[y][x]]
    assert dif, "sin diferencias en " + str(region)
    return caja_de(dif)


def pieza_por_contenido(img, region):
    """Recorta el dibujo (pixeles encendidos) que hay dentro de 'region'."""
    rx, ry, rw, rh = region
    on = [(x, y) for y in range(ry, ry + rh) for x in range(rx, rx + rw) if img[y][x]]
    assert on, "region vacia " + str(region)
    return caja_de(on)


def circulo(img, region, relleno):
    """Extrae el contorno de un circulo numerado (sin el numero) o su version rellena."""
    rx, ry, rw, rh = region
    fuera = set()                                                         # pixeles apagados conectados al borde
    pila = [(x, y) for x in range(rx, rx + rw) for y in (ry, ry + rh - 1)] + \
           [(x, y) for y in range(ry, ry + rh) for x in (rx, rx + rw - 1)]
    while pila:                                                           # relleno por inundacion desde el borde
        x, y = pila.pop()
        if (x, y) in fuera or not (rx <= x < rx + rw and ry <= y < ry + rh) or img[y][x]:
            continue
        fuera.add((x, y))
        pila += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    salida = [[0] * 128 for _ in range(64)]
    pix = []
    for y in range(ry, ry + rh):
        for x in range(rx, rx + rw):
            if (x, y) in fuera:
                continue                                                  # exterior: apagado
            vecino_fuera = any((x + dx, y + dy) in fuera for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            anillo = img[y][x] and vecino_fuera                           # trazo del circulo
            if anillo or relleno:
                salida[y][x] = 1
                pix.append((x, y))
    return salida, caja_de(pix)


# ----------------------------------------------------------------------------- FONDOS
# (nombre en C, captura base, rectangulos que se apagan porque el codigo los dibuja)
FONDOS = [
    ("FONDO_MENU", "01_menu_principal", [(108, 0, 20, 9)]),              # quita barras de senal
    ("FONDO_LLAMADAS", "05_llamadas", [(30, 36, 69, 16)]),               # quita cursor de ejemplo
    ("FONDO_LLAMANDO", "07_llamando", [(112, 0, 16, 7), (46, 35, 34, 9)]),  # senal y 00:00
    ("FONDO_EN_CURSO", "09_en_curso", [(112, 0, 16, 7), (46, 35, 34, 9)]),  # senal y 00:00
    ("FONDO_ENTRANTE", "11_entrante", [(16, 16, 96, 15)]),               # interior del recuadro del numero
    ("FONDO_MANDAR", "14_mandar_mensaje", [(30, 36, 73, 13)]),           # quita cursor de ejemplo
    ("FONDO_MENSAJE", "17_mensaje", []),                                 # ya viene limpio
    ("FONDO_BANDEJA", "20_bandeja", [(0, 14, 115, 50)]),                 # deja solo titulo, estrellas y flecha
    ("FONDO_LEER", "27_leer_msg1", []),                                  # el numero del circulo se limpia abajo
]

# ----------------------------------------------------------------------------- PIEZAS
# (nombre en C, captura con la pieza, captura base o None, region de busqueda)
PIEZAS = [
    ("SEL_MENU_LLAMADA", "02_menu_llamada_sel", "01_menu_principal", (10, 44, 32, 20)),
    ("SEL_MENU_MENSAJE", "03_menu_mensaje_sel", "01_menu_principal", (48, 44, 28, 20)),
    ("SEL_MENU_BANDEJA", "04_menu_bandeja_sel", "01_menu_principal", (86, 44, 32, 20)),
    ("SEL_LLAMADAS_X", "06_llamadas_x_sel", "05_llamadas", (0, 0, 12, 12)),
    ("SEL_LLAMANDO_COLGAR", "08_llamando_colgar_sel", "07_llamando", (0, 50, 50, 14)),
    ("SEL_EN_CURSO_COLGAR", "10_en_curso_colgar_sel", "09_en_curso", (0, 50, 50, 14)),
    ("SEL_ENTRANTE_CONTESTAR", "12_entrante_contestar_sel", "11_entrante", (14, 36, 32, 28)),
    ("SEL_ENTRANTE_RECHAZAR", "13_entrante_rechazar_sel", "11_entrante", (80, 36, 32, 28)),
    ("SEL_MANDAR_REGRESAR", "15_mandar_regresar_sel", "14_mandar_mensaje", (0, 54, 16, 10)),
    ("SEL_MANDAR_SEGUIR", "16_mandar_seguir_sel", "14_mandar_mensaje", (112, 54, 16, 10)),
    ("SEL_MENSAJE_ENVIAR", "18_mensaje_enviar_sel", "17_mensaje", (100, 53, 28, 11)),
    ("SEL_MENSAJE_CANCELAR", "19_mensaje_cancelar_sel", "17_mensaje", (0, 53, 20, 11)),
    ("SEL_BANDEJA_REGRESAR", "21_bandeja_regresar_sel", "20_bandeja", (115, 55, 13, 9)),
    ("SEL_LEER_REGRESAR", "28_leer_msg1_regresar_sel", "27_leer_msg1", (0, 54, 14, 10)),
    ("FLECHA_ARRIBA", "25_bandeja_subir", None, (115, 18, 13, 16)),     # contorno = no se puede subir
    ("FLECHA_ARRIBA_ACTIVA", "26_bandeja_subir_sel", None, (115, 18, 13, 16)),  # negra = si se puede
    ("FLECHA_ABAJO", "23_bandeja_mas_abajo", None, (115, 43, 13, 10)),
    ("FLECHA_ABAJO_ACTIVA", "24_bandeja_abajo_sel", None, (115, 43, 13, 10)),
]

CIRCULOS = [
    # (nombre, captura, region que contiene el circulo, relleno)
    ("CIRCULO_BANDEJA", "20_bandeja", (0, 15, 17, 16), False),
    ("CIRCULO_BANDEJA_LLENO", "20_bandeja", (0, 15, 17, 16), True),
    ("CIRCULO_LEER", "27_leer_msg1", (1, 0, 17, 15), False),
]


def emitir(f, nombre, img, x, y, w, h, comentario):
    datos = a_xbm(img, x, y, w, h)
    f.write("// %s\n" % comentario)
    f.write("#define %s_X %d   // columna donde se dibuja\n" % (nombre, x))
    f.write("#define %s_Y %d   // fila donde se dibuja\n" % (nombre, y))
    f.write("#define %s_W %d   // ancho en pixeles\n" % (nombre, w))
    f.write("#define %s_H %d   // alto en pixeles\n" % (nombre, h))
    f.write("static const unsigned char %s[] PROGMEM = {\n" % nombre)
    for i in range(0, len(datos), 16):
        f.write("  " + ", ".join("0x%02x" % v for v in datos[i:i + 16]) + ",\n")
    f.write("};\n\n")


def main():
    with open(SALIDA, "w", newline="\n") as f:
        f.write("// =============================================================================\n")
        f.write("//  graficos.h  -  GENERADO AUTOMATICAMENTE por herramientas/generar_graficos.py\n")
        f.write("//  NO EDITAR A MANO: modificar las PNG de herramientas/pantallas_png y volver\n")
        f.write("//  a ejecutar el script. Formato XBM (U8g2 drawXBMP): filas horizontales,\n")
        f.write("//  bit 0 = pixel de la izquierda, 1 = pixel encendido (negro).\n")
        f.write("// =============================================================================\n")
        f.write("#pragma once\n#include <Arduino.h>\n\n")

        f.write("// ------------------------------- FONDOS (128x64, parte estatica de cada menu)\n")
        for nombre, png, limpios in FONDOS:
            img = cargar(png)
            for r in limpios:
                limpiar(img, *r)
            if nombre == "FONDO_LEER":                                    # quita el "1" del circulo
                anillo, (cx, cy, cw, ch) = circulo(img, (1, 0, 17, 15), False)
                for yy in range(cy, cy + ch):
                    for xx in range(cx, cx + cw):
                        img[yy][xx] = anillo[yy][xx]
            emitir(f, nombre, img, 0, 0, 128, 64, "Fondo desde %s.png" % png)

        f.write("// ------------------------------- PIEZAS (seleccion invertida, flechas)\n")
        for nombre, png, base, region in PIEZAS:
            img = cargar(png)
            if base:
                x, y, w, h = pieza_por_diferencia(img, cargar(base), region)
            else:
                x, y, w, h = pieza_por_contenido(img, region)
            emitir(f, nombre, img, x, y, w, h, "Pieza desde %s.png" % png)

        f.write("// ------------------------------- CIRCULOS NUMERADOS (el numero lo pone el codigo)\n")
        for nombre, png, region, relleno in CIRCULOS:
            img, (x, y, w, h) = circulo(cargar(png), region, relleno)
            emitir(f, nombre, img, x, y, w, h, "Circulo desde %s.png" % png)
    print("Generado:", os.path.normpath(SALIDA))


if __name__ == "__main__":
    main()
