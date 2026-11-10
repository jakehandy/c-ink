//! USB-C link to a TICKEY running C-Ink (newline JSON, replies prefixed
//! "@cink "). Used for first-time setup, pushing a digest without Wi-Fi,
//! remote button presses, grabbing the device's frame buffer, and the
//! hardware bring-up ops (board profile, panel test, button learning).

use std::io::{BufRead, BufReader, Write};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use base64::Engine;
use serde::Serialize;
use serde_json::{json, Value};

const ESPRESSIF_VID: u16 = 0x303A;

#[derive(Clone, Debug, Serialize)]
pub struct Port {
    pub name: String,
    pub label: String,
    pub likely_tickey: bool,
}

pub fn ports() -> Vec<Port> {
    let mut out: Vec<Port> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .filter(|p| !p.port_name.contains("Bluetooth"))
        .map(|p| {
            let (label, likely) = match &p.port_type {
                serialport::SerialPortType::UsbPort(u) => (
                    u.product.clone().unwrap_or_else(|| format!("USB {:04x}:{:04x}", u.vid, u.pid)),
                    u.vid == ESPRESSIF_VID,
                ),
                _ => ("Serial port".to_string(), false),
            };
            Port { name: p.port_name, label, likely_tickey: likely }
        })
        .collect();
    // macOS lists every USB serial device twice (cu.* and tty.*); cu.* is the one to open.
    if cfg!(target_os = "macos") {
        out.retain(|p| !p.name.starts_with("/dev/tty."));
    }
    out.sort_by_key(|p| !p.likely_tickey);
    out
}

pub struct Link {
    port: Box<dyn serialport::SerialPort>,
    reader: BufReader<Box<dyn serialport::SerialPort>>,
}

impl Link {
    pub fn open(name: &str) -> Result<Link> {
        let port = serialport::new(name, 115_200)
            .timeout(Duration::from_millis(200))
            .open()
            .with_context(|| format!("opening {name}"))?;
        let reader = BufReader::new(port.try_clone()?);
        Ok(Link { port, reader })
    }

    fn send(&mut self, v: &Value) -> Result<()> {
        let mut line = serde_json::to_vec(v)?;
        line.push(b'\n');
        self.port.write_all(&line)?;
        self.port.flush()?;
        Ok(())
    }

