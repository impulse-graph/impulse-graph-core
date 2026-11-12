#ifndef OP_B9_CONTRACT_DEFINED
#define OP_B9_CONTRACT_DEFINED
/**
 * @file op_b9_stream_reduce_argmax.inc
 * @brief Opcode 0xB9: OP_STREAM_REDUCE_ARGMAX — Per-edge argmax reduction tracking maximum value and source node ID.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xB9 (OP_STREAM_REDUCE_ARGMAX)
 *  - Execution Scope: Edge Stream Shader Sub-Opcode (NOP in top-level VM dispatch)
 *  - Stream Layout: sDst (stream val reg 0..15), valReg (sPayloadLow), argReg (sPayloadHigh)
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
#define OP_B9_DEFINED_FLAGS        IMP_FLAG_NONE
#endif
                    case OP_STREAM_REDUCE_ARGMAX: {
                        IMPULSE_ASSERT(sDst < IMP_VM_STREAM_REG_COUNT);
                        float val = s_regs[sDst];
                        uint16_t valReg = sPayloadLow;
                        uint16_t argReg = sPayloadHigh;
                        IMP_VALIDATE_GPR(valReg);
                        IMPULSE_ASSERT(valReg < IMP_VM_GPR_COUNT);
                        int h_vec = -1;
                        if (vm_state->register_types[valReg] == TYPE_FLOAT_VECTOR) {
                            h_vec = static_cast<int>(vm_state->registers[valReg]);
                        } else {
                            h_vec = acquire_float_vector(vm_state->query_context);
                            if (h_vec >= 0) {
                                vm_state->registers[valReg] = static_cast<uint64_t>(h_vec);
                                vm_state->register_types[valReg] = TYPE_FLOAT_VECTOR;
                                if (target_node_bound > 0) {
                                    vm_state->query_context->float_vector_lengths[h_vec] = target_node_bound;
                                }
                            }
                        }
                        if (h_vec >= 0 && static_cast<size_t>(h_vec) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h_vec]) {
                            IMPULSE_TESTCASE(std::isnan(val));
                            if (std::isnan(val)) break;
                            auto& vec = vm_state->query_context->float_vectors[h_vec];
                            if (actualTarget < vec.size()) {
                                uint64_t rbit = IMP_BIT_MASK64(valReg);
                                bool first_touch = (actualTarget < reduced_reg_masks.size())
                                    ? ((reduced_reg_masks[actualTarget] & rbit) == 0ULL)
                                    : true;
                                IMPULSE_TESTCASE(first_touch);
                                float current = vec[actualTarget];
                                bool is_better = first_touch ||
                                    (op == OP_STREAM_REDUCE_ARGMIN && val < current) ||
                                    (op == OP_STREAM_REDUCE_ARGMAX && val > current);
                                IMPULSE_TESTCASE(is_better);
                                if (is_better) {
                                    if (actualTarget < reduced_reg_masks.size()) {
                                        reduced_reg_masks[actualTarget] |= rbit;
                                    }
                                    vec[actualTarget] = val;
                                    IMPULSE_TESTCASE(argReg < IMP_VM_GPR_COUNT);
                                    if (argReg < IMP_VM_GPR_COUNT) {
                                        vm_state->registers[argReg] = static_cast<uint64_t>(actualSource);
                                        vm_state->register_types[argReg] = TYPE_NODE_ID;
                                    }
                                }
                            }
                        }
                        break;
                    }
