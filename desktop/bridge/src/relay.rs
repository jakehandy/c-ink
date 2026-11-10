//! Optional relay (relay/ in this repo, a ~60 line Cloudflare Worker). The
//! bridge uploads each device's sealed blob; the TICKEY fetches it from any
//! Wi-Fi. The relay only ever holds ciphertext.

use anyhow::{bail, Result};

use crate::seal;
use crate::settings::{Device, Relay};

pub fn put(r: &Relay, d: &Device, etag: &str, blob: &[u8]) -> Result<()> {
    let url = format!("{}/v1/d/{}", r.url.trim_end_matches('/'), d.id);
    let resp = ureq::put(&url)
        .set("Authorization", &format!("Bearer {}", r.write_token))
        .set("X-Cink-Token-Hash", &seal::token_hash(&d.token))
        .set("X-Cink-ETag", etag)
        .set("Content-Type", "application/octet-stream")
        .timeout(std::time::Duration::from_secs(15))
        .send_bytes(blob);
    match resp {
        Ok(r) if r.status() < 300 => Ok(()),
        Ok(r) => bail!("relay answered {}", r.status()),
        Err(e) => bail!("relay: {e}"),
    }
}
