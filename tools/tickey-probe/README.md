# tickey-probe

Learns how a TICKEY is wired by watching its stock firmware, and writes a C-Ink board profile. See [docs/hardware.md](../../docs/hardware.md#getting-past-the-missing-panel-driver) for the whole flow.

```sh
pip install esptool                       # check and backup
# plus Espressif's OpenOCD (openocd-esp32) on PATH for snapshot

python3 probe.py check    --port /dev/cu.usbmodem1101
python3 probe.py backup   --port /dev/cu.usbmodem1101      # writes tickey-stock-<date>.bin
python3 probe.py snapshot --out snap.json                  # press each button and let the screen redraw meanwhile
python3 probe.py analyze  snap.json --dump tickey-stock-<date>.bin # writes tickey-board.json
```

| Command | What it does | Writes to the device? |
|---|---|---|
| `check` | Chip, flash size, and the security eFuses that decide whether the rest works | No |
| `backup` | Reads the whole flash to a file, so you can always go back to stock | No |
| `snapshot` | Starts OpenOCD on the S3's USB-JTAG, then halts, reads the GPIO, GPIO matrix and IO MUX registers, and resumes, a few dozen times | No (halts the CPU for milliseconds) |
| `analyze` | Decodes the snapshots into pin roles, guesses the panel interface, scans the dump for the init table, writes a board profile | No |

`analyze --controller uc81xx|ssd16xx` fills an SPI panel's command scripts from a template. Without it the probe picks one when the dump points at a controller.

Load `tickey-board.json` in C-Ink Settings → Device → Hardware, then **Test panel** and **Learn buttons**.

Tests: `python3 -m unittest discover tools/tickey-probe`.
