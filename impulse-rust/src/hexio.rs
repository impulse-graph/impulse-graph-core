//! SQLite `test_hexio.c`-Style In-Memory Binary Snapshot Inspection & Patching Utilities
//!
//! Provides deterministic byte-level reading and mutation over in-memory `.imps` snapshot buffers
//! (`Vec<u8>` / `&mut [u8]`) to exercise corruption recovery, bounds checking, and structural audits
//! without ever writing corrupted `.imps` files to disk (Rule 3.6 & 3.17).

use crate::spec::{compute_crc16, compute_sha256};

/// Reads `len` bytes starting at `offset` from `buf` and returns an uppercase hex string.
pub fn hexio_read(buf: &[u8], offset: usize, len: usize) -> Result<String, String> {
    if offset.saturating_add(len) > buf.len() {
        return Err(format!(
            "hexio_read out of bounds: offset={} len={} buf_len={}",
            offset,
            len,
            buf.len()
        ));
    }
    let mut out = String::with_capacity(len * 2);
    for &b in &buf[offset..offset + len] {
        out.push_str(&format!("{:02X}", b));
    }
    Ok(out)
}

fn hex_nibble(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

/// Decodes a hex string (ignoring whitespace) and writes the bytes into `buf` at `offset`.
/// Returns the number of bytes written.
pub fn hexio_write(buf: &mut [u8], offset: usize, hex: &str) -> Result<usize, String> {
    let clean: Vec<u8> = hex
        .bytes()
        .filter(|b| !b.is_ascii_whitespace() && *b != b'_')
        .collect();
    if !clean.len().is_multiple_of(2) {
        return Err(format!(
            "hexio_write requires an even number of hex digits, got {}",
            clean.len()
        ));
    }
    let byte_count = clean.len() / 2;
    if offset.saturating_add(byte_count) > buf.len() {
        return Err(format!(
            "hexio_write out of bounds: offset={} bytes={} buf_len={}",
            offset,
            byte_count,
            buf.len()
        ));
    }
    for i in 0..byte_count {
        let hi = hex_nibble(clean[i * 2])
            .ok_or_else(|| format!("Invalid hex character '{}'", clean[i * 2] as char))?;
        let lo = hex_nibble(clean[i * 2 + 1])
            .ok_or_else(|| format!("Invalid hex character '{}'", clean[i * 2 + 1] as char))?;
        buf[offset + i] = (hi << 4) | lo;
    }
    Ok(byte_count)
}

/// Writes a little-endian `u16` at `offset` in `buf`.
pub fn hexio_write_u16_le(buf: &mut [u8], offset: usize, val: u16) -> Result<(), String> {
    if offset + 2 > buf.len() {
        return Err(format!(
            "hexio_write_u16_le out of bounds at offset {}",
            offset
        ));
    }
    buf[offset..offset + 2].copy_from_slice(&val.to_le_bytes());
    Ok(())
}

/// Writes a little-endian `u32` at `offset` in `buf`.
pub fn hexio_write_u32_le(buf: &mut [u8], offset: usize, val: u32) -> Result<(), String> {
    if offset + 4 > buf.len() {
        return Err(format!(
            "hexio_write_u32_le out of bounds at offset {}",
            offset
        ));
    }
    buf[offset..offset + 4].copy_from_slice(&val.to_le_bytes());
    Ok(())
}

/// Writes a little-endian `u64` at `offset` in `buf`.
pub fn hexio_write_u64_le(buf: &mut [u8], offset: usize, val: u64) -> Result<(), String> {
    if offset + 8 > buf.len() {
        return Err(format!(
            "hexio_write_u64_le out of bounds at offset {}",
            offset
        ));
    }
    buf[offset..offset + 8].copy_from_slice(&val.to_le_bytes());
    Ok(())
}

/// Recomputes and writes the header CRC-16 (`0x3E..0x3F` over `0x00..0x3D`) and optionally
/// the file SHA-256 (`0x1E..0x3D`) so tests can mutate header/payload fields while keeping
/// the header checksum valid.
pub fn recompute_header_crc16(buf: &mut [u8]) -> Result<u16, String> {
    if buf.len() < 64 {
        return Err(format!("Snapshot buffer too small for header ({} < 64)", buf.len()));
    }
    let crc = compute_crc16(&buf[0..0x3E]);
    buf[0x3E..0x40].copy_from_slice(&crc.to_le_bytes());
    Ok(crc)
}

/// Recomputes both the file SHA-256 (zeroing `0x1E..0x40` during hash calculation) and the header CRC-16.
pub fn recompute_header_hashes(buf: &mut [u8]) -> Result<u16, String> {
    if buf.len() < 64 {
        return Err(format!("Snapshot buffer too small for header ({} < 64)", buf.len()));
    }
    buf[0x1E..0x40].fill(0);
    let sha = compute_sha256(buf);
    buf[0x1E..0x3E].copy_from_slice(&sha);
    recompute_header_crc16(buf)
}
