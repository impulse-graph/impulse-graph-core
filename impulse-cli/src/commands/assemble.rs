use impulse_graph::ffi::impulse_instruction_t;
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, Write};
use std::path::Path;

fn get_opcode(name: &str) -> Option<u8> {
    match name {
        "OP_HALT" => Some(0x00),
        "OP_NOP" => Some(0x01),
        "OP_INIT_INPUT_NODE" => Some(0x02),
        "OP_INIT_INPUT_SET" => Some(0x03),
        "OP_LOAD_CONST_INT" => Some(0x04),
        "OP_MAP_KEYS_TO_DENSE" => Some(0x05),
        "OP_LOAD_CONST_FLOAT" => Some(0x06),
        "OP_LOAD_CONST_STR_PREFIX" => Some(0x07),
        "OP_LOAD_INLINE_ARRAY" => Some(0x08),
        "OP_INIT_MOCK_GRAPH" => Some(0x09),
        "OP_LOAD_INLINE_SET" => Some(0x0A),
        "OP_INIT_MOCK_NODE_ATTR" => Some(0x0B),
        "OP_INIT_MOCK_EDGE_ATTR" => Some(0x0C),
        "OP_CSR_WALK_2HOP" => Some(0x0E),
        "OP_CSR_WALK_STATE" => Some(0x0F),

        "OP_CSR_WALK" => Some(0x10),
        "OP_CSR_WALK_FILTERED" => Some(0x11),
        "OP_CSR_DEGREE" => Some(0x12),
        "OP_CSR_WALK_PREDICATE" => Some(0x13),
        "OP_NODE_FILTER" => Some(0x14),
        "OP_NODE_FILTER_STR_PREFIX" => Some(0x15),
        "OP_CSR_WALK_REDUCE_SUM" => Some(0x16),
        "OP_CSR_WALK_REDUCE" => Some(0x17),
        "OP_CSC_WALK" => Some(0x18),
        "OP_HAS_CSR" => Some(0x19),
        "OP_HAS_CSC" => Some(0x1A),
        "OP_HAS_COO" => Some(0x1B),
        "OP_HAS_KEY_CATALOG" => Some(0x1C),
        "OP_ADAPTIVE_WALK" => Some(0x1D),
        "OP_CREATE_SCRATCH_INDEX" => Some(0x1E),
        "OP_DROP_SCRATCH_INDEX" => Some(0x1F),
        "OP_VEC_CMP_EQ" => Some(0x20),
        "OP_VEC_CMP_GT" => Some(0x21),
        "OP_VEC_CMP_LT" => Some(0x22),
        "OP_VEC_CMP_BETWEEN" => Some(0x23),
        "OP_MASK_AND" => Some(0x24),
        "OP_MASK_OR" => Some(0x25),
        "OP_MASK_NOT" => Some(0x26),
        "OP_VEC_BLEND" => Some(0x27),
        "OP_ASSERT_FINITE" => Some(0x2A),
        "OP_VEC_MATH_UNARY" => Some(0x2D),
        "OP_VEC_MATH_BINARY" => Some(0x2E),
        "OP_VEC_MATH_TERNARY" => Some(0x2F),

        "OP_SET_UNION" => Some(0x30),
        "OP_SET_INTERSECT" => Some(0x31),
        "OP_SET_DIFFERENCE" => Some(0x32),
        "OP_SET_CARDINALITY" => Some(0x33),
        "OP_VECTOR_MUL_ATTR" => Some(0x34),
        "OP_VECTOR_REDUCE_SUM" => Some(0x35),
        "OP_VECTOR_DIV" => Some(0x36),
        "OP_VECTOR_STR_CONCAT" => Some(0x37),
        "OP_FLOAT_VECTOR_SCALE" => Some(0x38),
        "OP_L1_NORM_DIFF" => Some(0x39),
        "OP_PROJECT_STATE" => Some(0x3A),
        "OP_COALESCE" => Some(0x3B),
        "OP_EXTRACT_VALIDITY" => Some(0x3C),
        "OP_VECTOR_TIME_VALID_AT" => Some(0x3D),

        "OP_CC_AFFOREST" => Some(0x40),
        "OP_MXV" => Some(0x41),
        "OP_VXM" => Some(0x42),
        "OP_EWISE_ADD" => Some(0x43),
        "OP_EWISE_MULT" => Some(0x44),
        "OP_REDUCE" => Some(0x45),
        "OP_CC_HOOK_COMPRESS" => Some(0x46),
        "OP_TC_SWEEP_BATCH" => Some(0x47),
        "OP_BRANDES_FORWARD" => Some(0x48),
        "OP_BRANDES_BACKWARD" => Some(0x49),
        "OP_DELTA_STEP_RELAX" => Some(0x4A),
        "OP_READ_EDGE_WEIGHT" => Some(0x4B),

        "OP_JMP" => Some(0x50),
        "OP_JZ" => Some(0x51),
        "OP_JNZ" => Some(0x52),
        "OP_LOOP_DECR" => Some(0x53),
        "OP_STABLE_CHECK" => Some(0x54),
        "OP_CALL" => Some(0x55),
        "OP_RET" => Some(0x56),
        "OP_ENTER_FRAME" => Some(0x57),
        "OP_LEAVE_FRAME" => Some(0x58),
        "OP_THROW" => Some(0x5A),
        "OP_ASSERT" => Some(0x5B),
        "OP_TRAP" => Some(0x5C),

        "OP_SAMPLE_NEIGHBORS" => Some(0x60),
        "OP_RANDOM_WALK" => Some(0x61),
        "OP_SCATTER_GATHER" => Some(0x62),
        "OP_REBAC_CHECK" => Some(0x63),
        "OP_ROARING_BITMAP_AND" => Some(0x64),
        "OP_ISLAND_DETECT" => Some(0x65),
        "OP_SPARSE_MATVEC" => Some(0x66),
        "OP_LOUVAIN_MODULARITY" => Some(0x67),
        "OP_KCORE_DECOMPOSITION" => Some(0x68),
        "OP_MOTIF_MATCH_3" => Some(0x69),
        "OP_GRAPH_ISOMORPHISM" => Some(0x6A),
        "OP_ROARING_BITMAP_OR" => Some(0x6B),
        "OP_ROARING_BITMAP_AND_NOT" => Some(0x6C),

        "OP_MOV" | "OP_MOVE" => Some(0x70),
        "OP_CLEAR_REG" => Some(0x71),
        "OP_LOAD_INDIRECT" => Some(0x72),
        "OP_ALLOC_SCRATCH" => Some(0x73),
        "OP_ASSERT_SCRATCH_BYTES" => Some(0x74),
        "OP_SET_MAX_DOP" => Some(0x75),

        "OP_LOAD_COLUMN_VECTOR" => Some(0x80),
        "OP_GATHER_NODE_ATTR" => Some(0x81),
        "OP_GATHER_EDGE_ATTR" => Some(0x82),
        "OP_BRIN_ZONE_SKIP" => Some(0x83),
        "OP_CSR_WALK_DIRECT_STORE" => Some(0x84),
        "OP_CSR_WALK_DENSE_STREAM" => Some(0x85),
        "OP_COO_WALK" => Some(0x86),
        "OP_CSC_WALK_DIRECT_STORE" => Some(0x87),
        "OP_FIXPOINT_KLEENE_STAR" => Some(0x88),
        "OP_SWAP_REG" => Some(0x89),
        "OP_FRONTIER_DIFF" => Some(0x8A),
        "OP_COO_WALK_FILTERED" => Some(0x8B),
        "OP_COO_WALK_REDUCE" => Some(0x8C),
        "OP_COO_WALK_DIRECT_STORE" => Some(0x8D),
        "OP_DENSE_WALK" => Some(0x8E),
        "OP_DENSE_WALK_BITMATRIX" => Some(0x8F),

        "OP_COLLECT_BITSET" => Some(0x90),
        "OP_COLLECT_ARRAY" => Some(0x91),
        "OP_MAP_DENSE_TO_KEYS" => Some(0x92),
        "OP_COLLECT_VALUE_MAP" => Some(0x93),
        "OP_COALESCE_BITSET" | "OP_DENSE_WALK_REDUCE" => Some(0x94),
        "OP_COALESCE_DENSE_ARRAY" | "OP_DENSE_WALK_DIRECT_STORE" => Some(0x95),

        // Edge Stream Shader Opcodes (0xA0 - 0xBF)
        "OP_COO_WALK_STREAM" => Some(0xA0),
        "OP_STREAM_FUNC_BEGIN" => Some(0xA1),
        "OP_STREAM_FUNC_END" => Some(0xA2),
        "OP_STREAM_LOAD_SRC" => Some(0xA3),
        "OP_STREAM_LOAD_EDGE" => Some(0xA4),
        "OP_STREAM_MATH_ADD" => Some(0xA5),
        "OP_STREAM_MATH_DIV" => Some(0xA6),
        "OP_STREAM_FILTER" => Some(0xA7),
        "OP_STREAM_REDUCE" => Some(0xA8),
        "OP_CSR_WALK_STREAM" => Some(0xA9),
        "OP_CSC_WALK_STREAM" => Some(0xAA),
        "OP_STREAM_LOAD_TGT" => Some(0xAB),
        "OP_STREAM_MATH_SUB" => Some(0xAC),
        "OP_STREAM_MATH_MUL" => Some(0xAD),
        "OP_STREAM_MATH_MOD" => Some(0xAE),
        "OP_STREAM_MATH_UNARY" => Some(0xAF),
        "OP_STREAM_CMP_EQ" => Some(0xB0),
        "OP_STREAM_CMP_NEQ" => Some(0xB1),
        "OP_STREAM_CMP_GT" => Some(0xB2),
        "OP_STREAM_CMP_LT" => Some(0xB3),
        "OP_STREAM_LOGIC_AND" => Some(0xB4),
        "OP_STREAM_LOGIC_OR" => Some(0xB5),
        "OP_STREAM_LOGIC_NOT" => Some(0xB6),
        "OP_STREAM_SELECT" => Some(0xB7),
        "OP_STREAM_REDUCE_ARGMIN" => Some(0xB8),
        "OP_STREAM_REDUCE_ARGMAX" => Some(0xB9),
        "OP_STREAM_LOAD_SRC_ID" => Some(0xBA),
        "OP_STREAM_LOAD_TGT_ID" => Some(0xBB),
        "OP_STREAM_LOAD_EDGE_ID" => Some(0xBC),
        "OP_STREAM_LOAD_CONST" => Some(0xBD),
        "OP_STREAM_YIELD" => Some(0xBE),
        "OP_STREAM_SCATTER_REDUCE" => Some(0xBF),

        // Multi-Lane Bit-Sliced Execution & Vector ANN Opcodes (0xC0 - 0xCB)
        "OP_LANE_INIT_SEEDS" => Some(0xC0),
        "OP_LANE_INIT_GROUPS" => Some(0xC1),
        "OP_LANE_SET_EDGE_MASK" => Some(0xC2),
        "OP_LANE_CSR_WALK" => Some(0xC3),
        "OP_LANE_CSC_WALK" => Some(0xC4),
        "OP_LANE_PRUNE_MASK" => Some(0xC5),
        "OP_LANE_FILTER_QUORUM" => Some(0xC6),
        "OP_LANE_REDUCE_AND" => Some(0xC7),
        "OP_LANE_REDUCE_ATTR" => Some(0xC8),
        "OP_LANE_SET_COVER" => Some(0xC9),
        "OP_VECTOR_ANN_SEARCH" => Some(0xCA),
        "OP_VECTOR_RERANK_DOT" => Some(0xCB),

        _ if name.starts_with("OP_RESERVED_") => {
            let hex_part = name.strip_prefix("OP_RESERVED_").unwrap();
            u8::from_str_radix(hex_part, 16).ok()
        }
        _ => None,
    }
}

