#!/usr/bin/env python3
"""Draws Clawd's poses and writes them for the renderer.

Clawd is the 12 x 8 pixel crab from Claude Code's art (the shape Jake traced
from Anthropic's own Clawd pictures): a block body, arms out at the sides,
four legs, solid black eyes. Each pose is composed here from that body, eye
shapes and small props, on a 40 x 24 grid of half pixels (4 pixels free
either side and above the head for hats and marks), and packed 2 bits a
half pixel into firmware/components/cink_core/src/clawd_data.inc.

    python3 tools/clawdgen.py [--preview sheet.png]
"""
import argparse
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GW, GH = 40, 24          # half-pixel grid
BX, BY = 8, 8            # where body pixel (0, 0) starts, in half pixels
T, B, K, Wh = 0, 1, 2, 3  # transparent, body (accent), black, white

# Bodies: 12 wide, rows -2..7 (two rows above the head for raised arms).
# '#' body, 'E' an eye spot (body, the eye is drawn on top), '.' nothing.
BODIES = {
    "front": [
        "............",
        "............",
        "..########..",
        "..#E####E#..",
        "############",
        "############",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "up": [
        "##........##",
        "##........##",
        "############",
        "###E####E###",
        "..########..",
        "..########..",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "one": [  # right arm raised to the shoulder line, holding something up
        "..........##",
        "..........##",
        "..##########",
        "..#E####E#..",
        "##########..",
        "##########..",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "side": [  # turned to the left: the near arm is the snout, the far one hidden
        "............",
        "............",
        "..########..",
        "..#E##E###..",
        "..########..",
        "##########..",
        "##########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "side_up": [  # turned left, far arm up (holding a flag)
        "............",
        "........##..",
        "..########..",
        "..#E##E###..",
        "..########..",
        "##########..",
        "##########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "back": [  # turned away, heads down
        "............",
        "............",
        "..########..",
        "..########..",
        "############",
        "############",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#.#..",
    ],
    "walk": [  # side on, mid-step
        "............",
        "............",
        "..########..",
        "..#E##E###..",
        "..########..",
        "##########..",
        "##########..",
        "..########..",
        "..#.#..#.#..",
        ".#...#..#..#",
    ],
    "tap": [  # tapping a foot
        "............",
        "............",
        "..########..",
        "..#E####E#..",
        "############",
        "############",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "..#.#..#....",
    ],
    "short": [  # floating: legs tucked to stubs
        "............",
        "............",
        "..########..",
        "..#E####E#..",
        "############",
        "############",
        "..########..",
        "..########..",
        "..#.#..#.#..",
        "............",
    ],
}

# Eyes, drawn in each 2 x 2 eye spot (half pixels), left eye then right.
EYES = {
    "open": ["KK", "KK"],
    "left": ["K.", "K."],     # pupils to the left side of the spot... kept solid below
    "closed": ["..", "KK"],
    "happy": [".K", "K."],    # a tiny ^ (drawn mirrored for the right eye)
    "none": ["..", ".."],
}


class Frame:
    def __init__(self):
        self.px = [[T] * GW for _ in range(GH)]

    def put(self, x, y, v):
        if 0 <= x < GW and 0 <= y < GH and v is not None:
            self.px[y][x] = v

    def block(self, bx, by, v):  # one body pixel (2 x 2 half pixels), body coords
        for dy in range(2):
            for dx in range(2):
                self.put(BX + 2 * bx + dx, BY + 2 * by + dy, v)

    def art(self, hx, hy, rows):
        """Half-pixel art at half-pixel coords. k black, w white, # body, x erase."""
        m = {"k": K, "w": Wh, "#": B, "x": T}
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in m:
                    self.put(hx + i, hy + j, m[ch])


def pose(body="front", eyes="open", lift=0, eye_shift=0, legs=True):
    f = Frame()
    spots = []
    rows = BODIES[body]
    for r, row in enumerate(rows):
        by = r - 2 + lift
        if not legs and r >= 8:
            continue
        for c, ch in enumerate(row):
            if ch in "#E":
                f.block(c, by, B)
            if ch == "E":
                spots.append((c, by))
    # Eyes: solid black squares, or shapes, inside their spots.
    for i, (c, r) in enumerate(spots):
        hx, hy = BX + 2 * c + eye_shift, BY + 2 * r
        e = eyes if not isinstance(eyes, tuple) else eyes[i]
        if e == "open":
            f.art(hx, hy, ["kk", "kk"])
        elif e == "closed":
            f.art(hx, hy, ["##", "kk"])
        elif e == "happy":
            f.art(hx - 1, hy, ["#kk#", "k##k"])
        elif e == "wide":
            f.art(hx, hy - 1, ["kk", "kk", "kk"])
    return f


def body_frame(**kw):
    return pose(**kw)


# ---------------------------------------------------------------- marks
def bang(f, x=33, y=0):
    f.art(x, y, ["kk", "kk", "kk", "kk", "..", "kk"])

def question(f, x=32, y=0):
    f.art(x, y, [".kkk.", "k...k", "...k.", "..k..", ".....", "..k.."])

def zz(f):
    f.art(30, 3, ["kkk", ".k.", "kkk"])
    f.art(34, 0, ["kkkk", "..k.", ".k..", "kkkk"])

def dots(f):
    for i in range(3):
        f.art(29 + i * 3, 4, ["kk", "kk"])

def bubbles(f):
    f.art(30, 5, ["kk", "kk"])
    f.art(33, 2, [".kk.", "k..k", ".kk."])
    f.art(36, 0, ["kk", "kk"])

def heart(f):
    f.art(31, 0, [".kk.kk.", "kkkkkkk", "kkkkkkk", ".kkkkk.", "..kkk..", "...k..."])

def drop(f):
    f.art(5, 3, [".k.", ".k.", "kkk", "kkk", ".k."])

def sparkle(f, x=32, y=0):
    f.art(x, y, ["..k..", "..k..", "kkkkk", "..k..", "..k.."])

def rain(f):
    for i in range(4):
        f.art(9 + i * 7, i % 2, [".k", ".k", "k.", "k."])

def shiver(f):
    for x in (3, 35):
        f.art(x, 11, ["k.", ".k", "k.", ".k", "k."])

def note(f):
    f.art(32, 0, ["..kkk", "..k.k", "..k.k", "kkk..", "kkk.."])

def steam(f):
    f.art(34, 0, ["k..k", ".k..k", "k..k"])

def confetti(f):
    for x, y, v in ((2, 2, K), (9, 0, B), (14, 4, K), (21, 1, B), (27, 3, K), (33, 0, B), (37, 4, K), (5, 7, B),
                    (36, 9, B), (1, 12, K)):
        f.art(x, y, ["kk"] if v == K else ["##"])

def poof(f):
    f.art(3, 21, ["k.k", ".k."])
    f.art(35, 21, ["k.k", ".k."])

def shadow(f):
    f.art(12, 23, ["k.k.k.k.k.k.k.k"])


# ---------------------------------------------------------------- props
def shades(f, lift=0):
    y = BY + 2 * (1 + lift)
    f.art(BX + 4, y, ["kkkkkkkkkkkkkkkk", "kkkk.......kkkkk"[:16]])
    f.art(BX + 4, y, ["kkkkkkkkkkkkkkkk", "kkkkk######kkkkk", ".kkk########kkk."])

def paper(f):
    f.art(BX - 2, BY + 4, [
        "kkkkkkkkkkkkkkkkkkkk",
        "kwwwwwwwwwkwwwwwwwwk",
        "kwkkkkkwwwkwkkkkkwwk",
        "kwwwwwwwwwkwwwwwwwwk",
        "kwkkkkkkwwkwkkkkkkwk",
        "kwwwwwwwwwkwwwwwwwwk",
        "kwkkkkkwwwkwkkkkkwwk",
        "kwwwwwwwwwkwwwwwwwwk",
        "kkkkkkkkkkkkkkkkkkkk",
    ])

def mug(f):
    f.art(31, 0, ["k.k.", ".k.k", "k.k."])
    f.art(30, 4, ["kkkkk.", "kwwwkkk", "kwwwk.k", "kwwwkkk", "kkkkk.."])

def laptop(f):
    f.art(BX + 2, BY + 8, [
        "kkkkkkkkkkkkkkkkkkkk",
        "kwwwwwwwwwwwwwwwwwwk",
        "kwwwwwwwkkkkwwwwwwwk",
        "kwwwwwwwwwwwwwwwwwwk",
        "kwwwwwwwwwwwwwwwwwwk",
        "kkkkkkkkkkkkkkkkkkkk",
        "kkkkkkkkkkkkkkkkkkkkkk"[:20],
    ])

def speed(f):
    for i, w in enumerate((4, 6, 4)):
        f.art(33 + (6 - w), 11 + i * 4, ["k" * w])

def hard_hat(f):
    f.art(BX + 4, BY - 5, [
        "....kkkkkkkk....",
        "..kkkkkwwkkkkk..",
        ".kkkkkkwwkkkkkk.",
        ".kkkkkkwwkkkkkk.",
        "kkkkkkkkkkkkkkkkkk"[:18],
    ])

def wrench(f):
    f.art(32, 1, ["k.k", "k.k", "kkk", ".k.", ".k.", ".k.", ".k."])

def wizard_hat(f):
    f.art(BX + 3, BY - 8, [
        "..........kk......",
        ".........kkkk.....",
        "........kkwkk.....",
        ".......kkkkkkk....",
        "......kkwkkkkkk...",
        ".....kkkkkkkwkk...",
        "....kkkwkkkkkkkk..",
        "..kkkkkkkkkkkkkkkk",
    ])

def wand(f):
    f.art(33, 2, ["..w", ".k.", "k.."])
    sparkle(f, 34, 0)

def fedora(f, lift=0):
    y = BY - 4 + 2 * lift
    f.art(BX + 3, y, [
        "...kkkkkkkkkk...",
        "...kkkkkkkkkk...",
        "...##########...",
        "kkkkkkkkkkkkkkkk",
    ])

def monocle(f):
    # A magnifier on the front eye (body pixel 3, row 1), handle down and out.
    hx, hy = BX + 2 * 3, BY + 2 * 1
    f.art(hx - 2, hy - 2, [".kkkk.", "kwwwwk", "kwkkwk", "kwkkwk", "kwwwwk", ".kkkk."])
    f.art(hx - 4, hy + 4, ["..k", ".k.", "k.."])

def skateboard(f):
    f.art(BX - 2, 19, ["kkkkkkkkkkkkkkkkkkkkkkkkkkkk"])
    f.art(BX - 3, 18, ["k"])
    f.art(BX + 24, 18, ["k"])
    f.art(BX + 1, 21, ["kkk", "kwk", "kkk"][:3])
    f.art(BX + 18, 21, ["kkk", "kwk", "kkk"][:3])

def flag(f):
    f.art(BX + 17, 0, [
        "kwkwkwk",
        "kkwkwkw",
        "kwkwkwk",
        "kkwkwkw",
        "k......",
        "k......",
    ])

def headband(f, lift=0):
    f.art(BX + 4, BY + 2 * (0 + lift) + 1, ["wwwwwwwwwwwwwwww"])
    f.art(BX + 4, BY + 2 * (0 + lift), ["kkkkkkkkkkkkkkkk"])

def dumbbells(f):
    for x in (BX - 3, BX + 19):
        f.art(x, 2, ["kk..kk", "kkkkkk", "kk..kk"])

def float_lines(f):
    f.art(BX + 5, 21, ["k.k.k.k.k.k.k"])


def build():
    P = {}
    P["calm"] = pose()
    P["blink"] = pose(eyes="closed")
    P["look-left"] = pose(eye_shift=-1)
    P["look-right"] = pose(eye_shift=1)
    P["wink"] = pose(eyes=("open", "closed"))
    P["happy"] = pose("up", eyes="happy")
    f = pose("one"); f.art(32, 0, ["k...", ".k..", "..k."]); f.art(36, 2, ["k", "k"]); P["wave"] = f
    f = pose("up", eyes="wide"); bang(f); P["alert"] = f
    f = pose("up", eyes="wide", lift=-2); poof(f); P["surprised"] = f
    f = pose(eye_shift=-1); question(f); P["puzzled"] = f
    f = pose(eyes="closed", lift=2, legs=False); zz(f); P["sleepy"] = f
    f = pose("tap", eye_shift=1); dots(f); P["busy"] = f
    f = pose(eye_shift=-1); bubbles(f); P["thinking"] = f
    P["focused"] = pose("back", eyes="none")
    f = pose("up", eyes="happy"); heart(f); P["love"] = f
    f = pose(); drop(f); P["sweat"] = f
    f = pose("up", eyes="happy"); sparkle(f); P["proud"] = f
    f = pose(); shades(f); P["cool"] = f
    f = pose(eyes="closed"); rain(f); P["rainy"] = f
    f = pose("up", eyes="closed", lift=1); shiver(f); P["cold"] = f
    f = pose("up"); paper(f); P["reading"] = f
    f = pose("one"); mug(f); P["coffee"] = f
    f = pose("walk"); speed(f); P["walking"] = f
    f = pose("tap", eyes="closed"); note(f); P["music"] = f
    f = pose(); laptop(f); P["typing"] = f
    f = pose("up", eyes="happy"); confetti(f); P["celebrate"] = f
    f = pose(eyes="closed", lift=2, legs=False); poof(f); P["boop"] = f
    f = pose("up", lift=-2); shadow(f); f.art(33, 0, [".kk.", "k##k", "k##k", ".kk."]); P["coin-hop"] = f
    P["crouch"] = pose(eye_shift=1, lift=2, legs=False)
    P["peek"] = pose("side")
    # Added from Jake's references.
    f = pose("one"); hard_hat(f); wrench(f); P["builder"] = f
    f = pose("one"); wizard_hat(f); wand(f); P["wizard"] = f
    f = pose("side", eyes=("open", "closed")); fedora(f); monocle(f); P["detective"] = f
    f = pose(lift=-2); skateboard(f); P["skate"] = f
    f = pose("side_up", eyes="happy"); flag(f); P["flag"] = f
    f = pose("up"); headband(f); dumbbells(f); P["lift"] = f
    f = pose("short", lift=-1); float_lines(f); P["float"] = f
    return P


ORDER = ["calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised", "puzzled",
         "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool", "rainy", "cold",
         "reading", "coffee", "walking", "music", "typing", "celebrate", "boop", "coin-hop", "crouch", "peek",
         "builder", "wizard", "detective", "skate", "flag", "lift", "float"]


def pack(f):
    out = []
    for y in range(GH):
        for x in range(0, GW, 4):
            b = 0
            for i in range(4):
                b |= f.px[y][x + i] << (6 - 2 * i)
            out.append(b)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    a = ap.parse_args()
    P = build()
    assert set(P) == set(ORDER), set(P) ^ set(ORDER)
    lines = ["// Generated by tools/clawdgen.py. Clawd's poses: 40 x 24 half pixels, 2 bits each",
             "// (0 none, 1 body, 2 black, 3 white), MSB first, rows top to bottom.",
             f"static const uint8_t kClawdFrames[{len(ORDER)}][{GW * GH // 4}] = {{"]
    for name in ORDER:
        lines.append(f"    /* {name} */ {{" + ", ".join(str(b) for b in pack(P[name])) + "},")
    lines.append("};")
    with open(os.path.join(ROOT, "firmware/components/cink_core/src/clawd_data.inc"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    if a.preview:
        from PIL import Image, ImageDraw
        s, cols = 6, 6
        cw, ch = GW * s // 2 + 20, GH * s // 2 + 34
        img = Image.new("RGB", (cols * cw, ((len(ORDER) + cols - 1) // cols) * ch), (236, 233, 224))
        d = ImageDraw.Draw(img)
        col = {B: (200, 104, 72), K: (34, 34, 38), Wh: (255, 255, 255)}
        for n, name in enumerate(ORDER):
            ox, oy = (n % cols) * cw + 10, (n // cols) * ch + 6
            for y in range(GH):
                for x in range(GW):
                    v = P[name].px[y][x]
                    if v:
                        d.rectangle([ox + x * s // 2, oy + y * s // 2, ox + (x + 1) * s // 2 - 1, oy + (y + 1) * s // 2 - 1], fill=col[v])
            d.text((ox, oy + GH * s // 2 + 6), name, fill=(34, 34, 38))
        img.save(a.preview)


if __name__ == "__main__":
    main()
