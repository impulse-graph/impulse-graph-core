#include "impulse_vm.h"
#include "impulse_graph.h"
#include "impulse_simd.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <vector>
#include <algorithm>
#include <unordered_map>

#if defined(_OPENMP)
#include <omp.h>
#endif

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
    const uint32_t* csc_offsets_ptr = nullptr;
    const uint32_t* csc_targets_ptr = nullptr;
    uint64_t node_count = 0;
    uint64_t edge_count = 0;
};

struct BoundAttributeSlot {
    const void* data_ptr = nullptr;
    uint64_t    data_bytes = 0;
    const void* offsets_ptr = nullptr;
    uint64_t    offsets_bytes = 0;
    uint8_t     type_code = 0;
    uint32_t    dimension = 0;
};

struct BoundValueMap {
    std::vector<const char*> keys;
    std::vector<float> values;
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

    // Thread-private workspace bitsets for parallel map/reduce walks
    int max_threads;
    uint64_t* private_arena_memory;
    std::vector<VmBitSet> private_bitsets;

    // Pre-indexed relation slots for zero-lookup overhead
    std::vector<BoundRelationSlot> slots;
    std::vector<std::vector<BoundAttributeSlot>> attribute_slots;

    // Pre-allocated float and double vectors for VM operations
    std::array<std::vector<float>, 4> float_vectors;
    std::array<bool, 4> float_vectors_allocated;
    std::array<std::vector<double>, 4> double_vectors;
    std::array<bool, 4> double_vectors_allocated;
    std::array<std::vector<uint64_t>, 4> node_vectors;
    std::array<bool, 4> node_vectors_allocated;
    std::array<std::vector<const char*>, 4> string_vectors;
    std::array<bool, 4> string_vectors_allocated;
    std::array<BoundValueMap, 4> value_maps;
    std::array<bool, 4> value_maps_allocated;

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

inline int acquire_float_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->float_vectors_allocated[i]) {
            ctx->float_vectors_allocated[i] = true;
            std::memset(ctx->float_vectors[i].data(), 0, ctx->float_vectors[i].size() * sizeof(float));
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_float_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->float_vectors_allocated[handle] = false;
    }
}

inline int acquire_double_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->double_vectors_allocated[i]) {
            ctx->double_vectors_allocated[i] = true;
            std::memset(ctx->double_vectors[i].data(), 0, ctx->double_vectors[i].size() * sizeof(double));
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_double_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->double_vectors_allocated[handle] = false;
    }
}

inline int acquire_node_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->node_vectors_allocated[i]) {
            ctx->node_vectors_allocated[i] = true;
            std::memset(ctx->node_vectors[i].data(), 0, ctx->node_vectors[i].size() * sizeof(uint64_t));
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_node_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->node_vectors_allocated[handle] = false;
    }
}

inline int acquire_string_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->string_vectors_allocated[i]) {
            ctx->string_vectors_allocated[i] = true;
            ctx->string_vectors[i].clear();
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_string_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->string_vectors[handle].clear();
        ctx->string_vectors_allocated[handle] = false;
    }
}

inline int acquire_value_map(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->value_maps_allocated[i]) {
            ctx->value_maps_allocated[i] = true;
            ctx->value_maps[i].keys.clear();
            ctx->value_maps[i].values.clear();
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void release_value_map(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->value_maps[handle].keys.clear();
        ctx->value_maps[handle].values.clear();
        ctx->value_maps_allocated[handle] = false;
    }
}

inline void bitset_add(VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    if (node_id < max_nodes) {
        size_t word_idx = node_id / 64;
        size_t bit_idx = node_id % 64;
        bs.words[word_idx] |= (1ULL << bit_idx);
    }
}

inline void bitset_add_atomic(VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    if (node_id < max_nodes) {
        size_t word_idx = node_id / 64;
        size_t bit_idx = node_id % 64;
        uint64_t mask = (1ULL << bit_idx);
#if defined(_OPENMP)
        __sync_or_and_fetch(&bs.words[word_idx], mask);
#else
        bs.words[word_idx] |= mask;
#endif
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

    ctx->private_arena_memory = nullptr;
#if defined(_OPENMP)
    ctx->max_threads = omp_get_max_threads();
#else
    ctx->max_threads = 1;
#endif
    if (ctx->max_threads > 1) {
        ctx->private_arena_memory = new uint64_t[ctx->max_threads * ctx->words_per_bitset]();
        ctx->private_bitsets.resize(ctx->max_threads);
        for (int i = 0; i < ctx->max_threads; ++i) {
            ctx->private_bitsets[i].words = ctx->private_arena_memory + (i * ctx->words_per_bitset);
            ctx->private_bitsets[i].word_count = ctx->words_per_bitset;
        }
    }

    // Pre-allocate float and double vector buffers
    for (size_t i = 0; i < 4; ++i) {
        ctx->float_vectors[i].resize(ctx->max_nodes, 0.0f);
        ctx->float_vectors_allocated[i] = false;
        ctx->double_vectors[i].resize(ctx->max_nodes, 0.0);
        ctx->double_vectors_allocated[i] = false;
        ctx->node_vectors[i].resize(ctx->max_nodes, 0);
        ctx->node_vectors_allocated[i] = false;
        ctx->string_vectors[i].reserve(ctx->max_nodes);
        ctx->string_vectors_allocated[i] = false;
        ctx->value_maps[i].keys.reserve(ctx->max_nodes);
        ctx->value_maps[i].values.reserve(ctx->max_nodes);
        ctx->value_maps_allocated[i] = false;
    }

    // Pre-index snapshot relations if a snapshot is present
    if (snapshot) {
        uint16_t rel_count = impulse_snapshot_relation_count(snapshot);
        ctx->slots.resize(rel_count);
        ctx->attribute_slots.resize(rel_count);
        for (uint16_t r = 0; r < rel_count; ++r) {
            // CSR buffers
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

            // CSC buffers
            const uint32_t* csc_offsets = nullptr;
            const uint32_t* csc_targets = nullptr;
            impulse_snapshot_get_relation_csc_buffers(
                snapshot, r, &csc_offsets, &csc_targets, nullptr, nullptr
            );
            ctx->slots[r].csc_offsets_ptr = csc_offsets;
            ctx->slots[r].csc_targets_ptr = csc_targets;

            // Attributes
            impulse_relation_directory_entry_t rel_entry{};
            impulse_snapshot_get_relation_entry(snapshot, r, &rel_entry);
            uint16_t attr_count = rel_entry.attr_count;
            ctx->attribute_slots[r].resize(attr_count);
            for (uint16_t a = 0; a < attr_count; ++a) {
                const void* data = nullptr;
                uint64_t data_bytes = 0;
                const void* offsets = nullptr;
                uint64_t offsets_bytes = 0;
                uint8_t type_code = 0;
                uint32_t dimension = 0;
                impulse_snapshot_get_attribute_buffers(
                    snapshot, r, a, &data, &data_bytes, &offsets, &offsets_bytes, &type_code, &dimension
                );
                ctx->attribute_slots[r][a].data_ptr = data;
                ctx->attribute_slots[r][a].data_bytes = data_bytes;
                ctx->attribute_slots[r][a].offsets_ptr = offsets;
                ctx->attribute_slots[r][a].offsets_bytes = offsets_bytes;
                ctx->attribute_slots[r][a].type_code = type_code;
                ctx->attribute_slots[r][a].dimension = dimension;
            }
        }
    }

    return ctx;
}

void impulse_vm_context_destroy(impulse_vm_context_t* ctx) {
    if (ctx) {
        delete[] ctx->private_arena_memory;
        delete[] ctx->arena_memory;
        delete ctx;
    }
}

size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx) {
    return ctx ? ctx->node_buffer.size() : 0;
}

const float* impulse_vm_context_get_float_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4 && ctx->float_vectors_allocated[handle]) {
        return ctx->float_vectors[handle].data();
    }
    return nullptr;
}

const double* impulse_vm_context_get_double_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4 && ctx->double_vectors_allocated[handle]) {
        return ctx->double_vectors[handle].data();
    }
    return nullptr;
}

int impulse_vm_context_acquire_bitset(impulse_vm_context_t* ctx) {
    return acquire_bitset(ctx);
}

void impulse_vm_context_release_bitset(impulse_vm_context_t* ctx, size_t handle) {
    release_bitset(ctx, handle);
}

void impulse_vm_context_bitset_add(impulse_vm_context_t* ctx, size_t handle, uint64_t node_id) {
    if (ctx && handle < 16 && ctx->bitset_allocated[handle]) {
        bitset_add(ctx->bitsets[handle], node_id, ctx->max_nodes);
    }
}

bool impulse_vm_context_bitset_test(const impulse_vm_context_t* ctx, size_t handle, uint64_t node_id) {
    if (ctx && handle < 16 && ctx->bitset_allocated[handle]) {
        return bitset_test(ctx->bitsets[handle], node_id, ctx->max_nodes);
    }
    return false;
}

int impulse_vm_context_acquire_float_vector(impulse_vm_context_t* ctx) {
    return acquire_float_vector(ctx);
}

void impulse_vm_context_release_float_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_float_vector(ctx, handle);
}

void impulse_vm_context_float_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, float val) {
    if (ctx && handle < 4 && ctx->float_vectors_allocated[handle] && index < ctx->max_nodes) {
        ctx->float_vectors[handle][index] = val;
    }
}

int impulse_vm_context_acquire_double_vector(impulse_vm_context_t* ctx) {
    return acquire_double_vector(ctx);
}

void impulse_vm_context_release_double_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_double_vector(ctx, handle);
}

void impulse_vm_context_double_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, double val) {
    if (ctx && handle < 4 && ctx->double_vectors_allocated[handle] && index < ctx->max_nodes) {
        ctx->double_vectors[handle][index] = val;
    }
}

