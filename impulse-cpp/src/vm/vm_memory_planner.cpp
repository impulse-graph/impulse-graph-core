/**
 * @file v11_memory_planner.cpp
 * @brief Static AOT memory planner (impulse_vm_plan_memory) for ImpulseVM v0.11.0.
 */
#include "vm_internal.hpp"
#include "impulse_graph_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

extern "C" IMPULSE_API impulse_vm_status_v11_t impulse_vm_plan_memory(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    uint16_t max_dop,
    impulse_vm_memory_plan_t* out_plan
) {
    using namespace impulse::vm;
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

    auto safe_add = [](size_t a, size_t b, size_t* out) -> bool {
        if (b > (std::numeric_limits<size_t>::max)() - a) return false;
        *out = a + b;
        return true;
    };
    auto safe_mul = [](size_t a, size_t b, size_t* out) -> bool {
        if (a != 0u && b > (std::numeric_limits<size_t>::max)() / a) return false;
        *out = a * b;
        return true;
    };

    if (max_dom_card > (std::numeric_limits<size_t>::max)() / 32ULL) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }
    const size_t card_sz = static_cast<size_t>(max_dom_card);
    size_t dense_bytes_per_f = ((card_sz + 63u) / 64u) * 8u;
    size_t id_array_per_f = 0u, f_slot_bound = 0u, v_data = 0u, v_slot_bound = 0u;
    if (!safe_mul(card_sz, sizeof(uint32_t), &id_array_per_f) ||
        !safe_add((std::max)(dense_bytes_per_f, id_array_per_f), 4096u, &f_slot_bound) ||
        !safe_mul(card_sz, sizeof(uint64_t), &v_data) ||
        !safe_add(v_data, dense_bytes_per_f, &v_slot_bound) ||
        !safe_add(v_slot_bound, 128u, &v_slot_bound)) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }

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
                size_t gk = 0u;
                if (!safe_mul(card_sz, 16u, &gk)) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                transient_max = (std::max)(transient_max, gk);
            } else if (subop == 3u || subop == 4u) {
                size_t tri_a = 0u, tri_b = 0u, tri = 0u;
                if (!safe_mul(card_sz, 8u, &tri_a) ||
                    !safe_mul(static_cast<size_t>(dop), (card_sz + 7u) / 8u, &tri_b) ||
                    !safe_add(tri_a, tri_b, &tri)) {
                    return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                }
                transient_max = (std::max)(transient_max, tri);
            }
        } else if (op == static_cast<uint8_t>(Opcode::OP_COLLECT)) {
            if (!safe_add(max_result, v_slot_bound, &max_result)) {
                return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            }
        }
        pc += ext ? 2u : 1u;
    }

    size_t min_scratch = 0u, f16 = 0u, v16 = 0u, guaranteed = 0u;
    if (!safe_add(ws_header, morsel_tls, &min_scratch) ||
        !safe_add(min_scratch, 65536u, &min_scratch) ||
        !safe_mul(16u, f_slot_bound, &f16) ||
        !safe_mul(16u, v_slot_bound, &v16) ||
        !safe_add(ws_header, morsel_tls, &guaranteed) ||
        !safe_add(guaranteed, f16, &guaranteed) ||
        !safe_add(guaranteed, v16, &guaranteed) ||
        !safe_add(guaranteed, f_slot_bound / 4u, &guaranteed) ||
        !safe_add(guaranteed, transient_max, &guaranteed) ||
        min_scratch > (std::numeric_limits<size_t>::max)() - 127u ||
        guaranteed > (std::numeric_limits<size_t>::max)() - 127u ||
        max_result > (std::numeric_limits<size_t>::max)() - 63u) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }

    out_plan->min_scratch_bytes        = (min_scratch + 127u) & ~size_t(127u);
    out_plan->guaranteed_scratch_bytes = (guaranteed + 127u) & ~size_t(127u);
    out_plan->max_result_bytes         = (max_result + 63u) & ~size_t(63u);
    return IMPULSE_VM_V11_OK;
}
