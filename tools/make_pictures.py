# The example scripts' panel pictures (examples/Pictures/), drawn here with
# the AmigaChrome logo (assets/ac-logo.png):
#   python3 tools/make_pictures.py
# welcome.png, copying.iff (IFF ILBM, 32 colours), download.gif (animated,
# 12 frames) and done.png, each 200 x 300, about the panel's shape.
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "examples" / "Pictures"
W, H = 200, 300


def font(size):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"):
        if Path(f).exists():
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def backdrop(top, bottom):
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    return im


def logo(size):
    return Image.open(ROOT / "assets" / "ac-logo.png").convert("RGBA").resize((size, size), Image.LANCZOS)


def caption(im, text, y):
    d = ImageDraw.Draw(im)
    f = font(20)
    w = d.textlength(text, font=f)
    d.text(((W - w) / 2, y), text, font=f, fill=(255, 255, 255))


def welcome():
    im = backdrop((20, 40, 90), (5, 10, 30))
    lg = logo(150)
    im.paste(lg, (25, 50), lg)
    caption(im, "Welcome", 220)
    im.save(OUT / "welcome.png", optimize=True)


def done():
    im = backdrop((10, 70, 40), (5, 20, 10))
    d = ImageDraw.Draw(im)
    d.ellipse((40, 60, 160, 180), outline=(255, 255, 255), width=8)
    d.line([(70, 120), (95, 148), (135, 92)], fill=(255, 255, 255), width=10)
    caption(im, "All done", 220)
    im.save(OUT / "done.png", optimize=True)


def download():
    frames = []
    for i in range(12):
        im = backdrop((40, 20, 80), (10, 5, 25))
        d = ImageDraw.Draw(im)
        for k in range(12):
            a = (k / 12) * 2 * math.pi
            shade = 255 - ((i - k) % 12) * 18
            x, y = 100 + 55 * math.sin(a), 120 - 55 * math.cos(a)
            d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(shade, shade, 255))
        d.polygon([(88, 95), (112, 95), (112, 118), (126, 118), (100, 145), (74, 118), (88, 118)],
                  fill=(255, 255, 255))
        caption(im, "Downloading", 220)
        frames.append(im.convert("P", palette=Image.ADAPTIVE, colors=64))
    frames[0].save(OUT / "download.gif", save_all=True, append_images=frames[1:], duration=80, loop=0)


def ilbm(im, path, depth=5):
    """IFF ILBM, uncompressed, 2^depth colours."""
    p = im.convert("P", palette=Image.ADAPTIVE, colors=1 << depth)
    pal = (p.getpalette() or [])[: 3 * (1 << depth)]
    pal += [0] * (3 * (1 << depth) - len(pal))
    px = p.load()
    rowbytes = ((W + 15) // 16) * 2
    body = bytearray()
    for y in range(H):
        for plane in range(depth):
            row = bytearray(rowbytes)
            for x in range(W):
                if (px[x, y] >> plane) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            body += row
    bmhd = struct.pack(">HHhhBBBBHBBhh", W, H, 0, 0, depth, 0, 0, 0, 0, 10, 11, W, H)
    chunks = b"ILBM"
    for cid, data in ((b"BMHD", bmhd), (b"CMAP", bytes(pal)), (b"BODY", bytes(body))):
        chunks += cid + struct.pack(">I", len(data)) + data + (b"\0" if len(data) & 1 else b"")
    path.write_bytes(b"FORM" + struct.pack(">I", len(chunks)) + chunks)


def copying():
    im = backdrop((90, 50, 10), (30, 15, 5))
    d = ImageDraw.Draw(im)
    for k, x in enumerate((30, 70, 110)):
        y = 60 + k * 25
        d.rectangle((x, y, x + 60, y + 80), fill=(240, 240, 240), outline=(60, 60, 60), width=2)
        d.polygon([(x + 42, y), (x + 60, y + 18), (x + 42, y + 18)], fill=(180, 180, 180))
    caption(im, "Copying", 220)
    ilbm(im, OUT / "copying.iff")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    welcome()
    copying()
    download()
    done()
    for f in sorted(OUT.iterdir()):
        print(f.name, f.stat().st_size)