int impulse_vm_context_acquire_node_vector(impulse_vm_context_t* ctx) {
    return acquire_node_vector(ctx);
}

void impulse_vm_context_release_node_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_node_vector(ctx, handle);
}

const uint64_t* impulse_vm_context_get_node_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4 && ctx->node_vectors_allocated[handle]) {
        return ctx->node_vectors[handle].data();
    }
    return nullptr;
}

int impulse_vm_context_acquire_string_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->string_vectors_allocated[i]) {
            ctx->string_vectors_allocated[i] = true;
            ctx->string_vectors[i].clear();
            return static_cast<int>(i);
        }
    }
    return -1;
}

void impulse_vm_context_release_string_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->string_vectors[handle].clear();
        ctx->string_vectors_allocated[handle] = false;
    }
}

void impulse_vm_context_string_vector_add(impulse_vm_context_t* ctx, size_t handle, const char* str) {
    if (ctx && handle < 4 && ctx->string_vectors_allocated[handle]) {
        ctx->string_vectors[handle].push_back(str);
    }
}

size_t impulse_vm_context_string_vector_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4 && ctx->string_vectors_allocated[handle]) {
        return ctx->string_vectors[handle].size();
    }
    return 0;
}

const char* impulse_vm_context_string_vector_get(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < 4 && ctx->string_vectors_allocated[handle] && index < ctx->string_vectors[handle].size()) {
        return ctx->string_vectors[handle][index];
    }
    return nullptr;
}

int impulse_vm_context_acquire_value_map(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < 4; ++i) {
        if (!ctx->value_maps_allocated[i]) {
            ctx->value_maps_allocated[i] = true;
            ctx->value_maps[i].keys.clear();
            ctx->value_maps[i].values.clear();
            return static_cast<int>(i);
        }
    }
    return -1;
}

void impulse_vm_context_release_value_map(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4) {
        ctx->value_maps[handle].keys.clear();
        ctx->value_maps[handle].values.clear();
        ctx->value_maps_allocated[handle] = false;
    }
}

size_t impulse_vm_context_value_map_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < 4 && ctx->value_maps_allocated[handle]) {
        return ctx->value_maps[handle].keys.size();
    }
    return 0;
}

const char* impulse_vm_context_value_map_get_key(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < 4 && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].keys.size()) {
        return ctx->value_maps[handle].keys[index];
    }
    return nullptr;
}

float impulse_vm_context_value_map_get_value(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < 4 && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].values.size()) {
        return ctx->value_maps[handle].values[index];
    }
    return 0.0f;
}

inline const BoundAttributeSlot* find_key_attribute(const impulse_vm_state_t* vm_state, uint16_t domain_id) {
    if (!vm_state || !vm_state->query_context) return nullptr;
    if (domain_id >= vm_state->query_context->attribute_slots.size()) return nullptr;
    const auto& attrs = vm_state->query_context->attribute_slots[domain_id];
    for (const auto& attr : attrs) {
        if (attr.data_ptr) {
            uint8_t base_type = attr.type_code & 0x7F;
            if (base_type == 0x0B || base_type == 0x0A || base_type == 0x03 || base_type == 0x04) {
                return &attr;
            }
        }
    }
    if (!attrs.empty() && attrs[0].data_ptr) {
        return &attrs[0];
    }
    return nullptr;
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
        [OP_CMP] = &&op_CMP,
        [OP_ADD] = &&op_ADD,
        [OP_SUB] = &&op_SUB,
        [OP_SET_UNION] = &&op_SET_UNION,
        [OP_SET_INTERSECT] = &&op_SET_INTERSECT,
        [OP_SET_DIFFERENCE] = &&op_SET_DIFFERENCE,
        [OP_SET_CARDINALITY] = &&op_SET_CARDINALITY,
        [OP_CSR_WALK] = &&op_CSR_WALK,
        [OP_CSR_DEGREE] = &&op_CSR_DEGREE,
        [OP_STABLE_CHECK] = &&op_STABLE_CHECK,
        [OP_NODE_FILTER] = &&op_NODE_FILTER,
        [OP_NODE_FILTER_STR_PREFIX] = &&op_NODE_FILTER_STR_PREFIX,
        [OP_CSR_WALK_REDUCE_SUM] = &&op_CSR_WALK_REDUCE_SUM,
        [OP_CSR_WALK_REDUCE] = &&op_CSR_WALK_REDUCE,
        [OP_CSC_WALK] = &&op_CSC_WALK,
        [OP_VECTOR_MUL_ATTR] = &&op_VECTOR_MUL_ATTR,
        [OP_VECTOR_REDUCE_SUM] = &&op_VECTOR_REDUCE_SUM,
        [OP_VECTOR_DIV] = &&op_VECTOR_DIV,
        [OP_VEC_GET] = &&op_VEC_GET,
        [OP_VEC_SET] = &&op_VEC_SET,
        [OP_VEC_SEQUENCE] = &&op_VEC_SEQUENCE,
        [OP_CSR_GET_NBR] = &&op_CSR_GET_NBR,
        [OP_CC_AFFOREST] = &&op_CC_AFFOREST,
        [OP_CALL] = &&op_CALL,
        [OP_RET] = &&op_RET,
        [OP_MAP_KEYS_TO_DENSE] = &&op_MAP_KEYS_TO_DENSE,
        [OP_MAP_DENSE_TO_KEYS] = &&op_MAP_DENSE_TO_KEYS,
        [OP_COLLECT_VALUE_MAP] = &&op_COLLECT_VALUE_MAP,
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

op_CMP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->registers[dst] == vm_state->registers[src]) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_ADD: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    vm_state->registers[dst] += vm_state->registers[src];
    if (vm_state->registers[dst] == 0) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

op_SUB: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    vm_state->registers[dst] -= vm_state->registers[src];
    if (vm_state->registers[dst] == 0) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
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
            size_t frontier_size = 0;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                frontier_size += std::popcount(bs_src.words[i]);
            }

            bool use_parallel = (vm_state->query_context->max_threads > 1 && frontier_size > 15000);
            if (use_parallel) {
#if defined(_OPENMP)
                int num_threads = vm_state->query_context->max_threads;
                auto* ctx = vm_state->query_context;
                size_t max_nodes = ctx->max_nodes;
                size_t words = ctx->words_per_bitset;

                #pragma omp parallel for schedule(static) num_threads(num_threads)
                for (int t = 0; t < num_threads; ++t) {
                    ctx->private_bitsets[t].clear();
                }

                #pragma omp parallel for schedule(dynamic, 1024) num_threads(num_threads)
                for (size_t i = 0; i < words; ++i) {
                    uint64_t w = bs_src.words[i];
                    if (w == 0) continue;
                    int tid = omp_get_thread_num();
                    auto& private_bs = ctx->private_bitsets[tid];
                    for (int b = 0; b < 64; ++b) {
                        if (w & (1ULL << b)) {
                            uint64_t u = i * 64 + b;
                            if (u < slot.node_count) {
                                uint32_t start = slot.offsets_ptr[u];
                                uint32_t end   = slot.offsets_ptr[u + 1];
                                for (uint32_t idx = start; idx < end; ++idx) {
                                    bitset_add(private_bs, slot.targets_ptr[idx], max_nodes);
                                }
                            }
                        }
                    }
                }

                if (accum) {
                    #pragma omp parallel for schedule(static) num_threads(num_threads)
                    for (size_t i = 0; i < words; ++i) {
                        uint64_t merged = bs_dst.words[i];
                        for (int t = 0; t < num_threads; ++t) {
                            merged |= ctx->private_bitsets[t].words[i];
                        }
                        bs_dst.words[i] = merged;
                    }
                } else {
                    #pragma omp parallel for schedule(static) num_threads(num_threads)
                    for (size_t i = 0; i < words; ++i) {
                        uint64_t merged = 0;
                        for (int t = 0; t < num_threads; ++t) {
                            merged |= ctx->private_bitsets[t].words[i];
                        }
                        bs_dst.words[i] = merged;
                    }
                }
#endif
            } else {
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

op_CSC_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t unv = (inst.payload >> 16) & 0xFF;
    uint16_t rel = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(unv);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    if (vm_state->register_types[src] != TYPE_BITSET_HANDLE ||
        vm_state->register_types[unv] != TYPE_BITSET_HANDLE) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_src = static_cast<int>(vm_state->registers[src]);
    int h_unv = static_cast<int>(vm_state->registers[unv]);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        if (dst == src || dst == unv) {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->query_context->bitsets[h_dst].clear();
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (slot.csc_offsets_ptr && slot.csc_targets_ptr) {
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        const auto& bs_unv = vm_state->query_context->bitsets[h_unv];

#if defined(_OPENMP)
        #pragma omp parallel for schedule(dynamic, 1024)
#endif
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w_unv = bs_unv.words[i];
            if (w_unv == 0) continue;

            uint64_t w_dst = 0;
            for (int b = 0; b < 64; ++b) {
                if (w_unv & (1ULL << b)) {
                    uint64_t v = i * 64 + b;
                    if (v < slot.node_count) {
                        uint32_t start = slot.csc_offsets_ptr[v];
                        uint32_t end   = slot.csc_offsets_ptr[v + 1];
                        for (uint32_t idx = start; idx < end; ++idx) {
                            uint64_t u = slot.csc_targets_ptr[idx];
                            if (bitset_test(bs_src, u, vm_state->query_context->max_nodes)) {
                                w_dst |= (1ULL << b);
                                break;
                            }
                        }
                    }
                }
            }
            bs_dst.words[i] = w_dst;
        }
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

op_NODE_FILTER: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t val_reg = (inst.payload >> 8) & 0xFF;
    uint8_t attr_id = (inst.payload >> 16) & 0xFF;
    uint8_t rel_id = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(val_reg);

    if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
    if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t val = vm_state->registers[val_reg];

    auto eval_match = [&](uint64_t u) -> bool {
        uint8_t base_type = attr.type_code & 0x7F;
        if (base_type == 0x01 || base_type == 0x02) {
            const int64_t* data = static_cast<const int64_t*>(attr.data_ptr);
            return data[u] == static_cast<int64_t>(val);
        } else if (base_type == 0x03) {
            const int32_t* data = static_cast<const int32_t*>(attr.data_ptr);
            return data[u] == static_cast<int32_t>(val);
        } else if (base_type == 0x08) {
            const float* data = static_cast<const float*>(attr.data_ptr);
            float comp = reinterpret_cast<const float&>(val);
            return data[u] == comp;
        } else if (base_type == 0x09) {
            const double* data = static_cast<const double*>(attr.data_ptr);
            double comp = reinterpret_cast<const double&>(val);
            return data[u] == comp;
        }
        return false;
    };

    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
#if defined(_OPENMP)
        #pragma omp parallel for schedule(dynamic, 1024)
#endif
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_src.words[i];
            if (w == 0) continue;
            uint64_t w_dst = 0;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (eval_match(u)) {
                        w_dst |= (1ULL << b);
                    }
                }
            }
            bs_dst.words[i] = w_dst;
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_NODE_FILTER_STR_PREFIX: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t val_reg = (inst.payload >> 8) & 0xFF;
    uint8_t attr_id = (inst.payload >> 16) & 0xFF;
    uint8_t rel_id = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(val_reg);

    if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
    if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);
    if (!prefix) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    size_t prefix_len = std::strlen(prefix);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);

    auto eval_match = [&](uint64_t u) -> bool {
        const char* str_ptr = nullptr;
        size_t str_len = 0;
        if (!attr.offsets_ptr) {
            str_ptr = static_cast<const char*>(attr.data_ptr) + u * attr.dimension;
            str_len = attr.dimension;
        } else {
            const uint32_t* offsets = static_cast<const uint32_t*>(attr.offsets_ptr);
            str_ptr = static_cast<const char*>(attr.data_ptr) + offsets[u];
            str_len = offsets[u + 1] - offsets[u];
        }
        if (!str_ptr) return false;
        for (size_t idx = 0; idx < prefix_len; ++idx) {
            if (idx >= str_len || str_ptr[idx] != prefix[idx]) return false;
        }
        return true;
    };

    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
