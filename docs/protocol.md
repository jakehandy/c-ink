# Protocols

## Wire digest (JSON, version 1)

Plaintext, before sealing. Max 20 KB. Short keys keep it small; the device ignores keys it does not know.

```jsonc
{
  "v": 1,
  "gen": 1794350700,          // generated at (unix s)
  "hz": 1794393900,           // trust horizon (default gen + 12 h)
  "hl": "Two meetings left, one fire in prod.",
  "cfg": { "rf": 15, "dis": 0, "q": [23, 7], "mono": false, "rot": 0, "tz": "PST8PDT,M3.2.0,M11.1.0" },
  "cards": [
    {
      "id": "cal:abc", "k": "event",          // kind: event review chat mail task ship money music quest focus brief doc other traffic db commute wins
      "t": "Design review", "sh": "Design review",
      "s": "with Priya and Marco", "b": "Priya wants a yes or no on pricing.", "m": "Meet · 30 min",
      "p": 70, "u": false,                    // priority 0-100, urgent
      "at": 1794351540, "st": 0, "en": 1794353340,
      "big": "$4,820", "dl": "+18% vs a usual Tuesday", "sp": [0, 12, 40, 100],
      "r": [ { "l": "Sam Ortiz", "d": "Wants your OK by 4 PM", "g": "DM", "tr": 0, "u": true } ]
    }
  ]
}
```

`dis` is a bitmask with bit *n* for kind *n* in the order listed above. New kinds are only ever appended, so old masks keep their meaning. `rot` 1 turns the picture 180° for wearing the badge the other way up (the device also swaps Up and Down).

A `commute` card's `at` is the leave-by time and `en` the meeting start; the device drops it 10 minutes after leave-by.

## Sealed blob

```
"CINK" 0x01 | nonce (12) | AES-256-GCM ciphertext | tag (16)
AAD = "CINK" 0x01 || device id (ASCII, e.g. "C1A7-22F0")
```

## LAN endpoint (C-Ink Settings / cink-bridge)

* mDNS service `_cink._tcp`, default port 7787.
* `GET /v1/digest/<device id>` with `Authorization: Bearer <device token>` → `200` blob + `ETag`, `304` if `If-None-Match` matches, `401`, or `503` before the first digest.
* `GET /v1/ping` → `{"ok":true,"host":"..."}`

## Relay

* `PUT /v1/d/<id>` with `Authorization: Bearer <WRITE_TOKEN>`, `X-Cink-Token-Hash: sha256(device token)`, `X-Cink-ETag`.
* `GET /v1/d/<id>` with the device's bearer token; `If-None-Match` supported.

## USB link

USB Serial/JTAG on the TICKEY's USB-C. The host writes one JSON object per line; the device answers with lines that start with `@cink ` (log output shares the wire).

| op | request | reply |
|---|---|---|
| `hello` / `status` | | device id, firmware, provisioned, battery, ssid, ip, last_sync, cards, refresh_min, networks |
| `config` | any of `wifi` [{ssid, pass}], `token`, `key` (base64, 32 B), `bridge_host`, `bridge_port`, `relay_url`, `refresh_min`, `disabled`, `quiet` [a, b] or null, `tz`, `mono`, `time` | ok |
| `blob_begin` / `blob_data` / `blob_end` | `len`; `b64` chunks; | ok once the blob verifies with the device key and is cached |
| `sync` | | ok, then syncs |
| `press` | `button`: up, down, back, info | ok |
| `frame` | | `frame` lines (plane, off, b64), then `frame_end`. Planes are the portrait canvas: 480 × 720, 60 bytes per row, MSB first |
| `board` | `profile`: a board profile (below), up to ~4.4 KB as one line | ok once it compiles and is saved; buttons and panel restart with it |
| `board_get` | | `backend` (spi, vendor, headless) and `profile` (pins and script sizes) |
| `board_reset` | | ok; back to the Kconfig pins and the fallback display |
| `panel_test` | | after a full refresh: `backend`, `busy_ms`, `total_ms`, `busy_seen`, `timeouts`, `frames` |
| `learn` | `secs` (1-30), `active_low` | `learn` (`watching`: pin count), then a `pin` line (gpio, level, t) per level change, then `learn_end` |
| `reset` | | ok, then factory reset and reboot |

## Board profile

The TICKEY's wiring as data, stored in NVS so the port can be finished without new firmware. `tools/tickey-probe` writes one from the stock firmware; **Learn buttons** in C-Ink Settings fills in `buttons`.

```jsonc
{
  "buttons": { "up": 4, "down": 5, "back": 6, "active_low": true },
  "panel": {
    "type": "spi",                       // or "none"
    "sck": 12, "mosi": 11, "cs": 10, "dc": 9, "rst": 8, "busy": 7, "pwr": -1,
    "busy_high": false,                  // SSD16xx: true, UC81xx: false
    "native_portrait": false,            // controller rows are 480 px wide
    "mirror": false,                     // controller scans rows right to left
    "hz": 10000000,
    "format": "mono1",                   // or "palette4" (4-bit palette codes, two pixels per byte)
    "palette": { "white": 1, "black": 0, "accent": 3 },
    "init":  [["reset"], ["wait", 5], ["cmd", 0x00, [0x0F]], ["cmd", 0x61, [2, 208, 1, 224]]],
    "write": [["plane", 0x10, "black_inv"], ["plane", 0x13, "accent"]],
    "full":  [["cmd", 0x12, []], ["wait", 40]],
    "fast":  [],
    "sleep": [["cmd", 0x02, []], ["wait", 5], ["cmd", 0x07, [165]]]
  }
}
```

Script steps: `["cmd", c, [data]]` sends a command byte with DC low then its data with DC high; `["wait", s]` waits up to *s* seconds for BUSY to clear; `["delay", ms]`; `["reset"]` pulses RST; `["power", 0|1]` drives the power-enable pin; `["plane", c, src]` sends command *c* and then the whole frame, row by row in the controller's scan order, from `black`, `black_inv`, `accent`, `accent_inv` or `palette`. Each present runs `init` (only after sleep), `write`, then `fast` (or `full` when a full refresh is due or `fast` is empty), then `sleep`. The scripts share 1 KB.
