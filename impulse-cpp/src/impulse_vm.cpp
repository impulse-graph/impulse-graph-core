#include "impulse_vm.h"
#include "impulse_graph.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <vector>
#include <algorithm>

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wgnu-label-as-value"
  #pragma clang diagnostic ignored "-Wgnu-designator"
  #pragma clang diagnostic ignored "-Wc99-designator"
  #pragma clang diagnostic ignored "-Winitializer-overrides"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define HAS_COMPUTED_GOTO 1
#endif

// Off-heap BitSet slice struct
struct VmBitSet {
    uint64_t* words = nullptr;
    size_t word_count = 0;

    void clear() {
        if (words && word_count > 0) {
            std::memset(words, 0, word_count * sizeof(uint64_t));
        }
    }
};

struct BoundRelationSlot {
    const uint32_t* offsets_ptr = nullptr;
    const uint32_t* targets_ptr = nullptr;
    uint64_t node_count = 0;
    uint64_t edge_count = 0;
};

// Thread-local virtual machine context implementation
struct impulse_vm_context {
    const impulse_snapshot_t* snapshot;
    std::array<uint32_t, 32> call_stack;
    uint32_t stack_pointer;

    // Off-heap BitSet Arena memory block
    uint64_t* arena_memory;
    size_t words_per_bitset;
    size_t max_nodes;

    std::array<VmBitSet, 16> bitsets;
    std::array<bool, 16> bitset_allocated;

    // Pre-indexed relation slots for zero-lookup overhead
    std::vector<BoundRelationSlot> slots;

    // Contiguous node buffer for array returns
    std::vector<uint64_t> node_buffer;
};

