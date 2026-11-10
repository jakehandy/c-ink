//! USB-C link to a TICKEY running C-Ink (newline JSON, replies prefixed
//! "@cink "). Used for first-time setup, pushing a digest without Wi-Fi,
//! remote button presses and grabbing the device's frame buffer.

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
        let plane = 720 / 8 * 480;
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
}
