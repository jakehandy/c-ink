# Protocols

## Wire digest (JSON, version 1)

Plaintext, before sealing. Max 20 KB. Short keys keep it small; the device ignores keys it does not know.

```jsonc
{
  "v": 1,
  "gen": 1794350700,          // generated at (unix s)
  "hz": 1794393900,           // trust horizon (default gen + 12 h)
  "hl": "Two meetings left, one fire in prod.",
  "cfg": { "rf": 15, "dis": 0, "q": [23, 7], "mono": false, "tz": "PST8PDT,M3.2.0,M11.1.0" },
  "cards": [
    {
      "id": "cal:abc", "k": "event",          // kind: event review chat mail task ship money music quest focus brief doc other
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

`dis` is a bitmask with bit *n* for kind *n* in the order listed above.

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
| `frame` | | `frame` lines (plane, off, b64), then `frame_end` |
| `reset` | | ok, then factory reset and reboot |
