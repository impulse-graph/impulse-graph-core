/**
 * @file v11_memory_planner.cpp
 * @brief Static AOT memory planner (impulse_vm_plan_memory) for ImpulseVM v0.11.0.
 */
#include "v11_internal.hpp"
#include "impulse_graph_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" IMPULSE_API impulse_vm_status_v11_t impulse_vm_plan_memory(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    uint16_t max_dop,
    impulse_vm_memory_plan_t* out_plan
) {
    using namespace impulse::vm::v11;
    if (!impb_bytes || !out_plan || impb_len < sizeof(impulse_impb_header_v11_t)) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }

    impulse_vm_validation_v11_t vr = impulse_vm_validate_v11(impb_bytes, impb_len);
    if (vr.status != IMPULSE_VM_V11_OK) {
        return vr.status;
    }

    impulse_impb_header_v11_t hdr{};
    std::memcpy(&hdr, impb_bytes, sizeof(hdr));

    uint16_t dop = (max_dop == 0u) ? 1u : max_dop;
    size_t ws_header = (sizeof(impulse_vm_workspace_v11_t) + 127u) & ~size_t(127u);
    size_t morsel_tls = static_cast<size_t>(dop) * 136192u; // ~133 KB per worker thread

    uint64_t max_dom_card = 64ULL;
    if (snapshot != nullptr) {
        for (const auto& dom : snapshot->domains) {
            if (dom.node_count > max_dom_card) {
                max_dom_card = dom.node_count;
            }
        }
    }

    size_t dense_bytes_per_f = static_cast<size_t>(((max_dom_card + 63ULL) / 64ULL) * 8ULL);
    size_t id_array_per_f    = static_cast<size_t>(max_dom_card * sizeof(uint32_t));
    size_t f_slot_bound      = (std::max)(dense_bytes_per_f, id_array_per_f) + 4096u;
    size_t v_slot_bound      = static_cast<size_t>(max_dom_card * sizeof(uint64_t)) +
                               static_cast<size_t>(((max_dom_card + 63ULL) / 64ULL) * 8ULL) + 128u;

    const auto* code_ptr = static_cast<const uint8_t*>(impb_bytes) + sizeof(hdr);
    size_t n = hdr.code_word_count;
    size_t transient_max = 0u;
    size_t max_result = 0u;

    for (size_t pc = 0; pc < n; ) {
        uint64_t w = 0;
        std::memcpy(&w, code_ptr + pc * sizeof(uint64_t), sizeof(uint64_t));
        uint8_t op = IMP_V11_OPCODE(w);
        uint8_t subop = IMP_V11_SUBOP(w);
        bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;

        if (op == static_cast<uint8_t>(Opcode::OP_SPMV) ||
            op == static_cast<uint8_t>(Opcode::OP_MORSEL_COMMIT)) {
            transient_max = (std::max)(transient_max, static_cast<size_t>(dop) * 32768u);
        } else if (op == static_cast<uint8_t>(Opcode::OP_GRAPH_KERNEL)) {
            if (subop == 1u || subop == 2u) {
                transient_max = (std::max)(transient_max, static_cast<size_t>(max_dom_card * 16ULL));
            } else if (subop == 3u || subop == 4u) {
                size_t tri = static_cast<size_t>(max_dom_card * 8ULL) +
                             static_cast<size_t>(dop) * static_cast<size_t>((max_dom_card + 7ULL) / 8ULL);
                transient_max = (std::max)(transient_max, tri);
            }
        } else if (op == static_cast<uint8_t>(Opcode::OP_COLLECT)) {
            max_result += v_slot_bound;
        }
        pc += ext ? 2u : 1u;
    }

    size_t min_scratch = ws_header + morsel_tls + 65536u;
    size_t guaranteed  = ws_header + morsel_tls + (16u * f_slot_bound) + (16u * v_slot_bound) +
                         (f_slot_bound / 4u) + transient_max;

    out_plan->min_scratch_bytes        = (min_scratch + 127u) & ~size_t(127u);
    out_plan->guaranteed_scratch_bytes = (guaranteed + 127u) & ~size_t(127u);
    out_plan->max_result_bytes         = (max_result + 63u) & ~size_t(63u);
    return IMPULSE_VM_V11_OK;
}