pub fn run(input_path: &Path, output_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let file = File::open(input_path)?;
    let reader = BufReader::new(file);

    let mut symbols: HashMap<String, u32> = HashMap::new();
    let mut labels: HashMap<String, usize> = HashMap::new();

    struct RawInstruction {
        opcode_str: String,
        flags_str: String,
        dst_str: String,
        payload_str: String,
        line_num: usize,
    }

    let mut raw_program: Vec<RawInstruction> = Vec::new();
    let mut pc = 0;

    for (idx, line_res) in reader.lines().enumerate() {
        let line_num = idx + 1;
        let line = line_res?;
        let trimmed = line.trim();

        // Skip comments and empty lines
        if trimmed.is_empty() || trimmed.starts_with(';') {
            continue;
        }

        // Strip inline comments
        let clean_line = match trimmed.split_once(';') {
            Some((code, _)) => code.trim(),
            None => trimmed,
        };

        // Parse Directives
        if clean_line.starts_with('.') {
            let parts: Vec<&str> = clean_line.split_whitespace().collect();
            if parts.len() >= 4 && parts[2] == "=" {
                let directive = parts[0];
                let symbol_name = parts[1].replace('"', "");
                let value_str = parts[3].trim_end_matches(',');
                let value = value_str.parse::<u32>()?;

                match directive {
                    ".domain" => {
                        symbols.insert(format!("DOMAIN_{}", symbol_name.to_uppercase()), value);
                        symbols.insert(symbol_name.clone(), value);
                    }
                    ".relation" => {
                        symbols.insert(format!("REL_{}", symbol_name.to_uppercase()), value);
                        symbols.insert(symbol_name.clone(), value);
                    }
                    ".attribute" => {
                        symbols.insert(format!("ATTR_{}", symbol_name.to_uppercase()), value);
                        symbols.insert(symbol_name.clone(), value);
                    }
                    _ => {
                        return Err(
                            format!("Unknown directive {} at line {}", directive, line_num).into(),
                        )
                    }
                }
            }
            continue;
        }

        // Check for Label definition (e.g. "loop_start:")
        let mut instr_text = clean_line;
        if let Some((label_name, remaining)) = clean_line.split_once(':') {
            let label = label_name.trim().to_string();
            labels.insert(label, pc);
            instr_text = remaining.trim();
            if instr_text.is_empty() {
                continue; // line had only a label
            }
        }

        // Parse Instruction
        // Example: OP_CSR_WALK R4, R0, REL_0
        // Or: 0x00: OP_CSR_WALK R4, R0, REL_0 (strip PC prefix if present)
        if let Some((first, second)) = instr_text.split_once(':') {
            if first.trim().starts_with("0x") {
                instr_text = second.trim();
            }
        }

        let parts: Vec<&str> = instr_text.split_whitespace().collect();
        if parts.is_empty() {
            continue;
        }

        let opcode_str = parts[0].to_string();

        // Parse operands
        let args_str = parts[1..].join(" ");
        let args: Vec<&str> = args_str
            .split(',')
            .map(|s| s.trim())
            .filter(|s| !s.is_empty())
            .collect();

        let mut flags_str = "0".to_string();
        let mut dst_str = "0".to_string();
        let mut payload_str = "0".to_string();

        let mut is_named = false;
        for arg in &args {
            if let Some((k, v)) = arg.split_once('=') {
                is_named = true;
                match k.trim().to_lowercase().as_str() {
                    "flags" => flags_str = v.trim().to_string(),
                    "dst" => dst_str = v.trim().to_string(),
                    "payload" => payload_str = v.trim().to_string(),
                    _ => {}
                }
            }
        }

        if !is_named {
            match args.len() {
                0 => {}
                1 => {
                    // E.g. OP_HALT or OP_JMP label
                    payload_str = args[0].to_string();
                }
                2 => {
                    // E.g. OP_INIT_INPUT_NODE R0, 0
                    // Or OP_COLLECT_BITSET R63, R5
                    dst_str = args[0].to_string();
                    payload_str = args[1].to_string();
                }
                3 => {
                    // E.g. OP_CSR_WALK R4, R0, REL_0
                    // Or OP_EWISE_ADD R5, R4, R3 (which is OP_EWISE_ADD dst_reg, src1, src2)
                    dst_str = args[0].to_string();
                    flags_str = "0".to_string();
                    payload_str = format!("{} | {}", args[1], args[2]);
                }
                4 => {
                    // E.g. OP_EWISE_ADD R4, R1, R2, BINARY_OP_ADD
                    dst_str = args[0].to_string();
                    payload_str = format!("{} | {} | {}", args[1], args[2], args[3]);
                }
                5 => {
                    // E.g. OP_VECTOR_ANN_SEARCH Rdst, b0, b1, b2, b3
                    dst_str = args[0].to_string();
                    payload_str = format!("{} | {} | {} | {}", args[1], args[2], args[3], args[4]);
                }
                _ => {
                    return Err(format!(
                        "Invalid number of arguments in: {} at line {}",
                        instr_text, line_num
                    )
                    .into())
                }
            }
        }

        raw_program.push(RawInstruction {
            opcode_str,
            flags_str,
            dst_str,
            payload_str,
            line_num,
        });
        pc += 1;
    }

    // Pass 2: Binary Generation & Label Patching
    let mut compiled_bytes: Vec<u8> = Vec::new();

    for (current_pc, raw) in raw_program.iter().enumerate() {
        let opcode = get_opcode(&raw.opcode_str).ok_or_else(|| {
            format!(
                "Invalid opcode: {} at line {}",
                raw.opcode_str, raw.line_num
            )
        })?;

        let flags = parse_flags(&raw.flags_str)?;
        let dst_reg = parse_register(&raw.dst_str)?;
        let payload = parse_payload(&raw.payload_str, opcode, current_pc, &labels, &symbols)?;

        let instr = impulse_instruction_t {
            opcode,
            flags,
            dst_reg,
            payload,
        };

        let ptr = &instr as *const impulse_instruction_t as *const u8;
        let slice = unsafe { std::slice::from_raw_parts(ptr, 8) };
        compiled_bytes.extend_from_slice(slice);
    }

    let mut out_file = File::create(output_path)?;
    out_file.write_all(&compiled_bytes)?;
    println!(
        "Assembled successfully: {} instructions -> {:?}",
        raw_program.len(),
        output_path
    );
    Ok(())
}

