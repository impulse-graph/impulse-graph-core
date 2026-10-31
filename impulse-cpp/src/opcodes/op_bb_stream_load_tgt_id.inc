/**
 * @file op_bb_stream_load_tgt_id.inc
 * @brief Opcode 0xBB: OP_STREAM_LOAD_TGT_ID — Loads current edge's target node ID as float into s_regs[sDst].
 *
 * ARCHITECTURAL NOTE (Edge Stream Shader Sub-Opcode vs. Top-Level VM Opcode):
 * ---------------------------------------------------------------------------
 * Unlike standard top-level ImpulseVM opcodes (which execute at the outer
 * `vm_state->pc` loop over the 64 general-purpose registers `R0..R63`), the
 * Edge Stream Shader sub-opcodes (`0xA1`..`0xBF`, excluding the outer walk
 * initiators `OP_CSR_WALK_STREAM` `0xA9` and `OP_CSC_WALK_STREAM` `0xAA`)
 * are evaluated PER-EDGE inside the fused inner loop of:
 *   - `OP_COO_WALK_STREAM` (`0xA0`)
 *   - `OP_CSR_WALK_STREAM` (`0xA9`)
 *   - `OP_CSC_WALK_STREAM` (`0xAA`)
 *
 * Execution Model:
 *   1. Operates on the 16 per-edge float shader registers `s_regs[0..15]` and
 *      current edge context (`actualSource`, `actualTarget`, `eIdx`, `sInst`,
 *      `sDst`, `sPayloadLow`, `sPayloadHigh`).
 *   2. Included directly into the per-edge shader `switch (op)` in
 *      `opcodes/shared/walk_stream_common.hpp`.
 *   3. When the enclosing `OP_*_WALK_STREAM` instruction finishes iterating
 *      edges, `vm_state->pc` jumps past `OP_STREAM_FUNC_END`. If encountered
 *      standalone in the outer VM dispatch loop, this opcode acts as `OP_NOP`.
 */
                    case OP_STREAM_LOAD_TGT_ID: s_regs[sDst] = static_cast<float>(actualTarget); break;
