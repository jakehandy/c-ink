#!/usr/bin/env python3
"""Draws Clawd's poses and writes them for the renderer.

Clawd is the pixel crab from Claude Code's art. Every pose here is built on
one grid of half pixels (one Clawd pixel is 2 x 2 of them):

    body   16 x 12, eyes 2 x 2, two in from each side and two down
    arms   4 x 4 at the sides, four rows down
    legs   2 wide and 4 long at 0, 4, 10 and 14 from the body's left edge

The standing Clawd, the raised arm, the side view with its lowered arm, the
happy eyes, the hard hat and wrench, the wizard hat, the fedora and
magnifier, the skateboard, the checkered flag, the dumbbells, the float, the
confetti jump, the crawl and the laptop are traced pixel for pixel from Anthropic's own
Clawd pictures. The other poses
reuse those same parts and add a small mark.

Frames are 40 x 32 half pixels (8 free either side of the 24 x 16 figure and
16 above for hats, flags and marks), packed 4 bits a half pixel into
firmware/components/cink_core/src/clawd_data.inc:

    0 nothing   1 body      2 black   3 white
    4 shade (the far side in three-quarter views)
    5 gray      6 light (the hard hat's yellow)
    7 black drawn on the body (eyes, glasses): white on a black-only panel

    python3 tools/clawdgen.py [--preview sheet.png]
"""
import argparse
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GW, GH = 40, 32          # frame, in half pixels
BX, BY = 8, 16           # where the figure's 24 x 16 box starts in the frame
N, B, K, Wh, S, G, L, E = range(8)
ON_BODY = (B, S, L, E)

CH = {"#": B, "k": K, "K": E, "w": Wh, "d": S, "g": G, "o": L, "x": N}


class Frame:
    """Coordinates are half pixels relative to the figure's box: the standing
    body is x 4..19, y 0..11, its feet reach y 15."""

    def __init__(self):
        self.px = [[N] * GW for _ in range(GH)]

    def get(self, x, y):
        return self.px[BY + y][BX + x]

    def put(self, x, y, v):
        cx, cy = BX + x, BY + y
        assert 0 <= cx < GW and 0 <= cy < GH, (x, y)
        if v == K and self.px[cy][cx] in ON_BODY:
            v = E
        self.px[cy][cx] = v

    def rect(self, x, y, w, h, v=B):
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, v)

    def art(self, x, y, rows):
        """'#' body, 'd' shade, 'k' black, 'K' black on the body, 'w' white,
        'g' gray, 'o' light, 'x' erase, anything else leaves the pixel."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in CH:
                    self.put(x + i, y + j, CH[ch])

    def mirrored(self):
        """Flipped left to right about the figure's box."""
        f = Frame()
        for cy in range(GH):
            for cx in range(GW):
                mx = 2 * BX + 23 - cx
                if 0 <= mx < GW:
                    f.px[cy][mx] = self.px[cy][cx]
                else:
                    assert self.px[cy][cx] == N
        return f


# ------------------------------------------------------------------ the body
ARM_Y = {"mid": 4, "dip": 5, "low": 6, "high": 2, "up": -1}
LEG_X = (4, 8, 14, 18)


def eye(f, x, y, kind):
    if kind == "open":
        f.art(x, y, ["KK", "KK"])
    elif kind == "closed":
        f.art(x, y + 1, ["KK"])
    elif kind == "happy":
        f.art(x - 1, y, [".KK.", "K..K"])


def body(dy=0, left="mid", right="mid", eyes="open", look=0, legs=(4, 4, 4, 4), arm_w=4):
    """The standing Clawd. dy moves him (negative is up); left / right pick an
    arm height from ARM_Y or None; eyes is one kind or a (left, right) pair."""
    f = Frame()
    f.rect(4, dy, 16, 12)
    if left:
        f.rect(4 - arm_w, dy + ARM_Y[left], arm_w, 4)
    if right:
        f.rect(20, dy + ARM_Y[right], arm_w, 4)
    for x, n in zip(LEG_X, legs):
        f.rect(x, dy + 12, 2, n)
    kinds = eyes if isinstance(eyes, tuple) else (eyes, eyes)
    eye(f, 6 + look, dy + 2, kinds[0])
    eye(f, 16 + look, dy + 2, kinds[1])
    return f