fn is_register_str(s: &str) -> bool {
    let trimmed = s.trim().to_uppercase();
    if let Some(num_str) = trimmed.strip_prefix('R') {
        !num_str.is_empty() && num_str.chars().all(|c| c.is_ascii_digit())
    } else {
        trimmed.parse::<u16>().is_ok()
    }
}

fn parse_register(s: &str) -> Result<u16, Box<dyn std::error::Error>> {
    let trimmed = s.trim().to_uppercase();
    if trimmed.is_empty() {
        return Ok(0);
    }
    if let Some(num_str) = trimmed.strip_prefix('R') {
        if num_str.chars().all(|c| c.is_ascii_digit()) {
            if let Ok(reg) = num_str.parse::<u16>() {
                if reg < 64 {
                    return Ok(reg);
                }
            }
        }
    }
    // Also support raw integers
    if let Ok(reg) = trimmed.parse::<u16>() {
        if reg < 64 {
            return Ok(reg);
        }
    }
    Err(format!("Invalid register identifier: {}", s).into())
}

fn parse_flags(s: &str) -> Result<u8, Box<dyn std::error::Error>> {
    let trimmed = s.trim().to_uppercase();
    if trimmed.is_empty() {
        return Ok(0);
    }
    let mut val = 0u8;
    for part in trimmed.split('|').map(|x| x.trim()) {
        match part {
            "FLAG_MODE_BITSET" | "BITSET" => val |= 0x01,
            "FLAG_ACCUMULATE" | "ACCUMULATE" => val |= 0x02,
            "FLAG_INVERT" | "INVERT" => val |= 0x04,
            "FLAG_OFFHEAP" | "OFFHEAP" => val |= 0x08,
            _ => {
                if let Some(hex_part) = part.strip_prefix("0X") {
                    if let Ok(num) = u8::from_str_radix(hex_part, 16) {
                        val |= num;
                        continue;
                    }
                }
                if let Ok(num) = part.parse::<u8>() {
                    val |= num;
                } else if !part.is_empty() {
                    return Err(format!("Invalid flag value: {}", part).into());
                }
            }
        }
    }
    Ok(val)
}

