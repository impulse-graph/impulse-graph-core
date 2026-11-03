/**
 * @file fuzz_vm_bytecode.cpp
 * @brief Fuzz target: ImpulseVM static validator + interpreter, with emphasis on the multi-lane
 *        ISA (OP_LANE_* 0xC0..0xC9) and Vamana ANN ops (0xCA/0xCB).
 *
 * Input layout (all bytes consumed, no length prerequisites):
 *   bytes [0..8)   : register presets for R1..R8. Low nibble = kind, high nibble = argument:
 *                      0 null, 1 node id, 2 relation id, 3 bitset (valid handle), 4 node vector (valid),
 *                      5 float vector, 6 double vector, 7 string vector, 8 value map,
 *                      9 lane frontier handle (acquired, uninitialised), 10 boolean, 11 float, 12 double,
 *                      13 random type tag + random payload, 14 bitset type with OUT-OF-RANGE handle,
 *                      15 node-vector type with NEGATIVE handle.
 *   bytes [8..)    : 8-byte impulse_instruction_t records (max 64). Decode bias: when (flags & 3) == 0 the
 *                    opcode is remapped into 0xC0..0xCB; when (flags & 4) != 0 dst_reg is masked to R0..R63
 *                    (otherwise it may be out of range on purpose).
 *
 * Contract checked: impulse_vm_validate and impulse_vm_execute (fuel-limited) return a status
 * without crashing / tripping ASan/UBSan/TSan.
 *
 * Mode (env IMPULSE_FUZZ_VM_MODE): "all" (default) executes every program, including ones the
 * validator rejects (the interpreter is also expected to be memory-safe on unvalidated bytecode);
 * "validated" executes only programs where impulse_vm_validate() == IMPULSE_VM_OK.
 */

#include "fuzz_snapshot_util.hpp"
#include "impulse_vm.h"

#include <cstdlib>

namespace {

constexpr size_t kHeader = 8;
constexpr size_t kMaxInstr = 64;

void apply_preset(impulse_vm_context_t* ctx, impulse_vm_state_t& st, int reg, uint8_t b, uint64_t rnd) {
    uint8_t kind = b & 0xF, arg = b >> 4;
    auto set = [&](uint8_t type, uint64_t v) {
        st.register_types[reg] = type;
        st.registers[reg] = v;
    };
    switch (kind) {
        case 0: set(TYPE_NULL, 0); break;
        case 1: set(TYPE_NODE_ID, arg); break;
        case 2: set(TYPE_RELATION_ID, arg % 3); break;
        case 3: {
            int h = impulse_vm_context_acquire_bitset(ctx);
            if (h >= 0) {
                impulse_vm_context_bitset_fill(ctx, static_cast<size_t>(h), static_cast<uint64_t>(arg) * 8);
                set(TYPE_BITSET_HANDLE, static_cast<uint64_t>(h));
            }
            break;
        }
        case 4: {
            int h = impulse_vm_context_acquire_node_vector(ctx);
            if (h >= 0) {
                uint64_t vals[17];
                for (uint8_t i = 0; i <= arg; ++i) vals[i] = (rnd >> (i % 8)) % 6 + (i & 1 ? 0 : i % 4);
                impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h), vals, static_cast<size_t>(arg) + 1);
                set(TYPE_NODE_VECTOR, static_cast<uint64_t>(h));
            }
            break;
        }
        case 5: {
            int h = impulse_vm_context_acquire_float_vector(ctx);
            if (h >= 0) {
                for (uint8_t i = 0; i < arg; ++i)
                    impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h), i, static_cast<float>(i) * 0.5f);
                set(TYPE_FLOAT_VECTOR, static_cast<uint64_t>(h));
            }
            break;
        }
        case 6: {
            int h = impulse_vm_context_acquire_double_vector(ctx);
            if (h >= 0) {
                for (uint8_t i = 0; i < arg; ++i)
                    impulse_vm_context_double_vector_set(ctx, static_cast<size_t>(h), i, static_cast<double>(i));
                set(TYPE_DOUBLE_VECTOR, static_cast<uint64_t>(h));
            }
            break;
        }
        case 7: {
            int h = impulse_vm_context_acquire_string_vector(ctx);
            if (h >= 0) {
                impulse_vm_context_string_vector_add(ctx, static_cast<size_t>(h), "a");
                set(TYPE_STRING_VECTOR, static_cast<uint64_t>(h));
            }
            break;
        }
        case 8: {
            int h = impulse_vm_context_acquire_value_map(ctx);
            if (h >= 0) set(TYPE_VALUE_MAP, static_cast<uint64_t>(h));
            break;
        }
        case 9: {
            int h = impulse_vm_context_acquire_lane_frontier(ctx);
            if (h >= 0) set(TYPE_LANE_FRONTIER, static_cast<uint64_t>(h));
            break;
        }
        case 10: set(TYPE_BOOLEAN, arg & 1); break;
        case 11: set(TYPE_FLOAT, rnd & 0xFFFFFFFFu); break;
        case 12: set(TYPE_DOUBLE, rnd); break;
        case 13: set(static_cast<uint8_t>(rnd >> 56), rnd); break;
        case 14: set(TYPE_BITSET_HANDLE, 1000u + arg); break;
        default: set(TYPE_NODE_VECTOR, static_cast<uint64_t>(-1 - arg)); break;
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static impulse_snapshot_t* snap = impfuzz::open_baseline_snapshot(1, "vmsnap");
    static const bool validated_only = [] {
        const char* m = std::getenv("IMPULSE_FUZZ_VM_MODE");
        return m && std::strcmp(m, "validated") == 0;
    }();
    if (!data || size < kHeader + sizeof(impulse_instruction_t)) return 0;

    size_t n = (size - kHeader) / sizeof(impulse_instruction_t);
    if (n > kMaxInstr) n = kMaxInstr;
    impulse_instruction_t prog[kMaxInstr];
    std::memcpy(prog, data + kHeader, n * sizeof(impulse_instruction_t));
    for (size_t i = 0; i < n; ++i) {
        if ((prog[i].flags & 3) == 0) prog[i].opcode = static_cast<uint8_t>(0xC0 + prog[i].opcode % 12);
        if (prog[i].flags & 4) prog[i].dst_reg &= 63;
    }

    impulse_vm_status_t vst = impulse_vm_validate(prog, n);
    if (validated_only && vst != IMPULSE_VM_OK) return 0;

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    if (!ctx) return 0;
    impulse_vm_context_set_fuel(ctx, 5000);
    impulse_vm_state_t st{};
    st.query_context = ctx;
    uint64_t rnd = impfuzz::fnv1a64(data, size);
    for (int r = 0; r < 8; ++r) apply_preset(ctx, st, r + 1, data[r], rnd * (r + 1) + static_cast<uint64_t>(r));
    // OP_INIT_INPUT_SET documents `input_param` as a raw pointer to bitset words; any non-zero scalar
    // would be a caller contract violation (not an engine bug), so scalar seeds are only passed when
    // the program does not contain that opcode.
    bool uses_input_set = false;
    for (size_t i = 0; i < n; ++i) uses_input_set |= (prog[i].opcode == OP_INIT_INPUT_SET);
    (void)impulse_vm_execute(prog, n, &st, uses_input_set ? 0 : (rnd & 0xF));
    impulse_vm_context_destroy(ctx);
    return 0;
}

