//! Canonical v0.11 `.impb` disassembler.

use impulse_graph::isa_tables::{IMPB_MAGIC, OPCODE_META};
use std::fs::File;
use std::io::Read;
use std::path::Path;

pub fn disassemble_bytes(buf: &[u8], _snapshot_path: Option<&Path>) -> Result<String, String> {
    let (code_bytes, ver) =
        if buf.len() >= 32 && u32::from_le_bytes(buf[0..4].try_into().unwrap()) == IMPB_MAGIC {
            let ver = u16::from_le_bytes(buf[4..6].try_into().unwrap());
            let word_count = u32::from_le_bytes(buf[8..12].try_into().unwrap()) as usize;
            let end = 32 + word_count * 8;
            if end > buf.len() {
                return Err("Truncated .impb container".to_string());
            }
            (&buf[32..end], ver)
        } else if buf.len().is_multiple_of(8) {
            (buf, 11u16)
        } else {
            return Err("Invalid .impb file length (must be multiple of 8 bytes)".to_string());
        };

    let count = code_bytes.len() / 8;
    let mut out = String::new();
    out.push_str(&format!(
        ".version 0.{}.0\n.instructions {}\n\n",
        ver, count
    ));
    for pc in 0..count {
        let w = u64::from_le_bytes(code_bytes[pc * 8..(pc + 1) * 8].try_into().unwrap());
        let op = (w & 0xFF) as usize;
        let sub = ((w >> 8) & 0xF) as u8;
        let flg = ((w >> 12) & 0xF) as u8;
        let s1 = ((w >> 16) & 0xF) as u8;
        let dst = ((w >> 20) & 0xF) as u8;
        let s3 = ((w >> 24) & 0xF) as u8;
        let s2 = ((w >> 28) & 0xF) as u8;
        let imm = ((w >> 32) & 0xFFFF_FFFF) as u32;
        let mnem = OPCODE_META[op].map(|m| m.mnemonic).unwrap_or("UNKNOWN");
        out.push_str(&format!(
            "  0x{:04x}:  {:<20} sub={} flg=0x{:x} dst={} s1={} s2={} s3={} imm=0x{:08x}\n",
            pc, mnem, sub, flg, dst, s1, s2, s3, imm
        ));
    }
    Ok(out)
}

pub fn run(input_path: &Path) -> Result<(), String> {
    let mut file =
        File::open(input_path).map_err(|e| format!("Failed to open {:?}: {}", input_path, e))?;
    let mut buf = Vec::new();
    file.read_to_end(&mut buf).map_err(|e| e.to_string())?;
    let dis = disassemble_bytes(&buf, None)?;
    print!("{}", dis);
    Ok(())
}