fn resolve_single_val(
    part: &str,
    symbols: &HashMap<String, u32>,
) -> Result<u32, Box<dyn std::error::Error>> {
    let trimmed = part.trim();
    if trimmed.starts_with("0x") || trimmed.starts_with("0X") {
        if let Ok(v) = u32::from_str_radix(&trimmed[2..], 16) {
            return Ok(v);
        }
    }
    if is_register_str(trimmed) {
        return parse_register(trimmed).map(|r| r as u32);
    }
    if let Some(&v) = symbols.get(trimmed) {
        return Ok(v);
    }
    if let Some(v) = lookup_constant(trimmed) {
        return Ok(v);
    }
    if trimmed.starts_with("REL_") || trimmed.starts_with("DOMAIN_") || trimmed.starts_with("ATTR_")
    {
        let suffix = trimmed
            .trim_start_matches("REL_")
            .trim_start_matches("DOMAIN_")
            .trim_start_matches("ATTR_");
        return Ok(suffix.parse::<u32>().unwrap_or(0));
    }
    trimmed
        .parse::<u32>()
        .map_err(|_| format!("Invalid integer inside payload operand: {}", trimmed).into())
}

fn parse_payload(
    s: &str,
    opcode: u8,
    current_pc: usize,
    labels: &HashMap<String, usize>,
    symbols: &HashMap<String, u32>,
) -> Result<u32, Box<dyn std::error::Error>> {
    let parts: Vec<&str> = s.split('|').map(|x| x.trim()).collect();

    if opcode == 0x18 {
        // OP_CSC_WALK
        if parts.len() == 2 {
            let src = parse_register(parts[0])? as u32;
            let rel = resolve_single_val(parts[1], symbols)?;
            return Ok(src | (63 << 16) | (rel << 24));
        } else if parts.len() == 3 {
            let src = parse_register(parts[0])? as u32;
            let unv = parse_register(parts[1])? as u32;
            let rel = resolve_single_val(parts[2], symbols)?;
            return Ok(src | (unv << 16) | (rel << 24));
        }
    }

    if parts.len() == 1 {
        let trimmed = parts[0];
        if let Some(&target_pc) = labels.get(trimmed) {
            let offset = (target_pc as i32) - (current_pc as i32);
            return Ok(offset as u32);
        }
        return resolve_single_val(trimmed, symbols);
    }

    if parts.len() == 4 {
        let b0 = resolve_single_val(parts[0], symbols)? & 0xFF;
        let b1 = resolve_single_val(parts[1], symbols)? & 0xFF;
        let b2 = resolve_single_val(parts[2], symbols)? & 0xFF;
        let b3 = resolve_single_val(parts[3], symbols)? & 0xFF;
        return Ok(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    }

    if parts.len() == 3 {
        let b0 = resolve_single_val(parts[0], symbols)? & 0xFF;
        let b1 = resolve_single_val(parts[1], symbols)? & 0xFF;
        let hi16 = resolve_single_val(parts[2], symbols)? & 0xFFFF;
        return Ok(b0 | (b1 << 8) | (hi16 << 16));
    }

    let mut packed = 0u32;
    for (i, part) in parts.iter().enumerate() {
        let val = resolve_single_val(part, symbols)?;
        if i == 0 {
            packed |= val & 0xFFFF;
        } else if i == 1 {
            packed |= (val & 0xFFFF) << 16;
        }
    }
    Ok(packed)
}

fn lookup_constant(s: &str) -> Option<u32> {
    match s.trim().to_uppercase().as_str() {
        "SEMIRING_PLUS_TIMES" => Some(0),
        "SEMIRING_MIN_PLUS" => Some(1),
        "SEMIRING_MAX_MIN" => Some(2),
        "SEMIRING_BOOL" => Some(3),
        "BINARY_OP_ADD" => Some(0),
        "BINARY_OP_MUL" => Some(1),
        "BINARY_OP_MIN" => Some(2),
        "BINARY_OP_MAX" => Some(3),
        "BINARY_OP_AND" => Some(4),
        "BINARY_OP_OR" => Some(5),
        "BINARY_OP_ARGMIN" => Some(6),
        "BINARY_OP_ARGMAX" => Some(7),
        _ => None,
    }
}
