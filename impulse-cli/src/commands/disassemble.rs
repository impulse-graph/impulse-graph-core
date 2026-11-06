use impulse_graph::ffi::impulse_instruction_t;
use std::fs::File;
use std::io::Read;
use std::path::Path;

fn get_opcode_name(opcode: u8) -> &'static str {
    match opcode {
        0x00 => "OP_HALT",
        0x01 => "OP_NOP",
        0x02 => "OP_INIT_INPUT_NODE",
        0x03 => "OP_INIT_INPUT_SET",
        0x04 => "OP_LOAD_CONST_INT",
        0x05 => "OP_MAP_KEYS_TO_DENSE",
        0x06 => "OP_LOAD_CONST_FLOAT",
        0x07 => "OP_LOAD_CONST_STR_PREFIX",
        0x08 => "OP_LOAD_INLINE_ARRAY",
        0x09 => "OP_INIT_MOCK_GRAPH",

        0x10 => "OP_CSR_WALK",
        0x11 => "OP_CSR_WALK_FILTERED",
        0x12 => "OP_CSR_DEGREE",
        0x13 => "OP_CSR_WALK_PREDICATE",
        0x14 => "OP_NODE_FILTER",
        0x15 => "OP_NODE_FILTER_STR_PREFIX",
        0x16 => "OP_CSR_WALK_REDUCE_SUM",
        0x17 => "OP_CSR_WALK_REDUCE",
        0x18 => "OP_CSC_WALK",
        0x19 => "OP_HAS_CSR",
        0x1A => "OP_HAS_CSC",
        0x1B => "OP_HAS_COO",
        0x1C => "OP_HAS_KEY_CATALOG",
        0x1D => "OP_ADAPTIVE_WALK",
        0x1E => "OP_CREATE_SCRATCH_INDEX",
        0x1F => "OP_DROP_SCRATCH_INDEX",
        0x20 => "OP_VEC_CMP_EQ",
        0x21 => "OP_VEC_CMP_GT",
        0x22 => "OP_VEC_CMP_LT",
        0x23 => "OP_VEC_CMP_BETWEEN",
        0x24 => "OP_MASK_AND",
        0x25 => "OP_MASK_OR",
        0x26 => "OP_MASK_NOT",
        0x27 => "OP_VEC_BLEND",
        0x2A => "OP_ASSERT_FINITE",
        0x2D => "OP_VEC_MATH_UNARY",
        0x2E => "OP_VEC_MATH_BINARY",
        0x2F => "OP_VEC_MATH_TERNARY",

        0x30 => "OP_SET_UNION",
        0x31 => "OP_SET_INTERSECT",
        0x32 => "OP_SET_DIFFERENCE",
        0x33 => "OP_SET_CARDINALITY",
        0x34 => "OP_VECTOR_MUL_ATTR",
        0x35 => "OP_VECTOR_REDUCE_SUM",
        0x36 => "OP_VECTOR_DIV",
        0x37 => "OP_VECTOR_STR_CONCAT",
        0x38 => "OP_FLOAT_VECTOR_SCALE",
        0x39 => "OP_L1_NORM_DIFF",
        0x3A => "OP_PROJECT_STATE",
        0x3B => "OP_COALESCE",
        0x3C => "OP_EXTRACT_VALIDITY",
        0x3D => "OP_VECTOR_TIME_VALID_AT",

        0x40 => "OP_CC_AFFOREST",
        0x41 => "OP_MXV",
        0x42 => "OP_VXM",
        0x43 => "OP_EWISE_ADD",
        0x44 => "OP_EWISE_MULT",
        0x45 => "OP_REDUCE",
        0x46 => "OP_CC_HOOK_COMPRESS",
        0x47 => "OP_TC_SWEEP_BATCH",
        0x48 => "OP_BRANDES_FORWARD",
        0x49 => "OP_BRANDES_BACKWARD",
        0x4A => "OP_DELTA_STEP_RELAX",
        0x4B => "OP_READ_EDGE_WEIGHT",

        0x50 => "OP_JMP",
        0x51 => "OP_JZ",
        0x52 => "OP_JNZ",
        0x53 => "OP_LOOP_DECR",
        0x54 => "OP_STABLE_CHECK",
        0x55 => "OP_CALL",
        0x56 => "OP_RET",
        0x57 => "OP_ENTER_FRAME",
        0x58 => "OP_LEAVE_FRAME",
        0x5A => "OP_THROW",
        0x5B => "OP_ASSERT",
        0x5C => "OP_TRAP",

        0x60 => "OP_SAMPLE_NEIGHBORS",
        0x61 => "OP_RANDOM_WALK",
        0x62 => "OP_SCATTER_GATHER",
        0x63 => "OP_REBAC_CHECK",
        0x64 => "OP_ROARING_BITMAP_AND",
        0x65 => "OP_ISLAND_DETECT",
        0x66 => "OP_SPARSE_MATVEC",
        0x67 => "OP_LOUVAIN_MODULARITY",
        0x68 => "OP_KCORE_DECOMPOSITION",
        0x69 => "OP_MOTIF_MATCH_3",
        0x6A => "OP_GRAPH_ISOMORPHISM",
        0x6B => "OP_ROARING_BITMAP_OR",
        0x6C => "OP_ROARING_BITMAP_AND_NOT",

        0x70 => "OP_MOV",
        0x71 => "OP_CLEAR_REG",
        0x72 => "OP_LOAD_INDIRECT",
        0x73 => "OP_ALLOC_SCRATCH",
        0x74 => "OP_ASSERT_SCRATCH_BYTES",
        0x75 => "OP_SET_MAX_DOP",

        0x80 => "OP_LOAD_COLUMN_VECTOR",
        0x81 => "OP_GATHER_NODE_ATTR",
        0x82 => "OP_GATHER_EDGE_ATTR",
        0x83 => "OP_BRIN_ZONE_SKIP",
        0x84 => "OP_CSR_WALK_DIRECT_STORE",
        0x85 => "OP_CSR_WALK_DENSE_STREAM",
        0x86 => "OP_COO_WALK",
        0x87 => "OP_CSC_WALK_DIRECT_STORE",
        0x88 => "OP_FIXPOINT_KLEENE_STAR",
        0x89 => "OP_SWAP_REG",
        0x8A => "OP_FRONTIER_DIFF",
        0x8B => "OP_COO_WALK_FILTERED",
        0x8C => "OP_COO_WALK_REDUCE",
        0x8D => "OP_COO_WALK_DIRECT_STORE",
        0x8E => "OP_DENSE_WALK",
        0x8F => "OP_DENSE_WALK_BITMATRIX",

        0x90 => "OP_COLLECT_BITSET",
        0x91 => "OP_COLLECT_ARRAY",
        0x92 => "OP_MAP_DENSE_TO_KEYS",
        0x93 => "OP_COLLECT_VALUE_MAP",
        0x94 => "OP_COALESCE_BITSET",
        0x95 => "OP_COALESCE_DENSE_ARRAY",

        // Edge Stream Shader Opcodes (0xA0 - 0xBF)
        0xA0 => "OP_COO_WALK_STREAM",
        0xA1 => "OP_STREAM_FUNC_BEGIN",
        0xA2 => "OP_STREAM_FUNC_END",
        0xA3 => "OP_STREAM_LOAD_SRC",
        0xA4 => "OP_STREAM_LOAD_EDGE",
        0xA5 => "OP_STREAM_MATH_ADD",
        0xA6 => "OP_STREAM_MATH_DIV",
        0xA7 => "OP_STREAM_FILTER",
        0xA8 => "OP_STREAM_REDUCE",
        0xA9 => "OP_CSR_WALK_STREAM",
        0xAA => "OP_CSC_WALK_STREAM",
        0xAB => "OP_STREAM_LOAD_TGT",
        0xAC => "OP_STREAM_MATH_SUB",
        0xAD => "OP_STREAM_MATH_MUL",
        0xAE => "OP_STREAM_MATH_MOD",
        0xAF => "OP_STREAM_MATH_UNARY",
        0xB0 => "OP_STREAM_CMP_EQ",
        0xB1 => "OP_STREAM_CMP_NEQ",
        0xB2 => "OP_STREAM_CMP_GT",
        0xB3 => "OP_STREAM_CMP_LT",
        0xB4 => "OP_STREAM_LOGIC_AND",
        0xB5 => "OP_STREAM_LOGIC_OR",
        0xB6 => "OP_STREAM_LOGIC_NOT",
        0xB7 => "OP_STREAM_SELECT",
        0xB8 => "OP_STREAM_REDUCE_ARGMIN",
        0xB9 => "OP_STREAM_REDUCE_ARGMAX",
        0xBA => "OP_STREAM_LOAD_SRC_ID",
        0xBB => "OP_STREAM_LOAD_TGT_ID",
        0xBC => "OP_STREAM_LOAD_EDGE_ID",
        0xBD => "OP_STREAM_LOAD_CONST",
        0xBE => "OP_STREAM_YIELD",
        0xBF => "OP_STREAM_SCATTER_REDUCE",

        // Multi-Lane Bit-Sliced Execution & Vector ANN Opcodes (0xC0 - 0xCB)
        0xC0 => "OP_LANE_INIT_SEEDS",
        0xC1 => "OP_LANE_INIT_GROUPS",
        0xC2 => "OP_LANE_SET_EDGE_MASK",
        0xC3 => "OP_LANE_CSR_WALK",
        0xC4 => "OP_LANE_CSC_WALK",
        0xC5 => "OP_LANE_PRUNE_MASK",
        0xC6 => "OP_LANE_FILTER_QUORUM",
        0xC7 => "OP_LANE_REDUCE_AND",
        0xC8 => "OP_LANE_REDUCE_ATTR",
        0xC9 => "OP_LANE_SET_COVER",
        0xCA => "OP_VECTOR_ANN_SEARCH",
        0xCB => "OP_VECTOR_RERANK_DOT",

        0x0A => "OP_LOAD_INLINE_SET",
        0x0B => "OP_INIT_MOCK_NODE_ATTR",
        0x0C => "OP_INIT_MOCK_EDGE_ATTR",
        0x0D => "OP_RESERVED_0D",
        0x0E => "OP_CSR_WALK_2HOP",
        0x0F => "OP_CSR_WALK_STATE",
        0x28 => "OP_RESERVED_28",
        0x29 => "OP_RESERVED_29",
        0x2B => "OP_RESERVED_2B",
        0x2C => "OP_RESERVED_2C",
        0x3E => "OP_RESERVED_3E",
        0x3F => "OP_RESERVED_3F",
        0x4C => "OP_RESERVED_4C",
        0x4D => "OP_RESERVED_4D",
        0x4E => "OP_RESERVED_4E",
        0x4F => "OP_RESERVED_4F",
        0x59 => "OP_RESERVED_59",
        0x5D => "OP_RESERVED_5D",
        0x5E => "OP_RESERVED_5E",
        0x5F => "OP_RESERVED_5F",
        0x6D => "OP_RESERVED_6D",
        0x6E => "OP_RESERVED_6E",
        0x6F => "OP_RESERVED_6F",
        0x76 => "OP_RESERVED_76",
        0x77 => "OP_RESERVED_77",
        0x78 => "OP_RESERVED_78",
        0x79 => "OP_RESERVED_79",
        0x7A => "OP_RESERVED_7A",
        0x7B => "OP_RESERVED_7B",
        0x7C => "OP_RESERVED_7C",
        0x7D => "OP_RESERVED_7D",
        0x7E => "OP_RESERVED_7E",
        0x7F => "OP_RESERVED_7F",
        _ => "OP_UNKNOWN",
    }
}

