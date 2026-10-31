#ifndef OP_A3_CONTRACT_DEFINED
#define OP_A3_CONTRACT_DEFINED
/**
 * @file op_a3_stream_load_src.inc
 * @brief Opcode 0xA3: OP_STREAM_LOAD_SRC — Loads source node attribute into stream register s_regs[sDst].
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xA3 (OP_STREAM_LOAD_SRC)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: sDst (stream reg 0..15), attr_id (payload & 0xFFFF)
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
#endif
                    case OP_STREAM_LOAD_SRC: {
                        uint16_t attr_id = sInst.payload & 0xFFFF;
                        const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
                        if (ma_ptr) {
                            const auto& ma = *ma_ptr;
                            if (actualSource < ma.float_data.size()) {
                                s_regs[sDst] = ma.float_data[actualSource];
                            } else if (actualSource < ma.int_data.size()) {
                                uint32_t bits = static_cast<uint32_t>(ma.int_data[actualSource]);
                                std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        } else if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                            const auto& vec = vm_state->query_context->float_vectors[attr_id];
                            s_regs[sDst] = (actualSource < vec.size()) ? vec[actualSource] : 0.0f;
                        } else {
                            s_regs[sDst] = 0.0f;
                        }
                        break;
                    }
