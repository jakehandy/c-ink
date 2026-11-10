//! Headless bridge for an always-on machine (a Mac mini, a home server):
//! same schedule and LAN endpoint as the desktop app, no window.
//!
//!   cink-bridge run              serve and sync on the schedule
//!   cink-bridge once             one digest run, print the wire JSON
//!   cink-bridge preview out.png  render the last digest like the device would

use anyhow::{bail, Result};
use cink_bridge::{preview, Bridge, Settings};

fn main() -> Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
    let args: Vec<String> = std::env::args().collect();
    let cmd = args.get(1).map(String::as_str).unwrap_or("run");
    let bridge = Bridge::new(Settings::load());
    match cmd {
        "run" => {
            bridge.start()?;
            log::info!("serving on port {}; Ctrl-C to stop", bridge.settings.read().port);
            loop {
                std::thread::park();
            }
        }
        "once" => {
            let st = bridge.run_now()?;
            if let Some(e) = st.last_error {
                bail!(e);
            }
            println!("{}", String::from_utf8_lossy(&bridge.wire().unwrap_or_default()));
            Ok(())
        }
        "preview" => {
            let out = args.get(2).cloned().unwrap_or_else(|| "c-ink-preview.png".into());
            let Some(wire) = bridge.wire() else { bail!("no digest yet; run `cink-bridge once` first") };
            let s = bridge.settings.read().clone();
            let opts = preview::FrameOpts {
                now: chrono::Local::now().timestamp(),
                last_sync: bridge.status().generated,
                battery: 80,
                mono: s.mono as i32,
                disabled: s.disabled_mask(),
                ..Default::default()
            };
            let f = preview::render(&wire, &opts)?;
            std::fs::write(&out, preview::png(&f, true))?;
            println!("wrote {out} ({} cards)", f.cards);
            Ok(())
        }
        _ => bail!("usage: cink-bridge [run|once|preview <out.png>]"),
    }
}