#if defined(_OPENMP)
        #pragma omp parallel for schedule(dynamic, 1024)
#endif
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_src.words[i];
            if (w == 0) continue;
            uint64_t w_dst = 0;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (eval_match(u)) {
                        w_dst |= (1ULL << b);
                    }
                }
            }
            bs_dst.words[i] = w_dst;
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_DIV: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t num = inst.payload & 0xFFFF;
    uint16_t denom = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(num);
    VALIDATE_REG(denom);

    size_t size = vm_state->query_context->max_nodes;
    bool num_is_double = (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR);
    bool denom_is_double = (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR);

    if (num_is_double || denom_is_double) {
        int h_dst = acquire_double_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        double* dst_vec = vm_state->query_context->double_vectors[h_dst].data();

        const double* num_vec = nullptr;
        std::vector<double> temp_num;
        if (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR) {
            num_vec = vm_state->query_context->double_vectors[vm_state->registers[num]].data();
        } else {
            temp_num.resize(size);
            if (vm_state->register_types[num] == TYPE_FLOAT_VECTOR) {
                const float* fvec = vm_state->query_context->float_vectors[vm_state->registers[num]].data();
                for (size_t i = 0; i < size; ++i) temp_num[i] = fvec[i];
            } else {
                double val = 0.0;
                if (vm_state->register_types[num] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[num]);
                else if (vm_state->register_types[num] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[num]);
                else val = static_cast<double>(vm_state->registers[num]);
                std::fill(temp_num.begin(), temp_num.end(), val);
            }
            num_vec = temp_num.data();
        }

        const double* denom_vec = nullptr;
        std::vector<double> temp_denom;
        if (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR) {
            denom_vec = vm_state->query_context->double_vectors[vm_state->registers[denom]].data();
        } else {
            temp_denom.resize(size);
            if (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR) {
                const float* fvec = vm_state->query_context->float_vectors[vm_state->registers[denom]].data();
                for (size_t i = 0; i < size; ++i) temp_denom[i] = fvec[i];
            } else {
                double val = 0.0;
                if (vm_state->register_types[denom] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[denom]);
                else if (vm_state->register_types[denom] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[denom]);
                else val = static_cast<double>(vm_state->registers[denom]);
                std::fill(temp_denom.begin(), temp_denom.end(), val);
            }
            denom_vec = temp_denom.data();
        }

#if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < size; ++i) {
            dst_vec[i] = (denom_vec[i] != 0.0) ? (num_vec[i] / denom_vec[i]) : 0.0;
        }

        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
    } else {
        int h_dst = acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

        const float* num_vec = nullptr;
        std::vector<float> temp_num;
        if (vm_state->register_types[num] == TYPE_FLOAT_VECTOR) {
            num_vec = vm_state->query_context->float_vectors[vm_state->registers[num]].data();
        } else {
            temp_num.resize(size);
            float val = 0.0f;
            if (vm_state->register_types[num] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[num]);
            else val = static_cast<float>(vm_state->registers[num]);
            std::fill(temp_num.begin(), temp_num.end(), val);
            num_vec = temp_num.data();
        }

        const float* denom_vec = nullptr;
        std::vector<float> temp_denom;
        if (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR) {
            denom_vec = vm_state->query_context->float_vectors[vm_state->registers[denom]].data();
        } else {
            temp_denom.resize(size);
            float val = 0.0f;
            if (vm_state->register_types[denom] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[denom]);
            else val = static_cast<float>(vm_state->registers[denom]);
            std::fill(temp_denom.begin(), temp_denom.end(), val);
            denom_vec = temp_denom.data();
        }

#if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < size; ++i) {
            dst_vec[i] = (denom_vec[i] != 0.0f) ? (num_vec[i] / denom_vec[i]) : 0.0f;
        }

        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }

    vm_state->pc++;
    DISPATCH();
}

op_VEC_GET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_vec = inst.payload & 0xFFFF;
    uint16_t idx_reg = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    VALIDATE_REG(idx_reg);

    uint64_t idx = vm_state->registers[idx_reg];
    if (vm_state->register_types[src_vec] == TYPE_UINT64_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle >= 4 || !vm_state->query_context->node_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->registers[dst] = vm_state->query_context->node_vectors[handle][idx];
        vm_state->register_types[dst] = TYPE_INT64;
    } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        float val = vm_state->query_context->float_vectors[handle][idx];
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = val;
        vm_state->register_types[dst] = TYPE_FLOAT;
    } else if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        double val = vm_state->query_context->double_vectors[handle][idx];
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(val);
        vm_state->register_types[dst] = TYPE_DOUBLE;
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    vm_state->pc++;
    DISPATCH();
}

op_VEC_SET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst_vec = inst.dst_reg;
    uint16_t val_reg = inst.payload & 0xFFFF;
    uint16_t idx_reg = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst_vec);
    VALIDATE_REG(val_reg);
    VALIDATE_REG(idx_reg);

    uint64_t idx = vm_state->registers[idx_reg];
    if (vm_state->register_types[dst_vec] == TYPE_UINT64_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[dst_vec]);
        if (handle >= 4 || !vm_state->query_context->node_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->query_context->node_vectors[handle][idx] = vm_state->registers[val_reg];
    } else if (vm_state->register_types[dst_vec] == TYPE_FLOAT_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[dst_vec]);
        if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        float val = 0.0f;
        if (vm_state->register_types[val_reg] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[val_reg]);
        else val = static_cast<float>(vm_state->registers[val_reg]);
        vm_state->query_context->float_vectors[handle][idx] = val;
    } else if (vm_state->register_types[dst_vec] == TYPE_DOUBLE_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[dst_vec]);
        if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        double val = 0.0;
        if (vm_state->register_types[val_reg] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[val_reg]);
        else if (vm_state->register_types[val_reg] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[val_reg]);
        else val = static_cast<double>(vm_state->registers[val_reg]);
        vm_state->query_context->double_vectors[handle][idx] = val;
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    vm_state->pc++;
    DISPATCH();
}

op_VEC_SEQUENCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (vm_state->register_types[dst] != TYPE_UINT64_VECTOR) {
        int h_dst = acquire_node_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
    }

    int handle = static_cast<int>(vm_state->registers[dst]);
    size_t size = vm_state->query_context->max_nodes;
    uint64_t* data = vm_state->query_context->node_vectors[handle].data();
#if defined(_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (size_t i = 0; i < size; ++i) {
        data[i] = i;
    }

    vm_state->pc++;
    DISPATCH();
}

