# Convierte las capturas .pbm del simulador en hojas PNG (para revisarlas a simple vista).
import sys, os, glob
from PIL import Image, ImageDraw
d = sys.argv[1]; salida = sys.argv[2]; filtro = sys.argv[3] if len(sys.argv) > 3 else ""
arch = sorted(f for f in glob.glob(os.path.join(d, "*.pbm")) if os.path.basename(f).startswith(filtro))
esc, cols = 3, 2
W, H = 128 * esc + 10, 64 * esc + 22
hoja = Image.new("RGB", (W * cols, H * ((len(arch) + cols - 1) // cols)), (190, 200, 150))
dib = ImageDraw.Draw(hoja)
for k, f in enumerate(arch):
    datos = open(f).read().split()[3:]
    im = Image.new("RGB", (128, 64))
    im.putdata([(30, 40, 30) if v == "1" else (160, 190, 120) for v in datos])
    x, y = (k % cols) * W, (k // cols) * H
    hoja.paste(im.resize((128 * esc, 64 * esc), Image.NEAREST), (x + 5, y + 18))
    dib.text((x + 5, y + 4), os.path.basename(f)[:-4], fill=(0, 0, 0))
hoja.save(salida)