static std::vector<impfuzz::Bytes> make_seeds() {
    using impfuzz::Bytes;
    auto push_ins = [](Bytes& b, uint8_t op, uint8_t flags, uint16_t dst, uint32_t payload) {
        impulse_instruction_t i{op, flags, dst, payload};
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&i);
        b.insert(b.end(), p, p + sizeof(i));
    };
    // Presets: R1 = node vector(arg 4), R2 = lane frontier handle, R3 = bitset, R4 = float vector,
    //          R5 = node id, R6 = null, R7 = bitset w/ bad handle, R8 = double vector.
    const uint8_t preset[8] = {0x44, 0x09, 0x33, 0x45, 0x31, 0x00, 0x0E, 0x46};
    std::vector<Bytes> seeds;
    auto header = [&](Bytes& b) { b.assign(preset, preset + 8); };
    for (uint32_t k : {0u, 1u, 8u, 63u, 64u, 65u, 200u}) {
        Bytes b;
        header(b);
        push_ins(b, OP_LANE_INIT_SEEDS, 1, 9, (k << 16) | 0u);
        push_ins(b, OP_HALT, 0, 0, 0);
        seeds.push_back(b);
    }
    for (uint8_t op = 0xC0; op <= 0xCB; ++op) {
        for (uint8_t flags : {uint8_t(0), uint8_t(1), uint8_t(3), uint8_t(0xFF)}) {
            for (uint32_t payload : {0u, 1u, 0x00020001u, 0x00410040u, 0xFFFFFFFFu}) {
                Bytes b;
                header(b);
                push_ins(b, OP_LANE_INIT_SEEDS, 1, 9, (4u << 16));
                push_ins(b, op, flags, 9, payload);
                push_ins(b, op, flags, 2, payload);   // dst = handle holder register (type tag mismatch cases)
                push_ins(b, op, flags, 6, payload);   // dst = null register
                push_ins(b, op, flags, 7, payload);   // dst = invalid-handle register
                push_ins(b, op, flags, 64, payload);  // dst out of range
                push_ins(b, OP_HALT, 0, 0, 0);
                seeds.push_back(b);
            }
        }
    }
    {   // INIT_GROUPS / INIT_SEEDS from packed node vector (R1) and from wrong-typed sources.
        Bytes b;
        header(b);
        push_ins(b, OP_LANE_INIT_GROUPS, 0, 9, 1u);
        push_ins(b, OP_LANE_INIT_SEEDS, 0, 9, 3u);
        push_ins(b, OP_LANE_INIT_GROUPS, 0, 9, 4u);
        push_ins(b, OP_LANE_INIT_SEEDS, 0, 9, 6u);
        push_ins(b, OP_HALT, 0, 0, 0);
        seeds.push_back(b);
    }
    return seeds;
}

IMPULSE_FUZZ_MAIN("fuzz_vm_bytecode", make_seeds, 1024)