# Three-quarter view, walking to the right, as traced. The far side is shaded.
CRAWL = [
    "...dddd############.....",
    "...dddd############.....",
    "...dddd############.....",
    "...dddd##KK######KK.....",
    "...dddd##KK######KK.....",
    "...dddd###############..",
    "...dddd###############..",
    "...dddd###############..",
    "...dddd###############..",
    "...dddd############.....",
    "...dddd############.....",
    "...dd..##....##..##.....",
    "..ddd.###...###.###.....",
    "..dd..##....##..##......",
]


def crawl():
    f = Frame()
    f.art(0, 2, CRAWL)
    return f


# ------------------------------------------------------------------- marks
def bang(f, x=26, y=-8):
    f.art(x, y, ["kk", "kk", "kk", "kk", "..", "kk"])

def question(f, x=25, y=-8):
    f.art(x, y, [".kkk.", "k...k", "...k.", "..k..", ".....", "..k.."])

def zz(f, x=22, y=-4):
    f.art(x, y + 3, ["kkk", ".k.", "kkk"])
    f.art(x + 4, y, ["kkkk", "..k.", ".k..", "kkkk"])

def dots(f, x=22, y=-4):
    for i in range(3):
        f.art(x + i * 3, y, ["kk", "kk"])

def bubbles(f):
    f.art(22, -3, ["kk", "kk"])
    f.art(25, -7, [".kk.", "k..k", "k..k", ".kk."])

def heart(f, x=23, y=-8):
    f.art(x, y, [".kk.kk.", "kkkkkkk", "kkkkkkk", ".kkkkk.", "..kkk..", "...k..."])

def drop(f):
    f.art(0, -3, [".k.", ".k.", "kkk", "kkk", ".k."])

def sparkle(f, x=25, y=-8):
    f.art(x, y, ["..k..", "..k..", "kkkkk", "..k..", "..k.."])

def rain(f):
    for i in range(4):
        f.art(1 + i * 7, -8 + i % 2, [".k", ".k", "k.", "k."])

def shiver(f):
    for x in (-4, 26):
        f.art(x, 2, ["k.", ".k", "k.", ".k", "k.", ".k"])

def note(f, x=24, y=-9):
    f.art(x, y, ["..kkk", "..k.k", "..k.k", "kkk..", "kkk.."])

def waves(f):
    f.art(25, -4, ["k.", ".k", ".k", ".k", "k."])
    f.art(28, -5, ["k.", ".k", ".k", ".k", ".k", ".k", "k."])

def poof(f):
    f.art(-5, 13, ["k.k", ".k."])
    f.art(26, 13, ["k.k", ".k."])

def shadow(f):
    f.art(4, 15, ["k.k.k.k.k.k.k.k."])

def speed(f):
    for i, w in enumerate((4, 6, 4)):
        f.art(26, 4 + i * 4, ["k" * w])

def coin(f):
    f.art(25, -8, [".kk.", "k##k", "k##k", ".kk."])

def confetti(f):
    # Scattered as in the reference; its pinks and blues become black, gray and body.
    for x, y, rows in ((-6, -15, ["k", "k"]), (-3, -9, ["##"]), (4, -16, ["#", "#"]), (5, -12, ["k.", ".k"]),
                       (9, -10, ["g"]), (8, -7, ["g", "g"]), (-6, -6, ["g.", ".g"]), (17, -13, ["g.", ".g"]),
                       (20, -16, ["##"]), (20, -14, ["k.", ".k"]), (25, -16, ["kk"]), (23, -11, ["##"]),
                       (18, -9, ["gg"]), (26, -8, ["g.", ".g"])):
        f.art(x, y, rows)


