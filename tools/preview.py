"""Monta prévias das telas do WinMac sobre um papel de parede, imitando o desfoque do Windows (DWM).

Uso: python tools/preview.py <papel.jpg> <janela.png> <saida.png> [pad] [raio] [largura] [altura]
  pad   = margem de sombra da janela em px (padrão 38, que é 30 px * 125%)
  raio  = raio dos cantos do painel em px (padrão 18)
"""
import sys
from PIL import Image, ImageDraw, ImageFilter


def main():
    bg_path, win_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    pad = int(sys.argv[4]) if len(sys.argv) > 4 else 38
    radius = int(sys.argv[5]) if len(sys.argv) > 5 else 18
    win = Image.open(win_path).convert("RGBA")
    W = int(sys.argv[6]) if len(sys.argv) > 6 else win.width + 160
    H = int(sys.argv[7]) if len(sys.argv) > 7 else win.height + 120
    bg = Image.open(bg_path).convert("RGB")
    s = max(W / bg.width, H / bg.height)
    bg = bg.resize((int(bg.width * s + 0.5), int(bg.height * s + 0.5)), Image.LANCZOS)
    l, t = (bg.width - W) // 2, (bg.height - H) // 2
    bg = bg.crop((l, t, l + W, t + H)).convert("RGBA")
    blurred = bg.filter(ImageFilter.GaussianBlur(22))
    x, y = (W - win.width) // 2, (H - win.height) // 2
    px, py, pw, ph = x + pad, y + pad, win.width - 2 * pad, win.height - 2 * pad
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([px, py, px + pw - 1, py + ph - 1], radius=radius, fill=255)
    base = Image.composite(blurred, bg, mask)
    base.alpha_composite(win, (x, y))
    base.convert("RGB").save(out)


if __name__ == "__main__":
    main()
