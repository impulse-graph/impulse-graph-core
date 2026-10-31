/**
 * @file op_65_island_detect.inc
 * @brief Formal Contract for OP_ISLAND_DETECT (0x65).
 *
 * Syntax: OP_ISLAND_DETECT <dst_reg>, <src1_reg>, <src2_reg> (payload: [rel_id:16 | src2:8 | src1:8])
 * Inputs:
 *   - dst_reg: Target register receiving count of critical disconnected pairs
 *   - src1_reg (payload_low8): First set of monitored candidate branch/bus lines
 *   - src2_reg (payload_byte1): Second set of monitored candidate lines
 * Preconditions:
 *   - src1_reg and src2_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_INT64
 *   - Flags: None
 */
#ifndef OP_65_CONTRACT_DEFINED
#define OP_65_CONTRACT_DEFINED
#define OP_65_DEFINED_FLAGS        0x80u
#define OP_65_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_65_ALLOWED_AUX1_TYPES   (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_65_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x65: OP_ISLAND_DETECT
op_ISLAND_DETECT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src1 = inst.payload & 0xFF;
    uint8_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_65_ALLOWED_SRC_TYPES);
    if (src2 > 0 || vm_state->register_types[src2] != TYPE_NULL) {
        IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_65_ALLOWED_AUX1_TYPES);
    }

    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }

    uint64_t critical_pairs_count = 0;
    {
        auto& lines1 = vm_state->query_context->scratch_u32_a;
        lines1.clear();
        if (src1 < 64) {
            if (vm_state->register_types[src1] == TYPE_INT64) {
                lines1.push_back(static_cast<uint32_t>(vm_state->registers[src1]));
            } else if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                uint32_t handle = static_cast<uint32_t>(vm_state->registers[src1]);
                if (handle < vm_state->query_context->bitsets.size()) {
                    extract_active_bits(vm_state->query_context->bitsets[handle], lines1);
                }
            }
        }
        if (lines1.empty()) lines1.push_back(-1);

        auto& lines2 = vm_state->query_context->scratch_u32_b;
        lines2.clear();
        if (src2 < 64) {
            if (vm_state->register_types[src2] == TYPE_INT64) {
                lines2.push_back(static_cast<uint32_t>(vm_state->registers[src2]));
            } else if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                uint32_t handle = static_cast<uint32_t>(vm_state->registers[src2]);
                if (handle < vm_state->query_context->bitsets.size()) {
                    extract_active_bits(vm_state->query_context->bitsets[handle], lines2);
                }
            }
        }
        if (lines2.empty()) lines2.push_back(-1);

        if (rel >= vm_state->query_context->slots.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& slot = vm_state->query_context->slots[rel];

        if (slot.offsets_ptr && slot.targets_ptr) {
            const int32_t* branch_ids = nullptr;
            if (rel < vm_state->query_context->attribute_slots.size() &&
                !vm_state->query_context->attribute_slots[rel].empty()) {
                branch_ids = static_cast<const int32_t*>(vm_state->query_context->attribute_slots[rel][0].data_ptr);
            }

            uint32_t N = static_cast<uint32_t>(slot.node_count);
            uint32_t base_components = run_island_detect_bfs(vm_state->query_context, N, slot, branch_ids, -1, -1);

            bool same_set = (src1 == src2);

            if (same_set) {
#if defined(_OPENMP)
                #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, 64)
#endif
                for (size_t i = 0; i < lines1.size(); ++i) {
                    for (size_t j = i + 1; j < lines1.size(); ++j) {
                        uint32_t comp = run_island_detect_bfs(vm_state->query_context, N, slot, branch_ids, lines1[i], lines1[j]);
                        if (comp > base_components) {
                            critical_pairs_count++;
                        }
                    }
                }
            } else {
#if defined(_OPENMP)
                #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, 64)
#endif
                for (size_t i = 0; i < lines1.size(); ++i) {
                    for (size_t j = 0; j < lines2.size(); ++j) {
                        uint32_t comp = run_island_detect_bfs(vm_state->query_context, N, slot, branch_ids, lines1[i], lines2[j]);
                        if (comp > base_components) {
                            critical_pairs_count++;
                        }
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = critical_pairs_count;
    vm_state->register_types[dst] = TYPE_INT64;

    vm_state->pc++;
    DISPATCH();
}

