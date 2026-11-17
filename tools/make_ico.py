"""Gera res/app.ico a partir dos PNGs exportados por: WinMac.exe --export-art <pasta>"""
import sys, os
from PIL import Image
d = sys.argv[1]
sizes = [256, 128, 64, 48, 40, 32, 24, 20, 16]
imgs = [Image.open(os.path.join(d, f"icon_{s}.png")).convert("RGBA") for s in sizes]
imgs[0].save(sys.argv[2], format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[1:])
print("ok")
