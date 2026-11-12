#ifndef OP_BE_CONTRACT_DEFINED
#define OP_BE_CONTRACT_DEFINED
/**
 * @file op_be_stream_yield.inc
 * @brief Opcode 0xBE: OP_STREAM_YIELD — Explicitly yields actualTarget into the destination frontier bitset.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xBE (OP_STREAM_YIELD)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: None
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
#define OP_BE_DEFINED_FLAGS        IMP_FLAG_NONE
#endif
                    case OP_STREAM_YIELD: {
                        IMPULSE_TESTCASE(src_is_lane);
                        if (src_is_lane) {
                            IMPULSE_ASSERT(lf_dst_ptr != nullptr);
                            if (actualTarget < lf_dst_ptr->lanes.size()) {
                                lf_dst_ptr->lanes[actualTarget] |= prop_mask;
                                lf_dst_ptr->active_words[IMP_WORD_INDEX(actualTarget)] |= IMP_BIT_MASK64(actualTarget);
                            }
                        } else {
                            IMPULSE_ASSERT(bs_dst_ptr != nullptr);
                            bitset_add(*bs_dst_ptr, actualTarget, vm_state->query_context->max_nodes);
                        }
                        break;
                    }
