#!/usr/bin/env python3
"""Makes the Top story photo for the simulator fixtures.

Draws an invented scene (an electric ferry crossing at dusk; no real photo,
no one's likeness), then crops and dithers it exactly as the bridge does
(desktop/bridge/src/photo.rs) and writes sim/fixtures/story_img.json, which
make_fixtures.py attaches to the sample digest. Pass a picture of your own
to see how it would come out on the panel:

    python3 tools/story_photo.py [photo.jpg] [--mono] [--preview out.png]
"""
import argparse
import base64
import json
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 216, 120
PAPER, INK, ACCENT = (236, 233, 224), (34, 34, 38), (200, 104, 72)


def scene(w=864, h=480):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    horizon = int(h * 0.58)
    for y in range(horizon):  # dusk sky: deep blue down to a hot orange band
        t = y / horizon
        c = [int(a + (b - a) * t ** 1.6) for a, b in zip((40, 52, 92), (246, 150, 92))]
        d.line([(0, y), (w, y)], fill=tuple(c))
    d.ellipse([w * 0.62, horizon - 70, w * 0.62 + 120, horizon + 50], fill=(255, 214, 150))
    rnd = random.Random(7)
    x = 0
    while x < w:  # skyline
        bw = rnd.randint(28, 70)
        bh = rnd.randint(30, 150) if abs(x - w * 0.3) < 160 else rnd.randint(18, 70)
        d.rectangle([x, horizon - bh, x + bw, horizon], fill=(40, 36, 48))
        x += bw + rnd.randint(0, 6)
    for y in range(horizon, h):  # water, with the sun's streak
        t = (y - horizon) / (h - horizon)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip((170, 104, 92), (30, 40, 66))))
        if (y // 3) % 2 == 0:
            sx = w * 0.62 + 60 + math.sin(y * 0.7) * 16
            half = 70 * (1 - t) + 6
            d.line([(sx - half, y), (sx + half, y)], fill=(250, 190, 128))
    # the ferry
    by = horizon + 72
    d.polygon([(150, by), (520, by), (488, by + 46), (182, by + 46)], fill=(238, 236, 228))
    d.rectangle([210, by - 44, 450, by], fill=(232, 230, 222))
    d.rectangle([250, by - 74, 400, by - 44], fill=(226, 224, 216))
    for i in range(9):
        d.rectangle([222 + i * 25, by - 34, 238 + i * 25, by - 16], fill=(60, 70, 96))
    d.rectangle([182, by + 30, 488, by + 46], fill=(206, 92, 60))
    d.line([(326, by - 74), (326, by - 112)], fill=(40, 40, 44), width=5)
    return img.filter(ImageFilter.GaussianBlur(1.2))


def frame(img):
    s = max(W / img.width, H / img.height)
    img = img.resize((max(W, round(img.width * s)), max(H, round(img.height * s))), Image.BILINEAR)
    l, t = (img.width - W) // 2, (img.height - H) // 2
    return img.crop((l, t, l + W, t + H)).convert("RGB")


def dither(img, mono=False):
    pal = [PAPER, INK] if mono else [PAPER, INK, ACCENT]
    raw = img.tobytes()
    px = [[max(0.0, min(255.0, (raw[i + k] - 128) * 1.15 + 136)) for k in range(3)] for i in range(0, len(raw), 3)]
    black, accent = bytearray(W // 8 * H), bytearray(W // 8 * H)
    for y in range(H):
        for x in range(W):
            old = px[y * W + x]
            best = min(range(len(pal)), key=lambda i: 0.30 * (old[0] - pal[i][0]) ** 2
                       + 0.59 * (old[1] - pal[i][1]) ** 2 + 0.11 * (old[2] - pal[i][2]) ** 2)
            new = pal[best]
            bit, at = 0x80 >> (x % 8), y * (W // 8) + x // 8
            if best == 1:
                black[at] |= bit
            elif best == 2:
                accent[at] |= bit
            err = [o - n for o, n in zip(old, new)]
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and ny < H:
                    p = px[ny * W + nx]
                    for k in range(3):
                        p[k] += err[k] * f
    return bytes(black), bytes(accent)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photo", nargs="?")
    ap.add_argument("--mono", action="store_true")
    ap.add_argument("--preview")
    a = ap.parse_args()
    src = Image.open(a.photo) if a.photo else scene()
    k, acc = dither(frame(src), a.mono)
    out = {"w": W, "h": H, "k": base64.b64encode(k).decode()}
    if any(acc):
        out["a"] = base64.b64encode(acc).decode()
    if a.preview:
        p = Image.new("RGB", (W, H), PAPER)
        for y in range(H):
            for x in range(W):
                bit, at = 0x80 >> (x % 8), y * (W // 8) + x // 8
                if acc[at] & bit:
                    p.putpixel((x, y), ACCENT)
                elif k[at] & bit:
                    p.putpixel((x, y), INK)
        p.resize((W * 2, H * 2), Image.NEAREST).save(a.preview)
    if not a.photo:
        with open(os.path.join(ROOT, "sim/fixtures/story_img.json"), "w") as f:
            json.dump(out, f)


if __name__ == "__main__":
    main()
