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
#define OP_65_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_65_ALLOWED_AUX1_TYPES   (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_65_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#define OP_65_NO_LINE_SENTINEL     (-1)
#define OP_65_OMP_CHUNK_SIZE       64
#endif

// Opcode 0x65: OP_ISLAND_DETECT
op_ISLAND_DETECT: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t src1 = IMP_INST_U8_0(inst);
    const uint8_t src2 = IMP_INST_U8_1(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src1);
    IMP_VALIDATE_GPR(src2);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_65_ALLOWED_SRC_TYPES);
    if (src2 > 0u || vm_state->register_types[src2] != TYPE_NULL) {
        IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_65_ALLOWED_AUX1_TYPES);
    }

    uint16_t rel;
    const bool is_ext = IMP_INST_IS_EXTENDED(inst);
    IMPULSE_TESTCASE(is_ext);
    if (is_ext) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }

    uint64_t critical_pairs_count = 0;
    {
        auto& lines1 = vm_state->query_context->scratch_u32_a;
        lines1.clear();
        if (src1 < IMP_VM_GPR_COUNT) {
            if (vm_state->register_types[src1] == TYPE_INT64) {
                lines1.push_back(static_cast<uint32_t>(vm_state->registers[src1]));
            } else if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                const uint32_t handle = static_cast<uint32_t>(vm_state->registers[src1]);
                if (handle < vm_state->query_context->bitsets.size()) {
                    extract_active_bits(vm_state->query_context->bitsets[handle], lines1);
                }
            }
        }
        if (lines1.empty()) lines1.push_back(OP_65_NO_LINE_SENTINEL);

        auto& lines2 = vm_state->query_context->scratch_u32_b;
        lines2.clear();
        if (src2 < IMP_VM_GPR_COUNT) {
            if (vm_state->register_types[src2] == TYPE_INT64) {
                lines2.push_back(static_cast<uint32_t>(vm_state->registers[src2]));
            } else if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                const uint32_t handle = static_cast<uint32_t>(vm_state->registers[src2]);
                if (handle < vm_state->query_context->bitsets.size()) {
                    extract_active_bits(vm_state->query_context->bitsets[handle], lines2);
                }
            }
        }
        if (lines2.empty()) lines2.push_back(OP_65_NO_LINE_SENTINEL);

        IMP_REQUIRE_REL_SLOT(vm_state, rel);
        const auto& slot = vm_state->query_context->slots[rel];

        if (slot.offsets_ptr && slot.targets_ptr) {
            const int32_t* branch_ids = nullptr;
            if (rel < vm_state->query_context->attribute_slots.size() &&
                !vm_state->query_context->attribute_slots[rel].empty()) {
                branch_ids = static_cast<const int32_t*>(vm_state->query_context->attribute_slots[rel][0].data_ptr);
            }

            const uint32_t N = static_cast<uint32_t>(slot.node_count);
            const uint32_t base_components = run_island_detect_bfs(
                vm_state->query_context, N, slot, branch_ids, OP_65_NO_LINE_SENTINEL, OP_65_NO_LINE_SENTINEL);

            const bool same_set = (src1 == src2);
            IMPULSE_TESTCASE(same_set);

            if (same_set) {
#if defined(_OPENMP)
                #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, OP_65_OMP_CHUNK_SIZE)
#endif
                for (size_t i = 0; i < lines1.size(); ++i) {
                    for (size_t j = i + 1; j < lines1.size(); ++j) {
                        const uint32_t comp = run_island_detect_bfs(vm_state->query_context, N, slot, branch_ids, lines1[i], lines1[j]);
                        if (comp > base_components) {
                            critical_pairs_count++;
                        }
                    }
                }
            } else {
#if defined(_OPENMP)
                #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, OP_65_OMP_CHUNK_SIZE)
#endif
                for (size_t i = 0; i < lines1.size(); ++i) {
                    for (size_t j = 0; j < lines2.size(); ++j) {
                        const uint32_t comp = run_island_detect_bfs(vm_state->query_context, N, slot, branch_ids, lines1[i], lines2[j]);
                        if (comp > base_components) {
                            critical_pairs_count++;
                        }
                    }
                }
            }
        }
    }

    IMPULSE_TESTCASE(critical_pairs_count == 0ULL);
    vm_state->registers[dst] = critical_pairs_count;
    vm_state->register_types[dst] = TYPE_INT64;

    vm_state->pc++;
    DISPATCH();
}
