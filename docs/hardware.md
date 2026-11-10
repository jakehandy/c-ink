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

ENILINX describes the firmware as open and says sources come after the crowdfunding campaign. Until then these are unknown: GPIO pinout, the panel interface and Modos driver API, PSRAM size, battery sense and charger pins, and the NFC chip.

C-Ink does not assume PSRAM: the frame, the digest and the network stacks all fit in the S3's internal RAM. If the TICKEY turns out to have PSRAM, enabling `CONFIG_SPIRAM` buys headroom but changes nothing else.

## Finishing the port

Everything hardware-specific is behind `firmware/components/cink_hal`:

1. **Display:** select `CINK_DISPLAY_TICKEY` in `idf.py menuconfig` → *C-Ink board*, add the vendor BSP as a component, and implement the three functions in `src/display_tickey.cpp` (`tickey_panel_init/draw/sleep`). `draw` receives a black plane and an accent plane (1 bit per pixel, MSB first, 90 bytes per row) and a full/fast flag. The accent plane is a single color today; mapping it to the Modos color palette is a small change there.
2. **Buttons:** set the three GPIOs in menuconfig. They must be RTC-capable to wake from deep sleep (`ext1`).
3. **Battery:** set the ADC GPIO and divider ratio, and the charger status pin if there is one.

Until then the headless backend draws nothing and the frame stays in the app's canvas; **Show its screen** on the Device tab of C-Ink Settings reads it back over USB, so the whole stack can be verified on the real device (or any ESP32-S3 dev board) before the panel works. The Remote buttons on that tab drive the UI over USB too.

## Flashing

```sh
cd firmware
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem* flash monitor
```

Hold the boot button while plugging in if the device does not enter download mode on its own (the TICKEY's button mapping for this is not documented yet). To return to the stock firmware, flash ENILINX's image the same way.

## Power budget (estimate)

A sync is ~4 s of Wi-Fi at ~100 mA when the last access point is still there (fast reconnect skips the scan), so 15-minute syncs cost ~11 mAh a day. Redraws only happen when the frame changes, typically 25-40 a day. With ENILINX's "1,000+ refreshes per charge" for the panel, expect roughly 2-3 weeks per charge at the defaults, more with quiet hours or a longer interval.
