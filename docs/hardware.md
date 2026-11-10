# Hardware notes: ENILINX TICKEY

## What is known (November 2026)

| | |
|---|---|
| SoC | ESP32-S3, dual-core LX7 @ 240 MHz |
| Memory | "8 MB RAM / Flash" on the standard unit; 128 MB on the Modos Developer Edition |
| Display | 3.7" color e-paper, 720 × 480, 48+ native colors, Modos driver; ~1 s mono, ~3 s fast color, ~13 s full refresh |
| Input | 3 side buttons: Up, Down, Back |
| Power | 360 mAh, USB-C 5 V, "1,000+ refreshes per charge" |
| Radio | Wi-Fi 4 (2.4 GHz), BLE 5.0, NFC (ISO 15693) |
| Size | 111 × 62 × 3.8 mm, 45 g |

Sources: [enilinxtickey.com](https://enilinxtickey.com/), [CNX Software](https://www.cnx-software.com/2026/09/17/enilinx-tickey-esp32-s3-color-e-paper-badge-with-open-firmware/), [Hackster](https://www.hackster.io/news/enilinx-s-tickey-is-a-modos-driven-espressif-esp32-s3-powered-smart-ticket-shaped-epaper-display-ff167a0e5daa).

## What is not public yet

ENILINX describes the firmware as open and says sources come after the crowdfunding campaign. Until then these are unknown: GPIO pinout, the panel interface and Modos driver API, PSRAM size, battery sense and charger pins, and the NFC chip. The next section is how C-Ink gets most of that without waiting.

C-Ink does not assume PSRAM: the frame, the digest and the network stacks all fit in the S3's internal RAM. If the TICKEY turns out to have PSRAM, enabling `CONFIG_SPIRAM` buys headroom but changes nothing else.

## Getting past the missing panel driver

C-Ink does not wait for ENILINX. Pins and the panel driver are data in a **board profile** stored on the device, and there is a tool to learn that data from the stock firmware.

**1. Read the wiring off the stock firmware** with [`tools/tickey-probe`](../tools/tickey-probe). The ESP32-S3's GPIO matrix registers record which pin carries which peripheral signal (SPI clock, LCD data, I2C) and which pins are plain inputs and outputs. The S3's built-in USB-JTAG lets OpenOCD halt the CPU for a moment, read those registers, and let it run again, while the stock firmware is driving the panel. Nothing is written to the device.

```sh
pip install esptool                                   # and Espressif's OpenOCD (openocd-esp32)
python3 tools/tickey-probe/probe.py check  --port /dev/cu.usbmodem1101   # chip, flash, security fuses
python3 tools/tickey-probe/probe.py backup --port /dev/cu.usbmodem1101   # full flash image: do this first
python3 tools/tickey-probe/probe.py snapshot --out snap.json             # press buttons, let it redraw
python3 tools/tickey-probe/probe.py analyze snap.json --dump tickey-stock-*.bin # pin roles + tickey-board.json
```

`analyze` says whether the panel hangs off an SPI controller or the S3's parallel LCD peripheral, names the SCK/MOSI/CS pins from the signal routing, guesses DC, RST and BUSY from which plain pins toggled during a redraw, lists button candidates (inputs with pull-ups), and scans the flash dump for the panel's init table (a 720×480 resolution write) and controller names. For an SPI panel it fills the command scripts from a UC81xx or SSD16xx template (picked from the dump, or `--controller`).

**2. Send the profile.** C-Ink Settings → Device → Hardware → **Load file**, check it, **Send to TICKEY**. The device saves it in NVS and restarts its panel with the scripted SPI driver (`cink_hal/src/panel_spi.cpp`). Script steps are documented in [protocol.md](protocol.md#board-profile).

**3. Test panel** runs a full refresh and reports how long the panel held BUSY. A few seconds means the controller took the frame. Zero means the BUSY pin or its polarity is wrong. Hitting the timeout means the refresh command or power-on step is wrong. Adjust the profile and send again; no reflashing.

**4. Learn buttons:** press Up, Down, Back once each within 15 seconds and C-Ink watches every safe GPIO (it never touches USB, flash, PSRAM, the console or panel pins) to see which line moved.

What this does not cover yet, honestly:

- **A parallel (Modos-style) panel.** ENILINX says the TICKEY uses a Modos driver, and Modos panels are usually driven by waveforms over a parallel bus rather than through a controller chip. If `analyze` reports `parallel`, the pin map is still right, but C-Ink needs an epdiy-style waveform backend for the S3's LCD peripheral before it can draw. The profile format reserves `"type": "parallel"` for it; the firmware rejects it with a clear message today.
- **The exact init sequence.** The templates are a starting point. The authoritative values are in the stock firmware's init table, which the dump scan locates for you.
- **Locked devices.** If `check` shows JTAG or USB-JTAG disabled in eFuse, or secure boot and flash encryption on, the snapshot step will not work and the dump will be encrypted. Then the vendor BSP route below is the way.

**Vendor BSP route.** Once ENILINX publishes its BSP, select `CINK_DISPLAY_TICKEY` in `idf.py menuconfig` → *C-Ink board* and add the BSP as a component. It needs `tickey_panel_init()`, `tickey_panel_draw(black, color, full, rotation)` and `tickey_panel_sleep()`; the planes are the portrait canvas (480 × 720, 60 bytes per row, MSB first). A stored SPI profile still wins over it, and **Forget profile** clears one.

**Battery:** set the ADC GPIO and divider ratio in menuconfig, and the charger status pin if there is one. (The probe's ADC-capable inputs without pull-ups are the candidates.)

Until a panel backend is running, the headless backend draws nothing and the frame stays in the app's canvas; **Show its screen** on the Device tab reads it back over USB, so the whole stack can be verified on the real device (or any ESP32-S3 dev board) first.

**Going back to stock:** `esptool.py --chip esp32s3 --port <port> write_flash 0 tickey-stock-<date>.bin`, with the image `backup` saved.

## Flashing

```sh
cd firmware
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem* flash monitor
```

Hold the boot button while plugging in if the device does not enter download mode on its own (the TICKEY's button mapping for this is not documented yet). To return to the stock firmware, flash ENILINX's image the same way.

## Power budget (estimate)

A sync is ~4 s of Wi-Fi at ~100 mA when the last access point is still there (fast reconnect skips the scan), so 15-minute syncs cost ~11 mAh a day. Redraws only happen when the frame changes, typically 25-40 a day. With ENILINX's "1,000+ refreshes per charge" for the panel, expect roughly 2-3 weeks per charge at the defaults, more with quiet hours or a longer interval.