op_CC_AFFOREST: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    if (vm_state->register_types[dst] != TYPE_UINT64_VECTOR) {
        int h_dst = acquire_node_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
    }

    int handle = static_cast<int>(vm_state->registers[dst]);
    auto& comp = vm_state->query_context->node_vectors[handle];
    size_t N = vm_state->query_context->max_nodes;
    comp.resize(N, 0);

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (size_t u = 0; u < N; ++u) {
        comp[u] = u;
    }

    auto find_root_u64 = [](uint64_t u, std::vector<uint64_t>& comp_vec) -> uint64_t {
        uint64_t curr = u;
        while (curr != comp_vec[curr]) {
            comp_vec[curr] = comp_vec[comp_vec[curr]];
            curr = comp_vec[curr];
        }
        return curr;
    };

    for (size_t r = 0; r < 2; ++r) {
#if defined(_OPENMP)
        #pragma omp parallel for schedule(static, 2048)
#endif
        for (size_t u = 0; u < N; ++u) {
            if (u < slot.node_count) {
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end = slot.offsets_ptr[u + 1];
                uint32_t deg = end - start;
                if (r < deg) {
                    uint32_t v = slot.targets_ptr[start + r];
                    if (v < N) {
                        uint64_t root_u = find_root_u64(u, comp);
                        uint64_t root_v = find_root_u64(v, comp);
                        if (root_u != root_v) {
                            uint64_t high_root = std::min(root_u, root_v);
                            uint64_t low_root = std::max(root_u, root_v);
                            comp[low_root] = high_root;
                        }
                    }
                }
            }
        }
    }

    uint64_t giant_root = 0;
    {
        std::unordered_map<uint64_t, uint32_t> c_counts;
        uint32_t max_count = 0;
        uint32_t sample_n = std::min(static_cast<uint32_t>(N), 100000u);
        for (uint32_t i = 0; i < sample_n; ++i) {
            uint64_t u = (i * 9973ULL) % N;
            uint64_t root = find_root_u64(u, comp);
            uint32_t cnt = ++c_counts[root];
            if (cnt > max_count) {
                max_count = cnt;
                giant_root = root;
            }
        }
    }

#if defined(_OPENMP)
    #pragma omp parallel for schedule(dynamic, 2048)
#endif
    for (size_t u = 0; u < N; ++u) {
        if (find_root_u64(u, comp) == giant_root) continue;

        if (u < slot.node_count) {
            uint32_t start = slot.offsets_ptr[u];
            uint32_t end = slot.offsets_ptr[u + 1];
            uint32_t deg = end - start;
            for (uint32_t i = 0; i < deg; ++i) {
                uint32_t v = slot.targets_ptr[start + i];
                if (v < N) {
                    uint64_t root_u = find_root_u64(u, comp);
                    uint64_t root_v = find_root_u64(v, comp);
                    if (root_u != root_v) {
                        uint64_t high_root = std::min(root_u, root_v);
                        uint64_t low_root = std::max(root_u, root_v);
                        comp[low_root] = high_root;
                        if (high_root == giant_root) break;
                    }
                }
            }
        }
    }

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static, 4096)
#endif
    for (size_t u = 0; u < N; ++u) {
        comp[u] = find_root_u64(u, comp);
    }

    vm_state->pc++;
    DISPATCH();
}

op_CSR_GET_NBR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t node_reg = inst.payload & 0xFFFF;
    uint32_t payload_hi = (inst.payload >> 16) & 0xFFFF;
    uint16_t idx_reg = payload_hi & 0xFF;
    uint16_t rel = (payload_hi >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(node_reg);
    VALIDATE_REG(idx_reg);

    uint64_t node = vm_state->registers[node_reg];
    uint64_t idx = vm_state->registers[idx_reg];
    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (node >= slot.node_count) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    uint32_t start = slot.offsets_ptr[node];
    uint32_t end = slot.offsets_ptr[node + 1];
    uint32_t deg = end - start;
    if (idx >= deg) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    vm_state->registers[dst] = slot.targets_ptr[start + idx];
    vm_state->register_types[dst] = TYPE_NODE_ID;

    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_MUL_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    size_t size = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR);

    if (inst.flags == 0) {
        float scalar = reinterpret_cast<const float&>(inst.payload);
        if (is_double) {
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
            for (size_t i = 0; i < size; ++i) vec[i] *= scalar;
        } else {
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            impulse_simd_vector_scale_f32(vec, scalar, size);
        }
    } else {
        uint8_t src_reg = inst.payload & 0xFF;
        uint8_t attr_id = (inst.payload >> 8) & 0xFF;
        uint8_t rel_id = (inst.payload >> 16) & 0xFF;
        VALIDATE_REG(src_reg);

        if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        uint8_t base_type = attr.type_code & 0x7F;

        if (is_double) {
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
#if defined(_OPENMP)
            #pragma omp parallel for schedule(static)
#endif
            for (size_t i = 0; i < size; ++i) {
                double val = 1.0;
                if (base_type == 0x08) val = static_cast<double>(static_cast<const float*>(attr.data_ptr)[i]);
                else if (base_type == 0x09) val = static_cast<const double*>(attr.data_ptr)[i];
                else if (base_type == 0x03) val = static_cast<double>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                else val = static_cast<double>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                vec[i] *= val;
            }
        } else {
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            if (base_type == 0x08) {
                impulse_simd_vector_mul_f32(vec, static_cast<const float*>(attr.data_ptr), size);
            } else {
#if defined(_OPENMP)
                #pragma omp parallel for schedule(static)
#endif
                for (size_t i = 0; i < size; ++i) {
                    float val = 1.0f;
                    if (base_type == 0x09) val = static_cast<float>(static_cast<const double*>(attr.data_ptr)[i]);
                    else if (base_type == 0x03) val = static_cast<float>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                    else val = static_cast<float>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                    vec[i] *= val;
                }
            }
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_REDUCE_SUM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    size_t size = vm_state->query_context->max_nodes;
    double sum = 0.0;

    if (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR) {
        const double* vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
#if defined(_OPENMP)
        #pragma omp parallel for reduction(+:sum) schedule(static)
#endif
        for (size_t i = 0; i < size; ++i) sum += vec[i];
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(sum);
        vm_state->register_types[dst] = TYPE_DOUBLE;
    } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
        const float* vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
        float fsum = impulse_simd_reduce_sum_f32(vec, size);
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fsum;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fsum;
    } else {
        vm_state->registers[dst] = 0;
        vm_state->register_types[dst] = TYPE_NULL;
    }

    if (sum == 0.0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_REDUCE_SUM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t attr_id = (inst.payload >> 8) & 0xFF;
    uint8_t rel_id = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel_id];

    bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;

    bool has_edge_attr = false;
    BoundAttributeSlot edge_attr{};
    if (attr_id < vm_state->query_context->attribute_slots[rel_id].size()) {
        edge_attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        if (edge_attr.data_ptr) has_edge_attr = true;
    }

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::memset(dst_vec, 0, max_nodes * sizeof(float));

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                if (src_val == 0.0) continue;
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end   = slot.offsets_ptr[u + 1];
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t target_node = slot.targets_ptr[idx];
                    if (target_node < max_nodes) {
                        float weight = 1.0f;
                        if (has_edge_attr) {
                            uint8_t base_type = edge_attr.type_code & 0x7F;
                            if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                            else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                        }
                        dst_vec[target_node] += static_cast<float>(src_val * weight);
                    }
                }
            }
        } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
            const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                float src_val = src_vec[u];
                if (src_val == 0.0f) continue;
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end   = slot.offsets_ptr[u + 1];
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t target_node = slot.targets_ptr[idx];
                    if (target_node < max_nodes) {
                        float weight = 1.0f;
                        if (has_edge_attr) {
                            uint8_t base_type = edge_attr.type_code & 0x7F;
                            if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                            else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                        }
                        dst_vec[target_node] += src_val * weight;
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

    bool is_empty = true;
    for (size_t i = 0; i < max_nodes; ++i) {
        if (dst_vec[i] != 0.0f) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t reduce_op = (inst.payload >> 8) & 0xFF;
    uint8_t rel_id = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel_id];

    bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::fill(dst_vec, dst_vec + max_nodes, 0.0f);

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end   = slot.offsets_ptr[u + 1];
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t target_node = slot.targets_ptr[idx];
                    if (target_node < max_nodes) {
                        float val = static_cast<float>(src_val);
                        if (reduce_op == 0) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == 1) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
            const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                float src_val = src_vec[u];
                uint32_t start = slot.offsets_ptr[u];
                uint32_t end   = slot.offsets_ptr[u + 1];
                for (uint32_t idx = start; idx < end; ++idx) {
                    uint32_t target_node = slot.targets_ptr[idx];
                    if (target_node < max_nodes) {
                        float val = src_val;
                        if (reduce_op == 0) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == 1) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

    bool is_empty = true;
    for (size_t i = 0; i < max_nodes; ++i) {
        if (dst_vec[i] != 0.0f) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

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

op_CALL: {
    const auto& inst = bytecode[vm_state->pc];
    uint32_t func_offset = inst.payload;
    if (vm_state->call_stack_depth >= 8) return IMPULSE_VM_ERR_STACK_OVERFLOW;
    vm_state->call_stack[vm_state->call_stack_depth++] = vm_state->pc + 1;
    vm_state->pc = func_offset;
    DISPATCH();
}

op_RET: {
    if (vm_state->call_stack_depth == 0) return IMPULSE_VM_ERR_STACK_UNDERFLOW;
    vm_state->pc = vm_state->call_stack[--vm_state->call_stack_depth];
    DISPATCH();
}

op_MAP_KEYS_TO_DENSE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_id = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    const impulse_vm_input_keys* input_keys = reinterpret_cast<const impulse_vm_input_keys*>(input_param);
    if (input_keys && input_keys->keys && input_keys->count > 0) {
        uint8_t base_type = attr->type_code & 0x7F;
        size_t node_count = vm_state->query_context->max_nodes;

        for (size_t k = 0; k < input_keys->count; ++k) {
            const char* target_key = input_keys->keys[k];
            if (!target_key) continue;

            for (size_t u = 0; u < node_count; ++u) {
                bool is_match = false;
                if (base_type == 0x0B) {
                    const char* str_ptr = nullptr;
                    size_t str_len = 0;
                    if (!attr->offsets_ptr) {
                        str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                        str_len = attr->dimension;
                    } else {
                        const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                        str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
                        str_len = offsets[u + 1] - offsets[u];
                    }
                    if (str_ptr) {
                        size_t target_len = std::strlen(target_key);
                        size_t actual_len = 0;
                        while (actual_len < str_len && str_ptr[actual_len] != '\0') {
                            actual_len++;
                        }
                        if (actual_len == target_len && std::strncmp(str_ptr, target_key, actual_len) == 0) {
                            is_match = true;
                        }
                    }
                } else if (base_type == 0x03 || base_type == 0x04) {
                    int64_t val = 0;
                    if (base_type == 0x03) val = static_cast<const int32_t*>(attr->data_ptr)[u];
                    else val = static_cast<const int64_t*>(attr->data_ptr)[u];
                    
                    char* endptr = nullptr;
                    int64_t target_val = std::strtoll(target_key, &endptr, 10);
                    if (endptr != target_key && val == target_val) {
                        is_match = true;
                    }
                }

                if (is_match) {
                    bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    break;
                }
            }
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_MAP_DENSE_TO_KEYS: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint16_t domain_id = (inst.payload >> 8) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& svec = vm_state->query_context->string_vectors[h_dst];
    svec.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint8_t base_type = attr->type_code & 0x7F;
    size_t max_nodes = vm_state->query_context->max_nodes;

    auto add_key = [&](uint64_t u) {
        if (base_type == 0x0B) {
            const char* str_ptr = nullptr;
            if (!attr->offsets_ptr) {
                str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
            } else {
                const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
            }
            if (str_ptr) {
                svec.push_back(str_ptr);
            }
        }
    };

    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_src.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (u < max_nodes) add_key(u);
                }
            }
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (u < max_nodes) add_key(u);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;

    if (svec.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_COLLECT_VALUE_MAP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t nodes_reg = inst.payload & 0xFF;
    uint8_t vals_reg = (inst.payload >> 8) & 0xFF;
    uint16_t domain_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(nodes_reg);
    VALIDATE_REG(vals_reg);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    int h_dst = acquire_value_map(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& vmap = vm_state->query_context->value_maps[h_dst];
    vmap.keys.clear();
    vmap.values.clear();

    bool nodes_is_bitset = (vm_state->register_types[nodes_reg] == TYPE_BITSET_HANDLE);
    bool vals_is_double = (vm_state->register_types[vals_reg] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;
    uint8_t base_type = attr->type_code & 0x7F;

    auto add_entry = [&](uint64_t u) {
        const char* str_ptr = nullptr;
        if (base_type == 0x0B) {
            if (!attr->offsets_ptr) {
                str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
            } else {
                const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
            }
        }
        
        float val = 0.0f;
        if (vals_is_double) {
            val = static_cast<float>(vm_state->query_context->double_vectors[vm_state->registers[vals_reg]][u]);
        } else if (vm_state->register_types[vals_reg] == TYPE_FLOAT_VECTOR) {
            val = vm_state->query_context->float_vectors[vm_state->registers[vals_reg]][u];
        }

        if (str_ptr) {
            vmap.keys.push_back(str_ptr);
            vmap.values.push_back(val);
        }
    };

    if (nodes_is_bitset) {
        int h_nodes = static_cast<int>(vm_state->registers[nodes_reg]);
        const auto& bs_nodes = vm_state->query_context->bitsets[h_nodes];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_nodes.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (u < max_nodes) add_entry(u);
                }
            }
        }
    } else {
        uint64_t u = vm_state->registers[nodes_reg];
        if (u < max_nodes) add_entry(u);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_VALUE_MAP;

    if (vmap.keys.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
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
            case OP_CMP: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                if (vm_state->registers[dst] == vm_state->registers[src]) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }

                vm_state->pc++;
                break;
            }
            case OP_ADD: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                vm_state->registers[dst] += vm_state->registers[src];
                if (vm_state->registers[dst] == 0) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }
                vm_state->pc++;
                break;
            }
            case OP_SUB: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                vm_state->registers[dst] -= vm_state->registers[src];
                if (vm_state->registers[dst] == 0) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }
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
                        size_t frontier_size = 0;
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            frontier_size += std::popcount(bs_src.words[i]);
                        }

                        bool use_parallel = (vm_state->query_context->max_threads > 1 && frontier_size > 15000);
                        if (use_parallel) {
#if defined(_OPENMP)
                            int num_threads = vm_state->query_context->max_threads;
                            auto* ctx = vm_state->query_context;
                            size_t max_nodes = ctx->max_nodes;
                            size_t words = ctx->words_per_bitset;

                            #pragma omp parallel for schedule(static) num_threads(num_threads)
                            for (int t = 0; t < num_threads; ++t) {
                                ctx->private_bitsets[t].clear();
                            }

                            #pragma omp parallel for schedule(dynamic, 1024) num_threads(num_threads)
                            for (size_t i = 0; i < words; ++i) {
                                uint64_t w = bs_src.words[i];
                                if (w == 0) continue;
                                int tid = omp_get_thread_num();
                                auto& private_bs = ctx->private_bitsets[tid];
                                for (int b = 0; b < 64; ++b) {
                                    if (w & (1ULL << b)) {
                                        uint64_t u = i * 64 + b;
                                        if (u < slot.node_count) {
                                            uint32_t start = slot.offsets_ptr[u];
                                            uint32_t end   = slot.offsets_ptr[u + 1];
                                            for (uint32_t idx = start; idx < end; ++idx) {
                                                bitset_add(private_bs, slot.targets_ptr[idx], max_nodes);
                                            }
                                        }
                                    }
                                }
                            }

                            if (accum) {
                                #pragma omp parallel for schedule(static) num_threads(num_threads)
                                for (size_t i = 0; i < words; ++i) {
                                    uint64_t merged = bs_dst.words[i];
                                    for (int t = 0; t < num_threads; ++t) {
                                        merged |= ctx->private_bitsets[t].words[i];
                                    }
                                    bs_dst.words[i] = merged;
                                }
                            } else {
                                #pragma omp parallel for schedule(static) num_threads(num_threads)
                                for (size_t i = 0; i < words; ++i) {
                                    uint64_t merged = 0;
                                    for (int t = 0; t < num_threads; ++t) {
                                        merged |= ctx->private_bitsets[t].words[i];
                                    }
                                    bs_dst.words[i] = merged;
                                }
                            }
#endif
                        } else {
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
            case OP_CSC_WALK: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                uint16_t unv = (inst.payload >> 16) & 0xFF;
                uint16_t rel = (inst.payload >> 24) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                VALIDATE_REG(unv);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];

                if (vm_state->register_types[src] != TYPE_BITSET_HANDLE ||
                    vm_state->register_types[unv] != TYPE_BITSET_HANDLE) {
                    return IMPULSE_VM_ERR_INVALID_REGISTER;
                }

                int h_src = static_cast<int>(vm_state->registers[src]);
                int h_unv = static_cast<int>(vm_state->registers[unv]);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                    if (dst == src || dst == unv) {
                        h_dst = acquire_bitset(vm_state->query_context);
                        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    vm_state->query_context->bitsets[h_dst].clear();
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->query_context->bitsets[h_dst].clear();
                }

                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    const auto& bs_unv = vm_state->query_context->bitsets[h_unv];