fn get_semiring_name(id: u32) -> &'static str {
    match id {
        0 => "SEMIRING_PLUS_TIMES",
        1 => "SEMIRING_MIN_PLUS",
        2 => "SEMIRING_MAX_MIN",
        3 => "SEMIRING_BOOL",
        _ => "SEMIRING_UNKNOWN",
    }
}

fn get_binary_op_name(id: u32) -> &'static str {
    match id {
        0 => "BINARY_OP_ADD",
        1 => "BINARY_OP_MUL",
        2 => "BINARY_OP_MIN",
        3 => "BINARY_OP_MAX",
        4 => "BINARY_OP_AND",
        5 => "BINARY_OP_OR",
        6 => "BINARY_OP_ARGMIN",
        7 => "BINARY_OP_ARGMAX",
        _ => "BINARY_OP_UNKNOWN",
    }
}

pub fn run(input_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open(input_path)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;

    let text = disassemble_bytes(&buffer, Some(input_path))?;
    print!("{}", text);
    Ok(())
}

pub fn disassemble_bytes(
    buffer: &[u8],
    input_path: Option<&Path>,
) -> Result<String, Box<dyn std::error::Error>> {
    if !buffer.len().is_multiple_of(8) {
        return Err("Binary bytecode file size must be a multiple of 8 bytes".into());
    }

    let instruction_count = buffer.len() / 8;
    let mut out = String::new();
    if let Some(p) = input_path {
        out.push_str(&format!("\n# Textual Disassembly of {:?}\n", p));
    }
    out.push_str(&format!("; Instruction Count: {}\n", instruction_count));
    out.push_str("; -------------------------------------------------------------\n");

    for pc in 0..instruction_count {
        let offset = pc * 8;
        let slice = &buffer[offset..offset + 8];
        let instr = unsafe { &*(slice.as_ptr() as *const impulse_instruction_t) };

        let op_name = get_opcode_name(instr.opcode);
        let mut flags_parts = Vec::new();
        if instr.flags & 0x01 != 0 {
            flags_parts.push("BITSET");
        }
        if instr.flags & 0x02 != 0 {
            flags_parts.push("ACCUMULATE");
        }
        if instr.flags & 0x04 != 0 {
            flags_parts.push("INVERT");
        }
        if instr.flags & 0x08 != 0 {
            flags_parts.push("OFFHEAP");
        }
        let flags_str = if flags_parts.is_empty() {
            "0".to_string()
        } else {
            flags_parts.join("|")
        };

        let mut output_str = format!("0x{:04X}: {:<25}", pc, op_name);

        // Decode operands based on opcode category
        match instr.opcode {
            // No operands / halt
            0x00 | 0x01 | 0x56 | 0xA2 | 0xFF => {
                // OP_HALT, OP_NOP, OP_RET, OP_STREAM_FUNC_END
            }
            // Jumps / branches (rel offset in payload)
            0x50..=0x52 => {
                // OP_JMP, OP_JZ, OP_JNZ
                let offset = instr.payload as i32;
                output_str.push_str(&format!("{:+}", offset));
            }
            // Loop branch (R_dst + rel offset)
            0x53 => {
                // OP_LOOP_DECR
                let offset = instr.payload as i32;
                output_str.push_str(&format!("R{}, {:+}", instr.dst_reg, offset));
            }
            // Constant Float loader
            0x06 => {
                // OP_LOAD_CONST_FLOAT
                let fval = f32::from_bits(instr.payload);
                output_str.push_str(&format!("R{}, {}f", instr.dst_reg, fval));
            }
            // Constant Int / Prefix loaders
            0x04 | 0x05 | 0x07 | 0x12 | 0x71 => {
                // OP_LOAD_CONST_INT, OP_MAP_KEYS_TO_DENSE, OP_LOAD_CONST_STR_PREFIX, OP_CSR_DEGREE, OP_CLEAR_REG
                output_str.push_str(&format!("R{}, {}", instr.dst_reg, instr.payload));
            }
            // 2-operand reg-to-reg / mapping
            0x02 | 0x03 | 0x33 | 0x35 | 0x54 | 0x70 | 0x90 | 0x91 | 0x92 | 0xC7 => {
                // OP_INIT_INPUT_NODE, OP_INIT_INPUT_SET, OP_SET_CARDINALITY, OP_VECTOR_REDUCE_SUM, OP_STABLE_CHECK, OP_MOV, OP_COLLECT_BITSET, OP_COLLECT_ARRAY, OP_MAP_DENSE_TO_KEYS, OP_LANE_REDUCE_AND
                output_str.push_str(&format!("R{}, R{}", instr.dst_reg, instr.payload));
            }
            // CSR Walk: payload holds src_reg (lower 16) and rel_id (upper 16)
            0x10 | 0x11 | 0x14 | 0x15 | 0x16 | 0x17 | 0x84 | 0x85 | 0x86 | 0x87 | 0x88 | 0x8E
            | 0x8F | 0x94 | 0x95 | 0xC3 | 0xC4 => {
                let src_reg = instr.payload & 0xFFFF;
                let rel_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, REL_{}", instr.dst_reg, src_reg, rel_id));
            }
            // Element-wise combinations: src1 (byte 0), src2 (byte 1), op_id (byte 2)
            0x43 | 0x44 => {
                // OP_EWISE_ADD, OP_EWISE_MULT
                let src1 = instr.payload & 0xFF;
                let src2 = (instr.payload >> 8) & 0xFF;
                let op_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!(
                    "R{}, R{}, R{}, {}",
                    instr.dst_reg,
                    src1,
                    src2,
                    get_binary_op_name(op_id)
                ));
            }
            // Vector Division: num_reg (lower 16) and denom_reg (upper 16)
            0x36 => {
                // OP_VECTOR_DIV
                let num = instr.payload & 0xFFFF;
                let denom = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, R{}", instr.dst_reg, num, denom));
            }
            // Reduction: src_reg (byte 0), op_id (byte 2)
            0x45 => {
                // OP_REDUCE
                let src = instr.payload & 0xFF;
                let op_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!(
                    "R{}, R{}, {}",
                    instr.dst_reg,
                    src,
                    get_binary_op_name(op_id)
                ));
            }
            // SpMV: src_vec (byte 0), rel_id (byte 1), semiring_id (byte 2)
            0x41 | 0x42 => {
                // OP_MXV, OP_VXM
                let src = instr.payload & 0xFF;
                let rel = (instr.payload >> 8) & 0xFF;
                let semiring = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!(
                    "R{}, R{}, REL_{}, {}",
                    instr.dst_reg,
                    src,
                    rel,
                    get_semiring_name(semiring)
                ));
            }
            // Island Detect: src1 (byte 0), src2 (byte 1), rel_id (byte 2)
            0x65 => {
                let src1 = instr.payload & 0xFF;
                let src2 = (instr.payload >> 8) & 0xFF;
                let rel = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!(
                    "R{}, R{}, R{}, REL_{}",
                    instr.dst_reg, src1, src2, rel
                ));
            }
            _ => {
                // Default fallback (can be re-assembled verbatim via named args)
                output_str.push_str(&format!(
                    "dst=R{}, flags={}, payload=0x{:08X}",
                    instr.dst_reg, flags_str, instr.payload
                ));
            }
        }

        out.push_str(&output_str);
        out.push('\n');
    }
    out.push_str("; -------------------------------------------------------------\n");
    Ok(out)
}
