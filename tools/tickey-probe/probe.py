#!/usr/bin/env python3
"""tickey-probe: learn how a TICKEY is wired by watching its stock firmware.

ENILINX has not published the TICKEY's pinout or panel driver. The ESP32-S3
does not need them to tell us: its GPIO matrix registers say which pin
carries which peripheral signal (SPI clock, LCD data, I2C), and the GPIO
registers say which pins are plain outputs and inputs. The S3's built-in
USB-JTAG lets OpenOCD read those registers while the stock firmware runs,
without changing anything on the device.

    probe.py check    --port /dev/cu.usbmodem1101        # chip, flash, security fuses
    probe.py backup   --port /dev/cu.usbmodem1101        # full flash image, before anything else
    probe.py snapshot --out snap.json                    # register snapshots via OpenOCD
    probe.py analyze  snap.json --dump stock.bin         # pin roles + a C-Ink board profile

Nothing here writes to the device. `backup` is what lets you go back to the
stock firmware later (esptool.py write_flash 0 stock.bin).

Needs: esptool (pip install esptool) for check/backup, and Espressif's
OpenOCD (bundled with ESP-IDF, or github.com/espressif/openocd-esp32) for
snapshot. analyze is pure Python.
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sigmap_esp32s3 as SIG  # noqa: E402

GPIO_BASE = 0x60004000
IO_MUX_BASE = 0x60009000
REG = {
    "out": GPIO_BASE + 0x04, "out1": GPIO_BASE + 0x10,
    "enable": GPIO_BASE + 0x20, "enable1": GPIO_BASE + 0x2C,
    "in": GPIO_BASE + 0x3C, "in1": GPIO_BASE + 0x40,
}
IN_SEL0 = GPIO_BASE + 0x154   # 256 words, one per input signal
OUT_SEL0 = GPIO_BASE + 0x554  # 49 words, one per GPIO
IO_MUX0 = IO_MUX_BASE + 0x04  # 49 words, one per GPIO
NPINS = 49
# GPIOs the S3 does not have.
MISSING = set(range(22, 26))
# Pins we never treat as panel / button candidates.
USB_PINS = {19, 20}
FLASH_PINS = set(range(26, 33))  # SPI flash / PSRAM (quad)
OCTAL_PINS = set(range(33, 38))  # octal PSRAM, when present
STRAPPING = {0, 3, 45, 46}


# --------------------------------------------------------------------- helpers

def run(cmd, **kw):
    print("$ " + " ".join(cmd), file=sys.stderr)
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw).stdout


def esptool(port, *args):
    exe = shutil.which("esptool.py") or shutil.which("esptool")
    cmd = [exe] if exe else [sys.executable, "-m", "esptool"]
    return run(cmd + ["--chip", "esp32s3", "--port", port, *args])


def espefuse(port, *args):
    exe = shutil.which("espefuse.py") or shutil.which("espefuse")
    cmd = [exe] if exe else [sys.executable, "-m", "espefuse"]
    return run(cmd + ["--chip", "esp32s3", "--port", port, *args])


# ----------------------------------------------------------------- check/backup

SECURITY_FUSES = {
    "SPI_BOOT_CRYPT_CNT": "flash encryption (a dump will be ciphertext if this is odd)",
    "SECURE_BOOT_EN": "secure boot (a different firmware will not boot)",
    "DIS_USB_JTAG": "USB-JTAG disabled (snapshot will not work)",
    "SOFT_DIS_JTAG": "JTAG soft-disabled (snapshot will not work)",
    "ENABLE_SECURITY_DOWNLOAD": "secure download mode (backup will not work)",
    "DIS_DOWNLOAD_MODE": "download mode disabled (backup and flashing will not work)",
}


def parse_fuses(summary):
    found = {}
    for line in summary.splitlines():
        m = re.match(r"\s*(\w+)\s+\(.*?\)\s+.*?=\s*(\S+)", line)
        if m and m.group(1) in SECURITY_FUSES:
            found[m.group(1)] = m.group(2)
    return found


def fuse_on(v):
    v = v.strip().lower()
    if v in ("false", "0", "0b000", "0b0", "0x0"):
        return False
    if v.startswith("0b"):
        return int(v[2:] or "0", 2) != 0
    if v.startswith("0x"):
        return int(v, 16) != 0
    return v in ("true", "1") or (v.isdigit() and int(v) != 0)


def cmd_check(a):
    print(esptool(a.port, "flash_id"))
    fuses = parse_fuses(espefuse(a.port, "summary"))
    print("\nSecurity:")
    blocked = False
    for k, why in SECURITY_FUSES.items():
        v = fuses.get(k, "?")
        on = v != "?" and fuse_on(v)
        if k == "SPI_BOOT_CRYPT_CNT" and v != "?":
            on = bin(int(v, 0) if v[:2] in ("0x", "0b") else int(v)).count("1") % 2 == 1
        print(f"  {'!!' if on else 'ok'}  {k} = {v}  ({why})")
        blocked |= on
    print("\nLooks open: backup and snapshot should work." if not blocked else
          "\nSome protections are on. See the notes above before going further.")


def cmd_backup(a):
    info = esptool(a.port, "flash_id")
    m = re.search(r"Detected flash size:\s*(\d+)MB", info)
    size = int(m.group(1)) * 1024 * 1024 if m else 8 * 1024 * 1024
    out = a.out or f"tickey-stock-{time.strftime('%Y%m%d-%H%M%S')}.bin"
    esptool(a.port, "--baud", str(a.baud), "read_flash", "0", hex(size), out)
    print(f"Saved {size // 1024} KB to {out}. Restore any time with:\n"
          f"  esptool.py --chip esp32s3 --port {a.port} write_flash 0 {out}")


# -------------------------------------------------------------------- snapshot

class OpenOCD:
    """Minimal client for OpenOCD's Tcl RPC port (commands end with 0x1a)."""

    def __init__(self, host="127.0.0.1", port=6666):
        self.s = socket.create_connection((host, port), timeout=10)

    def cmd(self, c):
        self.s.sendall(c.encode() + b"\x1a")
        buf = b""
        while not buf.endswith(b"\x1a"):
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("OpenOCD closed the connection")
            buf += chunk
        return buf[:-1].decode(errors="replace")

    def words(self, addr, n):
        out = self.cmd(f"read_memory {addr:#x} 32 {n}")
        vals = [int(x, 0) for x in out.split()]
        if len(vals) != n:
            # Older OpenOCD: fall back to mdw and parse "0x...: v v v".
            out = self.cmd(f"capture \"mdw {addr:#x} {n}\"")
            vals = [int(x, 16) for line in out.splitlines() for x in line.split(":", 1)[-1].split()]
        return vals[:n]


