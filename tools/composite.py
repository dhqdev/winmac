import sys, os, re, glob
from PIL import Image, ImageFilter
dump, wall, out = sys.argv[1], sys.argv[2], sys.argv[3]
W, H = 1920, 1080
base = Image.open(wall).convert("RGB")
# cobre a tela
s = max(W / base.width, H / base.height)
base = base.resize((int(base.width * s + 0.5), int(base.height * s + 0.5)), Image.LANCZOS)
l, t = (base.width - W) // 2, (base.height - H) // 2
base = base.crop((l, t, l + W, t + H)).convert("RGBA")
blurred = base.filter(ImageFilter.GaussianBlur(18))
files = {}
for f in glob.glob(os.path.join(dump, "*.png")):
    m = re.match(r"(.+)_(-?\d+)_(-?\d+)\.png$", os.path.basename(f))
    if not m: continue
    key = m.group(1)
    if "DockGlass" in key:
        key = "DockGlass"
        if key in files and os.path.getsize(files[key][0]) > os.path.getsize(f): continue
    files[key] = (f, int(m.group(2)), int(m.group(3)))
order = sorted(files.items(), key=lambda kv: (0 if "Widget" in kv[0] else 1 if "Glass" in kv[0] else 2))
for key, (f, x, y) in order:
    im = Image.open(f).convert("RGBA")
    if "Corner" not in key:
        # região de vidro: aplica o blur do fundo (aproximação do DWM)
        region = blurred.crop((x, y, x + im.width, y + im.height))
        if "Dock_" in key and "Glass" not in key:
            region = base.crop((x, y, x + im.width, y + im.height))
        mask = Image.new("L", im.size, 255)
        if "Widget" in key or "DockGlass" in key:
            from PIL import ImageDraw
            mask = Image.new("L", im.size, 0)
            r = 28 if "Widget" in key else int(im.height * 0.36)
            ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.width - 1, im.height - 1], radius=r, fill=255)
        if "Dock_" not in key or "Glass" in key:
            base.paste(region, (x, y), mask)
    base.alpha_composite(im, (x, y))
base.convert("RGB").save(out)
print("ok", [k for k, _ in order])