#if defined(_OPENMP)
                    #pragma omp parallel for schedule(dynamic, 1024)
#endif
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w_unv = bs_unv.words[i];
                        if (w_unv == 0) continue;

                        uint64_t w_dst = 0;
                        for (int b = 0; b < 64; ++b) {
                            if (w_unv & (1ULL << b)) {
                                uint64_t v = i * 64 + b;
                                if (v < slot.node_count) {
                                    uint32_t start = slot.csc_offsets_ptr[v];
                                    uint32_t end   = slot.csc_offsets_ptr[v + 1];
                                    for (uint32_t idx = start; idx < end; ++idx) {
                                        uint64_t u = slot.csc_targets_ptr[idx];
                                        if (bitset_test(bs_src, u, vm_state->query_context->max_nodes)) {
                                            w_dst |= (1ULL << b);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        bs_dst.words[i] = w_dst;
                    }
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
            case OP_NODE_FILTER: {
                uint16_t dst = inst.dst_reg;
                uint8_t src = inst.payload & 0xFF;
                uint8_t val_reg = (inst.payload >> 8) & 0xFF;
                uint8_t attr_id = (inst.payload >> 16) & 0xFF;
                uint8_t rel_id = (inst.payload >> 24) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                VALIDATE_REG(val_reg);

                if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
                if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                int h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                bs_dst.clear();

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                uint64_t val = vm_state->registers[val_reg];

                auto eval_match = [&](uint64_t u) -> bool {
                    uint8_t base_type = attr.type_code & 0x7F;
                    if (base_type == 0x01 || base_type == 0x02) {
                        const int64_t* data = static_cast<const int64_t*>(attr.data_ptr);
                        return data[u] == static_cast<int64_t>(val);
                    } else if (base_type == 0x03) {
                        const int32_t* data = static_cast<const int32_t*>(attr.data_ptr);
                        return data[u] == static_cast<int32_t>(val);
                    } else if (base_type == 0x08) {
                        const float* data = static_cast<const float*>(attr.data_ptr);
                        float comp = reinterpret_cast<const float&>(val);
                        return data[u] == comp;
                    } else if (base_type == 0x09) {
                        const double* data = static_cast<const double*>(attr.data_ptr);
                        double comp = reinterpret_cast<const double&>(val);
                        return data[u] == comp;
                    }
                    return false;
                };

                if (src_is_bitset) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
#if defined(_OPENMP)
                    #pragma omp parallel for schedule(dynamic, 1024)
#endif
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w = bs_src.words[i];
                        if (w == 0) continue;
                        uint64_t w_dst = 0;
                        for (int b = 0; b < 64; ++b) {
                            if (w & (1ULL << b)) {
                                uint64_t u = i * 64 + b;
                                if (eval_match(u)) {
                                    w_dst |= (1ULL << b);
                                }
                            }
                        }
                        bs_dst.words[i] = w_dst;
                    }
                } else {
                    uint64_t u = vm_state->registers[src];
                    if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) { is_empty = false; break; }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_NODE_FILTER_STR_PREFIX: {
                uint16_t dst = inst.dst_reg;
                uint8_t src = inst.payload & 0xFF;
                uint8_t val_reg = (inst.payload >> 8) & 0xFF;
                uint8_t attr_id = (inst.payload >> 16) & 0xFF;
                uint8_t rel_id = (inst.payload >> 24) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);
                VALIDATE_REG(val_reg);

                if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
                if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);
                if (!prefix) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                size_t prefix_len = std::strlen(prefix);

                int h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                bs_dst.clear();

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);

                auto eval_match = [&](uint64_t u) -> bool {
                    const char* str_ptr = nullptr;
                    size_t str_len = 0;
                    if (!attr.offsets_ptr) {
                        str_ptr = static_cast<const char*>(attr.data_ptr) + u * attr.dimension;
                        str_len = attr.dimension;
                    } else {
                        const uint32_t* offsets = static_cast<const uint32_t*>(attr.offsets_ptr);
                        str_ptr = static_cast<const char*>(attr.data_ptr) + offsets[u];
                        str_len = offsets[u + 1] - offsets[u];
                    }
                    if (!str_ptr) return false;
                    for (size_t idx = 0; idx < prefix_len; ++idx) {
                        if (idx >= str_len || str_ptr[idx] != prefix[idx]) return false;
                    }
                    return true;
                };

                if (src_is_bitset) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
#if defined(_OPENMP)
                    #pragma omp parallel for schedule(dynamic, 1024)
#endif
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w = bs_src.words[i];
                        if (w == 0) continue;
                        uint64_t w_dst = 0;
                        for (int b = 0; b < 64; ++b) {
                            if (w & (1ULL << b)) {
                                uint64_t u = i * 64 + b;
                                if (eval_match(u)) {
                                    w_dst |= (1ULL << b);
                                }
                            }
                        }
                        bs_dst.words[i] = w_dst;
                    }
                } else {
                    uint64_t u = vm_state->registers[src];
                    if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) { is_empty = false; break; }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_VECTOR_DIV: {
                uint16_t dst = inst.dst_reg;
                uint16_t num = inst.payload & 0xFFFF;
                uint16_t denom = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(num);
                VALIDATE_REG(denom);

                size_t size = vm_state->query_context->max_nodes;
                bool num_is_double = (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR);
                bool denom_is_double = (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR);

                if (num_is_double || denom_is_double) {
                    int h_dst = acquire_double_vector(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    double* dst_vec = vm_state->query_context->double_vectors[h_dst].data();

                    const double* num_vec = nullptr;
                    std::vector<double> temp_num;
                    if (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR) {
                        num_vec = vm_state->query_context->double_vectors[vm_state->registers[num]].data();
                    } else {
                        temp_num.resize(size);
                        if (vm_state->register_types[num] == TYPE_FLOAT_VECTOR) {
                            const float* fvec = vm_state->query_context->float_vectors[vm_state->registers[num]].data();
                            for (size_t i = 0; i < size; ++i) temp_num[i] = fvec[i];
                        } else {
                            double val = 0.0;
                            if (vm_state->register_types[num] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[num]);
                            else if (vm_state->register_types[num] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[num]);
                            else val = static_cast<double>(vm_state->registers[num]);
                            std::fill(temp_num.begin(), temp_num.end(), val);
                        }
                        num_vec = temp_num.data();
                    }

                    const double* denom_vec = nullptr;
                    std::vector<double> temp_denom;
                    if (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR) {
                        denom_vec = vm_state->query_context->double_vectors[vm_state->registers[denom]].data();
                    } else {
                        temp_denom.resize(size);
                        if (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR) {
                            const float* fvec = vm_state->query_context->float_vectors[vm_state->registers[denom]].data();
                            for (size_t i = 0; i < size; ++i) temp_denom[i] = fvec[i];
                        } else {
                            double val = 0.0;
                            if (vm_state->register_types[denom] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[denom]);
                            else if (vm_state->register_types[denom] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[denom]);
                            else val = static_cast<double>(vm_state->registers[denom]);
                            std::fill(temp_denom.begin(), temp_denom.end(), val);
                        }
                        denom_vec = temp_denom.data();
                    }

#if defined(_OPENMP)
                    #pragma omp parallel for schedule(static)
#endif
                    for (size_t i = 0; i < size; ++i) {
                        dst_vec[i] = (denom_vec[i] != 0.0) ? (num_vec[i] / denom_vec[i]) : 0.0;
                    }

                    vm_state->registers[dst] = h_dst;
                    vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
                } else {
                    int h_dst = acquire_float_vector(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

                    const float* num_vec = nullptr;
                    std::vector<float> temp_num;
                    if (vm_state->register_types[num] == TYPE_FLOAT_VECTOR) {
                        num_vec = vm_state->query_context->float_vectors[vm_state->registers[num]].data();
                    } else {
                        temp_num.resize(size);
                        float val = 0.0f;
                        if (vm_state->register_types[num] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[num]);
                        else val = static_cast<float>(vm_state->registers[num]);
                        std::fill(temp_num.begin(), temp_num.end(), val);
                        num_vec = temp_num.data();
                    }

                    const float* denom_vec = nullptr;
                    std::vector<float> temp_denom;
                    if (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR) {
                        denom_vec = vm_state->query_context->float_vectors[vm_state->registers[denom]].data();
                    } else {
                        temp_denom.resize(size);
                        float val = 0.0f;
                        if (vm_state->register_types[denom] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[denom]);
                        else val = static_cast<float>(vm_state->registers[denom]);
                        std::fill(temp_denom.begin(), temp_denom.end(), val);
                        denom_vec = temp_denom.data();
                    }

#if defined(_OPENMP)
                    #pragma omp parallel for schedule(static)
#endif
                    for (size_t i = 0; i < size; ++i) {
                        dst_vec[i] = (denom_vec[i] != 0.0f) ? (num_vec[i] / denom_vec[i]) : 0.0f;
                    }

                    vm_state->registers[dst] = h_dst;
                    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                }

                vm_state->pc++;
                break;
            }
            case OP_VEC_GET: {
                uint16_t dst = inst.dst_reg;
                uint16_t src_vec = inst.payload & 0xFFFF;
                uint16_t idx_reg = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src_vec);
                VALIDATE_REG(idx_reg);

                uint64_t idx = vm_state->registers[idx_reg];
                if (vm_state->register_types[src_vec] == TYPE_UINT64_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[src_vec]);
                    if (handle >= 4 || !vm_state->query_context->node_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    vm_state->registers[dst] = vm_state->query_context->node_vectors[handle][idx];
                    vm_state->register_types[dst] = TYPE_INT64;
                } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[src_vec]);
                    if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    float val = vm_state->query_context->float_vectors[handle][idx];
                    vm_state->registers[dst] = 0;
                    reinterpret_cast<float&>(vm_state->registers[dst]) = val;
                    vm_state->register_types[dst] = TYPE_FLOAT;
                } else if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[src_vec]);
                    if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    double val = vm_state->query_context->double_vectors[handle][idx];
                    vm_state->registers[dst] = reinterpret_cast<uint64_t&>(val);
                    vm_state->register_types[dst] = TYPE_DOUBLE;
                } else {
                    return IMPULSE_VM_ERR_INVALID_REGISTER;
                }

                vm_state->pc++;
                break;
            }
            case OP_VEC_SET: {
                uint16_t dst_vec = inst.dst_reg;
                uint16_t val_reg = inst.payload & 0xFFFF;
                uint16_t idx_reg = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst_vec);
                VALIDATE_REG(val_reg);
                VALIDATE_REG(idx_reg);

                uint64_t idx = vm_state->registers[idx_reg];
                if (vm_state->register_types[dst_vec] == TYPE_UINT64_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[dst_vec]);
                    if (handle >= 4 || !vm_state->query_context->node_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    vm_state->query_context->node_vectors[handle][idx] = vm_state->registers[val_reg];
                } else if (vm_state->register_types[dst_vec] == TYPE_FLOAT_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[dst_vec]);
                    if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    float val = 0.0f;
                    if (vm_state->register_types[val_reg] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[val_reg]);
                    else val = static_cast<float>(vm_state->registers[val_reg]);
                    vm_state->query_context->float_vectors[handle][idx] = val;
                } else if (vm_state->register_types[dst_vec] == TYPE_DOUBLE_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[dst_vec]);
                    if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle] || idx >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    double val = 0.0;
                    if (vm_state->register_types[val_reg] == TYPE_DOUBLE) val = reinterpret_cast<const double&>(vm_state->registers[val_reg]);
                    else if (vm_state->register_types[val_reg] == TYPE_FLOAT) val = reinterpret_cast<const float&>(vm_state->registers[val_reg]);
                    else val = static_cast<double>(vm_state->registers[val_reg]);
                    vm_state->query_context->double_vectors[handle][idx] = val;
                } else {
                    return IMPULSE_VM_ERR_INVALID_REGISTER;
                }

                vm_state->pc++;
                break;
            }
            case OP_VEC_SEQUENCE: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);

                if (vm_state->register_types[dst] != TYPE_UINT64_VECTOR) {
                    int h_dst = acquire_node_vector(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->registers[dst] = h_dst;
                    vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
                }

                int handle = static_cast<int>(vm_state->registers[dst]);
                size_t size = vm_state->query_context->max_nodes;
                uint64_t* data = vm_state->query_context->node_vectors[handle].data();
#if defined(_OPENMP)
                #pragma omp parallel for schedule(static)
#endif
                for (size_t i = 0; i < size; ++i) {
                    data[i] = i;
                }

                vm_state->pc++;
                break;
            }
            case OP_CSR_GET_NBR: {
                uint16_t dst = inst.dst_reg;
                uint16_t node_reg = inst.payload & 0xFFFF;
                uint32_t payload_hi = (inst.payload >> 16) & 0xFFFF;
                uint16_t idx_reg = payload_hi & 0xFF;
                uint16_t rel = (payload_hi >> 8) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(node_reg);
                VALIDATE_REG(idx_reg);

                uint64_t node = vm_state->registers[node_reg];
                uint64_t idx = vm_state->registers[idx_reg];
                if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                const auto& slot = vm_state->query_context->slots[rel];
                if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

                if (node >= slot.node_count) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                uint32_t start = slot.offsets_ptr[node];
                uint32_t end = slot.offsets_ptr[node + 1];
                uint32_t deg = end - start;
                if (idx >= deg) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                vm_state->registers[dst] = slot.targets_ptr[start + idx];
                vm_state->register_types[dst] = TYPE_NODE_ID;

                vm_state->pc++;
                break;
            }
            case OP_CC_AFFOREST: {
                uint16_t dst = inst.dst_reg;
                uint16_t rel = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];
                if (!slot.offsets_ptr || !slot.targets_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }

                if (vm_state->register_types[dst] != TYPE_UINT64_VECTOR) {
                    int h_dst = acquire_node_vector(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->registers[dst] = h_dst;
                    vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
                }

                int handle = static_cast<int>(vm_state->registers[dst]);
                auto& comp = vm_state->query_context->node_vectors[handle];
                size_t N = vm_state->query_context->max_nodes;
                comp.resize(N, 0);

#if defined(_OPENMP)
                #pragma omp parallel for schedule(static)
#endif
                for (size_t u = 0; u < N; ++u) {
                    comp[u] = u;
                }

                auto find_root_u64 = [](uint64_t u, std::vector<uint64_t>& comp_vec) -> uint64_t {
                    uint64_t curr = u;
                    while (curr != comp_vec[curr]) {
                        comp_vec[curr] = comp_vec[comp_vec[curr]];
                        curr = comp_vec[curr];
                    }
                    return curr;
                };

                for (size_t r = 0; r < 2; ++r) {
#if defined(_OPENMP)
                    #pragma omp parallel for schedule(static, 2048)
#endif
                    for (size_t u = 0; u < N; ++u) {
                        if (u < slot.node_count) {
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end = slot.offsets_ptr[u + 1];
                            uint32_t deg = end - start;
                            if (r < deg) {
                                uint32_t v = slot.targets_ptr[start + r];
                                if (v < N) {
                                    uint64_t root_u = find_root_u64(u, comp);
                                    uint64_t root_v = find_root_u64(v, comp);
                                    if (root_u != root_v) {
                                        uint64_t high_root = std::min(root_u, root_v);
                                        uint64_t low_root = std::max(root_u, root_v);
                                        comp[low_root] = high_root;
                                    }
                                }
                            }
                        }
                    }
                }

                uint64_t giant_root = 0;
                {
                    std::unordered_map<uint64_t, uint32_t> c_counts;
                    uint32_t max_count = 0;
                    uint32_t sample_n = std::min(static_cast<uint32_t>(N), 100000u);
                    for (uint32_t i = 0; i < sample_n; ++i) {
                        uint64_t u = (i * 9973ULL) % N;
                        uint64_t root = find_root_u64(u, comp);
                        uint32_t cnt = ++c_counts[root];
                        if (cnt > max_count) {
                            max_count = cnt;
                            giant_root = root;
                        }
                    }
                }

#if defined(_OPENMP)
                #pragma omp parallel for schedule(dynamic, 2048)
#endif
                for (size_t u = 0; u < N; ++u) {
                    if (find_root_u64(u, comp) == giant_root) continue;

                    if (u < slot.node_count) {
                        uint32_t start = slot.offsets_ptr[u];
                        uint32_t end = slot.offsets_ptr[u + 1];
                        uint32_t deg = end - start;
                        for (uint32_t i = 0; i < deg; ++i) {
                            uint32_t v = slot.targets_ptr[start + i];
                            if (v < N) {
                                uint64_t root_u = find_root_u64(u, comp);
                                uint64_t root_v = find_root_u64(v, comp);
                                if (root_u != root_v) {
                                    uint64_t high_root = std::min(root_u, root_v);
                                    uint64_t low_root = std::max(root_u, root_v);
                                    comp[low_root] = high_root;
                                    if (high_root == giant_root) break;
                                }
                            }
                        }
                    }
                }

#if defined(_OPENMP)
                #pragma omp parallel for schedule(static, 4096)
#endif
                for (size_t u = 0; u < N; ++u) {
                    comp[u] = find_root_u64(u, comp);
                }

                vm_state->pc++;
                break;
            }
            case OP_VECTOR_MUL_ATTR: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);

                size_t size = vm_state->query_context->max_nodes;
                bool is_double = (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR);

                if (inst.flags == 0) {
                    float scalar = reinterpret_cast<const float&>(inst.payload);
                    if (is_double) {
                        double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
                        for (size_t i = 0; i < size; ++i) vec[i] *= scalar;
                    } else {
                        float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                        impulse_simd_vector_scale_f32(vec, scalar, size);
                    }
                } else {
                    uint8_t src_reg = inst.payload & 0xFF;
                    uint8_t attr_id = (inst.payload >> 8) & 0xFF;
                    uint8_t rel_id = (inst.payload >> 16) & 0xFF;
                    VALIDATE_REG(src_reg);

                    if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
                    if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                    uint8_t base_type = attr.type_code & 0x7F;

                    if (is_double) {
                        double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
#if defined(_OPENMP)
                        #pragma omp parallel for schedule(static)
#endif
                        for (size_t i = 0; i < size; ++i) {
                            double val = 1.0;
                            if (base_type == 0x08) val = static_cast<double>(static_cast<const float*>(attr.data_ptr)[i]);
                            else if (base_type == 0x09) val = static_cast<const double*>(attr.data_ptr)[i];
                            else if (base_type == 0x03) val = static_cast<double>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                            else val = static_cast<double>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                            vec[i] *= val;
                        }
                    } else {
                        float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                        if (base_type == 0x08) {
                            impulse_simd_vector_mul_f32(vec, static_cast<const float*>(attr.data_ptr), size);
                        } else {
#if defined(_OPENMP)
                            #pragma omp parallel for schedule(static)
#endif
                            for (size_t i = 0; i < size; ++i) {
                                float val = 1.0f;
                                if (base_type == 0x09) val = static_cast<float>(static_cast<const double*>(attr.data_ptr)[i]);
                                if (base_type == 0x03) val = static_cast<float>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                                else val = static_cast<float>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                                vec[i] *= val;
                            }
                        }
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_VECTOR_REDUCE_SUM: {
                uint16_t dst = inst.dst_reg;
                uint16_t src = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                size_t size = vm_state->query_context->max_nodes;
                double sum = 0.0;

                if (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR) {
                    const double* vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
#if defined(_OPENMP)
                    #pragma omp parallel for reduction(+:sum) schedule(static)
#endif
                    for (size_t i = 0; i < size; ++i) sum += vec[i];
                    vm_state->registers[dst] = reinterpret_cast<uint64_t&>(sum);
                    vm_state->register_types[dst] = TYPE_DOUBLE;
                } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
                    const float* vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
                    float fsum = impulse_simd_reduce_sum_f32(vec, size);
                    vm_state->registers[dst] = 0;
                    reinterpret_cast<float&>(vm_state->registers[dst]) = fsum;
                    vm_state->register_types[dst] = TYPE_FLOAT;
                    sum = fsum;
                } else {
                    vm_state->registers[dst] = 0;
                    vm_state->register_types[dst] = TYPE_NULL;
                }

                if (sum == 0.0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_CSR_WALK_REDUCE_SUM: {
                uint16_t dst = inst.dst_reg;
                uint8_t src = inst.payload & 0xFF;
                uint8_t attr_id = (inst.payload >> 8) & 0xFF;
                uint8_t rel_id = (inst.payload >> 16) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                const auto& slot = vm_state->query_context->slots[rel_id];

                bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
                size_t max_nodes = vm_state->query_context->max_nodes;

                bool has_edge_attr = false;
                BoundAttributeSlot edge_attr{};
                if (attr_id < vm_state->query_context->attribute_slots[rel_id].size()) {
                    edge_attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
                    if (edge_attr.data_ptr) has_edge_attr = true;
                }

                int h_dst = acquire_float_vector(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
                std::memset(dst_vec, 0, max_nodes * sizeof(float));

                if (slot.offsets_ptr && slot.targets_ptr) {
                    if (src_is_double) {
                        const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
                        for (uint64_t u = 0; u < slot.node_count; ++u) {
                            double src_val = src_vec[u];
                            if (src_val == 0.0) continue;
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                uint32_t target_node = slot.targets_ptr[idx];
                                if (target_node < max_nodes) {
                                    float weight = 1.0f;
                                    if (has_edge_attr) {
                                        uint8_t base_type = edge_attr.type_code & 0x7F;
                                        if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                                        else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                                    }
                                    dst_vec[target_node] += static_cast<float>(src_val * weight);
                                }
                            }
                        }
                    } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
                        const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
                        for (uint64_t u = 0; u < slot.node_count; ++u) {
                            float src_val = src_vec[u];
                            if (src_val == 0.0f) continue;
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                uint32_t target_node = slot.targets_ptr[idx];
                                if (target_node < max_nodes) {
                                    float weight = 1.0f;
                                    if (has_edge_attr) {
                                        uint8_t base_type = edge_attr.type_code & 0x7F;
                                        if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                                        else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                                    }
                                    dst_vec[target_node] += src_val * weight;
                                }
                            }
                        }
                    }
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

                bool is_empty = true;
                for (size_t i = 0; i < max_nodes; ++i) {
                    if (dst_vec[i] != 0.0f) { is_empty = false; break; }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_CSR_WALK_REDUCE: {
                uint16_t dst = inst.dst_reg;
                uint8_t src = inst.payload & 0xFF;
                uint8_t reduce_op = (inst.payload >> 8) & 0xFF;
                uint8_t rel_id = (inst.payload >> 16) & 0xFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                const auto& slot = vm_state->query_context->slots[rel_id];

                bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
                size_t max_nodes = vm_state->query_context->max_nodes;

                int h_dst = acquire_float_vector(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
                std::fill(dst_vec, dst_vec + max_nodes, 0.0f);

                if (slot.offsets_ptr && slot.targets_ptr) {
                    if (src_is_double) {
                        const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
                        for (uint64_t u = 0; u < slot.node_count; ++u) {
                            double src_val = src_vec[u];
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                uint32_t target_node = slot.targets_ptr[idx];
                                if (target_node < max_nodes) {
                                    float val = static_cast<float>(src_val);
                                    if (reduce_op == 0) {
                                        if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                                    } else if (reduce_op == 1) {
                                        if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                                    } else {
                                        dst_vec[target_node] = val;
                                    }
                                }
                            }
                        }
                    } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
                        const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
                        for (uint64_t u = 0; u < slot.node_count; ++u) {
                            float src_val = src_vec[u];
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                uint32_t target_node = slot.targets_ptr[idx];
                                if (target_node < max_nodes) {
                                    float val = src_val;
                                    if (reduce_op == 0) {
                                        if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                                    } else if (reduce_op == 1) {
                                        if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                                    } else {
                                        dst_vec[target_node] = val;
                                    }
                                }
                            }
                        }
                    }
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

                bool is_empty = true;
                for (size_t i = 0; i < max_nodes; ++i) {
                    if (dst_vec[i] != 0.0f) { is_empty = false; break; }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

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
            case OP_CALL: {
                uint32_t func_offset = inst.payload;
                if (vm_state->call_stack_depth >= 8) return IMPULSE_VM_ERR_STACK_OVERFLOW;
                vm_state->call_stack[vm_state->call_stack_depth++] = vm_state->pc + 1;
                vm_state->pc = func_offset;
                break;
            }
            case OP_RET: {
                if (vm_state->call_stack_depth == 0) return IMPULSE_VM_ERR_STACK_UNDERFLOW;
                vm_state->pc = vm_state->call_stack[--vm_state->call_stack_depth];
                break;
            }
            case OP_MAP_KEYS_TO_DENSE: {
                uint16_t dst = inst.dst_reg;
                uint16_t domain_id = inst.payload & 0xFFFF;
                VALIDATE_REG(dst);

                const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
                if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                int h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                bs_dst.clear();

                const impulse_vm_input_keys* input_keys = reinterpret_cast<const impulse_vm_input_keys*>(input_param);
                if (input_keys && input_keys->keys && input_keys->count > 0) {
                    uint8_t base_type = attr->type_code & 0x7F;
                    size_t node_count = vm_state->query_context->max_nodes;

                    for (size_t k = 0; k < input_keys->count; ++k) {
                        const char* target_key = input_keys->keys[k];
                        if (!target_key) continue;

                        for (size_t u = 0; u < node_count; ++u) {
                            bool is_match = false;
                            if (base_type == 0x0B) {
                                const char* str_ptr = nullptr;
                                size_t str_len = 0;
                                if (!attr->offsets_ptr) {
                                    str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                                    str_len = attr->dimension;
                                } else {
                                    const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                                    str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
                                    str_len = offsets[u + 1] - offsets[u];
                                }
                                if (str_ptr) {
                                    size_t target_len = std::strlen(target_key);
                                    size_t actual_len = 0;
                                    while (actual_len < str_len && str_ptr[actual_len] != '\0') {
                                        actual_len++;
                                    }
                                    if (actual_len == target_len && std::strncmp(str_ptr, target_key, actual_len) == 0) {
                                        is_match = true;
                                    }
                                }
                            } else if (base_type == 0x03 || base_type == 0x04) {
                                int64_t val = 0;
                                if (base_type == 0x03) val = static_cast<const int32_t*>(attr->data_ptr)[u];
                                else val = static_cast<const int64_t*>(attr->data_ptr)[u];
                                
                                char* endptr = nullptr;
                                int64_t target_val = std::strtoll(target_key, &endptr, 10);
                                if (endptr != target_key && val == target_val) {
                                    is_match = true;
                                }
                            }

                            if (is_match) {
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                                break;
                            }
                        }
                    }
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

                bool is_empty = true;
                for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                    if (bs_dst.words[i] != 0) { is_empty = false; break; }
                }
                if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_MAP_DENSE_TO_KEYS: {
                uint16_t dst = inst.dst_reg;
                uint8_t src = inst.payload & 0xFF;
                uint16_t domain_id = (inst.payload >> 8) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src);

                const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
                if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                int h_dst = acquire_string_vector(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                auto& svec = vm_state->query_context->string_vectors[h_dst];
                svec.clear();

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                uint8_t base_type = attr->type_code & 0x7F;
                size_t max_nodes = vm_state->query_context->max_nodes;

                auto add_key = [&](uint64_t u) {
                    if (base_type == 0x0B) {
                        const char* str_ptr = nullptr;
                        if (!attr->offsets_ptr) {
                            str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                        } else {
                            const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                            str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
                        }
                        if (str_ptr) {
                            svec.push_back(str_ptr);
                        }
                    }
                };

                if (src_is_bitset) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    const auto& bs_src = vm_state->query_context->bitsets[h_src];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w = bs_src.words[i];
                        if (w == 0) continue;
                        for (int b = 0; b < 64; ++b) {
                            if (w & (1ULL << b)) {
                                uint64_t u = i * 64 + b;
                                if (u < max_nodes) add_key(u);
                            }
                        }
                    }
                } else {
                    uint64_t u = vm_state->registers[src];
                    if (u < max_nodes) add_key(u);
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_STRING_VECTOR;

                if (svec.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_COLLECT_VALUE_MAP: {
                uint16_t dst = inst.dst_reg;
                uint8_t nodes_reg = inst.payload & 0xFF;
                uint8_t vals_reg = (inst.payload >> 8) & 0xFF;
                uint16_t domain_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(nodes_reg);
                VALIDATE_REG(vals_reg);

                const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
                if (!attr || !attr->data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

                int h_dst = acquire_value_map(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                auto& vmap = vm_state->query_context->value_maps[h_dst];
                vmap.keys.clear();
                vmap.values.clear();

                bool nodes_is_bitset = (vm_state->register_types[nodes_reg] == TYPE_BITSET_HANDLE);
                bool vals_is_double = (vm_state->register_types[vals_reg] == TYPE_DOUBLE_VECTOR);
                size_t max_nodes = vm_state->query_context->max_nodes;
                uint8_t base_type = attr->type_code & 0x7F;

                auto add_entry = [&](uint64_t u) {
                    const char* str_ptr = nullptr;
                    if (base_type == 0x0B) {
                        if (!attr->offsets_ptr) {
                            str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                        } else {
                            const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                            str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
                        }
                    }
                    
                    float val = 0.0f;
                    if (vals_is_double) {
                        val = static_cast<float>(vm_state->query_context->double_vectors[vm_state->registers[vals_reg]][u]);
                    } else if (vm_state->register_types[vals_reg] == TYPE_FLOAT_VECTOR) {
                        val = vm_state->query_context->float_vectors[vm_state->registers[vals_reg]][u];
                    }

                    if (str_ptr) {
                        vmap.keys.push_back(str_ptr);
                        vmap.values.push_back(val);
                    }
                };

                if (nodes_is_bitset) {
                    int h_nodes = static_cast<int>(vm_state->registers[nodes_reg]);
                    const auto& bs_nodes = vm_state->query_context->bitsets[h_nodes];
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        uint64_t w = bs_nodes.words[i];
                        if (w == 0) continue;
                        for (int b = 0; b < 64; ++b) {
                            if (w & (1ULL << b)) {
                                uint64_t u = i * 64 + b;
                                if (u < max_nodes) add_entry(u);
                            }
                        }
                    }
                } else {
                    uint64_t u = vm_state->registers[nodes_reg];
                    if (u < max_nodes) add_entry(u);
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_VALUE_MAP;

                if (vmap.keys.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
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

void impulse_vm_context_mock_csc(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csc_offsets,
    const uint32_t* csc_targets
) {
    if (ctx && relation_index < ctx->slots.size()) {
        ctx->slots[relation_index].csc_offsets_ptr = csc_offsets;
        ctx->slots[relation_index].csc_targets_ptr = csc_targets;
    }
}

void impulse_vm_context_bitset_fill(impulse_vm_context_t* ctx, size_t handle, uint64_t count) {
    if (ctx && handle < ctx->bitsets.size()) {
        auto& bs = ctx->bitsets[handle];
        bs.clear();
        size_t full_words = count / 64;
        for (size_t i = 0; i < full_words; ++i) {
            bs.words[i] = ~0ULL;
        }
        size_t rem = count % 64;
        if (rem > 0) {
            bs.words[full_words] = (1ULL << rem) - 1;
        }
    }
}

uint64_t impulse_vm_context_bitset_get_word(const impulse_vm_context_t* ctx, size_t handle, size_t word_idx) {
    if (ctx && handle < ctx->bitsets.size()) {
        const auto& bs = ctx->bitsets[handle];
        if (word_idx < bs.word_count) {
            return bs.words[word_idx];
        }
    }
    return 0;
}

} // extern "C"

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
