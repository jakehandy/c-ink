# C-Ink

A custom OS for the [ENILINX TICKEY](https://enilinxtickey.com/) e-ink badge that uses Claude to put the single most relevant thing in your day on its 3.7" screen: the meeting you are about to walk into, the PRs and Slack threads that need you, the deploy that just broke, and a few fun ones on top.

<p>
<img src="docs/screens/next-up.png" width="49%" alt="Next meeting with a countdown">
<img src="docs/screens/slack.png" width="49%" alt="Slack sorted by importance">
<img src="docs/screens/review.png" width="49%" alt="Pull requests waiting on you">
<img src="docs/screens/money.png" width="49%" alt="Revenue today with a sparkline">
</p>

These are real frames from the device's renderer (`sim/`), with sample data.

## How it works

```
 claude.ai connectors            your Mac / PC                                 TICKEY (ESP32-S3)
 (Calendar, Slack, Gmail,  ┌──────────────────────────────┐   LAN (mDNS)    ┌────────────────────────┐
  GitHub, Linear, Vercel,  │ C-Ink Settings               │ ──────────────▶ │ wake ▸ sync ▸ re-rank  │
  Stripe, ONCE, Tavern…)   │  ├ runs `claude -p` every 15m│   sealed blob   │ ▸ draw ▸ deep sleep    │
        ▲                  │  │  read-only tools only     │                 │                        │
        └──────────────────│  ├ trims to a 20 KB digest   │   relay (opt.)  │ 12 h of cards cached,  │
                           │  ├ seals it per device (AES) │ ──▶ ☁ ──▶       │ ranked on-device as    │
                           │  └ serves it + previews it   │  ciphertext only│ the day moves          │
                           └──────────────────────────────┘                 └────────────────────────┘
```

1. **Claude does the judgement.** C-Ink Settings runs your own Claude Code (signed in with your Claude account) in headless mode with a prompt that asks for the cards worth showing over the next 12 hours. Claude Code brings the connectors you enabled on claude.ai. C-Ink allows only read-only tools, so nothing gets sent, edited or deleted.
2. **The device does the timing.** Each digest is a short timeline: cards with priorities and time windows. The TICKEY re-ranks it every time it wakes, so a meeting climbs to the top as it gets close and drops off when it ends, with or without Wi-Fi.
3. **E-ink first.** The screen only redraws when the pixels change. Fast 1-bit refreshes by default, a full refresh when the accent color matters or ghosting builds up. Deep sleep in between, waking for the next sync, the next screen change, or a button.

## Screens

| Screen | From | What it shows |
|---|---|---|
| Next up | Calendar | Countdown to your next meeting plus the one thing to know going in |
| Pull requests | GitHub (or the `gh` CLI) | Reviews waiting on you, your PRs failing CI |
| Slack | Slack | Messages grouped into *needs you now*, *today*, *when you can* |
| Inbox | Gmail | Threads where a person is waiting on your reply |
| Linear | Linear | Issues due soon or blocking someone |
| Ship status | Vercel / Cloudflare | Failed production deploys and error spikes |
| Revenue | Stripe | Today vs a usual day for that weekday, with a sparkline |
| Releases | ONCE | How your latest release is doing this week |
| Game night | Tavern | Next tabletop session and where your character stands |
| Focus window | Calendar + everything | When you have a real gap, the one thing worth doing with it |
| Brief & wrap | Everything | A morning brief and an end-of-day wrap |
| Docs | Drive | Docs shared with you or waiting on your comments (off by default) |

Plus a night screen during quiet hours, an "all clear" screen, an offline state, a device info screen and a setup screen. See [`docs/screens/`](docs/screens).

Buttons: **Up/Down** page through cards, **Back** returns to the top card (press again for device info), **hold Back** to sync now.

## Repo layout

| Path | What |
|---|---|
| `firmware/` | ESP-IDF 5.4 project for the TICKEY. `components/cink_core` is the portable renderer, ranking and digest parser; `components/cink_hal` the board layer; `main/` the app (sync, cache, USB setup link, sleep). |
| `sim/` | Builds `cink_core` for your computer: renders frames to PNG and runs the core tests. |
| `desktop/` | C-Ink Settings (Tauri 2, Mac / Windows / Linux) and `cink-bridge`, the Rust library that runs Claude, builds and seals digests and serves them. Also builds as a headless `cink-bridge` binary for an always-on machine. |
| `relay/` | Optional Cloudflare Worker so the TICKEY can sync away from home. Stores ciphertext only. |
| `tools/` | Font rasterizer (Inter → 1-bit bitmaps) and the firmware size check. |
| `docs/` | [Architecture](docs/architecture.md), [wire protocol](docs/protocol.md), [hardware notes](docs/hardware.md). |

## Getting started

1. **Install C-Ink Settings** from the latest CI build (macOS `.dmg`, Windows `.exe`/`.msi`). The builds are unsigned for now: on macOS right-click → Open the first time.
2. **Claude tab:** sign in with your Claude account (it uses the Claude Code CLI; the app links to the installer if it is missing). Add any connectors you want at [claude.ai/settings/connectors](https://claude.ai/settings/connectors), then **Recheck connections** on the Screens tab.
3. **Flash the firmware** (see [hardware notes](docs/hardware.md); `idf.py -p <port> flash` from `firmware/`).
4. **Device tab:** plug the TICKEY in with USB-C, add your Wi-Fi networks, press **Send to TICKEY**. It pairs, gets its key, and shows today's digest right away.

## Size

The firmware image is 1.08 MB, 13% of the 8 MB flash (CI fails the build above 2 MB). The fonts are the biggest piece of C-Ink's own code at ~210 KB; the rest is ESP-IDF, Wi-Fi and TLS. In RAM, the frame (86 KB) and the digest (42 KB) are static and ~94 KB of internal DRAM is left for Wi-Fi and TLS at runtime. No PSRAM required.

## Status

Everything above the display runs today: digest generation, sealing, LAN and relay sync, caching, ranking, rendering, USB setup, buttons and sleep. ENILINX has not published the TICKEY's pinout or the Modos panel driver yet (promised with the open firmware after the campaign), so the firmware currently builds with a headless display backend and the button and battery pins unset. Once the vendor BSP lands, finishing the port is one small file per peripheral; see [docs/hardware.md](docs/hardware.md).

## Development

```sh
# renderer + tests
cmake -S sim -B sim/build && cmake --build sim/build -j && ./sim/build/cink_tests
./sim/build/cink_sim sim/fixtures/day.json --tz PST8PDT,M3.2.0,M11.1.0 --now 1794350820 --out frame.png --paper

# bridge + desktop app
cd desktop && cargo test -p cink-bridge && npx @tauri-apps/cli@2 dev

# firmware
cd firmware && idf.py set-target esp32s3 build
```
