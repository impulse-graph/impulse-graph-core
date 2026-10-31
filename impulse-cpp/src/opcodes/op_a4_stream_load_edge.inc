/**
 * @file op_a4_stream_load_edge.inc
 * @brief Opcode 0xA4: OP_STREAM_LOAD_EDGE — Loads edge attribute for eIdx into stream register s_regs[sDst].
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
                    case OP_STREAM_LOAD_EDGE: {
                        uint16_t attr_id = sInst.payload & 0xFFFF;
                        const auto* ma_e_ptr = find_mock_edge_attr(vm_state->query_context, attr_id);
                        if (ma_e_ptr) {
                            const auto& ma = *ma_e_ptr;
                            if (eIdx < ma.float_data.size()) {
                                s_regs[sDst] = ma.float_data[eIdx];
                            } else if (eIdx < ma.int_data.size()) {
                                uint32_t bits = static_cast<uint32_t>(ma.int_data[eIdx]);
                                std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        } else {
                            const auto* ma_n_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
                            if (ma_n_ptr) {
                                const auto& ma = *ma_n_ptr;
                                if (eIdx < ma.float_data.size()) {
                                    s_regs[sDst] = ma.float_data[eIdx];
                                } else if (eIdx < ma.int_data.size()) {
                                    uint32_t bits = static_cast<uint32_t>(ma.int_data[eIdx]);
                                    std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                            } else if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                                const auto& vec = vm_state->query_context->float_vectors[attr_id];
                                s_regs[sDst] = (eIdx < vec.size()) ? vec[eIdx] : 0.0f;
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        }
                        break;
                    }
