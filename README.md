# C-Ink

A custom OS for the [ENILINX TICKEY](https://enilinxtickey.com/) e-ink badge that uses Claude to put the single most relevant thing in your day on its 3.7" screen: the meeting you are about to walk into, when to leave for it, the PRs, Slack threads and emails that need you, the Claude Code session waiting on your OK, the deploy that just broke, and a few fun ones on top. It is laid out for wearing the badge upright, with a small Clawd sitting on the Up next line whose pose follows what is on screen.

> [!WARNING]
> **C-Ink is highly experimental.** It replaces your TICKEY's firmware with unofficial software built without published hardware documentation. Flashing it can leave your device unusable, and you do it entirely at your own risk. **I am in no way responsible for bricked or broken devices**, lost data, or anything else that happens from using this project. Keep a backup of the stock firmware (`tools/tickey-probe` makes one) before you flash anything.

<p>
<img src="docs/screens/next-up.png" width="24%" alt="Next meeting with a countdown">
<img src="docs/screens/claude-code.png" width="24%" alt="Claude Code sessions waiting on you">
<img src="docs/screens/inbox.png" width="24%" alt="Inbox across three email accounts">
<img src="docs/screens/news.png" width="24%" alt="Morning news in three sections">
</p>

These are real frames from the device's renderer (`sim/`), with invented sample data. Every screen is further down.

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

Every source you connect feeds one or more screens. Each screen has a full view, shown when it is the most relevant thing, and a one-line row in **Up next**. Each can be switched off on its own in the Marketplace, and the device skips switched-off screens even in its cached digest.

| | | | |
|:-:|:-:|:-:|:-:|
| <img src="docs/screens/next-up.png" width="200"><br>**Next up** | <img src="docs/screens/slack.png" width="200"><br>**Slack** | <img src="docs/screens/inbox.png" width="200"><br>**Inbox** | <img src="docs/screens/review.png" width="200"><br>**Pull requests** |
| <img src="docs/screens/claude-code.png" width="200"><br>**Claude Code** | <img src="docs/screens/news.png" width="200"><br>**Morning news** | <img src="docs/screens/weather.png" width="200"><br>**Weather** | <img src="docs/screens/deliveries.png" width="200"><br>**Deliveries** |
| <img src="docs/screens/ship.png" width="200"><br>**Ship status** | <img src="docs/screens/cloudflare.png" width="200"><br>**Cloudflare alerts** | <img src="docs/screens/database.png" width="200"><br>**Database health** | <img src="docs/screens/commute.png" width="200"><br>**Commute** |
| <img src="docs/screens/focus.png" width="200"><br>**Focus window** | <img src="docs/screens/money.png" width="200"><br>**Revenue** | <img src="docs/screens/traffic.png" width="200"><br>**Traffic** | <img src="docs/screens/releases.png" width="200"><br>**Releases** |
| <img src="docs/screens/quest.png" width="200"><br>**Game night** | <img src="docs/screens/wins.png" width="200"><br>**This week's wins** | <img src="docs/screens/all-clear.png" width="200"><br>**All clear** | <img src="docs/screens/night.png" width="200"><br>**Night** |
| <img src="docs/screens/next-up-mono.png" width="200"><br>**Black and white** | <img src="docs/screens/offline.png" width="200"><br>**Offline** | <img src="docs/screens/info.png" width="200"><br>**Device info** | <img src="docs/screens/setup.png" width="200"><br>**Setup** |

| Screen | Source | What it shows |
|---|---|---|
| Next up | Google Calendar | Countdown to your next meeting plus the one thing to know going in |
| Commute | Google Calendar | When to leave for your next in-person meeting. Claude estimates door to door from where you will be before it and how you get around, plus your buffer |
| Focus window | Google Calendar | When you have a real gap, the one thing worth doing with it |
| Slack | Slack | Messages grouped into *needs you now*, *today*, *when you can* |
| Inbox | Gmail (one connector per account) | Threads where a person is waiting on you, with a section per account |
| Pull requests | GitHub (or the `gh` CLI) | Reviews waiting on you, your PRs failing CI |
| This week's wins | GitHub, Linear, Vercel, ONCE | Merged PRs, closed issues and releases, from Thursday afternoon on |
| Linear | Linear | Issues due soon or blocking someone |
| Ship status | Vercel | Failed production deploys and error spikes |
| Traffic | Vercel Web Analytics | Visitors today against a usual day, when it is unusual |
| Cloudflare alerts | Cloudflare (API token) | Notifications from the last day: Workers errors, origin and DDoS alerts, certificates |
| Database health | Supabase | Tables with row level security off, advisor warnings, paused projects |
| Revenue | Stripe | Today against a usual day for that weekday, with a sparkline |
| Deliveries | Shop, Link (via Gmail) | Packages arriving today or delayed |
| Releases | ONCE | How your latest release is doing this week |
| Game night | Tavern | Your next tabletop session and where your character stands |
| Docs | Google Drive | Docs waiting on your comments (off by default) |
| Claude Code | This computer | Sessions waiting on your OK or your reply, commits today, token use |
| Weather | Open-Meteo | Now, today's high and low, and when rain starts, for the place you set |
| Morning news | The web | A morning briefing from your own prompt, researched once a morning |
| Brief & wrap, Anything else | Claude | A morning brief, an end-of-day wrap, and anything important that fits nowhere else |

Three screens never cost a Claude run: **Weather** comes straight from Open-Meteo (only the place you set is sent there), **Cloudflare alerts** from Cloudflare's notification history with a read-only token, and **Claude Code** from the session logs on your computer. That last one reads timing, token counts, folder and branch names only, never what was said, and none of it goes to Claude. **Morning news** is one extra Claude run a morning that can only search and read the web: no connectors are loaded in it, so nothing on the open web can reach your mail or calendar.

Buttons: **Up/Down** page through cards, **Back** returns to the top card (press again for device info), **hold Back** to sync now. If you wear it the other way up, tick **Orientation** in Settings: the picture turns 180° and Up and Down swap.

### Clawd

<img src="docs/screens/clawd.png" width="240" align="right" alt="All of Clawd's poses">

Clawd sits on the right end of the Up next line and changes pose with the top card: surprised when a meeting is five minutes out, typing while a Claude Code session works, waving when one needs you, reading the paper with the news, sunglasses on a clear day, a rain cloud when it is wet, walking for a commute, proud on a good week, a sweat drop when three things are on fire, asleep at night, puzzled offline. 25 poses in all, drawn from the same block-pixel sprite as Claude Code's terminal banner.

<br clear="right">

## Marketplace

C-Ink Settings lists every source in a small Marketplace. Each one opens to a short connect flow and the switches for its screens.

<p>
<img src="docs/app/marketplace.png" width="49%" alt="The Marketplace">
<img src="docs/app/marketplace-gmail.png" width="49%" alt="Gmail with three accounts and its screens">
</p>

- **claude.ai connectors** (Calendar, Slack, Gmail, GitHub, Linear, Vercel, Supabase, Stripe, Shop, ONCE, Tavern, Drive): connect on claude.ai, then **Recheck**. Add Gmail once per account for a multi-account inbox.
- **Built in:** Weather takes a zip code, city or coordinates. Cloudflare takes a read-only API token (Notifications: Read, Account Settings: Read). News takes a sentence describing what you want, for example *"Local news for my zip code, progressive political news, and general news."*
- **On this computer:** Claude Code is found automatically.
- **Link** has no connector, so Deliveries reads Link's order and shipping emails from your Gmail.

## Repo layout

| Path | What |
|---|---|
| `firmware/` | ESP-IDF 5.4 project for the TICKEY. `components/cink_core` is the portable renderer, ranking and digest parser; `components/cink_hal` the board layer; `main/` the app (sync, cache, USB setup link, sleep). |
| `sim/` | Builds `cink_core` for your computer: renders frames to PNG and runs the core tests. |
| `desktop/` | C-Ink Settings (Tauri 2, Mac / Windows / Linux) and `cink-bridge`, the Rust library that runs Claude, builds and seals digests and serves them. Also builds as a headless `cink-bridge` binary for an always-on machine. |
| `relay/` | Optional Cloudflare Worker so the TICKEY can sync away from home. Stores ciphertext only. |
| `tools/` | Font and app-icon rasterizers, the screenshot renderer for this README, the firmware size check, and `tickey-probe`, which reads the TICKEY's pin map off its stock firmware. |
| `docs/` | [Architecture](docs/architecture.md), [wire protocol](docs/protocol.md), [hardware notes](docs/hardware.md). |

## Getting started

1. **Install C-Ink Settings** from the latest CI build (macOS `.dmg`, Windows `.exe`/`.msi`). The builds are unsigned for now: on macOS right-click → Open the first time.
2. **Claude tab:** sign in with your Claude account (it uses the Claude Code CLI; the app links to the installer if it is missing). Then open the **Marketplace**, connect what you use and switch on the screens you want.
3. **Back up and flash the firmware** (read the warning at the top first; see [hardware notes](docs/hardware.md); `idf.py -p <port> flash` from `firmware/`).
4. **Device tab:** plug the TICKEY in with USB-C, add your Wi-Fi networks, press **Send to TICKEY**. It pairs, gets its key, and shows today's digest right away.

## Size

The firmware image is 1.1 MB, 13% of the 8 MB flash (CI fails the build above 2 MB). The fonts are the biggest piece of C-Ink's own code at ~210 KB; the rest is ESP-IDF, Wi-Fi and TLS. In RAM, the frame (86 KB) and the digest (42 KB) are static and ~85 KB of internal DRAM is left for Wi-Fi and TLS at runtime. No PSRAM required.

## Status

Everything above the display runs today: digest generation, sealing, LAN and relay sync, caching, ranking, rendering, USB setup, buttons and sleep. ENILINX has not published the TICKEY's pinout or panel driver yet, so C-Ink learns them instead of waiting: `tools/tickey-probe` reads the pin map off the stock firmware over USB-JTAG, C-Ink Settings sends it to the device as a board profile, a scripted SPI e-paper driver runs the panel from that profile, and **Learn buttons** finds the buttons by watching the pins. If the probe shows a waveform-driven parallel panel (likely, given the Modos driver), that needs one more backend; see [docs/hardware.md](docs/hardware.md#getting-past-the-missing-panel-driver) for the steps and the honest limits.

## Development

```sh
# renderer + tests
cmake -S sim -B sim/build && cmake --build sim/build -j && ./sim/build/cink_tests
./sim/build/cink_sim sim/fixtures/day.json --tz PST8PDT,M3.2.0,M11.1.0 --now 1794350820 --out frame.png --paper

# every screenshot in this README
python3 sim/fixtures/make_fixtures.py && python3 tools/render_screens.py

# bridge + desktop app
cd desktop && cargo test -p cink-bridge && npx @tauri-apps/cli@2 dev

# firmware
cd firmware && idf.py set-target esp32s3 build
```

## Trademarks

Brand names and logos (Google Calendar, Gmail, Google Drive, Slack, GitHub, Linear, Vercel, Cloudflare, Supabase, Stripe, Shopify, Notion, Spotify, Claude) belong to their owners. Monochrome marks come from [Simple Icons](https://simpleicons.org) (CC0) and are used only to label where a card's information comes from. C-Ink is not affiliated with or endorsed by any of them, or by ENILINX.
