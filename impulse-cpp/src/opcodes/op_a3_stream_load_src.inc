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
#define OP_A3_DEFINED_FLAGS        IMP_FLAG_NONE
#define OP_A3_ELEM_SIZE_64         8u
#define OP_A3_ELEM_SIZE_32         4u
#endif
                    case OP_STREAM_LOAD_SRC: {
                        IMPULSE_ASSERT(sDst < IMP_VM_STREAM_REG_COUNT);
                        uint16_t attr_id = IMP_INST_LOW16(sInst);
                        const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
                        IMPULSE_TESTCASE(ma_ptr != nullptr);
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
                            uint16_t src_dom = (rel < vm_state->query_context->slots.size())
                                ? vm_state->query_context->slots[rel].src_domain_id
                                : rel;
                            const BoundAttributeSlot* aslot_ptr = nullptr;
                            if (src_dom < vm_state->query_context->domain_attribute_slots.size() &&
                                attr_id < vm_state->query_context->domain_attribute_slots[src_dom].size() &&
                                vm_state->query_context->domain_attribute_slots[src_dom][attr_id].data_ptr != nullptr) {
                                aslot_ptr = &vm_state->query_context->domain_attribute_slots[src_dom][attr_id];
                            } else if (rel < vm_state->query_context->attribute_slots.size() &&
                                       attr_id < vm_state->query_context->attribute_slots[rel].size()) {
                                aslot_ptr = &vm_state->query_context->attribute_slots[rel][attr_id];
                            }
                            if (aslot_ptr) {
                                const auto& aslot = *aslot_ptr;
                                uint8_t base_tc = IMP_ATTR_BASE_TYPE(aslot.type_code);
                                bool is_null = IMP_ATTR_IS_NULLABLE(aslot.type_code);
                                IMPULSE_TESTCASE(is_null);
                                size_t elem_sz = (base_tc == IMP_TYPE_CODE_FLOAT64 || base_tc == IMP_TYPE_CODE_INT64)
                                    ? OP_A3_ELEM_SIZE_64
                                    : OP_A3_ELEM_SIZE_32;
                                size_t dom_rows = resolve_domain_row_count(vm_state->query_context, src_dom);
                                size_t bm_bytes = (is_null && aslot.data_bytes > IMP_SNAP_SECTION_ALIGN)
                                    ? static_cast<size_t>(IMP_VALIDITY_PADDED_BYTES(dom_rows))
                                    : 0u;
                                if (bm_bytes >= aslot.data_bytes && is_null && aslot.data_bytes > IMP_SNAP_SECTION_ALIGN) {
                                    bm_bytes = static_cast<size_t>(IMP_SNAP_SECTION_ALIGN);
                                }
                                if (aslot.data_ptr && aslot.data_bytes > bm_bytes) {
                                    const uint8_t* raw_b = static_cast<const uint8_t*>(aslot.data_ptr);
                                    size_t cnt = (aslot.data_bytes - bm_bytes) / elem_sz;
                                    if (actualSource < cnt) {
                                        const bool is_valid = !is_null || IMP_VALIDITY_IS_VALID(raw_b, actualSource);
                                        IMPULSE_TESTCASE(!is_valid);
                                        if (!is_valid) {
                                            s_regs[sDst] = std::numeric_limits<float>::quiet_NaN();
                                        } else if (base_tc == IMP_TYPE_CODE_FLOAT64) {
                                            s_regs[sDst] = static_cast<float>(reinterpret_cast<const double*>(raw_b + bm_bytes)[actualSource]);
                                        } else if (base_tc == IMP_TYPE_CODE_FLOAT32) {
                                            s_regs[sDst] = reinterpret_cast<const float*>(raw_b + bm_bytes)[actualSource];
                                        } else if (base_tc == IMP_TYPE_CODE_INT32) {
                                            s_regs[sDst] = static_cast<float>(reinterpret_cast<const int32_t*>(raw_b + bm_bytes)[actualSource]);
                                        } else if (base_tc == IMP_TYPE_CODE_INT64) {
                                            s_regs[sDst] = static_cast<float>(reinterpret_cast<const int64_t*>(raw_b + bm_bytes)[actualSource]);
                                        } else {
                                            s_regs[sDst] = 0.0f;
                                        }
                                    } else {
                                        s_regs[sDst] = 0.0f;
                                    }
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        }
                        break;
                    }