    /// Next "@cink" reply for `op`, skipping log lines.
    fn reply(&mut self, op: &str, timeout: Duration) -> Result<Value> {
        let start = Instant::now();
        let mut line = String::new();
        while start.elapsed() < timeout {
            line.clear();
            match self.reader.read_line(&mut line) {
                Ok(0) => continue,
                Ok(_) => {
                    if let Some(rest) = line.trim().strip_prefix("@cink ") {
                        if let Ok(v) = serde_json::from_str::<Value>(rest) {
                            if v["op"] == op {
                                return Ok(v);
                            }
                        }
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
                Err(e) => return Err(e.into()),
            }
        }
        bail!("no answer from the device for {op}")
    }

    fn call(&mut self, v: Value, timeout: Duration) -> Result<Value> {
        let op = v["op"].as_str().unwrap_or("").to_string();
        self.send(&v)?;
        let r = self.reply(&op, timeout)?;
        if r["ok"] != json!(true) {
            bail!("{op} failed: {}", r["error"].as_str().unwrap_or("unknown error"));
        }
        Ok(r)
    }

    pub fn hello(&mut self) -> Result<Value> {
        // The device may still be booting or mid-log; try a few times.
        for _ in 0..5 {
            if let Ok(v) = self.call(json!({"op": "hello"}), Duration::from_millis(1500)) {
                return Ok(v);
            }
        }
        bail!("this does not look like a TICKEY running C-Ink (flash the firmware first)")
    }

    pub fn configure(&mut self, cfg: Value) -> Result<()> {
        let mut c = cfg;
        c["op"] = json!("config");
        self.call(c, Duration::from_secs(5)).map(|_| ())
    }

    pub fn push_blob(&mut self, blob: &[u8]) -> Result<()> {
        self.call(json!({"op": "blob_begin", "len": blob.len()}), Duration::from_secs(3))?;
        for chunk in blob.chunks(3000) {
            let b64 = base64::engine::general_purpose::STANDARD.encode(chunk);
            self.call(json!({"op": "blob_data", "b64": b64}), Duration::from_secs(3))?;
        }
        self.call(json!({"op": "blob_end"}), Duration::from_secs(5)).map(|_| ())
    }

    pub fn press(&mut self, button: &str) -> Result<()> {
        self.call(json!({"op": "press", "button": button}), Duration::from_secs(3)).map(|_| ())
    }

    pub fn status(&mut self) -> Result<Value> {
        self.call(json!({"op": "status"}), Duration::from_secs(3))
    }

    pub fn sync(&mut self) -> Result<()> {
        self.call(json!({"op": "sync"}), Duration::from_secs(3)).map(|_| ())
    }

    pub fn reset(&mut self) -> Result<()> {
        self.call(json!({"op": "reset"}), Duration::from_secs(5)).map(|_| ())
    }

    /// The frame the device last drew, as (black, accent) planes.
    pub fn frame(&mut self) -> Result<(Vec<u8>, Vec<u8>)> {
        self.send(&json!({"op": "frame"}))?;
        let plane = crate::preview::W / 8 * crate::preview::H;
        let mut black = vec![0u8; plane];
        let mut accent = vec![0u8; plane];
        let start = Instant::now();
        let mut line = String::new();
        while start.elapsed() < Duration::from_secs(20) {
            line.clear();
            if self.reader.read_line(&mut line).unwrap_or(0) == 0 {
                continue;
            }
            let Some(rest) = line.trim().strip_prefix("@cink ") else { continue };
            let Ok(v) = serde_json::from_str::<Value>(rest) else { continue };
            if v["op"] == "frame_end" {
                return Ok((black, accent));
            }
            if v["op"] != "frame" {
                continue;
            }
            let off = v["off"].as_u64().unwrap_or(0) as usize;
            let bytes = base64::engine::general_purpose::STANDARD
                .decode(v["b64"].as_str().unwrap_or(""))
                .map_err(|e| anyhow!("bad frame chunk: {e}"))?;
            let dst = if v["plane"] == "accent" { &mut accent } else { &mut black };
            if off + bytes.len() <= dst.len() {
                dst[off..off + bytes.len()].copy_from_slice(&bytes);
            }
        }
        bail!("timed out reading the frame")
    }

    /// Sends a board profile (pins, panel type and command scripts; see
    /// tools/tickey-probe). The device saves it and restarts its panel.
    pub fn board(&mut self, profile: &Value) -> Result<()> {
        let line = serde_json::to_string(profile)?;
        if line.len() > 4400 {
            bail!("this profile is {} bytes; the device takes up to about 4.4 KB", line.len());
        }
        self.call(json!({"op": "board", "profile": profile}), Duration::from_secs(5)).map(|_| ())
    }

    /// The device's active board profile (pins and script sizes) and which
    /// panel backend it is running.
    pub fn board_get(&mut self) -> Result<Value> {
        self.call(json!({"op": "board_get"}), Duration::from_secs(3))
    }

    pub fn board_reset(&mut self) -> Result<()> {
        self.call(json!({"op": "board_reset"}), Duration::from_secs(5)).map(|_| ())
    }

    /// Full refresh of the current screen. The answer says how long the panel
    /// held BUSY: a few seconds means the controller took the frame, zero
    /// means it never answered (wrong pins or init script).
    pub fn panel_test(&mut self) -> Result<Value> {
        self.call(json!({"op": "panel_test"}), Duration::from_secs(90))
    }

    /// Watches every safe GPIO for `secs` seconds while the user presses the
    /// buttons and returns the level changes the device saw.
    pub fn learn(&mut self, secs: u32, active_low: bool) -> Result<Vec<PinEvent>> {
        let start = self.call(json!({"op": "learn", "secs": secs, "active_low": active_low}), Duration::from_secs(3))?;
        if start["watching"].as_u64().unwrap_or(0) == 0 {
            bail!("the device had no pins it could watch");
        }
        let deadline = Instant::now() + Duration::from_secs(secs as u64 + 5);
        let mut out = vec![];
        let mut line = String::new();
        while Instant::now() < deadline {
            line.clear();
            if self.reader.read_line(&mut line).unwrap_or(0) == 0 {
                continue;
            }
            let Some(rest) = line.trim().strip_prefix("@cink ") else { continue };
            let Ok(v) = serde_json::from_str::<Value>(rest) else { continue };
            match v["op"].as_str() {
                Some("learn_end") => return Ok(out),
                Some("pin") => out.push(PinEvent {
                    gpio: v["gpio"].as_u64().unwrap_or(0) as u8,
                    level: v["level"].as_u64().unwrap_or(0) as u8,
                    t_ms: v["t"].as_u64().unwrap_or(0),
                }),
                _ => {}
            }
        }
        bail!("the device stopped answering while learning buttons")
    }
}

#[derive(Clone, Copy, Debug, Serialize, PartialEq)]
pub struct PinEvent {
    pub gpio: u8,
    pub level: u8,
    pub t_ms: u64,
}

/// Turns a learning session where the user pressed Up, Down and Back once
/// each, in that order, into pin numbers. A press is a pin going to the
/// pressed level; pins that chatter (a clock, a floating line) are ignored.
pub fn assign_buttons(events: &[PinEvent], active_low: bool) -> [Option<u8>; 3] {
    let pressed = if active_low { 0 } else { 1 };
    let mut count = std::collections::HashMap::<u8, usize>::new();
    for e in events {
        *count.entry(e.gpio).or_default() += 1;
    }
    let mut order: Vec<u8> = vec![];
    for e in events {
        // One press and release is 2 changes; allow a little bounce.
        if e.level == pressed && count[&e.gpio] <= 8 && !order.contains(&e.gpio) {
            order.push(e.gpio);
        }
    }
    [order.first().copied(), order.get(1).copied(), order.get(2).copied()]
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ev(gpio: u8, level: u8, t_ms: u64) -> PinEvent {
        PinEvent { gpio, level, t_ms }
    }

    #[test]
    fn learns_three_buttons_in_order() {
        let mut e = vec![];
        // A noisy pin toggling the whole time.
        for i in 0..20 {
            e.push(ev(4, (i % 2) as u8, i * 500));
        }
        e.extend([ev(14, 0, 1000), ev(14, 1, 1100), ev(14, 0, 1110), ev(14, 1, 1120)]); // Up, with bounce
        e.extend([ev(0, 0, 3000), ev(0, 1, 3150)]); // Down
        e.extend([ev(21, 0, 5000), ev(21, 1, 5200)]); // Back
        e.sort_by_key(|x| x.t_ms);
        assert_eq!(assign_buttons(&e, true), [Some(14), Some(0), Some(21)]);
        // Active-high wiring: the release is what we saw first.
        assert_eq!(assign_buttons(&[ev(5, 1, 0), ev(5, 0, 90)], false), [Some(5), None, None]);
    }
}