inline int acquire_bitset(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 16; ++i) {
        if (!ctx->bitset_allocated[i]) {
            ctx->bitset_allocated[i] = true;
            ctx->bitsets[i].clear();
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_bitset(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 16) {
        ctx->bitsets[handle].clear();
        ctx->bitset_allocated[handle] = false;
    }
}

inline void bitset_add(VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    if (node_id < max_nodes) {
        size_t word_idx = node_id / 64;
        size_t bit_idx = node_id % 64;
        bs.words[word_idx] |= (1ULL << bit_idx);
    }
}

inline bool bitset_test(const VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    if (node_id < max_nodes) {
        size_t word_idx = node_id / 64;
        size_t bit_idx = node_id % 64;
        return (bs.words[word_idx] & (1ULL << bit_idx)) != 0;
    }
    return false;
}

extern "C" {

impulse_vm_context_t* impulse_vm_context_create(const impulse_snapshot_t* snapshot) {
    auto* ctx = new impulse_vm_context();
    ctx->snapshot = snapshot;
    ctx->stack_pointer = 0;
    ctx->call_stack.fill(0);

    uint64_t max_nodes = 0;
    if (snapshot) {
        max_nodes = impulse_snapshot_max_node_count(snapshot);
    }
    if (max_nodes == 0) {
        max_nodes = 1024 * 1024; // Default fallback for tests
    }
    ctx->max_nodes = max_nodes;
    ctx->words_per_bitset = (max_nodes + 63) / 64;

    // Pre-allocate 16 off-heap bitsets in a contiguous memory block
    ctx->arena_memory = new uint64_t[16 * ctx->words_per_bitset]();
    for (size_t i = 0; i < 16; ++i) {
        ctx->bitsets[i].words = ctx->arena_memory + (i * ctx->words_per_bitset);
        ctx->bitsets[i].word_count = ctx->words_per_bitset;
        ctx->bitset_allocated[i] = false;
    }

    // Pre-index snapshot relations if a snapshot is present
    if (snapshot) {
        uint16_t rel_count = impulse_snapshot_relation_count(snapshot);
        ctx->slots.resize(rel_count);
        for (uint16_t r = 0; r < rel_count; ++r) {
            const uint32_t* offsets = nullptr;
            const uint32_t* targets = nullptr;
            uint64_t node_count = 0;
            uint64_t edge_count = 0;
            impulse_snapshot_get_relation_buffers(
                snapshot, r, &offsets, &targets, &node_count, &edge_count
            );
            ctx->slots[r].offsets_ptr = offsets;
            ctx->slots[r].targets_ptr = targets;
            ctx->slots[r].node_count = node_count;
            ctx->slots[r].edge_count = edge_count;
        }
    }

    return ctx;
}

void impulse_vm_context_destroy(impulse_vm_context_t* ctx) {
    if (ctx) {
        delete[] ctx->arena_memory;
        delete ctx;
    }
}

size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx) {
    return ctx ? ctx->node_buffer.size() : 0;
}

// Main execution routine
impulse_vm_status_t impulse_vm_execute(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param
) {
    if (!bytecode || !vm_state) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

#define VALIDATE_REG(r) \
    do { \
        if ((r) >= 64) return IMPULSE_VM_ERR_INVALID_REGISTER; \
    } while(0)

#if HAS_COMPUTED_GOTO
    // Jump table containing addresses of labels
    static const void* dispatch_table[256] = {
        [0 ... 255] = &&op_INVALID,
        [OP_NOP] = &&op_NOP,
        [OP_INIT_INPUT_NODE] = &&op_INIT_INPUT_NODE,
        [OP_INIT_INPUT_SET] = &&op_INIT_INPUT_SET,
        [OP_LOAD_CONST_INT] = &&op_LOAD_CONST_INT,
        [OP_LOAD_CONST_FLOAT] = &&op_LOAD_CONST_FLOAT,
        [OP_JMP] = &&op_JMP,
        [OP_JZ] = &&op_JZ,
        [OP_JNZ] = &&op_JNZ,
        [OP_LOOP_DECR] = &&op_LOOP_DECR,
        [OP_MOV] = &&op_MOV,
        [OP_CLEAR_REG] = &&op_CLEAR_REG,
        [OP_SET_UNION] = &&op_SET_UNION,
        [OP_SET_INTERSECT] = &&op_SET_INTERSECT,
        [OP_SET_DIFFERENCE] = &&op_SET_DIFFERENCE,
        [OP_SET_CARDINALITY] = &&op_SET_CARDINALITY,
        [OP_CSR_WALK] = &&op_CSR_WALK,
        [OP_CSR_DEGREE] = &&op_CSR_DEGREE,
        [OP_STABLE_CHECK] = &&op_STABLE_CHECK,
        [OP_COLLECT_BITSET] = &&op_COLLECT_BITSET,
        [OP_COLLECT_ARRAY] = &&op_COLLECT_ARRAY,
        [OP_HALT] = &&op_HALT
    };

    #define DISPATCH() \
        do { \
            if (vm_state->pc >= instruction_count) goto op_OUT_OF_BOUNDS; \
            uint8_t op = bytecode[vm_state->pc].opcode; \
            goto *dispatch_table[op]; \
        } while(0)

    // Start execution
    DISPATCH();

op_NOP:
    vm_state->pc++;
    DISPATCH();

op_INIT_INPUT_NODE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = input_param;
    vm_state->register_types[dst] = TYPE_NODE_ID;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

op_INIT_INPUT_SET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    int handle = acquire_bitset(vm_state->query_context);
    if (handle < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    vm_state->registers[dst] = static_cast<uint64_t>(handle);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    auto& bs = vm_state->query_context->bitsets[handle];
    bs.clear();

    bool is_empty = true;
    if (input_param) {
        const uint64_t* src_words = reinterpret_cast<const uint64_t*>(input_param);
        std::memcpy(bs.words, src_words, vm_state->query_context->words_per_bitset * sizeof(uint64_t));
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if (bs.words[i] != 0) {
                is_empty = false;
                break;
            }
        }
    }

    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_LOAD_CONST_INT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    int32_t imm = static_cast<int32_t>(inst.payload);
    vm_state->registers[dst] = static_cast<uint64_t>(static_cast<int64_t>(imm));
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

op_LOAD_CONST_FLOAT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_FLOAT;
    vm_state->pc++;
    DISPATCH();
}

op_JMP: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    vm_state->pc += offset;
    DISPATCH();
}

op_JZ: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    if (vm_state->flags & IMPULSE_VM_FLAG_ZF) {
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}

op_JNZ: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    if (!(vm_state->flags & IMPULSE_VM_FLAG_ZF)) {
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}

op_LOOP_DECR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    int64_t val = static_cast<int64_t>(vm_state->registers[dst]);
    val--;
    vm_state->registers[dst] = static_cast<uint64_t>(val);
    if (val == 0) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
    if (val > 0) {
        int32_t offset = static_cast<int32_t>(inst.payload);
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}

op_MOV: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->register_types[dst] = vm_state->register_types[src];
    vm_state->pc++;
    DISPATCH();
}

