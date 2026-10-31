#ifndef OP_A8_CONTRACT_DEFINED
#define OP_A8_CONTRACT_DEFINED
/**
 * @file op_a8_stream_reduce.inc
 * @brief Opcode 0xA8: OP_STREAM_REDUCE — Reduces s_regs[sDst] into a global target float vector register.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xA8 (OP_STREAM_REDUCE)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: sDst (stream val reg 0..15), globalReg (sPayloadLow), monoid (sPayloadHigh)
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
                    case OP_STREAM_REDUCE: {
                        float val = s_regs[sDst];
                        uint16_t globalReg = sPayloadLow;
                        uint16_t monoid = sPayloadHigh;
                        VALIDATE_REG(globalReg);
                        int h_vec = -1;
                        if (vm_state->register_types[globalReg] == TYPE_FLOAT_VECTOR) {
                            h_vec = static_cast<int>(vm_state->registers[globalReg]);
                        } else {
                            h_vec = acquire_float_vector(vm_state->query_context);
                            if (h_vec >= 0) {
                                vm_state->registers[globalReg] = static_cast<uint64_t>(h_vec);
                                vm_state->register_types[globalReg] = TYPE_FLOAT_VECTOR;
                            }
                        }
                        if (h_vec >= 0 && static_cast<size_t>(h_vec) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h_vec]) {
                            auto& vec = vm_state->query_context->float_vectors[h_vec];
                            if (actualTarget < vec.size()) {
                                uint64_t rbit = (globalReg < 64) ? (1ULL << globalReg) : 1ULL;
                                bool first_touch = (actualTarget < reduced_reg_masks.size())
                                    ? ((reduced_reg_masks[actualTarget] & rbit) == 0ULL)
                                    : true;
                                if (actualTarget < reduced_reg_masks.size()) {
                                    reduced_reg_masks[actualTarget] |= rbit;
                                }
                                float current = vec[actualTarget];
                                float next = current;
                                switch (monoid) {
                                    case 0: next = current + val; break;
                                    case 1: next = first_touch ? val : std::max(current, val); break;
                                    case 2: next = first_touch ? val : std::min(current, val); break;
                                }
                                vec[actualTarget] = next;
                            }
                        }
                        break;
                    }
