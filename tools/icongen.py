#!/usr/bin/env python3
"""Rasterize monochrome app icons for the C-Ink renderer and the desktop app.

Icons come from Simple Icons (CC0, https://simpleicons.org): 24x24 single-path
SVGs. Marks Simple Icons no longer carries (Slack, the Microsoft apps,
Intercom, from simple-icons 9.21.0) and ones it never had (ONCE and Tavern,
supplied by their owner) live in tools/brand-icons and win over --icons. An
icon drawn with a stroke (fill="none") is rasterized as a stroke. The output
is committed, so building firmware never needs Python:

    npm pack simple-icons@16.37.0 && tar xzf simple-icons-16.37.0.tgz
    python3 tools/icongen.py --icons package/icons

writes firmware/components/cink_core/src/brands_data.inc (1-bit bitmaps at
the sizes the UI draws) and desktop/ui/icons/<slug>.svg.

Brand names and logos belong to their owners; they are used here only to
label where a card's information comes from.
"""
import argparse
import math
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw

# Brand id (as the firmware and digest name it) -> Simple Icons slug.
BRANDS = [
    ("github", "github"),
    ("gmail", "gmail"),
    ("gcal", "googlecalendar"),
    ("gdrive", "googledrive"),
    ("linear", "linear"),
    ("vercel", "vercel"),
    ("stripe", "stripe"),
    ("cloudflare", "cloudflare"),
    ("supabase", "supabase"),
    ("claude", "claude"),
    ("shopify", "shopify"),
    ("notion", "notion"),
    ("spotify", "spotify"),
    ("slack", "slack"),
    ("outlook", "microsoftoutlook"),
    ("teams", "microsoftteams"),
    ("m365", "microsoftoffice"),
    ("intercom", "intercom"),
    ("once", "once"),
    ("tavern", "tavern"),
]
SIZES = [18, 20, 24]
SS = 8  # supersampling

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TOKEN = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def arc_points(x1, y1, rx, ry, phi, fa, fs, x2, y2, n=24):
    """SVG elliptical arc to points (endpoint parameterization, F.6.5)."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1p ** 2) / (rx ** 2) + (y1p ** 2) / (ry ** 2)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if fa == fs:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not fs and dt > 0:
        dt -= 2 * math.pi
    elif fs and dt < 0:
        dt += 2 * math.pi
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return pts


def subpaths(d):
    """Flattens an SVG path into closed polygons (lists of points)."""
    toks = TOKEN.findall(d)
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    lcx = lcy = None  # last control point for S/T
    prev = ""
    out, cur = [], []

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def flag():
        # Arc flags may be packed without separators ("a1 1 0 011 1").
        nonlocal i
        t = toks[i]
        if len(t) > 1 and t[0] in "01" and not t.startswith(("0.", "1.")):
            toks[i] = t[1:]
            return int(t[0])
        i += 1
        return int(float(t))

    def cubic(p0, p1, p2, p3, n=16):
        return [((1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t ** 2 * p2[0] + t ** 3 * p3[0],
                 (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t ** 2 * p2[1] + t ** 3 * p3[1])
                for t in (k / n for k in range(1, n + 1))]

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        if c == "Z":
            if cur:
                out.append(cur)
            cur = []
            x, y = sx, sy
            lcx = lcy = None
            continue
        if c == "M":
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            if cur:
                out.append(cur)
            x, y, sx, sy = nx, ny, nx, ny
            cur = [(x, y)]
            cmd = "l" if rel else "L"
            lcx = lcy = None
        elif c == "L":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
            lcx = lcy = None
        elif c == "H":
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
            lcx = lcy = None
        elif c == "V":
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
            lcx = lcy = None
        elif c in "CS":
            if c == "C":
                x1, y1 = num(), num()
                if rel:
                    x1, y1 = x + x1, y + y1
            else:
                x1, y1 = (2 * x - lcx, 2 * y - lcy) if lcx is not None and prev in "CS" else (x, y)
            x2, y2, nx, ny = num(), num(), num(), num()
            if rel:
                x2, y2, nx, ny = x + x2, y + y2, x + nx, y + ny
            cur += cubic((x, y), (x1, y1), (x2, y2), (nx, ny))
            lcx, lcy = x2, y2
            x, y = nx, ny
        elif c in "QT":
            if c == "Q":
                qx, qy = num(), num()
                if rel:
                    qx, qy = x + qx, y + qy
            else:
                qx, qy = (2 * x - lcx, 2 * y - lcy) if lcx is not None and prev in "QT" else (x, y)
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            c1 = (x + 2 / 3 * (qx - x), y + 2 / 3 * (qy - y))
            c2 = (nx + 2 / 3 * (qx - nx), ny + 2 / 3 * (qy - ny))
            cur += cubic((x, y), c1, c2, (nx, ny))
            lcx, lcy = qx, qy
            x, y = nx, ny
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            fa, fs = flag(), flag()
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            cur += arc_points(x, y, rx, ry, phi, fa, fs, nx, ny)
            x, y = nx, ny
            lcx = lcy = None
        prev = c
    if cur:
        out.append(cur)
    return out


def rasterize(d, size, stroke=0.0):
    big = size * SS
    k = big / 24.0
    acc = Image.new("1", (big, big), 0)
    if stroke:
        draw = ImageDraw.Draw(acc)
        w = max(1, round(stroke * k))
        for poly in subpaths(d):
            pts = [(px * k, py * k) for px, py in poly]
            draw.line(pts + pts[:1], fill=1, width=w, joint="curve")
            for px, py in pts:  # round caps and joins
                draw.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=1)
        gray = acc.convert("L").resize((size, size), Image.BOX)
        return gray.point(lambda v: 255 if v >= 96 else 0).convert("1")
    for poly in subpaths(d):
        if len(poly) < 3:
            continue
        m = Image.new("1", (big, big), 0)
        ImageDraw.Draw(m).polygon([(px * k, py * k) for px, py in poly], fill=1)
        acc = ImageChops.logical_xor(acc, m)  # even-odd: holes cut out
    gray = acc.convert("L").resize((size, size), Image.BOX)
    return gray.point(lambda v: 255 if v >= 110 else 0).convert("1")


def pack(img):
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for bx in range((w + 7) // 8):
            b = 0
            for bit in range(8):
                xx = bx * 8 + bit
                if xx < w and px[xx, y]:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--icons", required=True, help="simple-icons package/icons directory")
    ap.add_argument("--preview", help="write a PNG sheet of every icon here")
    a = ap.parse_args()
    paths, strokes = {}, {}
    local = os.path.join(HERE, "brand-icons")
    for bid, slug in BRANDS:
        src = os.path.join(local, slug + ".svg")
        if not os.path.exists(src):
            src = os.path.join(a.icons, slug + ".svg")
        svg = open(src).read()
        m = re.search(r'<path d="([^"]+)"', svg)
        if not m:
            sys.exit(f"no path in {slug}.svg")
        paths[bid] = m.group(1)
        sw = re.search(r'stroke-width="([\d.]+)"', svg) if 'fill="none"' in svg else None
        strokes[bid] = float(sw.group(1)) if sw else 0.0
        os.makedirs(os.path.join(ROOT, "desktop/ui/icons"), exist_ok=True)
        with open(os.path.join(ROOT, "desktop/ui/icons", bid + ".svg"), "w") as f:
            if strokes[bid]:
                f.write('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
                        f'stroke-width="{sw.group(1)}" stroke-linecap="round" stroke-linejoin="round"><path d="{m.group(1)}"/></svg>\n')
            else:
                f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="{m.group(1)}"/></svg>\n')

    lines = ["// Generated by tools/icongen.py from Simple Icons (CC0). Do not edit.",
             "// Brand marks belong to their owners; used only to label a card's source.", ""]
    sheet = Image.new("1", (len(BRANDS) * 28 + 4, len(SIZES) * 28 + 4), 1)
    for si, s in enumerate(SIZES):
        for bi, (bid, _) in enumerate(BRANDS):
            img = rasterize(paths[bid], s, strokes[bid])
            data = pack(img)
            lines.append(f"static const uint8_t kBrand_{bid}_{s}[] = {{{', '.join(str(v) for v in data)}}};")
            sheet.paste(ImageChops.invert(img), (bi * 28 + 2, si * 28 + 2))
    lines.append("")
    lines.append("static const BrandBitmap kBrandBitmaps[] = {")
    for s in SIZES:
        for bid, _ in BRANDS:
            lines.append(f"  {{\"{bid}\", {s}, kBrand_{bid}_{s}}},")
    lines.append("};")
    out = os.path.join(ROOT, "firmware/components/cink_core/src/brands_data.inc")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    if a.preview:
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(a.preview)
    print(f"wrote {out} ({len(BRANDS)} brands x {len(SIZES)} sizes)")


if __name__ == "__main__":
    main()
