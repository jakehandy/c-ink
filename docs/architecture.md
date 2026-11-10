# Architecture

## The split

Claude is good at judgement and bad at clocks. The TICKEY is good at clocks and has no business doing judgement. So:

* **Claude (via C-Ink Settings)** decides *what* matters: which meeting needs prep, which Slack threads are real asks, which PR is blocking people, whether today's revenue is worth a look. It returns cards with a priority, optional time anchors (`at`, `start`, `end`) and short, absolute-time copy.
* **The device** decides *when*: it re-ranks the cached cards against the real clock on every wake. A meeting gains up to +60 as it gets within 5 minutes and falls off 15 minutes after it starts (or at its end). Untimed cards (a Slack ask, a PR) slowly sink once the digest is more than 3 hours old, because someone has probably handled them.

That is what makes offline graceful: one sync covers the next 12 hours.

## Sync path

1. The bridge's scheduler fires every `refresh_min` (default 15) unless quiet hours are on.
2. It refreshes the list of read-only MCP tools once a day: a cheap Haiku run lists the tool names it has, the bridge keeps only names that read (`list_`, `get_`, `search_`, `read_`…) and drops anything that writes. That exact list becomes `--allowedTools`, and `--permission-mode dontAsk` denies everything else.
3. It runs `claude -p <prompt> --output-format json --json-schema <schema> --restricted --no-session-persistence --model sonnet`. `--restricted` removes Bash and the other code-running tools and ignores local settings files, and the working directory is C-Ink's own config folder so no project `CLAUDE.md` leaks in.
4. If no GitHub connector exists but the `gh` CLI does, the bridge fetches your review queue itself and passes it in as context.
5. `digest::build` trims every field to what fits on screen, converts times to unix seconds, drops disabled screens, runs the privacy filter a second time, sorts by priority, and drops the lowest cards until the JSON is under 20 KB. Settings that the device needs (refresh rate, screens, quiet hours, timezone, mono) ride along in `cfg`, so changing them in the app reaches the device over Wi-Fi.
6. For each paired device the digest is sealed with AES-256-GCM using that device's key, with the device id in the AAD.
7. The device fetches `GET /v1/digest/<id>` from the bridge (found with mDNS `_cink._tcp`) with its bearer token and `If-None-Match`; an unchanged digest costs a 304. If the bridge is not on this network, it tries the relay with the same blob.

## Device loop

```
boot ─▶ load config (NVS) ─▶ open cached blob (flash) ─▶ button wake? ─▶ interactive (20 s)
     ─▶ sync due? ─▶ Wi-Fi (last BSSID first) ─▶ NTP if stale ─▶ fetch ─▶ verify ─▶ cache
     ─▶ render ─▶ pixels changed? ─▶ present (fast or full)
     ─▶ USB host? ─▶ stay awake for C-Ink Settings
     ─▶ deep sleep until min(next sync, next screen change, quiet hours edge)
```

* **Cache:** two 64 KB slots on a raw `cache` partition. The blob is stored exactly as received (still encrypted), body first and header last, CRC-checked, newest valid slot wins. A torn write never loses the previous digest.
* **Backoff:** failed syncs back off 1x, 2x, 4x, 8x the interval, capped at 2 hours. The status bar says "Offline · 2 h ago".
* **Refresh policy:** fast mono refresh by default; a full refresh when the accent plane changes or after 12 fast ones.
* **Quiet hours:** a night screen with tomorrow's first meeting, no syncing, one wake at the end of quiet hours.

## Privacy

* Only read-only tools are ever allowed.
* The "Never show" list is in the prompt as a hard rule *and* applied again to the output before it leaves the computer.
* Every digest is encrypted per device. The LAN, the relay and the device's flash only ever hold ciphertext; plaintext exists in the device's RAM while it renders.
* Wi-Fi passwords go from the app to the device over USB and are not stored by the app.

## Rendering

`cink_core` is plain C++17 with no allocation: a 720x480 canvas with a black plane and an accent plane (86 KB), Inter rasterized to 1-bit at ten sizes with kerning (quarter-pixel advances), and procedural line icons. The same code runs on the ESP32-S3, in the simulator, and inside the desktop app (via the `cc` crate), so the preview in C-Ink Settings is the exact frame the device will draw.
