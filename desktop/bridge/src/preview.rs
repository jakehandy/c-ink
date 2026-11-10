//! Pixel-exact previews: the same C++ renderer the TICKEY runs, linked in.

use std::os::raw::{c_char, c_int};

use anyhow::{bail, Result};
use parking_lot::Mutex;

pub const W: usize = 480;
pub const H: usize = 720;
const PLANE: usize = W / 8 * H;

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct FrameOpts {
    pub now: i64,
    pub last_sync: i64,
    pub battery: i32,
    pub charging: i32,
    pub net: i32,
    pub mono: i32,
    pub screen: i32,
    pub index: i32,
    pub disabled: u32,
}

extern "C" {
    fn cink_render_json(json: *const c_char, len: usize, o: *const FrameOpts, black: *mut u8, accent: *mut u8) -> c_int;
    fn cink_next_change(json: *const c_char, len: usize, now: i64, disabled: u32) -> i64;
}

static LOCK: Mutex<()> = Mutex::new(());

pub struct Frame {
    pub black: Vec<u8>,
    pub accent: Vec<u8>,
    pub cards: i32,
}

pub fn render(json: &[u8], o: &FrameOpts) -> Result<Frame> {
    let _g = LOCK.lock();
    let mut black = vec![0u8; PLANE];
    let mut accent = vec![0u8; PLANE];
    let n = unsafe { cink_render_json(json.as_ptr() as *const c_char, json.len(), o, black.as_mut_ptr(), accent.as_mut_ptr()) };
    if n < 0 {
        bail!("digest did not parse");
    }
    Ok(Frame { black, accent, cards: n })
}

pub fn next_change(json: &[u8], now: i64, disabled: u32) -> i64 {
    let _g = LOCK.lock();
    unsafe { cink_next_change(json.as_ptr() as *const c_char, json.len(), now, disabled) }
}

/// PNG of a frame. `paper` approximates the panel's colors instead of pure RGB.
pub fn png(f: &Frame, paper: bool) -> Vec<u8> {
    let pal: [[u8; 3]; 3] =
        if paper { [[236, 233, 224], [34, 34, 38], [200, 104, 72]] } else { [[255, 255, 255], [0, 0, 0], [217, 119, 87]] };
    let mut px = Vec::with_capacity(W * H * 3);
    for y in 0..H {
        for x in 0..W {
            let i = y * (W / 8) + x / 8;
            let m = 0x80u8 >> (x % 8);
            let c = if f.accent[i] & m != 0 { 2 } else if f.black[i] & m != 0 { 1 } else { 0 };
            px.extend_from_slice(&pal[c]);
        }
    }
    let mut out = vec![];
    {
        let mut e = ::png::Encoder::new(&mut out, W as u32, H as u32);
        e.set_color(::png::ColorType::Rgb);
        e.set_depth(::png::BitDepth::Eight);
        let mut w = e.write_header().expect("png header");
        w.write_image_data(&px).expect("png data");
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renders_a_digest() {
        let now = 1_794_350_820i64;
        let json = format!(r#"{{"v":1,"gen":{},"cards":[{{"k":"event","t":"Standup","at":{}}}]}}"#, now - 60, now + 600);
        let f = render(json.as_bytes(), &FrameOpts { now, battery: 80, ..Default::default() }).unwrap();
        assert_eq!(f.cards, 1);
        assert!(f.black.iter().any(|b| *b != 0));
        assert!(png(&f, true).starts_with(b"\x89PNG"));
        assert!(render(b"nope", &FrameOpts::default()).is_err());
        assert_eq!(next_change(json.as_bytes(), now, 0), now + 600 - 5 * 60);
    }
}
