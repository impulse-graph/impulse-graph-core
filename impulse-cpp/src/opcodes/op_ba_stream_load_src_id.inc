#ifndef OP_BA_CONTRACT_DEFINED
#define OP_BA_CONTRACT_DEFINED
/**
 * @file op_ba_stream_load_src_id.inc
 * @brief Opcode 0xBA: OP_STREAM_LOAD_SRC_ID — Loads current edge's source node ID as float into s_regs[sDst].
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xBA (OP_STREAM_LOAD_SRC_ID)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: sDst (stream reg 0..15)
 *  - Allowed Types: None (Stream sub-opcode)
 *  - Output: None (Top-level VM registers untouched)
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
#define OP_BA_DEFINED_FLAGS        0x03u
#define OP_BA_MODE_SRC_ID          0x00u
#define OP_BA_MODE_LANE_POPCOUNT   0x01u
#define OP_BA_MODE_LANE_MASK       0x02u
#endif
                    case OP_STREAM_LOAD_SRC_ID: {
                        IMPULSE_ASSERT(sDst < IMP_VM_STREAM_REG_COUNT);
                        const uint8_t s_mode = IMP_INST_FLAGS(sInst);
                        IMPULSE_TESTCASE(s_mode != OP_BA_MODE_SRC_ID);
                        if (s_mode == OP_BA_MODE_LANE_POPCOUNT) {
                            s_regs[sDst] = static_cast<float>(std::popcount(u_lane_mask));
                        } else if (s_mode == OP_BA_MODE_LANE_MASK) {
                            s_regs[sDst] = static_cast<float>(u_lane_mask);
                        } else {
                            s_regs[sDst] = static_cast<float>(actualSource);
                        }
                        break;
                    }
