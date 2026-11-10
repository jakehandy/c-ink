import os
import tempfile
import unittest

import probe


def snap(out_sel=None, in_sel=None, mux=None, enable=0, out=0, inp=0):
    st = {"out": out, "out1": 0, "enable": enable, "enable1": 0, "in": inp, "in1": 0,
          "out_sel": [256] * probe.NPINS, "in_sel": [0x3C] * 256, "io_mux": [(1 << 12) | (1 << 9)] * probe.NPINS}
    for g, sig in (out_sel or {}).items():
        st["out_sel"][g] = sig
    for sig, g in (in_sel or {}).items():
        st["in_sel"][sig] = 0x80 | g
    for g, v in (mux or {}).items():
        st["io_mux"][g] = v
    return st


PU = (1 << 12) | (1 << 9) | (1 << 8)


class Analyze(unittest.TestCase):
    def spi_board(self, dc_level, busy_level, btn_level):
        enable = (1 << 9) | (1 << 8) | (1 << 10) | (1 << 11) | (1 << 12)
        return snap(out_sel={12: 101, 11: 103, 10: 110}, mux={4: PU, 5: PU, 6: PU, 19: 0, 20: 0},
                    enable=enable, out=(dc_level << 9) | (1 << 8),
                    inp=(busy_level << 7) | (btn_level << 4) | (1 << 5) | (1 << 6))

    def test_spi_panel(self):
        r = probe.analyze([self.spi_board(0, 1, 1), self.spi_board(1, 0, 0), self.spi_board(0, 1, 1)])
        self.assertEqual(r["kind"], "spi")
        p = r["profile"]["panel"]
        self.assertEqual((p["sck"], p["mosi"], p["cs"]), (12, 11, 10))
        self.assertEqual(p["dc"], 9)    # the output that toggled
        self.assertEqual(p["rst"], 8)
        self.assertEqual(p["busy"], 7)  # the input without a pull-up that toggled
        self.assertEqual(r["profile"]["buttons"]["up"], 4)
        self.assertTrue(any("GPIO 12: out: FSPICLK" in line for line in r["pins"]))

    def test_templates(self):
        r = probe.analyze([self.spi_board(0, 1, 1), self.spi_board(1, 0, 0)])
        p = probe.apply_template(r["profile"], "ssd16xx")["panel"]
        self.assertTrue(p["busy_high"])
        self.assertIn(["plane", 0x24, "black_inv"], p["write"])
        self.assertTrue(p["full"])
        self.assertEqual(probe.guess_controller(["string at 0x1000: ssd1677 init"]), "ssd16xx")
        self.assertEqual(probe.guess_controller(["720x480 big-endian at 0x2000: 61 02 d0 01 e0"]), "uc81xx")
        self.assertIsNone(probe.guess_controller([]))
        # Every op is one the firmware's profile compiler knows.
        for tpl in probe.TEMPLATES.values():
            for k in ("init", "write", "full", "fast", "sleep"):
                for op in tpl[k]:
                    self.assertIn(op[0], ("cmd", "wait", "delay", "reset", "plane", "power"))

    def test_parallel_panel(self):
        outs = {g: 133 + i for i, g in enumerate(range(1, 9))}
        outs[14] = 154  # LCD_PCLK
        r = probe.analyze([snap(out_sel=outs, enable=0x1FF)])
        self.assertEqual(r["kind"], "parallel")
        self.assertEqual(r["profile"]["panel"]["lcd_pins"]["LCD_PCLK"], 14)

    def test_dump_scan(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"\x00" * 100 + bytes([0x61, 0x02, 0xD0, 0x01, 0xE0]) + b"\x00" * 50 + b"Modos waveform v2\x00")
        try:
            hints = probe.scan_dump(f.name)
        finally:
            os.unlink(f.name)
        self.assertTrue(any("720x480 big-endian at 0x000065" in h for h in hints), hints)
        self.assertTrue(any("Modos waveform" in h for h in hints))

    def test_fuses(self):
        s = "DIS_USB_JTAG (BLOCK0)    Disables USB JTAG    = False R/W (0b0)\n" \
            "SOFT_DIS_JTAG (BLOCK0)   Set this bit         = 0 R/W (0b000)\n"
        f = probe.parse_fuses(s)
        self.assertEqual(f["DIS_USB_JTAG"], "False")
        self.assertFalse(probe.fuse_on(f["DIS_USB_JTAG"]))
        self.assertFalse(probe.fuse_on(f["SOFT_DIS_JTAG"]))


if __name__ == "__main__":
    unittest.main()
