# Genera casos.txt a partir de casos_plantilla.txt, poniendo los SMS en UCS2
# (texto en hexadecimal, como los entrega la SIM900 cuando el SMS trae tildes).
def hexu(t): return ''.join('%04X' % ord(c) for c in t)
largo = "Él dijo: ¿mañana a las 8? Sí, después de la reunión en la oficina!!"
casos = open('casos_plantilla.txt', encoding='utf-8').read()
casos = casos.replace('@HEX1@', hexu("Canción €")).replace('@HEX2@', hexu(largo)).replace('@LARGO@', largo)
open('casos.txt', 'w', encoding='utf-8').write(casos)