op_CLEAR_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_NULL;
    vm_state->pc++;
    DISPATCH();
}

op_SET_UNION: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] |= bs_src.words[i];
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }

    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_SET_INTERSECT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] &= bs_src.words[i];
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t node_id = vm_state->registers[src];
        bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
        bs_dst.clear();
        if (keeps) {
            bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
        }
    } else {
        bs_dst.clear();
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }

    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_SET_DIFFERENCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] &= ~bs_src.words[i];
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t node_id = vm_state->registers[src];
        if (node_id < vm_state->query_context->max_nodes) {
            size_t word_idx = node_id / 64;
            size_t bit_idx = node_id % 64;
            bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
        }
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }

    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_SET_CARDINALITY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    uint64_t count = 0;
    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int handle = static_cast<int>(vm_state->registers[src]);
        const auto& bs = vm_state->query_context->bitsets[handle];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            count += std::popcount(bs.words[i]);
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        count = 1;
    }

    vm_state->registers[dst] = count;
    vm_state->register_types[dst] = TYPE_INT64;

    if (count == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    int h_dst = -1;
    bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);

    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        int h_existing = static_cast<int>(vm_state->registers[dst]);
        if (accum) {
            h_dst = h_existing;
        } else {
            if (dst == src) {
                h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->query_context->bitsets[h_dst].clear();
            } else {
                h_dst = h_existing;
                vm_state->query_context->bitsets[h_dst].clear();
            }
        }
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = bs_src.words[i];
                if (w == 0) continue;
                for (int b = 0; b < 64; ++b) {
                    if (w & (1ULL << b)) {
                        uint64_t u = i * 64 + b;
                        if (u < slot.node_count) {
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                bitset_add(bs_dst, slot.targets_ptr[idx], vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end   = slot.offsets_ptr[u + 1];
                for (uint32_t idx = start; idx < end; ++idx) {
                    bitset_add(bs_dst, slot.targets_ptr[idx], vm_state->query_context->max_nodes);
                }
            }
        }
    }

    if (!accum && dst == src && src_is_bitset) {
        release_bitset(vm_state->query_context, h_src);
    }
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_CSR_DEGREE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    uint64_t degree = 0;
    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        if (slot.offsets_ptr && u < slot.node_count) {
            degree = slot.offsets_ptr[u + 1] - slot.offsets_ptr[u];
        }
    }

    vm_state->registers[dst] = degree;
    vm_state->register_types[dst] = TYPE_INT64;

    if (degree == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_STABLE_CHECK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    bool is_subset = true;
    bool dst_is_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);
    int h_dst = dst_is_bitset ? static_cast<int>(vm_state->registers[dst]) : -1;
    uint64_t scalar_dst = !dst_is_bitset ? vm_state->registers[dst] : 0;

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    if (src_is_bitset && dst_is_bitset) {
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if ((bs_src.words[i] & ~bs_dst.words[i]) != 0) {
                is_subset = false;
                break;
            }
        }
    } else if (!src_is_bitset && dst_is_bitset) {
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        is_subset = bitset_test(bs_dst, scalar_src, vm_state->query_context->max_nodes);
    } else if (!src_is_bitset && !dst_is_bitset) {
        is_subset = (scalar_src == scalar_dst);
    } else {
        is_subset = false;
    }

    if (is_subset) {
        vm_state->flags |= IMPULSE_VM_FLAG_ST;
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ST;
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_COLLECT_BITSET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->register_types[dst] = vm_state->register_types[src];

    vm_state->pc++;
    DISPATCH();
}

