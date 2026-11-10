#!/usr/bin/env python3
"""Renders the screenshots in docs/screens from the simulator fixtures.

    cmake -S sim -B sim/build && cmake --build sim/build
    python3 sim/fixtures/make_fixtures.py
    python3 tools/render_screens.py [--sheet contact-sheet.png]

Each screen is shown as the device would show it when that card is the most
relevant one: the fixture is copied with that card raised to the top.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIM = os.path.join(ROOT, "sim/build/cink_sim")
FIX = os.path.join(ROOT, "sim/fixtures")
OUT = os.path.join(ROOT, "docs/screens")
TZ = "PST8PDT,M3.2.0,M11.1.0"
NOW = 1794350820  # 2026-11-10 2:47 PM, as in make_fixtures.py

# (file name, card id to raise or None, extra sim args, caption)
SHOTS = [
    ("next-up", None, [], "Next up"),
    ("slack", "slack:triage", [], "Slack"),
    ("inbox", "gmail:replies", [], "Inbox, three accounts"),
    ("review", "gh:queue", [], "Pull requests"),
    ("ship", "vercel:web", [], "Ship status"),
    ("claude-code", "claude:code", [], "Claude Code"),
    ("news", "news:morning", [], "Morning news"),
    ("weather", "weather:now", [], "Weather"),
    ("deliveries", "parcel:today", [], "Deliveries"),
    ("support", "intercom:inbox", [], "Customer inbox (Intercom)"),
    ("teams", "teams:triage", [], "Teams"),
    ("cloudflare", "cloud:alerts", [], "Cloudflare alerts"),
    ("database", "supabase:advisors", [], "Database health"),
    ("commute", "commute:vanta-drinks", ["--now", str(NOW + 50 * 60)], "Commute"),
    ("focus", "focus:pm", ["--now", str(NOW + 45 * 60)], "Focus window"),
    ("money", "stripe:today", [], "Revenue"),
    ("traffic", "vercel:traffic", [], "Traffic"),
    ("releases", "once:low-tide", [], "Releases on ONCE"),
    ("quest", "tavern:s14", [], "Game night on Tavern"),
    ("wins", "wins:week", [], "This week's wins"),
    ("next-up-mono", None, ["--mono"], "Black and white"),
    ("all-clear", "@empty", [], "All clear"),
    ("night", None, ["--screen", "night"], "Night"),
    ("offline", None, ["--offline", "--sync-age", "7200"], "Offline"),
    ("info", None, ["--screen", "info"], "Device info"),
    ("setup", "@none", ["--screen", "setup"], "Setup"),
    ("clawd", "@none", ["--screen", "clawds"], "Clawd's poses"),
]


def render(name, raise_id, extra, tmp):
    if raise_id == "@empty":
        src = os.path.join(FIX, "empty.json")
    elif raise_id == "@none":
        src = None
    else:
        doc = json.load(open(os.path.join(FIX, "day.json")))
        if raise_id:
            # The meeting in 12 minutes would outrank anything, so leave it out.
            doc["cards"] = [c for c in doc["cards"] if c["id"] != "cal:design-review" or c["id"] == raise_id]
            for c in doc["cards"]:
                if c["id"] == raise_id:
                    c["p"] = 100
                    c.pop("st", None)
                    continue
                # Calm the rest down so the raised card leads.
                c["p"] = min(c.get("p", 50), 59)
                c.pop("u", None)
                for r in c.get("r", []):
                    r.pop("u", None)
        src = os.path.join(tmp, name + ".json")
        json.dump(doc, open(src, "w"))
    out = os.path.join(OUT, name + ".png")
    args = [SIM] + ([src] if src else []) + ["--tz", TZ, "--paper", "--out", out]
    if "--now" not in extra and src:
        args += ["--now", str(NOW)]
    subprocess.run(args + extra, check=True, stdout=subprocess.DEVNULL)
    return out


def sheet(paths, captions, dest):
    from PIL import Image, ImageDraw, ImageFont
    cols, scale, pad, cap = 5, 0.5, 18, 28
    w, h = int(480 * scale), int(720 * scale)
    rows = (len(paths) + cols - 1) // cols
    img = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + cap) + pad), (244, 242, 237))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 15)
    except OSError:
        font = ImageFont.load_default()
    for i, (p, c) in enumerate(zip(paths, captions)):
        x, y = pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad + cap)
        im = Image.open(p).convert("RGB").resize((w, h), Image.LANCZOS)
        d.rectangle([x - 1, y - 1, x + w, y + h], outline=(200, 197, 190))
        img.paste(im, (x, y))
        d.text((x, y + h + 7), c, fill=(60, 60, 64), font=font)
    img.save(dest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", help="also write a contact sheet of every screen here")
    a = ap.parse_args()
    if not os.path.exists(SIM):
        sys.exit("build the simulator first: cmake -S sim -B sim/build && cmake --build sim/build")
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        paths = [render(n, r, e, tmp) for n, r, e, _ in SHOTS]
    if a.sheet:
        sheet(paths, [s[3] for s in SHOTS], a.sheet)
    print(f"wrote {len(paths)} screens to {OUT}")


if __name__ == "__main__":
    main()
