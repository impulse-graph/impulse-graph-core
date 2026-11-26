//! Canonical v0.11 `.impas` assembler emitting 32-byte-header `.impb` containers.

use impulse_graph::isa_tables::{encode_word, IMPB_MAGIC, IMPB_VERSION, OPCODE_META};
use std::fs::File;
use std::io::{BufRead, BufReader, Write};
use std::path::Path;

fn lookup_opcode(token: &str) -> Option<impulse_graph::isa_tables::Opcode> {
    let upper = token.trim().to_ascii_uppercase();
    let base = upper.split('.').next().unwrap_or(&upper);
    for meta in OPCODE_META.iter().flatten() {
        if meta.mnemonic == base || meta.name == base {
            return Some(meta.op);
        }
    }
    None
}

fn parse_u32_val(s: &str) -> Result<u32, String> {
    let t = s.trim();
    if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        u32::from_str_radix(hex, 16).map_err(|e| format!("Invalid hex '{}': {}", t, e))
    } else if let Ok(v) = t.parse::<u32>() {
        Ok(v)
    } else if let Ok(v) = t.parse::<i32>() {
        Ok(v as u32)
    } else {
        Err(format!("Invalid integer '{}'", t))
    }
}

pub fn run(input_path: &Path, output_path: &Path) -> Result<(), String> {
    let file =
        File::open(input_path).map_err(|e| format!("Failed to open {:?}: {}", input_path, e))?;
    let reader = BufReader::new(file);

    let mut words: Vec<u64> = Vec::new();
    for (line_idx, line_res) in reader.lines().enumerate() {
        let lineno = line_idx + 1;
        let raw = line_res.map_err(|e| format!("Read error at line {}: {}", lineno, e))?;
        let line = raw.split(';').next().unwrap_or("").trim();
        if line.is_empty() || line.starts_with('.') || line.starts_with('#') {
            continue;
        }
        // Strip optional leading address prefix `0x0000:`
        let rest = if let Some(colon) = line.find(':') {
            let prefix = line[..colon].trim();
            if prefix.starts_with("0x")
                || prefix.starts_with("0X")
                || prefix.chars().all(|c| c.is_ascii_digit())
            {
                line[colon + 1..].trim()
            } else {
                // Label line
                if line[colon + 1..].trim().is_empty() {
                    continue;
                }
                line[colon + 1..].trim()
            }
        } else {
            line
        };
        if rest.is_empty() {
            continue;
        }
        let tokens: Vec<&str> = rest.split_whitespace().collect();
        let op = lookup_opcode(tokens[0])
            .ok_or_else(|| format!("Invalid opcode: {} at line {}", tokens[0], lineno))?;

        let mut subop: u8 = 0;
        let mut flags: u8 = 0;
        let mut dst: u8 = 0;
        let mut s1: u8 = 0;
        let mut s2: u8 = 0;
        let mut s3: u8 = 0;
        let mut imm32: u32 = 0;

        for tok in &tokens[1..] {
            if let Some((k, v)) = tok.split_once('=') {
                let val = parse_u32_val(v)?;
                match k {
                    "sub" | "subop" => subop = (val & 0xF) as u8,
                    "flg" | "flags" => flags = (val & 0xF) as u8,
                    "dst" => dst = (val & 0xF) as u8,
                    "s1" => s1 = (val & 0xF) as u8,
                    "s2" => s2 = (val & 0xF) as u8,
                    "s3" => s3 = (val & 0xF) as u8,
                    "imm" | "imm32" => imm32 = val,
                    _ => {}
                }
            }
        }
        words.push(encode_word(op, flags, subop, dst, s1, s2, s3, imm32));
    }

    // Write canonical 32-byte IMPB v11 header + 64-bit code words
    let mut out = File::create(output_path)
        .map_err(|e| format!("Failed to create {:?}: {}", output_path, e))?;
    let mut hdr = [0u8; 32];
    hdr[0..4].copy_from_slice(&IMPB_MAGIC.to_le_bytes());
    hdr[4..6].copy_from_slice(&IMPB_VERSION.to_le_bytes());
    hdr[6] = 0; // header_flags
    hdr[7] = 16; // max_call_depth
    hdr[8..12].copy_from_slice(&(words.len() as u32).to_le_bytes());
    hdr[12..16].copy_from_slice(&0u32.to_le_bytes()); // const_pool_bytes
    hdr[16..20].copy_from_slice(&0u32.to_le_bytes()); // entry_pc
    hdr[20..22].copy_from_slice(&0u16.to_le_bytes()); // required_domains
    out.write_all(&hdr).map_err(|e| e.to_string())?;
    for w in &words {
        out.write_all(&w.to_le_bytes()).map_err(|e| e.to_string())?;
    }
    Ok(())
}