def read_state(ocd):
    st = {k: ocd.words(addr, 1)[0] for k, addr in REG.items()}
    st["out_sel"] = ocd.words(OUT_SEL0, NPINS)
    st["in_sel"] = ocd.words(IN_SEL0, 256)
    st["io_mux"] = ocd.words(IO_MUX0, NPINS)
    st["t"] = time.time()
    return st


def cmd_snapshot(a):
    proc = None
    if not a.no_start:
        exe = shutil.which("openocd") or "openocd"
        proc = subprocess.Popen([exe, "-f", a.openocd_cfg], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        time.sleep(2.5)
    try:
        ocd = OpenOCD(port=a.tcl_port)
        snaps = []
        print(f"Taking {a.count} snapshots. While they run, press a TICKEY button so it redraws.")
        for i in range(a.count):
            ocd.cmd("halt")
            snaps.append(read_state(ocd))
            ocd.cmd("resume")
            print(f"  {i + 1}/{a.count}", end="\r", file=sys.stderr)
            time.sleep(a.interval)
        with open(a.out, "w") as f:
            json.dump({"chip": "esp32s3", "snapshots": snaps}, f)
        print(f"\nSaved {len(snaps)} snapshots to {a.out}. Next: probe.py analyze {a.out} --dump <backup.bin>")
    finally:
        if proc:
            proc.terminate()


# --------------------------------------------------------------------- analyze

def bit(v, n):
    return (v >> n) & 1


def pin_bits(st, key):
    lo = st.get(key, 0)
    hi = st.get(key + "1", 0)
    return lo | (hi << 32)


def decode(st):
    """One snapshot -> per-pin description."""
    pins = {}
    enable = pin_bits(st, "enable")
    for n in range(NPINS):
        if n in MISSING:
            continue
        mux = st["io_mux"][n]
        mcu_sel = (mux >> 12) & 7
        p = {"gpio": n, "mux": mcu_sel, "ie": bit(mux, 9), "pu": bit(mux, 8), "pd": bit(mux, 7),
             "oe": bit(enable, n), "out": None, "ins": []}
        sel = st["out_sel"][n]
        if mcu_sel == 1:
            sig = sel & 0x1FF
            if sig != 256:
                p["out"] = SIG.OUT.get(sig, f"sig{sig}")
        pins[n] = p
    for m, v in enumerate(st["in_sel"]):
        if not bit(v, 7):
            continue
        g = v & 0x3F
        if g in pins:
            pins[g]["ins"].append(SIG.IN.get(m, f"sig{m}"))
    return pins


def toggles(snaps):
    """Pins whose input or output level changed across snapshots."""
    seen_in, seen_out = {}, {}
    for st in snaps:
        lin, lout = pin_bits(st, "in"), pin_bits(st, "out")
        for n in range(NPINS):
            seen_in.setdefault(n, set()).add(bit(lin, n))
            seen_out.setdefault(n, set()).add(bit(lout, n))
    return ({n for n, s in seen_in.items() if len(s) > 1}, {n for n, s in seen_out.items() if len(s) > 1})


def first(pins, *names):
    for n, p in pins.items():
        for name in names:
            if p["out"] == name or name in p["ins"]:
                return n
    return -1


def analyze(snaps, dump=None):
    pins = decode(snaps[-1])
    tog_in, tog_out = toggles(snaps)
    lines, notes = [], []
    reserved = USB_PINS | FLASH_PINS
    if any(pins[n]["mux"] != 1 for n in OCTAL_PINS if n in pins):
        reserved |= OCTAL_PINS
        notes.append("GPIO 33-37 are on the IO MUX, which usually means octal PSRAM is fitted.")

    for n, p in sorted(pins.items()):
        what = []
        if p["mux"] != 1:
            what.append(f"IO MUX function {p['mux']}")
        if p["out"]:
            what.append(f"out: {p['out']}")
        if p["ins"]:
            what.append("in: " + ", ".join(p["ins"]))
        if p["mux"] == 1 and not p["out"]:
            if p["oe"]:
                what.append("GPIO output")
            elif p["ie"]:
                what.append("GPIO input" + (" (pull-up)" if p["pu"] else " (pull-down)" if p["pd"] else ""))
        if n in tog_in:
            what.append("input toggled")
        if n in tog_out:
            what.append("output toggled")
        if what:
            lines.append(f"GPIO {n:2d}: " + "; ".join(what))

    spi = {
        "sck": first(pins, "FSPICLK", "SPI3_CLK"),
        "mosi": first(pins, "FSPID", "SPI3_D"),
        "cs": first(pins, "FSPICS0", "SPI3_CS0"),
        "miso": first(pins, "FSPIQ", "SPI3_Q"),
    }
    lcd = sorted(n for n, p in pins.items() if p["out"] and p["out"].startswith("LCD_"))
    i2c = {k: first(pins, *v) for k, v in {"scl": ("I2CEXT0_SCL", "I2CEXT1_SCL"), "sda": ("I2CEXT0_SDA", "I2CEXT1_SDA")}.items()}

    def free(n):
        return n not in reserved and n not in spi.values() and n not in lcd and n not in i2c.values()

    plain_out = [n for n, p in pins.items() if p["mux"] == 1 and not p["out"] and p["oe"] and free(n)]
    plain_in = [n for n, p in pins.items() if p["mux"] == 1 and not p["out"] and not p["oe"] and p["ie"] and free(n)]
    buttons = [n for n in plain_in if pins[n]["pu"] and n in tog_in]
    if not buttons:
        buttons = [n for n in plain_in if pins[n]["pu"]]
    busy = [n for n in plain_in if not pins[n]["pu"] and n in tog_in] or [n for n in plain_in if not pins[n]["pu"]]

    profile = {"buttons": {"up": -1, "down": -1, "back": -1, "active_low": True}, "panel": {"type": "none"}}
    if len(buttons) >= 3:
        notes.append(f"Button candidates (inputs with pull-ups{', toggled while you pressed' if tog_in else ''}): "
                     f"{buttons}. Confirm which is which with Learn buttons in C-Ink Settings.")
    if lcd:
        kind = "parallel"
        notes.append(f"The panel is driven over the S3's LCD peripheral on {len(lcd)} pins {lcd}: a parallel "
                     "e-paper panel with no controller, driven by waveforms (Modos-style). C-Ink needs an "
                     "epdiy-style backend for this; the pin map below is what it will use.")
        profile["panel"] = {"type": "parallel", "lcd_pins": {pins[n]["out"]: n for n in lcd},
                            "gpio_out": plain_out, "i2c": i2c}
    elif spi["sck"] >= 0:
        kind = "spi"
        # DC toggles constantly during a transfer, RST pulses once: both are plain outputs near the bus.
        out_tog = [n for n in plain_out if n in tog_out]
        rest = out_tog + [n for n in plain_out if n not in out_tog]
        profile["panel"] = {
            "type": "spi", "sck": spi["sck"], "mosi": spi["mosi"], "cs": spi["cs"],
            "dc": rest[0] if len(rest) > 0 else -1, "rst": rest[1] if len(rest) > 1 else -1,
            "busy": busy[0] if busy else -1, "pwr": -1, "busy_high": False, "hz": 10_000_000,
            "format": "mono1", "init": [], "write": [], "full": [], "fast": [], "sleep": [],
        }
        notes.append("SPI panel controller found. DC and RST are guesses from the plain outputs "
                     f"{plain_out}; BUSY from inputs {busy}. Use Test panel in C-Ink Settings to confirm: "
                     "BUSY should go active for seconds during a refresh.")
    else:
        kind = "unknown"
        notes.append("No SPI or LCD signal is routed right now. Take the snapshot while the screen is redrawing.")
    for b, name in zip(buttons[:3], ("up", "down", "back")):
        profile["buttons"][name] = b

    hints = scan_dump(dump) if dump else []
    return {"kind": kind, "pins": lines, "notes": notes, "dump_hints": hints, "profile": profile}


KEYWORDS = [b"modos", b"caster", b"glider", b"epdiy", b"waveform", b"wbf", b"spectra", b"gallery", b"kaleido",
            b"acep", b"uc81", b"ssd16", b"ssd1677", b"jd79", b"il0", b"tps651", b"epd", b"eink", b"e-ink", b"panel",
            b"vcom", b"lut"]


def scan_dump(path):
    with open(path, "rb") as f:
        data = f.read()
    hints = []
    # A 720x480 resolution register write, in either byte order, marks the panel init table.
    for label, pat in (("720x480 big-endian", bytes([0x02, 0xD0, 0x01, 0xE0])),
                       ("480x720 big-endian", bytes([0x01, 0xE0, 0x02, 0xD0])),
                       ("720x480 little-endian", bytes([0xD0, 0x02, 0xE0, 0x01]))):
        start = 0
        while len(hints) < 40:
            i = data.find(pat, start)
            if i < 0:
                break
            ctx = data[max(0, i - 24):i + 28]
            hints.append(f"{label} at {i:#08x}: {ctx.hex(' ')}")
            start = i + 1
    for m in re.finditer(rb"[\x20-\x7e]{5,80}", data):
        s = m.group(0)
        if any(k in s.lower() for k in KEYWORDS):
            hints.append(f"string at {m.start():#08x}: {s.decode(errors='replace')}")
            if len(hints) > 200:
                break
    return hints


# Starting-point command scripts for the two common SPI e-paper controller
# families, set up for a 720x480 panel. They are a first try, not gospel:
# check them against the init table the dump scan finds (the 720x480 bytes
# sit inside it) and adjust in the profile, then use Test panel.
TEMPLATES = {
    # UltraChip UC81xx (UC8179 and friends): BUSY is low while busy, black and
    # color go to separate RAMs (0x10 old/black, 0x13 new/color).
    "uc81xx": {
        "busy_high": False,
        "init": [["reset"], ["wait", 5],
                 ["cmd", 0x01, [0x07, 0x07, 0x3F, 0x3F]],          # power setting
                 ["cmd", 0x06, [0x17, 0x17, 0x28, 0x17]],          # booster soft start
                 ["cmd", 0x04, []], ["delay", 100], ["wait", 5],   # power on
                 ["cmd", 0x00, [0x0F]],                            # panel setting: KWR, scan up, shift right
                 ["cmd", 0x61, [0x02, 0xD0, 0x01, 0xE0]],          # resolution 720 x 480
                 ["cmd", 0x15, [0x00]],
                 ["cmd", 0x50, [0x11, 0x07]],                      # VCOM and data interval
                 ["cmd", 0x60, [0x22]]],
        "write": [["plane", 0x10, "black_inv"], ["plane", 0x13, "accent"]],
        "full": [["cmd", 0x12, []], ["delay", 100], ["wait", 40]],
        "fast": [],
        "sleep": [["cmd", 0x02, []], ["wait", 5], ["cmd", 0x07, [0xA5]]],
    },
    # Solomon SSD16xx (SSD1677 and friends): BUSY is high while busy, 0x24 is
    # black/white RAM (1 = white), 0x26 the red/accent RAM.
    "ssd16xx": {
        "busy_high": True,
        "init": [["reset"], ["wait", 5],
                 ["cmd", 0x12, []], ["wait", 5],                    # software reset
                 ["cmd", 0x01, [0xDF, 0x01, 0x00]],                 # 480 gate lines
                 ["cmd", 0x11, [0x03]],                             # data entry: x then y, increasing
                 ["cmd", 0x44, [0x00, 0x00, 0xCF, 0x02]],           # x window 0..719
                 ["cmd", 0x45, [0x00, 0x00, 0xDF, 0x01]],           # y window 0..479
                 ["cmd", 0x3C, [0x01]],                             # border
                 ["cmd", 0x18, [0x80]]],                            # internal temperature sensor
        "write": [["cmd", 0x4E, [0x00, 0x00]], ["cmd", 0x4F, [0x00, 0x00]], ["plane", 0x24, "black_inv"],
                  ["cmd", 0x4E, [0x00, 0x00]], ["cmd", 0x4F, [0x00, 0x00]], ["plane", 0x26, "accent"]],
        "full": [["cmd", 0x22, [0xF7]], ["cmd", 0x20, []], ["wait", 40]],
        "fast": [["cmd", 0x22, [0xFF]], ["cmd", 0x20, []], ["wait", 10]],
        "sleep": [["cmd", 0x10, [0x01]]],
    },
}


def apply_template(profile, name):
    """Fills an SPI profile's scripts from a controller template."""
    t = TEMPLATES[name]
    panel = profile["panel"]
    if panel.get("type") != "spi":
        raise ValueError("templates only apply to SPI panels")
    panel["busy_high"] = t["busy_high"]
    for k in ("init", "write", "full", "fast", "sleep"):
        panel[k] = [list(op) for op in t[k]]
    return profile


def guess_controller(hints):
    """Which template the dump points at, from strings and the init table."""
    text = " ".join(hints).lower()
    if "ssd16" in text or "ssd1677" in text:
        return "ssd16xx"
    if "uc81" in text or "720x480 big-endian" in text:
        return "uc81xx"  # UC81xx takes resolution as 0x61 hi lo hi lo
    return None


def cmd_analyze(a):
    with open(a.snapshots) as f:
        snaps = json.load(f)["snapshots"]
    r = analyze(snaps, a.dump)
    ctl = a.controller or (guess_controller(r["dump_hints"]) if r["kind"] == "spi" else None)
    if ctl and r["kind"] == "spi":
        apply_template(r["profile"], ctl)
        r["notes"].append(f"Filled the panel scripts from the {ctl} template"
                          f"{'' if a.controller else ', picked from what the dump scan found'}. "
                          "Compare them with the init table in the dump before trusting them.")
    elif r["kind"] == "spi":
        r["notes"].append("The panel scripts are empty. Re-run with --controller uc81xx or ssd16xx "
                          "(see the dump hints) to fill them with a starting point.")
    print(f"Panel interface: {r['kind']}\n")
    print("\n".join(r["pins"]))
    print("\nNotes:\n- " + "\n- ".join(r["notes"]))
    if r["dump_hints"]:
        print("\nIn the firmware image:")
        print("\n".join("  " + h for h in r["dump_hints"][:80]))
    with open(a.out, "w") as f:
        json.dump(r["profile"], f, indent=2)
    print(f"\nBoard profile skeleton written to {a.out}. Load it in C-Ink Settings > Device > Hardware.")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check"); c.add_argument("--port", required=True); c.set_defaults(fn=cmd_check)
    b = sub.add_parser("backup"); b.add_argument("--port", required=True); b.add_argument("--out")
    b.add_argument("--baud", type=int, default=921600); b.set_defaults(fn=cmd_backup)
    s = sub.add_parser("snapshot"); s.add_argument("--out", default="snap.json")
    s.add_argument("--count", type=int, default=30); s.add_argument("--interval", type=float, default=0.3)
    s.add_argument("--openocd-cfg", default="board/esp32s3-builtin.cfg"); s.add_argument("--tcl-port", type=int, default=6666)
    s.add_argument("--no-start", action="store_true", help="OpenOCD is already running"); s.set_defaults(fn=cmd_snapshot)
    z = sub.add_parser("analyze"); z.add_argument("snapshots"); z.add_argument("--dump")
    z.add_argument("--out", default="tickey-board.json")
    z.add_argument("--controller", choices=sorted(TEMPLATES), help="fill SPI panel scripts from this template")
    z.set_defaults(fn=cmd_analyze)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
