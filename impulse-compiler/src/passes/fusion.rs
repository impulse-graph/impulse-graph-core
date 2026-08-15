pub struct OpcodeFusionPass;

impl OpcodeFusionPass {
    pub fn fuse_assembly_instructions(asm_lines: Vec<&str>) -> Vec<String> {
        let mut fused = Vec::new();
        let mut i = 0;

        while i < asm_lines.len() {
            let line = asm_lines[i].trim();
            if i + 1 < asm_lines.len() {
                let next_line = asm_lines[i + 1].trim();

                // Pattern 1: OP_CSR_WALK followed by OP_ROARING_BITMAP_AND_NOT (or OP_SET_DIFFERENCE)
                if line.contains("OP_CSR_WALK") && (next_line.contains("OP_ROARING_BITMAP_AND_NOT") || next_line.contains("OP_SET_DIFFERENCE")) {
                    let rel_op = if let Some(idx) = line.rfind(',') {
                        line[idx + 1..].trim()
                    } else {
                        "REL_DIRECT_MEMBER"
                    };
                    fused.push(format!("    OP_CSR_WALK_FILTERED        R5, R2, R3, {} ; [JIT Fused Walk+Diff]", rel_op));
                    i += 2;
                    continue;
                }

                // Pattern 2: OP_SET_INTERSECT followed by OP_SET_DIFFERENCE
                if line.contains("OP_SET_INTERSECT") && next_line.contains("OP_SET_DIFFERENCE") {
                    fused.push("    OP_SET_INTERSECT_AND_NOT    R4, R2, R3 ; [JIT Fused BitSet Mask]".to_string());
                    i += 2;
                    continue;
                }
            }

            fused.push(asm_lines[i].to_string());
            i += 1;
        }

        fused
    }
}
