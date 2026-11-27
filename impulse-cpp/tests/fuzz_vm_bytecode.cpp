/**
 * @file fuzz_vm_bytecode.cpp
 * @brief Fuzz target: ImpulseVM v0.11 static validator, memory planner, and executor.
 *
 * Contract checked: impulse_vm_validate_v11, impulse_vm_plan_memory, and
 * impulse_vm_execute_v11 (gas-limited) return a status without crashing or
 * tripping ASan/UBSan/TSan on arbitrary or structured .impb v11 bytecode.
 */

#include "fuzz_snapshot_util.hpp"
#include "impulse_vm_v11.h"

#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static impulse_snapshot_t* g_snap = impfuzz::open_baseline_snapshot(1, "vmsnap");
    alignas(128) static uint8_t g_scratch[1024 * 1024];
    if (!data || size == 0) return 0;

    // 1. Fuzz raw bytes directly against validator + executor
    (void)impulse_vm_validate_v11(data, size);

    impulse_vm_workspace_v11_t* ws = nullptr;
    if (impulse_workspace_init(g_scratch, sizeof(g_scratch), &ws) != IMPULSE_VM_V11_OK || !ws) {
        return 0;
    }

    impulse_vm_params_v11_t params{};
    params.gas_limit = 5000ULL;
    size_t out_bytes = 0;
    (void)impulse_vm_execute_v11(data, size, g_snap, &params, ws, nullptr, 0, &out_bytes);

    // 2. Wrap fuzz bytes as a valid 32B .impb v11 container header + instruction words
    size_t n_words = size / 8u;
    if (n_words > 64u) n_words = 64u;
    if (n_words > 0u) {
        std::vector<uint8_t> impb(sizeof(impulse_impb_header_v11_t) + n_words * 8u, 0u);
        impulse_impb_header_v11_t hdr{};
        hdr.magic = 0x494D5042u;
        hdr.isa_version = 0x000Bu;
        hdr.code_word_count = static_cast<uint32_t>(n_words);
        hdr.max_call_depth = 8u;
        std::memcpy(impb.data(), &hdr, sizeof(hdr));
        std::memcpy(impb.data() + sizeof(hdr), data, n_words * 8u);

        impulse_vm_validation_v11_t v = impulse_vm_validate_v11(impb.data(), impb.size());
        if (v.status == IMPULSE_VM_V11_OK) {
            impulse_vm_memory_plan_t plan{};
            (void)impulse_vm_plan_memory(impb.data(), impb.size(), g_snap, 1u, &plan);
        }
        (void)impulse_vm_execute_v11(impb.data(), impb.size(), g_snap, &params, ws, nullptr, 0, &out_bytes);
    }
    return 0;
}

static std::vector<impfuzz::Bytes> make_seeds() {
    std::vector<impfuzz::Bytes> out;
    // Minimal HALT word
    uint64_t halt_word = 0ULL;
    impfuzz::Bytes b(8, 0);
    std::memcpy(b.data(), &halt_word, 8);
    out.push_back(b);
    return out;
}

IMPULSE_FUZZ_MAIN("fuzz_vm_bytecode", make_seeds, 512)
