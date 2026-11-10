//! The big story's photo: downloaded by the bridge (never by the device),
//! cropped to the badge's frame and dithered to the panel's three inks.
//!
//! The wire form is two 1-bit planes (black, accent), row-major, MSB first,
//! base64. 216x120 drawn at 2x is 6.5 KB raw, about 8.7 KB in the digest.

use std::io::Read;

use anyhow::{bail, Context, Result};
use base64::Engine;
use image::{imageops::FilterType, RgbImage};
use serde_json::{json, Value};

pub const W: u32 = 216;
pub const H: u32 = 120;
const MAX_DOWNLOAD: u64 = 6 * 1024 * 1024;

/// The panel's inks as they look on the glass: paper, black, Claude orange.
const PAPER: [f32; 3] = [236.0, 233.0, 224.0];
const INK: [f32; 3] = [34.0, 34.0, 38.0];
const ACCENT: [f32; 3] = [200.0, 104.0, 72.0];

/// Downloads an image over https, refusing anything large or not an image.
pub fn fetch(url: &str) -> Result<Vec<u8>> {
    if !url.starts_with("https://") {
        bail!("photo url is not https");
    }
    let resp = ureq::AgentBuilder::new()
        .timeout(std::time::Duration::from_secs(20))
        .user_agent("C-Ink")
        .build()
        .get(url)
        .call()
        .context("downloading the story photo")?;
    let ct = resp.content_type().to_lowercase();
    if !ct.starts_with("image/") {
        bail!("story photo is {ct}, not an image");
    }
    let mut buf = vec![];
    resp.into_reader().take(MAX_DOWNLOAD + 1).read_to_end(&mut buf)?;
    if buf.len() as u64 > MAX_DOWNLOAD {
        bail!("story photo is over 6 MB");
    }
    Ok(buf)
}

/// Decodes and center-crops to the frame.
pub fn frame(bytes: &[u8]) -> Result<RgbImage> {
    let img = image::load_from_memory(bytes).context("decoding the story photo")?;
    if img.width() < 120 || img.height() < 60 {
        bail!("story photo is too small");
    }
    Ok(img.resize_to_fill(W, H, FilterType::Triangle).to_rgb8())
}

/// Floyd-Steinberg onto paper/black/accent (paper/black when mono). Returns
/// the black and accent planes. A little contrast first: e-paper is flat.
pub fn dither(img: &RgbImage, mono: bool) -> (Vec<u8>, Vec<u8>) {
    let (w, h) = (img.width() as usize, img.height() as usize);
    let mut px: Vec<[f32; 3]> = img
        .pixels()
        .map(|p| {
            let c = |v: u8| ((v as f32 - 128.0) * 1.15 + 136.0).clamp(0.0, 255.0);
            [c(p[0]), c(p[1]), c(p[2])]
        })
        .collect();
    let palette: &[[f32; 3]] = if mono { &[PAPER, INK] } else { &[PAPER, INK, ACCENT] };
    let stride = w / 8;
    let (mut black, mut accent) = (vec![0u8; stride * h], vec![0u8; stride * h]);
    for y in 0..h {
        for x in 0..w {
            let old = px[y * w + x];
            let (best, _) = palette
                .iter()
                .enumerate()
                .map(|(i, c)| {
                    // Weighted distance: the eye is kinder to red and blue error.
                    let d = [old[0] - c[0], old[1] - c[1], old[2] - c[2]];
                    (i, 0.30 * d[0] * d[0] + 0.59 * d[1] * d[1] + 0.11 * d[2] * d[2])
                })
                .fold((0, f32::MAX), |a, b| if b.1 < a.1 { b } else { a });
            let new = palette[best];
            let bit = 0x80u8 >> (x % 8);
            match best {
                1 => black[y * stride + x / 8] |= bit,
                2 => accent[y * stride + x / 8] |= bit,
                _ => {}
            }
            let err = [old[0] - new[0], old[1] - new[1], old[2] - new[2]];
            let mut spread = |dx: isize, dy: usize, f: f32| {
                let nx = x as isize + dx;
                if nx < 0 || nx >= w as isize || y + dy >= h {
                    return;
                }
                let p = &mut px[(y + dy) * w + nx as usize];
                for k in 0..3 {
                    p[k] += err[k] * f;
                }
            };
            spread(1, 0, 7.0 / 16.0);
            spread(-1, 1, 3.0 / 16.0);
            spread(0, 1, 5.0 / 16.0);
            spread(1, 1, 1.0 / 16.0);
        }
    }
    (black, accent)
}

/// The digest's "image" for a card: {"w", "h", "k", "a"} with base64 planes.
pub fn wire(img: &RgbImage, mono: bool) -> Value {
    let (k, a) = dither(img, mono);
    let b64 = base64::engine::general_purpose::STANDARD;
    let mut v = json!({"w": img.width(), "h": img.height(), "k": b64.encode(&k)});
    if a.iter().any(|&b| b != 0) {
        v["a"] = json!(b64.encode(&a));
    }
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dithers_to_the_three_inks() {
        let img = RgbImage::from_fn(W, H, |x, _| {
            if x < 72 { image::Rgb([250, 250, 245]) } else if x < 144 { image::Rgb([10, 10, 10]) } else { image::Rgb([205, 100, 70]) }
        });
        let (k, a) = dither(&img, false);
        let bit = |p: &Vec<u8>, x: usize, y: usize| p[y * 27 + x / 8] & (0x80 >> (x % 8)) != 0;
        assert!(!bit(&k, 10, 10) && !bit(&a, 10, 10));
        assert!(bit(&k, 100, 10) && !bit(&a, 100, 10));
        assert!(bit(&a, 200, 10));
        let (_, a) = dither(&img, true);
        assert!(a.iter().all(|&b| b == 0));
        let v = wire(&img, false);
        assert_eq!(v["w"], 216);
        assert!(v["k"].as_str().unwrap().len() > 4000);
    }

    #[test]
    fn refuses_plain_http() {
        assert!(fetch("http://example.com/a.jpg").is_err());
    }
}