# ------------------------------------------------------------------- props
def shades(f):
    f.art(4, 2, ["kkkkkkkkkkkkkkkk", ".kkkkk....kkkkk.", "..kkk......kkk.."])

def paper(f):
    f.art(2, 5, [
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
    f.art(22, -8, ["k.k.", ".k.k", "k.k."])
    f.art(21, -4, ["kkkkk..", "kwwwkkk", "kwwwk.k", "kwwwkkk", "kkkkk.."])

# Traced: the yellow shell is light, its ribs, badge and band are body colour.
HARD_HAT = [
    "......oooooo......",
    "....o#oooooo#o....",
    "..ooo#oo##oo#ooo..",
    "..ooo#o####o#ooo..",
    ".oooo#o####o#oooo.",
    ".oooo#oo##oo#oooo.",
    ".ooooo#oooo#ooooo.",
    ".################.",
    "oooooooooooooooooo",
]
WRENCH = [
    ".g...g.",
    "gg...gg",
    "gg...gg",
    "ggg.ggg",
    ".ggggg.",
    "..ggg..",
    "..ggg..",
    "..ggg..",
    "..ggg..",
    "...gg..",
    "...gg..",
    "...g...",
]
BOLT = ["kkkk", "kkkk", ".kk.", ".kk.", ".kk."]

def hard_hat(f):
    f.art(3, -8, HARD_HAT)

def wrench(f):
    f.art(21, -7, WRENCH)
    f.art(25, 10, BOLT)

# Traced: purple becomes black, the darker band body colour, the stars white.
WIZARD_HAT = [
    "......k.............",
    ".....kk.............",
    ".....kk.............",
    ".....kkk............",
    "....kkkk............",
    "....kkwkkkk.........",
    ".....kkkkwkkkk......",
    ".....kkkkkkkkwk.....",
    ".....kwkkkkkkkkk....",
    ".....kkkkkwkkkkk....",
    "....kkkkkkkkkkkkk...",
    "...kkkwkkkkkkwkkk...",
    "..################..",
    "..################..",
    ".kkwkkkkkkkwkkkkkkk.",
    "kkkkkkkwkkkkkkkwkkkk",
    "kk................kk",
]

def wizard_hat(f):
    f.art(2, -16, WIZARD_HAT)

# The fedora and magnifier, with the three-quarter body they were drawn on.
DETECTIVE = [
    ".......kkkkk.kkkk.......",
    "......kkkkkkkkkkkk......",
    "......kkkkkkkkkkkk......",
    ".....kkkkkkkkkkkkkk.....",
    ".....##############.....",
    ".....##############.....",
    "...kkkkkkkkkkkkkkkkkk...",
    "..kkkkkkkkkkkkkkkkkkkk..",
    "....d##########K####K...",
    "....d#########K#####wK..",
    "dd############K##KKKwK..",
    "dd######KKK###K##KKKwK..",
    "dd############K##KKKwK..",
    "dd#############K####KK#.",
    "....d###########KKKKKKK.",
    "....d################kkk",
    "...dd#################kk",
    "...dd##################.",
    "...dd..##....##..##.....",
    "..ddd.###...###.###.....",
    "..dd..##....##..##......",
    "..dd..##....##..##......",
]

# Three-quarter view on a skateboard, legs trailing.
SKATE = [
    "....ddd########.........",
    "....ddd#############....",
    "....ddd#############....",
    "ddd#################....",
    "ddd######KK######KK####.",
    "ddd######KK######KK####.",
    "ddd####################.",
    "....ddd################.",
    "....ddd#############....",
    "....ddd#############....",
    "....ddd#############....",
    "...dddd#############....",
    "...dd...##....#######...",
    "...ddd..##......##.##...",
    "....dd...##.....##.##...",
    ".........##......##.##..",
    "..kk.............##.##..",
    "...kk...................",
    "....kkkkkkkkkk.......kkk",
    "......oo.....kkkkkkkkk..",
    ".....ooko.........oo....",
    ".....okoo........ooko...",
    "......oo.........okoo...",
    "..................oo....",
]

FLAG = [
    "...kwk",
    "..kwkw",
    ".kwkwk",
    "kwkwkw",
    "wkwkwk",
    "kwkwk.",
    "wkw...",
    "kwk...",
    ".k....",
]
WEIGHT = [".kk.", "kkkk", "kkkk", "kkkk", ".kk."]

# Mid-jump, tilted, one arm thrown up.
JUMP = [
    ".....................##.",
    "....................###.",
    "............###########.",
    "........###############.",
    "....###################.",
    "....############KK####..",
    "....############KK##....",
    "....##KK############....",
    "....##KK############....",
    "....################....",
    "....################....",
    "####################....",
    "####################....",
    "####################....",
    "####################....",
    "....########..##..##....",
    "....##..##....##..##....",
    "....##..##....##..##....",
    "....##..##....##..##....",
    "....##..##....##..##....",
    "....##..##..............",
]


def build():
    P = {}
    P["calm"] = body()
    P["blink"] = body(eyes="closed")
    P["look-left"] = body(look=-1)
    P["look-right"] = body(look=1)
    P["wink"] = body(eyes=("open", "closed"))
    P["happy"] = body(left="up", right="up", eyes="happy")
    f = body(left="dip", right="up"); waves(f); P["wave"] = f
    f = body(left="up", right="up"); bang(f); P["alert"] = f
    f = body(dy=-4, left="up", right="up"); bang(f, 26, -14); poof(f); P["surprised"] = f
    f = body(look=-1); question(f); P["puzzled"] = f
    f = body(dy=4, eyes="closed", legs=(0, 0, 0, 0)); zz(f); P["sleepy"] = f
    f = body(look=1, legs=(4, 4, 4, 2)); dots(f); P["busy"] = f
    f = body(look=1); bubbles(f); P["thinking"] = f
    P["focused"] = body(eyes="none")
    f = body(left="up", right="up", eyes="happy"); heart(f); P["love"] = f
    f = body(); drop(f); P["sweat"] = f
    f = body(left="up", right="up", eyes="happy"); sparkle(f); P["proud"] = f
    f = body(); shades(f); P["cool"] = f
    f = body(eyes="closed"); rain(f); P["rainy"] = f
    f = body(eyes="closed"); shiver(f); P["cold"] = f
    f = body(); paper(f); P["reading"] = f
    f = body(right="high"); mug(f); P["coffee"] = f
    f = crawl().mirrored(); speed(f); P["walking"] = f
    f = body(eyes="closed", legs=(4, 4, 4, 2)); note(f); P["music"] = f
    # Typing, as traced: three-quarter view, hunched over a laptop with its cable trailing.
    f = crawl()
    f.rect(19, 12, 4, 3, S)
    f.art(20, 10, [".........g", "........gg", ".......gg.", "......gg..", ".....gg...", "gggggg...."])
    P["typing"] = f.mirrored()

    f = Frame(); f.art(0, -5, JUMP); confetti(f); P["celebrate"] = f
    f = body(dy=2, eyes="closed", legs=(2, 2, 2, 2)); poof(f); P["boop"] = f

    f = body(dy=-4, left="up", right="up"); shadow(f); coin(f); P["coin-hop"] = f
    P["crouch"] = body(dy=2, look=1, legs=(2, 2, 2, 2))
    P["peek"] = body(left="low", right=None)

    f = body(right="high"); hard_hat(f); wrench(f); P["builder"] = f
    f = body(); wizard_hat(f); P["wizard"] = f
    f = Frame(); f.art(0, -6, DETECTIVE); P["detective"] = f
    f = Frame(); f.art(0, -8, SKATE); P["skate"] = f

    # Side on, far arm up with the flag. The near legs are a row shorter.
    f = body(left="low", right=None, eyes="happy")
    f.rect(16, -3, 4, 3)
    f.rect(14, 12, 6, 1)
    f.art(12, -12, FLAG)
    f.rect(18, -12, 1, 10, K)
    P["flag"] = f

    # One arm pressing a dumbbell, the head tipped up toward it, a headband.
    f = Frame()
    f.rect(4, 0, 16, 12); f.rect(12, -1, 8, 1)
    f.rect(0, 6, 4, 4); f.rect(16, -5, 4, 4)
    for x in LEG_X:
        f.rect(x, 12, 2, 4)
    eye(f, 6, 3, "open"); eye(f, 16, 2, "open")
    f.rect(5, 1, 7, 1, K); f.rect(12, 0, 7, 1, K)
    f.art(11, -6, WEIGHT); f.art(19, -6, WEIGHT); f.rect(15, -4, 4, 1, K)
    f.art(-2, 7, WEIGHT)
    P["lift"] = f

    f = body(dy=-2, arm_w=2, legs=(3, 1, 3, 1)); shadow(f); P["float"] = f
    return P


ORDER = ["calm", "blink", "look-left", "look-right", "wink", "happy", "wave", "alert", "surprised", "puzzled",
         "sleepy", "busy", "thinking", "focused", "love", "sweat", "proud", "cool", "rainy", "cold",
         "reading", "coffee", "walking", "music", "typing", "celebrate", "boop", "coin-hop", "crouch", "peek",
         "builder", "wizard", "detective", "skate", "flag", "lift", "float"]


def pack(f):
    out = []
    for y in range(GH):
        for x in range(0, GW, 2):
            out.append(f.px[y][x] << 4 | f.px[y][x + 1])
    return out


def headroom(f):
    """Rows used above the figure's box."""
    for y in range(BY):
        if any(f.px[y]):
            return BY - y
    return 0


def preview(P, path, s=8):
    from PIL import Image, ImageDraw
    cols = 6
    cw, ch = GW * s + 16, GH * s + 30
    img = Image.new("RGB", (cols * cw, ((len(ORDER) + cols - 1) // cols) * ch), (236, 233, 224))
    d = ImageDraw.Draw(img)
    col = {B: (200, 104, 72), K: (34, 34, 38), Wh: (255, 255, 255), S: (168, 86, 60), G: (150, 150, 150),
           L: (232, 180, 110), E: (34, 34, 38)}
    for n, name in enumerate(ORDER):
        ox, oy = (n % cols) * cw + 8, (n // cols) * ch + 4
        for y in range(GH):
            for x in range(GW):
                v = P[name].px[y][x]
                if v:
                    d.rectangle([ox + x * s, oy + y * s, ox + (x + 1) * s - 1, oy + (y + 1) * s - 1], fill=col[v])
        d.text((ox, oy + GH * s + 6), name, fill=(34, 34, 38))
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    a = ap.parse_args()
    P = build()
    assert set(P) == set(ORDER), set(P) ^ set(ORDER)
    lines = ["// Generated by tools/clawdgen.py. Clawd's poses: 40 x 32 half pixels, 4 bits each",
             "// (0 none, 1 body, 2 black, 3 white, 4 shade, 5 gray, 6 light, 7 black on the body),",
             "// high nibble first, rows top to bottom.",
             f"static const uint8_t kClawdFrames[{len(ORDER)}][{GW * GH // 2}] = {{"]
    for name in ORDER:
        lines.append(f"    /* {name} */ {{" + ", ".join(str(b) for b in pack(P[name])) + "},")
    lines.append("};")
    lines.append("// Half-pixel rows each pose uses above the figure (hats, flags, marks).")
    lines.append(f"static const uint8_t kClawdHeadroom[{len(ORDER)}] = {{"
                 + ", ".join(str(headroom(P[n])) for n in ORDER) + "};")
    with open(os.path.join(ROOT, "firmware/components/cink_core/src/clawd_data.inc"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    if a.preview:
        preview(P, a.preview)


if __name__ == "__main__":
    main()
