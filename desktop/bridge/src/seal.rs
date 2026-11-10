//! Seals a digest for one device: "CINK" 0x01 | nonce | AES-256-GCM(ciphertext+tag),
//! with AAD = header + device id. Mirrors firmware/main/crypto.cpp.

use aes_gcm::aead::{Aead, KeyInit, Payload};
use aes_gcm::{Aes256Gcm, Nonce};
use anyhow::{anyhow, Result};
use base64::Engine;
use rand::RngCore;
use sha2::{Digest, Sha256};

const HEADER: &[u8; 5] = b"CINK\x01";

fn aad(device_id: &str) -> Vec<u8> {
    let mut a = HEADER.to_vec();
    a.extend(device_id.as_bytes().iter().take(16));
    a
}

pub fn new_key() -> String {
    let mut k = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut k);
    base64::engine::general_purpose::STANDARD.encode(k)
}

pub fn new_token() -> String {
    let mut k = [0u8; 24];
    rand::thread_rng().fill_bytes(&mut k);
    k.iter().map(|b| format!("{b:02x}")).collect()
}

fn key_bytes(key_b64: &str) -> Result<[u8; 32]> {
    let k = base64::engine::general_purpose::STANDARD.decode(key_b64)?;
    k.try_into().map_err(|_| anyhow!("key must be 32 bytes"))
}

pub fn seal(plain: &[u8], key_b64: &str, device_id: &str) -> Result<Vec<u8>> {
    let cipher = Aes256Gcm::new_from_slice(&key_bytes(key_b64)?)?;
    let mut nonce = [0u8; 12];
    rand::thread_rng().fill_bytes(&mut nonce);
    let ct = cipher
        .encrypt(Nonce::from_slice(&nonce), Payload { msg: plain, aad: &aad(device_id) })
        .map_err(|_| anyhow!("encryption failed"))?;
    let mut out = HEADER.to_vec();
    out.extend_from_slice(&nonce);
    out.extend(ct);
    Ok(out)
}

pub fn open(blob: &[u8], key_b64: &str, device_id: &str) -> Result<Vec<u8>> {
    if blob.len() < 5 + 12 + 16 || &blob[..5] != HEADER {
        return Err(anyhow!("not a C-Ink blob"));
    }
    let cipher = Aes256Gcm::new_from_slice(&key_bytes(key_b64)?)?;
    cipher
        .decrypt(Nonce::from_slice(&blob[5..17]), Payload { msg: &blob[17..], aad: &aad(device_id) })
        .map_err(|_| anyhow!("blob did not verify"))
}

/// ETag of the plaintext, so an unchanged digest costs the device a 304.
pub fn etag(plain: &[u8]) -> String {
    let h = Sha256::digest(plain);
    format!("\"{}\"", h[..12].iter().map(|b| format!("{b:02x}")).collect::<String>())
}

pub fn token_hash(token: &str) -> String {
    Sha256::digest(token.as_bytes()).iter().map(|b| format!("{b:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_and_binding() {
        let k = new_key();
        let b = seal(b"{\"v\":1}", &k, "C1A7-22F0").unwrap();
        assert_eq!(open(&b, &k, "C1A7-22F0").unwrap(), b"{\"v\":1}");
        assert!(open(&b, &k, "OTHER-DEV").is_err());
        assert!(open(&b, &new_key(), "C1A7-22F0").is_err());
        let mut t = b.clone();
        *t.last_mut().unwrap() ^= 1;
        assert!(open(&t, &k, "C1A7-22F0").is_err());
    }
}