op_COLLECT_ARRAY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    auto& node_buf = vm_state->query_context->node_buffer;
    node_buf.clear();

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int handle = static_cast<int>(vm_state->registers[src]);
        const auto& bs = vm_state->query_context->bitsets[handle];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t node_id = i * 64 + b;
                    node_buf.push_back(node_id);
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        node_buf.push_back(vm_state->registers[src]);
    }

    vm_state->registers[dst] = reinterpret_cast<uint64_t>(node_buf.data());
    vm_state->register_types[dst] = TYPE_NODE_VECTOR;

    if (node_buf.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_HALT:
    return IMPULSE_VM_OK;

op_INVALID:
    return IMPULSE_VM_ERR_INVALID_OPCODE;

op_OUT_OF_BOUNDS:
    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

#else
    // Fallback switch-case loop for compilers without computed goto (MSVC)
    while (vm_state->pc < instruction_count) {
        const auto& inst = bytecode[vm_state->pc];
        switch (inst.opcode) {
            case OP_NOP:
                vm_state->pc++;
                break;
            case OP_HALT:
                return IMPULSE_VM_OK;
            case OP_MOV: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                vm_state->registers[dst] = vm_state->registers[src];
                vm_state->register_types[dst] = vm_state->register_types[src];
                vm_state->pc++;
                break;
            }
            case OP_CLEAR_REG: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                vm_state->registers[dst] = 0;
                vm_state->register_types[dst] = TYPE_NULL;
                vm_state->pc++;
                break;
            }
            case OP_LOAD_CONST_INT: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                int32_t imm = static_cast<int32_t>(inst.payload);
                vm_state->registers[dst] = static_cast<uint64_t>(static_cast<int64_t>(imm));
                vm_state->register_types[dst] = TYPE_INT64;
                vm_state->pc++;
                break;
            }
            case OP_LOAD_CONST_FLOAT: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                vm_state->registers[dst] = inst.payload;
                vm_state->register_types[dst] = TYPE_FLOAT;
                vm_state->pc++;
                break;
            }
            case OP_INIT_INPUT_NODE: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                vm_state->registers[dst] = input_param;
                vm_state->register_types[dst] = TYPE_NODE_ID;
                vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                vm_state->pc++;
                break;
            }
            case OP_INIT_INPUT_SET: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                int handle = acquire_bitset(vm_state->query_context);
                if (handle < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = static_cast<uint64_t>(handle);
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

                auto& bs = vm_state->query_context->bitsets[handle];
                bs.clear();

                bool is_empty = true;
                if (input_param) {
                    const uint64_t* src_words = reinterpret_cast<const uint64_t*>(input_param);
                    std::memcpy(bs.words, src_words, vm_state->query_context->words_per_bitset * sizeof(uint64_t));
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        if (bs.words[i] != 0) {
                            is_empty = false;
                            break;
                        }
                    }
                }
                if (is_empty) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }
                vm_state->pc++;
                break;
            }
            case OP_JMP: {
                int32_t offset = static_cast<int32_t>(inst.payload);
                vm_state->pc += offset;
                break;
            }
            case OP_JZ: {
                int32_t offset = static_cast<int32_t>(inst.payload);
                if (vm_state->flags & IMPULSE_VM_FLAG_ZF) {
                    vm_state->pc += offset;
                } else {
                    vm_state->pc++;
                }
                break;
            }
            case OP_JNZ: {
                int32_t offset = static_cast<int32_t>(inst.payload);
                if (!(vm_state->flags & IMPULSE_VM_FLAG_ZF)) {
                    vm_state->pc += offset;
                } else {
                    vm_state->pc++;
                }
                break;
            }
            case OP_LOOP_DECR: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                int64_t val = static_cast<int64_t>(vm_state->registers[dst]);
                val--;
                vm_state->registers[dst] = static_cast<uint64_t>(val);
                if (val == 0) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }
                if (val > 0) {
                    int32_t offset = static_cast<int32_t>(inst.payload);
                    vm_state->pc += offset;
                } else {
                    vm_state->pc++;
                }
                break;
            }
            case OP_SET_UNION: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        bs_dst.words[i] |= bs_src.words[i];
                    }
                } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
                }

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) {
                        is_empty = false;
                        break;
                    }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_SET_INTERSECT: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        bs_dst.words[i] &= bs_src.words[i];
                    }
                } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    uint64_t node_id = vm_state->registers[src];
                    bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
                    bs_dst.clear();
                    if (keeps) {
                        bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                    }
                } else {
                    bs_dst.clear();
                }

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) {
                        is_empty = false;
                        break;
                    }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_SET_DIFFERENCE: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        bs_dst.words[i] &= ~bs_src.words[i];
                    }
                } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    uint64_t node_id = vm_state->registers[src];
                    if (node_id < vm_state->query_context->max_nodes) {
                        size_t word_idx = node_id / 64;
                        size_t bit_idx = node_id % 64;
                        bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
                    }
                }

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) {
                        is_empty = false;
                        break;
                    }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_SET_CARDINALITY: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                uint64_t count = 0;
                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int handle = static_cast<int>(vm_state->registers[src]);
                    const auto& bs = vm_state->query_context->bitsets[handle];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        count += std::popcount(bs.words[i]);
                    }
                } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    count = 1;
                }

                vm_state->registers[dst] = count;
                vm_state->register_types[dst] = TYPE_INT64;

                if (count == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_CSR_WALK: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
                uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

                int h_dst = -1;
                bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);

                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    int h_existing = static_cast<int>(vm_state->registers[dst]);
                    if (accum) {
                        h_dst = h_existing;
                    } else {
                        if (dst == src) {
                            h_dst = acquire_bitset(vm_state->query_context);
                            if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                            vm_state->query_context->bitsets[h_dst].clear();
                        } else {
                            h_dst = h_existing;
                            vm_state->query_context->bitsets[h_dst].clear();
                        }
                    }
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                }

                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (slot.offsets_ptr && slot.targets_ptr) {
                    if (src_is_bitset) {
                        const auto& bs_src = vm_state->query_context->bitsets[h_src];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            uint64_t w = bs_src.words[i];
                            if (w == 0) continue;
                            for (int b = 0; b < 64; ++b) {
                                if (w & (1ULL << b)) {
                                    uint64_t u = i * 64 + b;
                                    if (u < slot.node_count) {
                                        uint32_t start = slot.offsets_ptr[u];
                                        uint32_t end   = slot.offsets_ptr[u + 1];
                                        for (uint32_t idx = start; idx < end; ++idx) {
                                            bitset_add(bs_dst, slot.targets_ptr[idx], vm_state->query_context->max_nodes);
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        uint64_t u = scalar_src;
                        if (u < slot.node_count) {
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                bitset_add(bs_dst, slot.targets_ptr[idx], vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }

                if (!accum && dst == src && src_is_bitset) {
                    release_bitset(vm_state->query_context, h_src);
                }
                vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) {
                        is_empty = false;
                        break;
                    }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_CSR_DEGREE: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];

                uint64_t degree = 0;
                if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    uint64_t u = vm_state->registers[src];
                    if (slot.offsets_ptr && u < slot.node_count) {
                        degree = slot.offsets_ptr[u + 1] - slot.offsets_ptr[u];
                    }
                }

                vm_state->registers[dst] = degree;
                vm_state->register_types[dst] = TYPE_INT64;

                if (degree == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_STABLE_CHECK: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                bool is_subset = true;
                bool dst_is_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);
                int h_dst = dst_is_bitset ? static_cast<int>(vm_state->registers[dst]) : -1;
                uint64_t scalar_dst = !dst_is_bitset ? vm_state->registers[dst] : 0;

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
                uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

                if (src_is_bitset && dst_is_bitset) {
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        if ((bs_src.words[i] & ~bs_dst.words[i]) != 0) {
                            is_subset = false;
                            break;
                        }
                    }
                } else if (!src_is_bitset && dst_is_bitset) {
                    const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    is_subset = bitset_test(bs_dst, scalar_src, vm_state->query_context->max_nodes);
                } else if (!src_is_bitset && !dst_is_bitset) {
                    is_subset = (scalar_src == scalar_dst);
                } else {
                    is_subset = false;
                }

                if (is_subset) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ST;
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ST;
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }

                vm_state->pc++;
                break;
            }
            case OP_COLLECT_BITSET: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                vm_state->registers[dst] = vm_state->registers[src];
                vm_state->register_types[dst] = vm_state->register_types[src];

                vm_state->pc++;
                break;
            }
            case OP_COLLECT_ARRAY: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                auto& node_buf = vm_state->query_context->node_buffer;
                node_buf.clear();

                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int handle = static_cast<int>(vm_state->registers[src]);
                    const auto& bs = vm_state->query_context->bitsets[handle];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w = bs.words[i];
                        if (w == 0) continue;
                        for (int b = 0; b < 64; ++b) {
                            if (w & (1ULL << b)) {
                                uint64_t node_id = i * 64 + b;
                                node_buf.push_back(node_id);
                            }
                        }
                    }
                } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                    node_buf.push_back(vm_state->registers[src]);
                }

                vm_state->registers[dst] = reinterpret_cast<uint64_t>(node_buf.data());
                vm_state->register_types[dst] = TYPE_NODE_VECTOR;

                if (node_buf.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            default:
                return IMPULSE_VM_ERR_INVALID_OPCODE;
        }
    }
    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
#endif
}

} // extern "C"

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
