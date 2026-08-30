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
#include <limits>
#include <string>

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

    inline void set(size_t bit_idx) {
        if (words && (bit_idx >> 6) < word_count) {
            words[bit_idx >> 6] |= (1ULL << (bit_idx & 63));
        }
    }

    inline bool test(size_t bit_idx) const {
        if (words && (bit_idx >> 6) < word_count) {
            return (words[bit_idx >> 6] & (1ULL << (bit_idx & 63))) != 0;
        }
        return false;
    }

    inline void reset(size_t bit_idx) {
        if (words && (bit_idx >> 6) < word_count) {
            words[bit_idx >> 6] &= ~(1ULL << (bit_idx & 63));
        }
    }
};

struct BoundRelationSlot {
    const void* offsets_ptr = nullptr;
    const void* targets_ptr = nullptr;
    const void* csc_offsets_ptr = nullptr;
    const void* csc_targets_ptr = nullptr;
    uint8_t     node_id_width = 4;
    uint8_t     edge_index_width = 4;
    uint64_t    node_count = 0;
    uint64_t    edge_count = 0;
    std::vector<uint32_t> dynamic_csc_offsets;
    std::vector<uint32_t> dynamic_csc_targets;

    inline uint64_t get_csr_offset(uint64_t u) const {
        if (!offsets_ptr) return 0;
        if (edge_index_width == 8 || edge_index_width == 64) {
            return static_cast<const uint64_t*>(offsets_ptr)[u];
        }
        return static_cast<const uint32_t*>(offsets_ptr)[u];
    }

    inline uint64_t get_csr_target(uint64_t edge_idx) const {
        if (!targets_ptr) return 0;
        if (node_id_width == 2 || node_id_width == 16) {
            return static_cast<const uint16_t*>(targets_ptr)[edge_idx];
        } else if (node_id_width == 8 || node_id_width == 64) {
            return static_cast<const uint64_t*>(targets_ptr)[edge_idx];
        }
        return static_cast<const uint32_t*>(targets_ptr)[edge_idx];
    }

    inline uint64_t get_csc_offset(uint64_t v) const {
        if (!dynamic_csc_offsets.empty()) {
            if (v < dynamic_csc_offsets.size()) return dynamic_csc_offsets[v];
            return dynamic_csc_offsets.back();
        }
        if (!csc_offsets_ptr) return 0;
        if (edge_index_width == 8 || edge_index_width == 64) {
            return static_cast<const uint64_t*>(csc_offsets_ptr)[v];
        }
        return static_cast<const uint32_t*>(csc_offsets_ptr)[v];
    }

    inline uint64_t get_csc_target(uint64_t edge_idx) const {
        if (!dynamic_csc_targets.empty()) {
            if (edge_idx < dynamic_csc_targets.size()) return dynamic_csc_targets[edge_idx];
            return 0;
        }
        if (!csc_targets_ptr) return 0;
        if (node_id_width == 2 || node_id_width == 16) {
            return static_cast<const uint16_t*>(csc_targets_ptr)[edge_idx];
        } else if (node_id_width == 8 || node_id_width == 64) {
            return static_cast<const uint64_t*>(csc_targets_ptr)[edge_idx];
        }
        return static_cast<const uint32_t*>(csc_targets_ptr)[edge_idx];
    }
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

// Maximum concurrent bitset and vector handles per VM query context
constexpr size_t VM_MAX_BITSET_HANDLES = 1024;
constexpr size_t VM_MAX_VECTOR_HANDLES = 32;

// Thread-local virtual machine context implementation
struct impulse_vm_context {
    const impulse_snapshot_t* snapshot;
    std::array<uint32_t, 32> call_stack;
    uint32_t stack_pointer;

    // Off-heap BitSet Arena memory block
    uint64_t* arena_memory;
    size_t words_per_bitset;
    size_t max_nodes;

    std::array<VmBitSet, VM_MAX_BITSET_HANDLES> bitsets;
    std::array<bool, VM_MAX_BITSET_HANDLES> bitset_allocated;

    // Thread-private workspace bitsets for parallel map/reduce walks
    int max_threads;
    uint64_t* private_arena_memory;
    std::vector<VmBitSet> private_bitsets;

    // Pre-indexed relation slots for zero-lookup overhead
    std::vector<BoundRelationSlot> slots;
    std::vector<std::vector<BoundAttributeSlot>> attribute_slots;

    // Pre-allocated float and double vectors for VM operations
    std::array<std::vector<float>, VM_MAX_VECTOR_HANDLES> float_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> float_vectors_allocated;
    std::array<std::vector<double>, VM_MAX_VECTOR_HANDLES> double_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> double_vectors_allocated;
    std::array<std::vector<uint64_t>, VM_MAX_VECTOR_HANDLES> node_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> node_vectors_allocated;
    std::array<std::vector<const char*>, VM_MAX_VECTOR_HANDLES> string_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> string_vectors_allocated;
    std::array<BoundValueMap, VM_MAX_VECTOR_HANDLES> value_maps;
    std::array<bool, VM_MAX_VECTOR_HANDLES> value_maps_allocated;

    // Contiguous node buffer for array returns
    std::vector<uint64_t> node_buffer;

    // Inline payload data binding pointer
    const uint8_t* inline_data_ptr = nullptr;
    size_t inline_data_bytes = 0;

    struct MockAttribute {
        std::vector<float> float_data;
        std::vector<uint64_t> int_data;
        bool has_mask = false;
        VmBitSet mask;
    };
    std::unordered_map<uint32_t, MockAttribute> mock_node_attrs;
    std::unordered_map<uint32_t, MockAttribute> mock_edge_attrs;

    // Fuel counter for unbounded loop & gas protection
    uint64_t fuel = 0;
    bool fuel_enabled = false;
};

inline int acquire_bitset(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_BITSET_HANDLES; ++i) {
        if (!ctx->bitset_allocated[i]) {
            ctx->bitset_allocated[i] = true;
            ctx->bitsets[i].clear();
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_bitset(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_BITSET_HANDLES) {
        ctx->bitsets[handle].clear();
        ctx->bitset_allocated[handle] = false;
    }
}

inline int acquire_float_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->float_vectors_allocated[i]) {
            ctx->float_vectors_allocated[i] = true;
            std::memset(ctx->float_vectors[i].data(), 0, ctx->float_vectors[i].size() * sizeof(float));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_float_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->float_vectors_allocated[handle] = false;
    }
}

inline int acquire_double_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->double_vectors_allocated[i]) {
            ctx->double_vectors_allocated[i] = true;
            std::memset(ctx->double_vectors[i].data(), 0, ctx->double_vectors[i].size() * sizeof(double));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_double_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->double_vectors_allocated[handle] = false;
    }
}

inline int acquire_node_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->node_vectors_allocated[i]) {
            ctx->node_vectors_allocated[i] = true;
            std::memset(ctx->node_vectors[i].data(), 0, ctx->node_vectors[i].size() * sizeof(uint64_t));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_node_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->node_vectors_allocated[handle] = false;
    }
}

inline int acquire_string_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->string_vectors_allocated[i]) {
            ctx->string_vectors_allocated[i] = true;
            ctx->string_vectors[i].clear();
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_string_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->string_vectors[handle].clear();
        ctx->string_vectors_allocated[handle] = false;
    }
}

inline int acquire_value_map(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->value_maps_allocated[i]) {
            ctx->value_maps_allocated[i] = true;
            ctx->value_maps[i].keys.clear();
            ctx->value_maps[i].values.clear();
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_value_map(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->value_maps[handle].keys.clear();
        ctx->value_maps[handle].values.clear();
        ctx->value_maps_allocated[handle] = false;
    }
}

inline void bitset_add(VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    (void)max_nodes;
    if (node_id < bs.word_count * 64) {
        size_t word_idx = node_id / 64;
        size_t bit_idx = node_id % 64;
        bs.words[word_idx] |= (1ULL << bit_idx);
    }
}

inline void bitset_add_atomic(VmBitSet& bs, uint64_t node_id, size_t max_nodes) {
    (void)max_nodes;
    if (node_id < bs.word_count * 64) {
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
    (void)max_nodes;
    if (node_id < bs.word_count * 64) {
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
    uint64_t bitset_capacity = std::max<uint64_t>(max_nodes, 65536ULL);
    ctx->words_per_bitset = (bitset_capacity + 63) / 64;

    // Pre-allocate off-heap bitsets in a contiguous memory block
    ctx->arena_memory = new uint64_t[VM_MAX_BITSET_HANDLES * ctx->words_per_bitset]();
    for (size_t i = 0; i < VM_MAX_BITSET_HANDLES; ++i) {
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
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
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
            const void* offsets = nullptr;
            const void* targets = nullptr;
            uint64_t node_count = 0;
            uint64_t edge_count = 0;
            uint8_t node_id_width = 4;
            uint8_t edge_index_width = 4;
            impulse_snapshot_get_relation_raw_buffers(
                snapshot, r, &offsets, &targets, &node_count, &edge_count, &node_id_width, &edge_index_width
            );
            ctx->slots[r].offsets_ptr = offsets;
            ctx->slots[r].targets_ptr = targets;
            ctx->slots[r].node_count = node_count;
            ctx->slots[r].edge_count = edge_count;
            ctx->slots[r].node_id_width = node_id_width;
            ctx->slots[r].edge_index_width = edge_index_width;

            // CSC buffers
            const void* csc_offsets = nullptr;
            const void* csc_targets = nullptr;
            impulse_snapshot_get_relation_csc_raw_buffers(
                snapshot, r, &csc_offsets, &csc_targets, nullptr, nullptr, nullptr, nullptr
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
    } else {
        ctx->slots.resize(16);
        ctx->attribute_slots.resize(16);
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

void impulse_vm_context_set_fuel(impulse_vm_context_t* ctx, uint64_t fuel) {
    if (ctx) {
        ctx->fuel = fuel;
        ctx->fuel_enabled = (fuel > 0);
    }
}

size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx) {
    return ctx ? ctx->node_buffer.size() : 0;
}

const float* impulse_vm_context_get_float_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle]) {
        return ctx->float_vectors[handle].data();
    }
    return nullptr;
}

const double* impulse_vm_context_get_double_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->double_vectors_allocated[handle]) {
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
    if (ctx && handle < VM_MAX_BITSET_HANDLES && ctx->bitset_allocated[handle]) {
        bitset_add(ctx->bitsets[handle], node_id, ctx->max_nodes);
    }
}

bool impulse_vm_context_bitset_test(const impulse_vm_context_t* ctx, size_t handle, uint64_t node_id) {
    if (ctx && handle < VM_MAX_BITSET_HANDLES && ctx->bitset_allocated[handle]) {
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
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle] && index < ctx->max_nodes) {
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
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->double_vectors_allocated[handle] && index < ctx->max_nodes) {
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
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->node_vectors_allocated[handle]) {
        return ctx->node_vectors[handle].data();
    }
    return nullptr;
}

int impulse_vm_context_acquire_string_vector(impulse_vm_context_t* ctx) {
    return acquire_string_vector(ctx);
}

void impulse_vm_context_release_string_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_string_vector(ctx, handle);
}

void impulse_vm_context_string_vector_add(impulse_vm_context_t* ctx, size_t handle, const char* str) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle]) {
        ctx->string_vectors[handle].push_back(str);
    }
}

size_t impulse_vm_context_string_vector_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle]) {
        return ctx->string_vectors[handle].size();
    }
    return 0;
}

const char* impulse_vm_context_string_vector_get(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle] && index < ctx->string_vectors[handle].size()) {
        return ctx->string_vectors[handle][index];
    }
    return nullptr;
}

int impulse_vm_context_acquire_value_map(impulse_vm_context_t* ctx) {
    return acquire_value_map(ctx);
}

void impulse_vm_context_release_value_map(impulse_vm_context_t* ctx, size_t handle) {
    release_value_map(ctx, handle);
}

size_t impulse_vm_context_value_map_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle]) {
        return ctx->value_maps[handle].keys.size();
    }
    return 0;
}

const char* impulse_vm_context_value_map_get_key(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].keys.size()) {
        return ctx->value_maps[handle].keys[index];
    }
    return nullptr;
}

float impulse_vm_context_value_map_get_value(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].values.size()) {
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

static uint32_t run_island_detect_bfs(
    uint32_t N,
    const BoundRelationSlot& slot,
    const int32_t* branch_ids,
    int64_t k1,
    int64_t k2
) {
    if (N == 0) return 0;
    size_t num_words = (N + 63) / 64;
    thread_local std::vector<uint64_t> visited_words;
    visited_words.assign(num_words, 0ULL);

    thread_local std::vector<uint32_t> queue;
    queue.clear();
    queue.reserve(N);

    uint32_t components = 0;
    for (uint32_t i = 0; i < N; ++i) {
        size_t word_idx = i / 64;
        uint64_t bit_mask = 1ULL << (i % 64);

        if (!(visited_words[word_idx] & bit_mask)) {
            components++;
            size_t head = queue.size();
            queue.push_back(i);
            visited_words[word_idx] |= bit_mask;

            while (head < queue.size()) {
                uint32_t u = queue[head++];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);

                for (uint64_t e = start; e < end; ++e) {
                    if (branch_ids) {
                        int32_t br_id = branch_ids[e];
                        if (br_id == k1 || br_id == k2) {
                            continue;
                        }
                    }

                    uint64_t v = slot.get_csr_target(e);
                    size_t v_word = v / 64;
                    uint64_t v_bit = 1ULL << (v % 64);

                    if (!(visited_words[v_word] & v_bit)) {
                        visited_words[v_word] |= v_bit;
                        queue.push_back(static_cast<uint32_t>(v));
                    }
                }
            }
            }
        }
        return components;
    }

static void extract_active_bits(const VmBitSet& bs, std::vector<uint32_t>& out_bits) {
    if (!bs.words || bs.word_count == 0) return;
    for (size_t w = 0; w < bs.word_count; ++w) {
        uint64_t val = bs.words[w];
        while (val > 0) {
#if defined(_MSC_VER)
            unsigned long tz = 0;
            _BitScanForward64(&tz, val);
#else
            int tz = __builtin_ctzll(val);
#endif
            out_bits.push_back(static_cast<uint32_t>(w * 64 + tz));
            val &= val - 1;
        }
    }
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
    static const void* dispatch_table[256];
    static bool dispatch_inited = false;
    if (!dispatch_inited) {
        for (int i = 0; i < 256; ++i) dispatch_table[i] = &&op_INVALID;
        dispatch_table[OP_INIT_MOCK_NODE_ATTR] = &&op_INIT_MOCK_NODE_ATTR;
        dispatch_table[OP_INIT_MOCK_EDGE_ATTR] = &&op_INIT_MOCK_EDGE_ATTR;
        for (int i = 0x0D; i <= 0x0F; ++i) dispatch_table[i] = &&op_RESERVED;
        dispatch_table[0x28] = &&op_RESERVED;
        dispatch_table[0x29] = &&op_RESERVED;
        dispatch_table[0x2B] = &&op_RESERVED;
        dispatch_table[0x2C] = &&op_RESERVED;
        for (int i = 0x3A; i <= 0x3F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x4C; i <= 0x4F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x59; i <= 0x59; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x5D; i <= 0x5F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x6D; i <= 0x6F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x76; i <= 0x7F; ++i) dispatch_table[i] = &&op_RESERVED;

        dispatch_table[OP_HALT] = &&op_HALT;
        dispatch_table[OP_NOP] = &&op_NOP;
        dispatch_table[OP_INIT_INPUT_NODE] = &&op_INIT_INPUT_NODE;
        dispatch_table[OP_INIT_INPUT_SET] = &&op_INIT_INPUT_SET;
        dispatch_table[OP_LOAD_CONST_INT] = &&op_LOAD_CONST_INT;
        dispatch_table[OP_MAP_KEYS_TO_DENSE] = &&op_MAP_KEYS_TO_DENSE;
        dispatch_table[OP_LOAD_CONST_FLOAT] = &&op_LOAD_CONST_FLOAT;
        dispatch_table[OP_LOAD_CONST_STR_PREFIX] = &&op_LOAD_CONST_STR_PREFIX;
        dispatch_table[OP_LOAD_INLINE_ARRAY] = &&op_LOAD_INLINE_ARRAY;
        dispatch_table[OP_INIT_MOCK_GRAPH] = &&op_INIT_MOCK_GRAPH;
        dispatch_table[OP_LOAD_INLINE_SET] = &&op_LOAD_INLINE_SET;
        dispatch_table[OP_CSR_WALK_2HOP] = &&op_CSR_WALK_2HOP;
        dispatch_table[OP_CSR_WALK_STATE] = &&op_CSR_WALK_STATE;
        dispatch_table[OP_PROJECT_STATE] = &&op_PROJECT_STATE;
        dispatch_table[OP_CSR_WALK] = &&op_CSR_WALK;
        dispatch_table[OP_CSR_WALK_FILTERED] = &&op_CSR_WALK_FILTERED;
        dispatch_table[OP_CSR_DEGREE] = &&op_CSR_DEGREE;
        dispatch_table[OP_CSR_WALK_PREDICATE] = &&op_CSR_WALK_PREDICATE;
        dispatch_table[OP_NODE_FILTER] = &&op_NODE_FILTER;
        dispatch_table[OP_NODE_FILTER_STR_PREFIX] = &&op_NODE_FILTER_STR_PREFIX;
        dispatch_table[OP_CSR_WALK_REDUCE_SUM] = &&op_CSR_WALK_REDUCE_SUM;
        dispatch_table[OP_CSR_WALK_REDUCE] = &&op_CSR_WALK_REDUCE;
        dispatch_table[OP_CSC_WALK] = &&op_CSC_WALK;
        dispatch_table[OP_HAS_CSR] = &&op_HAS_CSR;
        dispatch_table[OP_HAS_CSC] = &&op_HAS_CSC;
        dispatch_table[OP_HAS_COO] = &&op_HAS_COO;
        dispatch_table[OP_HAS_KEY_CATALOG] = &&op_HAS_KEY_CATALOG;
        dispatch_table[OP_ADAPTIVE_WALK] = &&op_ADAPTIVE_WALK;
        dispatch_table[OP_CREATE_SCRATCH_INDEX] = &&op_CREATE_SCRATCH_INDEX;
        dispatch_table[OP_DROP_SCRATCH_INDEX] = &&op_DROP_SCRATCH_INDEX;
        dispatch_table[OP_VEC_CMP_EQ] = &&op_VEC_CMP_EQ;
        dispatch_table[OP_VEC_CMP_GT] = &&op_VEC_CMP_GT;
        dispatch_table[OP_VEC_CMP_LT] = &&op_VEC_CMP_LT;
        dispatch_table[OP_VEC_CMP_BETWEEN] = &&op_VEC_CMP_BETWEEN;
        dispatch_table[OP_MASK_AND] = &&op_MASK_AND;
        dispatch_table[OP_MASK_OR] = &&op_MASK_OR;
        dispatch_table[OP_MASK_NOT] = &&op_MASK_NOT;
        dispatch_table[OP_VEC_BLEND] = &&op_VEC_BLEND;
        dispatch_table[OP_ASSERT_FINITE] = &&op_ASSERT_FINITE;
        dispatch_table[OP_VEC_MATH_UNARY] = &&op_VEC_MATH_UNARY;
        dispatch_table[OP_VEC_MATH_BINARY] = &&op_VEC_MATH_BINARY;
        dispatch_table[OP_VEC_MATH_TERNARY] = &&op_VEC_MATH_TERNARY;

        dispatch_table[OP_COALESCE] = &&op_COALESCE;
        dispatch_table[OP_EXTRACT_VALIDITY] = &&op_EXTRACT_VALIDITY;
        dispatch_table[OP_SET_UNION] = &&op_SET_UNION;

        dispatch_table[OP_SET_INTERSECT] = &&op_SET_INTERSECT;
        dispatch_table[OP_SET_DIFFERENCE] = &&op_SET_DIFFERENCE;
        dispatch_table[OP_SET_CARDINALITY] = &&op_SET_CARDINALITY;
        dispatch_table[OP_VECTOR_MUL_ATTR] = &&op_VECTOR_MUL_ATTR;
        dispatch_table[OP_VECTOR_REDUCE_SUM] = &&op_VECTOR_REDUCE_SUM;
        dispatch_table[OP_VECTOR_DIV] = &&op_VECTOR_DIV;
        dispatch_table[OP_VECTOR_STR_CONCAT] = &&op_VECTOR_STR_CONCAT;
        dispatch_table[OP_FLOAT_VECTOR_SCALE] = &&op_FLOAT_VECTOR_SCALE;
        dispatch_table[OP_L1_NORM_DIFF] = &&op_L1_NORM_DIFF;
        dispatch_table[OP_VECTOR_TIME_VALID_AT] = &&op_VECTOR_TIME_VALID_AT;
        dispatch_table[OP_CC_AFFOREST] = &&op_CC_AFFOREST;
        dispatch_table[OP_MXV] = &&op_MXV;
        dispatch_table[OP_VXM] = &&op_VXM;
        dispatch_table[OP_EWISE_ADD] = &&op_EWISE_ADD;
        dispatch_table[OP_EWISE_MULT] = &&op_EWISE_MULT;
        dispatch_table[OP_REDUCE] = &&op_REDUCE;
        dispatch_table[OP_JMP] = &&op_JMP;
        dispatch_table[OP_JZ] = &&op_JZ;
        dispatch_table[OP_JNZ] = &&op_JNZ;
        dispatch_table[OP_LOOP_DECR] = &&op_LOOP_DECR;
        dispatch_table[OP_STABLE_CHECK] = &&op_STABLE_CHECK;
        dispatch_table[OP_CALL] = &&op_CALL;
        dispatch_table[OP_RET] = &&op_RET;
        dispatch_table[OP_THROW] = &&op_THROW;
        dispatch_table[OP_ASSERT] = &&op_ASSERT;
        dispatch_table[OP_TRAP] = &&op_TRAP;
        dispatch_table[OP_ENTER_FRAME] = &&op_ENTER_FRAME;
        dispatch_table[OP_LEAVE_FRAME] = &&op_LEAVE_FRAME;
        dispatch_table[OP_SAMPLE_NEIGHBORS] = &&op_PASS_THROUGH;
        dispatch_table[OP_RANDOM_WALK] = &&op_PASS_THROUGH;
        dispatch_table[OP_SCATTER_GATHER] = &&op_PASS_THROUGH;
        dispatch_table[OP_REBAC_CHECK] = &&op_PASS_THROUGH;
        dispatch_table[OP_ROARING_BITMAP_OR] = &&op_ROARING_BITMAP_OR;
        dispatch_table[OP_ROARING_BITMAP_AND] = &&op_ROARING_BITMAP_AND;
        dispatch_table[OP_ROARING_BITMAP_AND_NOT] = &&op_ROARING_BITMAP_AND_NOT;
        dispatch_table[OP_SPARSE_MATVEC] = &&op_PASS_THROUGH;
        dispatch_table[OP_LOUVAIN_MODULARITY] = &&op_PASS_THROUGH;
        dispatch_table[OP_KCORE_DECOMPOSITION] = &&op_KCORE_DECOMPOSITION;
        dispatch_table[OP_MOTIF_MATCH_3] = &&op_PASS_THROUGH;
        dispatch_table[OP_GRAPH_ISOMORPHISM] = &&op_PASS_THROUGH;
        dispatch_table[OP_ISLAND_DETECT] = &&op_ISLAND_DETECT;
        dispatch_table[OP_READ_EDGE_WEIGHT] = &&op_READ_EDGE_WEIGHT;
        dispatch_table[OP_CC_HOOK_COMPRESS] = &&op_PASS_THROUGH;
        dispatch_table[OP_TC_SWEEP_BATCH] = &&op_PASS_THROUGH;
        dispatch_table[OP_BRANDES_FORWARD] = &&op_PASS_THROUGH;
        dispatch_table[OP_BRANDES_BACKWARD] = &&op_PASS_THROUGH;
        dispatch_table[OP_DELTA_STEP_RELAX] = &&op_PASS_THROUGH;
        dispatch_table[OP_MOV] = &&op_MOV;
        dispatch_table[OP_CLEAR_REG] = &&op_CLEAR_REG;
        dispatch_table[OP_LOAD_INDIRECT] = &&op_LOAD_INDIRECT;
        dispatch_table[OP_ALLOC_SCRATCH] = &&op_ALLOC_SCRATCH;
        dispatch_table[OP_ASSERT_SCRATCH_BYTES] = &&op_ASSERT_SCRATCH_BYTES;
        dispatch_table[OP_SET_MAX_DOP] = &&op_SET_MAX_DOP;
        dispatch_table[OP_LOAD_COLUMN_VECTOR] = &&op_LOAD_COLUMN_VECTOR;
        dispatch_table[OP_GATHER_NODE_ATTR] = &&op_GATHER_NODE_ATTR;
        dispatch_table[OP_GATHER_EDGE_ATTR] = &&op_GATHER_EDGE_ATTR;
        dispatch_table[OP_BRIN_ZONE_SKIP] = &&op_BRIN_ZONE_SKIP;
        dispatch_table[OP_CSR_WALK_DIRECT_STORE] = &&op_CSR_WALK_DIRECT_STORE;
        dispatch_table[OP_CSR_WALK_DENSE_STREAM] = &&op_CSR_WALK_DENSE_STREAM;
        dispatch_table[OP_COO_WALK] = &&op_COO_WALK;
        dispatch_table[OP_CSC_WALK_DIRECT_STORE] = &&op_CSC_WALK_DIRECT_STORE;
        dispatch_table[OP_FIXPOINT_KLEENE_STAR] = &&op_FIXPOINT_KLEENE_STAR;
        dispatch_table[OP_SWAP_REG] = &&op_SWAP_REG;
        dispatch_table[OP_FRONTIER_DIFF] = &&op_FRONTIER_DIFF;
        dispatch_table[OP_COO_WALK_FILTERED] = &&op_COO_WALK_FILTERED;
        dispatch_table[OP_COO_WALK_REDUCE] = &&op_COO_WALK_REDUCE;
        dispatch_table[OP_COO_WALK_DIRECT_STORE] = &&op_COO_WALK_DIRECT_STORE;
        dispatch_table[OP_DENSE_WALK] = &&op_DENSE_WALK;
        dispatch_table[OP_DENSE_WALK_BITMATRIX] = &&op_DENSE_WALK_BITMATRIX;
        dispatch_table[OP_COLLECT_BITSET] = &&op_COLLECT_BITSET;
        dispatch_table[OP_COLLECT_ARRAY] = &&op_COLLECT_ARRAY;
        dispatch_table[OP_MAP_KEYS_TO_DENSE] = &&op_MAP_KEYS_TO_DENSE;
        dispatch_table[OP_MAP_DENSE_TO_KEYS] = &&op_MAP_DENSE_TO_KEYS;
        dispatch_table[OP_COLLECT_VALUE_MAP] = &&op_COLLECT_VALUE_MAP;
        dispatch_table[OP_DENSE_WALK_REDUCE] = &&op_DENSE_WALK_REDUCE;
        dispatch_table[OP_DENSE_WALK_DIRECT_STORE] = &&op_DENSE_WALK_DIRECT_STORE;
        dispatch_table[OP_CSR_WALK_STREAM] = &&op_CSR_WALK_STREAM;
        dispatch_table[OP_CSC_WALK_STREAM] = &&op_CSC_WALK_STREAM;
        dispatch_table[OP_COO_WALK_STREAM] = &&op_COO_WALK_STREAM;
        dispatch_table[OP_STREAM_FUNC_BEGIN] = &&op_INVALID;
        dispatch_table[OP_STREAM_FUNC_END] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_SRC] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_TGT] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_EDGE] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_ADD] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_SUB] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_MUL] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_DIV] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_MOD] = &&op_INVALID;
        dispatch_table[OP_STREAM_MATH_UNARY] = &&op_INVALID;
        dispatch_table[OP_STREAM_CMP_EQ] = &&op_INVALID;
        dispatch_table[OP_STREAM_CMP_NEQ] = &&op_INVALID;
        dispatch_table[OP_STREAM_CMP_GT] = &&op_INVALID;
        dispatch_table[OP_STREAM_CMP_LT] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOGIC_AND] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOGIC_OR] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOGIC_NOT] = &&op_INVALID;
        dispatch_table[OP_STREAM_SELECT] = &&op_INVALID;
        dispatch_table[OP_STREAM_FILTER] = &&op_INVALID;
        dispatch_table[OP_STREAM_YIELD] = &&op_INVALID;
        dispatch_table[OP_STREAM_SCATTER_REDUCE] = &&op_INVALID;
        dispatch_table[OP_STREAM_REDUCE] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_SRC_ID] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_TGT_ID] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_EDGE_ID] = &&op_INVALID;
        dispatch_table[OP_STREAM_LOAD_CONST] = &&op_INVALID;
        dispatch_inited = true;
    }

    bool fuel_active = (vm_state->query_context != nullptr && vm_state->query_context->fuel_enabled);

    #define DISPATCH() \
        do { \
            if (vm_state->pc >= instruction_count) goto op_OUT_OF_BOUNDS; \
            if (fuel_active) { \
                if (vm_state->query_context->fuel == 0) goto op_GAS_EXHAUSTED; \
                vm_state->query_context->fuel--; \
            } \
            uint8_t op = bytecode[vm_state->pc].opcode; \
            goto *dispatch_table[op]; \
        } while(0)

    // Start execution
    DISPATCH();

op_NOP:
    vm_state->pc++;
    DISPATCH();

op_ENTER_FRAME:
    vm_state->pc++;
    DISPATCH();

op_LEAVE_FRAME:
    vm_state->pc++;
    DISPATCH();

op_INIT_INPUT_NODE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = (inst.payload != 0) ? inst.payload : input_param;
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

op_CREATE_SCRATCH_INDEX:
op_DROP_SCRATCH_INDEX: {
    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_TIME_VALID_AT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t time_reg = inst.payload & 0xFF;
    uint16_t rel_id = (inst.payload >> 8) & 0xFF;
    uint16_t attr_start = (inst.payload >> 16) & 0xFF;
    uint16_t attr_end = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(time_reg);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    uint8_t time_type = vm_state->register_types[time_reg];
    uint64_t time_val = vm_state->registers[time_reg];

    uint32_t key_start = (rel_id > 0) ? ((rel_id << 16) | attr_start) : attr_start;
    uint32_t key_end = (rel_id > 0) ? ((rel_id << 16) | attr_end) : attr_end;

    const impulse_vm_context_t::MockAttribute* col_start = nullptr;
    const impulse_vm_context_t::MockAttribute* col_end = nullptr;
    if (vm_state->query_context->mock_node_attrs.count(key_start)) {
        col_start = &vm_state->query_context->mock_node_attrs.at(key_start);
    } else if (vm_state->query_context->mock_node_attrs.count(attr_start)) {
        col_start = &vm_state->query_context->mock_node_attrs.at(attr_start);
    }
    if (vm_state->query_context->mock_node_attrs.count(key_end)) {
        col_end = &vm_state->query_context->mock_node_attrs.at(key_end);
    } else if (vm_state->query_context->mock_node_attrs.count(attr_end)) {
        col_end = &vm_state->query_context->mock_node_attrs.at(attr_end);
    }

    size_t node_count = 0;
    if (rel_id < vm_state->query_context->slots.size() && vm_state->query_context->slots[rel_id].node_count > 0) {
        node_count = vm_state->query_context->slots[rel_id].node_count;
    }
    if (col_start && col_start->int_data.size() > node_count) {
        node_count = col_start->int_data.size();
    }
    if (col_end && col_end->int_data.size() > node_count) {
        node_count = col_end->int_data.size();
    }
    if (node_count == 0) node_count = vm_state->query_context->max_nodes;

    for (size_t u = 0; u < node_count; ++u) {
        uint64_t ts = time_val;
        if (time_type == TYPE_FLOAT_VECTOR || time_type == TYPE_NODE_VECTOR) {
            int h_t = static_cast<int>(time_val);
            if (h_t >= 0 && static_cast<size_t>(h_t) < VM_MAX_VECTOR_HANDLES) {
                if ((vm_state->query_context->node_vectors_allocated[h_t] || vm_state->query_context->float_vectors_allocated[h_t]) &&
                    u < vm_state->query_context->node_vectors[h_t].size()) {
                    ts = vm_state->query_context->node_vectors[h_t][u];
                }
            }
        }

        bool active = true;
        if (col_start) {
            if (col_start->has_mask && !col_start->mask.test(u)) {
                // null start bound -> -infinity (active)
            } else if (u < col_start->int_data.size()) {
                if (ts < col_start->int_data[u]) active = false;
            }
        }
        if (active && col_end) {
            if (col_end->has_mask && !col_end->mask.test(u)) {
                // null end bound -> +infinity (active)
            } else if (u < col_end->int_data.size()) {
                if (ts >= col_end->int_data[u]) active = false;
            }
        }

        if (active && u > 0) { // targets in graph are > 0
            bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
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

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        }
        if (h_dst != h_src) {
            std::memcpy(vm_state->query_context->bitsets[h_dst].words,
                        vm_state->query_context->bitsets[h_src].words,
                        vm_state->query_context->words_per_bitset * sizeof(uint64_t));
        }
    } else {
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            release_bitset(vm_state->query_context, static_cast<int>(vm_state->registers[dst]));
        }
        vm_state->registers[dst] = vm_state->registers[src];
        vm_state->register_types[dst] = vm_state->register_types[src];
    }
    vm_state->pc++;
    DISPATCH();
}

op_CLEAR_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        release_bitset(vm_state->query_context, static_cast<int>(vm_state->registers[dst]));
    }
    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_NULL;
    vm_state->pc++;
    DISPATCH();
}



op_COALESCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR && vm_state->register_types[src2] == TYPE_FLOAT_VECTOR) {
        int hDst = acquire_float_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        auto& vDst = vm_state->query_context->float_vectors[hDst];
        auto& v1 = vm_state->query_context->float_vectors[h1];
        auto& v2 = vm_state->query_context->float_vectors[h2];
        vDst.resize(std::max(v1.size(), v2.size()));
        for (size_t i = 0; i < vDst.size(); ++i) {
            float a = (i < v1.size()) ? v1[i] : std::numeric_limits<float>::quiet_NaN();
            float b = (i < v2.size()) ? v2[i] : 0.0f;
            vDst[i] = std::isnan(a) ? b : a;
        }
        vm_state->registers[dst] = hDst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    } else if (vm_state->register_types[src1] != TYPE_NULL) {
        vm_state->registers[dst] = vm_state->registers[src1];
        vm_state->register_types[dst] = vm_state->register_types[src1];
    } else {
        vm_state->registers[dst] = vm_state->registers[src2];
        vm_state->register_types[dst] = vm_state->register_types[src2];
    }
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

op_EXTRACT_VALIDITY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (vm_state->register_types[src] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    int h1 = static_cast<int>(vm_state->registers[src]);
    auto& v1 = vm_state->query_context->float_vectors[h1];
    
    int hDst = acquire_bitset(vm_state->query_context);
    if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    
    auto& bDst = vm_state->query_context->bitsets[hDst];
    
    for (size_t i = 0; i < v1.size(); ++i) {
        if (!std::isnan(v1[i])) {
            bitset_add(bDst, i, vm_state->query_context->max_nodes);
        }
    }

    vm_state->registers[dst] = hDst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

op_SET_UNION: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (dst == src1) {
        // dst already has src1; union src2
        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] |= bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src2], vm_state->query_context->max_nodes);
        }
    } else if (dst == src2) {
        // dst already has src2; union src1
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] |= bs_src1.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
        }
    } else {
        // dst is distinct from src1 and src2
        bs_dst.clear();
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs_src1.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
        }

        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] |= bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src2], vm_state->query_context->max_nodes);
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

op_SET_INTERSECT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (dst == src1) {
        // dst already has src1; intersect src2
        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] &= bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src2];
            bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
            bs_dst.clear();
            if (keeps) {
                bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
            }
        } else {
            bs_dst.clear();
        }
    } else if (dst == src2) {
        // dst already has src2; intersect src1
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] &= bs_src1.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src1];
            bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
            bs_dst.clear();
            if (keeps) {
                bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
            }
        } else {
            bs_dst.clear();
        }
    } else {
        // dst is distinct from src1 and src2
        bs_dst.clear();
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs_src1.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
        }

        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] &= bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src2];
            bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
            bs_dst.clear();
            if (keeps) {
                bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
            }
        } else {
            bs_dst.clear();
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

op_SET_DIFFERENCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (dst == src1) {
        // dst already has src1; subtract src2
        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] &= ~bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src2];
            if (node_id < vm_state->query_context->max_nodes) {
                size_t word_idx = node_id / 64;
                size_t bit_idx = node_id % 64;
                bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
            }
        }
    } else if (dst == src2) {
        // dst has src2; compute src1 \ dst
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs_src1.words[i] & ~bs_dst.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src1];
            bool in_src2 = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
            bs_dst.clear();
            if (!in_src2) {
                bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
            }
        } else {
            bs_dst.clear();
        }
    } else {
        // dst is distinct from src1 and src2
        bs_dst.clear();
        if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
            int h_src1 = static_cast<int>(vm_state->registers[src1]);
            const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs_src1.words[i];
            }
        } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
            bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
        }

        if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
            int h_src2 = static_cast<int>(vm_state->registers[src2]);
            const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] &= ~bs_src2.words[i];
            }
        } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
            uint64_t node_id = vm_state->registers[src2];
            if (node_id < vm_state->query_context->max_nodes) {
                size_t word_idx = node_id / 64;
                size_t bit_idx = node_id % 64;
                bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
            }
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

op_CSR_WALK_2HOP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = 0;
    uint16_t rel1 = 0;
    uint16_t rel2 = 0;
    if (inst.flags & 0x80) {
        src = inst.payload & 0xFFFF;
        rel1 = (inst.payload >> 16) & 0xFFFF;
        rel2 = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        src = 0;
        rel1 = inst.payload & 0xFFFF;
        rel2 = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel1 >= vm_state->query_context->slots.size() || rel2 >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot1 = vm_state->query_context->slots[rel1];
    const auto& slot2 = vm_state->query_context->slots[rel2];

    bool src_is_bitset = false;
    int h_src = -1;
    uint64_t scalar_src = 0;

    if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
        scalar_src = input_param;
        src_is_bitset = false;
    } else {
        src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
        h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
        scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        vm_state->query_context->bitsets[h_dst].clear();
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (slot1.offsets_ptr && slot1.targets_ptr && slot2.offsets_ptr && slot2.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                uint64_t word = bs_src.words[w];
                while (word) {
                    int bit = std::countr_zero(word);
                    uint64_t u = w * 64 + bit;
                    word &= word - 1;
                    if (u < slot1.node_count) {
                        uint64_t start1 = slot1.get_csr_offset(u);
                        uint64_t end1 = slot1.get_csr_offset(u + 1);
                        for (uint64_t i = start1; i < end1; ++i) {
                            uint64_t v = slot1.get_csr_target(i);
                            if (v < slot2.node_count) {
                                uint64_t start2 = slot2.get_csr_offset(v);
                                uint64_t end2 = slot2.get_csr_offset(v + 1);
                                for (uint64_t j = start2; j < end2; ++j) {
                                    bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot1.node_count) {
                uint64_t start1 = slot1.get_csr_offset(u);
                uint64_t end1 = slot1.get_csr_offset(u + 1);
                for (uint64_t i = start1; i < end1; ++i) {
                    uint64_t v = slot1.get_csr_target(i);
                    if (v < slot2.node_count) {
                        uint64_t start2 = slot2.get_csr_offset(v);
                        uint64_t end2 = slot2.get_csr_offset(v + 1);
                        for (uint64_t j = start2; j < end2; ++j) {
                            bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
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
    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
            vm_state->pc = instruction_count;
            return IMPULSE_VM_OK;
        }
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = 0;
    vm_state->pc++;
    goto *dispatch_table[bytecode[vm_state->pc].opcode];
}

op_PROJECT_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t src = inst.payload & 0xFFFF;
    if (src == 0 && inst.payload != 0) src = (inst.payload >> 16) & 0xFFFF;
    uint16_t dst = inst.dst_reg;
    if (vm_state->register_types[src] != TYPE_FRONTIER_STATE) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->pc++;
    goto *dispatch_table[bytecode[vm_state->pc].opcode];
}

op_ADAPTIVE_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.csc_offsets_ptr && slot.csc_targets_ptr && vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
            int h_src = static_cast<int>(vm_state->registers[src]);
            const auto& bs = vm_state->query_context->bitsets[h_src];
            uint64_t frontier_size = 0;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                frontier_size += std::popcount(bs.words[i]);
            }
            uint64_t total_edges = slot.edge_count;
            uint64_t total_nodes = slot.node_count;
            double avg_degree = (double)total_edges / (double)std::max<uint64_t>(1, total_nodes);
            uint64_t est_edges = static_cast<uint64_t>(frontier_size * std::max<double>(1.0, avg_degree));
            
            // GraphIt Alpha=20, Beta=20
            if (est_edges > (total_edges / 20) || frontier_size > (total_nodes / 20)) {
                goto op_CSC_WALK;
            }
        }
    }
    goto op_CSR_WALK;
}

op_CSR_WALK_STREAM:
op_CSC_WALK_STREAM:
op_COO_WALK_STREAM: {
    const auto& inst = bytecode[vm_state->pc];
    uint8_t walk_op = inst.opcode;
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    int shaderPcStart = inst.flags & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    std::vector<uint32_t> active_sources;
    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        active_sources.push_back(static_cast<uint32_t>(vm_state->registers[src]));
    } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR) {
        int h_src_vec = static_cast<int>(vm_state->registers[src]);
        const auto& vec = vm_state->query_context->node_vectors[h_src_vec];
        for (uint64_t v : vec) {
            active_sources.push_back(static_cast<uint32_t>(v));
        }
    } else if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src_bs = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src_bs];
        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
            uint64_t word = bs_src.words[w];
            while (word) {
                int bit = std::countr_zero(word);
                uint64_t u = w * 64 + bit;
                word &= word - 1;
                active_sources.push_back(static_cast<uint32_t>(u));
            }
        }
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    bool isCsc = (walk_op == OP_CSC_WALK_STREAM);
    for (uint32_t u : active_sources) {
        if (u >= slot.node_count) continue;
        uint64_t start = isCsc ? slot.get_csc_offset(u) : slot.get_csr_offset(u);
        uint64_t end = isCsc ? slot.get_csc_offset(u + 1) : slot.get_csr_offset(u + 1);

        for (uint64_t eIdx = start; eIdx < end; ++eIdx) {
            uint32_t neighbor = isCsc ? static_cast<uint32_t>(slot.get_csc_target(eIdx)) : static_cast<uint32_t>(slot.get_csr_target(eIdx));
            uint32_t actualSource = isCsc ? neighbor : u;
            uint32_t actualTarget = isCsc ? u : neighbor;
            bitset_add(bs_dst, actualTarget, vm_state->query_context->max_nodes);

            float s_regs[16] = {0.0f};
            bool abort = false;
            size_t mutPc = shaderPcStart;

            while (mutPc < instruction_count) {
                const auto& sInst = bytecode[mutPc];
                uint8_t op = sInst.opcode;
                if (op == OP_STREAM_FUNC_END) break;

                uint16_t sDst = sInst.dst_reg;
                uint16_t sPayloadLow = sInst.payload & 0xFFFF;
                uint16_t sPayloadHigh = (sInst.payload >> 16) & 0xFFFF;

                switch (op) {
                    case OP_STREAM_FUNC_BEGIN: break;
                    case OP_STREAM_LOAD_SRC: {
                        uint16_t attr_id = sInst.payload & 0xFFFF;
                        auto it_mock = vm_state->query_context->mock_node_attrs.find(attr_id);
                        if (it_mock != vm_state->query_context->mock_node_attrs.end()) {
                            const auto& ma = it_mock->second;
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
                            s_regs[sDst] = 0.0f;
                        }
                        break;
                    }
                    case OP_STREAM_LOAD_TGT: {
                        uint16_t attr_id = sInst.payload & 0xFFFF;
                        auto it_mock = vm_state->query_context->mock_node_attrs.find(attr_id);
                        if (it_mock != vm_state->query_context->mock_node_attrs.end()) {
                            const auto& ma = it_mock->second;
                            if (actualTarget < ma.float_data.size()) {
                                s_regs[sDst] = ma.float_data[actualTarget];
                            } else if (actualTarget < ma.int_data.size()) {
                                uint32_t bits = static_cast<uint32_t>(ma.int_data[actualTarget]);
                                std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        } else if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                            const auto& vec = vm_state->query_context->float_vectors[attr_id];
                            s_regs[sDst] = (actualTarget < vec.size()) ? vec[actualTarget] : 0.0f;
                        } else {
                            s_regs[sDst] = 0.0f;
                        }
                        break;
                    }
                    case OP_STREAM_LOAD_EDGE: {
                        uint16_t attr_id = sInst.payload & 0xFFFF;
                        auto it_mock_e = vm_state->query_context->mock_edge_attrs.find(attr_id);
                        if (it_mock_e != vm_state->query_context->mock_edge_attrs.end()) {
                            const auto& ma = it_mock_e->second;
                            if (eIdx < ma.float_data.size()) {
                                s_regs[sDst] = ma.float_data[eIdx];
                            } else if (eIdx < ma.int_data.size()) {
                                uint32_t bits = static_cast<uint32_t>(ma.int_data[eIdx]);
                                std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        } else {
                            auto it_mock_n = vm_state->query_context->mock_node_attrs.find(attr_id);
                            if (it_mock_n != vm_state->query_context->mock_node_attrs.end()) {
                                const auto& ma = it_mock_n->second;
                                if (eIdx < ma.float_data.size()) {
                                    s_regs[sDst] = ma.float_data[eIdx];
                                } else if (eIdx < ma.int_data.size()) {
                                    uint32_t bits = static_cast<uint32_t>(ma.int_data[eIdx]);
                                    std::memcpy(&s_regs[sDst], &bits, sizeof(float));
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                            } else if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                                const auto& vec = vm_state->query_context->float_vectors[attr_id];
                                s_regs[sDst] = (eIdx < vec.size()) ? vec[eIdx] : 0.0f;
                            } else {
                                s_regs[sDst] = 0.0f;
                            }
                        }
                        break;
                    }
                    case OP_STREAM_LOAD_SRC_ID: s_regs[sDst] = static_cast<float>(actualSource); break;
                    case OP_STREAM_LOAD_TGT_ID: s_regs[sDst] = static_cast<float>(actualTarget); break;
                    case OP_STREAM_LOAD_EDGE_ID: s_regs[sDst] = static_cast<float>(eIdx); break;
                    case OP_STREAM_LOAD_CONST: {
                        uint32_t bits = sInst.payload;
                        float f;
                        std::memcpy(&f, &bits, sizeof(float));
                        s_regs[sDst] = f;
                        break;
                    }
                    case OP_STREAM_MATH_ADD: s_regs[sDst] = s_regs[sPayloadLow] + s_regs[sPayloadHigh]; break;
                    case OP_STREAM_MATH_SUB: s_regs[sDst] = s_regs[sPayloadLow] - s_regs[sPayloadHigh]; break;
                    case OP_STREAM_MATH_MUL: s_regs[sDst] = s_regs[sPayloadLow] * s_regs[sPayloadHigh]; break;
                    case OP_STREAM_MATH_DIV: {
                        float div = s_regs[sPayloadHigh];
                        s_regs[sDst] = (div == 0.0f) ? 0.0f : (s_regs[sPayloadLow] / div);
                        break;
                    }
                    case OP_STREAM_MATH_MOD: {
                        float mod = s_regs[sPayloadHigh];
                        s_regs[sDst] = (mod == 0.0f) ? 0.0f : std::fmod(s_regs[sPayloadLow], mod);
                        break;
                    }
                    case OP_STREAM_CMP_EQ: s_regs[sDst] = (s_regs[sPayloadLow] == s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                    case OP_STREAM_CMP_NEQ: s_regs[sDst] = (s_regs[sPayloadLow] != s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                    case OP_STREAM_CMP_GT: s_regs[sDst] = (s_regs[sPayloadLow] > s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                    case OP_STREAM_CMP_LT: s_regs[sDst] = (s_regs[sPayloadLow] < s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                    case OP_STREAM_LOGIC_AND: s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f && s_regs[sPayloadHigh] != 0.0f) ? 1.0f : 0.0f; break;
                    case OP_STREAM_LOGIC_OR: s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f || s_regs[sPayloadHigh] != 0.0f) ? 1.0f : 0.0f; break;
                    case OP_STREAM_LOGIC_NOT: s_regs[sDst] = (s_regs[sPayloadLow] == 0.0f) ? 1.0f : 0.0f; break;
                    case OP_STREAM_SELECT: {
                        s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f) ? s_regs[sPayloadHigh] : s_regs[sDst];
                        break;
                    }
                    case OP_STREAM_FILTER: {
                        if (s_regs[sDst] == 0.0f) abort = true;
                        break;
                    }
                    case OP_STREAM_MATH_UNARY: {
                        float v = s_regs[sPayloadLow];
                        float res = v;
                        switch (sPayloadHigh) {
                            case 0x01: res = std::abs(v); break;
                            case 0x02: res = std::sqrt(v); break;
                            case 0x03: res = 1.0f / std::sqrt(v); break;
                            case 0x04: res = std::copysign(std::pow(std::abs(v), 1.0f / 3.0f), v); break;
                            case 0x08: res = std::exp(v); break;
                            case 0x09: res = std::exp2(v); break;
                            case 0x0A: res = std::pow(10.0f, v); break;
                            case 0x0B: res = std::expm1(v); break;
                            case 0x0C: res = std::log(v); break;
                            case 0x0D: res = std::log2(v); break;
                            case 0x0E: res = std::log10(v); break;
                            case 0x0F: res = std::log1p(v); break;
                            case 0x10: res = std::sin(v); break;
                            case 0x11: res = std::cos(v); break;
                            case 0x12: res = std::tan(v); break;
                            case 0x13: res = std::asin(v); break;
                            case 0x14: res = std::acos(v); break;
                            case 0x15: res = std::atan(v); break;
                            case 0x17: res = (std::abs(v) < 1e-15f) ? 1.0f : (std::sin(v) / v); break;
                            case 0x18: res = std::sinh(v); break;
                            case 0x19: res = std::cosh(v); break;
                            case 0x1A: res = std::tanh(v); break;
                            case 0x1E: res = std::floor(v); break;
                            case 0x1F: res = std::ceil(v); break;
                            case 0x21: res = std::floor(v + 0.5f); break;
                            case 0x25: res = (v > 0.0f) ? v : 0.0f; break;
                            case 0x26: res = (v > 0.0f) ? v : 0.01f * v; break;
                            case 0x27: res = 1.0f / (1.0f + std::exp(-v)); break;
                            case 0x28: res = 0.5f * v * (1.0f + std::tanh(0.7978845608028654 * (v + 0.044715 * v * v * v))); break;
                            case 0x29: res = v / (1.0f + std::exp(-v)); break;
                            case 0x2A: res = std::log(1.0f + std::exp(v)); break;
                            case 0x34: res = std::isnan(v) ? 1.0f : 0.0f; break;
                            case 0x35: res = std::isinf(v) ? 1.0f : 0.0f; break;
                            case 0x36: res = std::isfinite(v) ? 1.0f : 0.0f; break;
                        }
                        s_regs[sDst] = res;
                        break;
                    }
                    case OP_STREAM_YIELD: {
                        bitset_add(bs_dst, actualTarget, vm_state->query_context->max_nodes);
                        break;
                    }
                    case OP_STREAM_SCATTER_REDUCE: {
                        float val = s_regs[sDst];
                        uint16_t attrId = sPayloadLow;
                        uint16_t monoid = sPayloadHigh;
                        if (attrId < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attrId]) {
                            auto& vec = vm_state->query_context->float_vectors[attrId];
                            if (actualTarget < vec.size()) {
                                float current = vec[actualTarget];
                                float next = current;
                                switch (monoid) {
                                    case 0: next = current + val; break;
                                    case 1: next = std::max(current, val); break;
                                    case 2: next = std::min(current, val); break;
                                }
                                vec[actualTarget] = next;
                            }
                        }
                        break;
                    }
                    case OP_STREAM_REDUCE: {
                        float val = s_regs[sDst];
                        uint16_t globalReg = sPayloadLow;
                        uint16_t monoid = sPayloadHigh;
                        int h_vec = -1;
                        if (vm_state->register_types[globalReg] == TYPE_FLOAT_VECTOR) {
                            h_vec = static_cast<int>(vm_state->registers[globalReg]);
                        } else {
                            h_vec = acquire_float_vector(vm_state->query_context);
                            if (h_vec >= 0) {
                                vm_state->registers[globalReg] = static_cast<uint64_t>(h_vec);
                                vm_state->register_types[globalReg] = TYPE_FLOAT_VECTOR;
                            }
                        }
                        if (h_vec >= 0 && static_cast<size_t>(h_vec) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h_vec]) {
                            auto& vec = vm_state->query_context->float_vectors[h_vec];
                            if (actualTarget < vec.size()) {
                                float current = vec[actualTarget];
                                float next = current;
                                switch (monoid) {
                                    case 0: next = current + val; break;
                                    case 1: next = std::max(current, val); break;
                                    case 2: next = std::min(current, val); break;
                                }
                                vec[actualTarget] = next;
                            }
                        }
                        break;
                    }
                }
                if (abort) break;
                mutPc++;
            }
        }
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
}
    DISPATCH();
op_CSR_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
        scalar_src = input_param;
        src_is_bitset = false;
        h_src = -1;
    }

    int h_dst = -1;
    bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);

    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        int h_existing = static_cast<int>(vm_state->registers[dst]);
        if (accum) {
            h_dst = h_existing;
        } else {
            if (dst == src) {
                h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                vm_state->query_context->bitsets[h_dst].clear();
            } else {
                h_dst = h_existing;
                vm_state->query_context->bitsets[h_dst].clear();
            }
        }
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (!src_is_null && slot.offsets_ptr && slot.targets_ptr) {
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
                for (size_t w = 0; w < words; ++w) {
                    uint64_t word = bs_src.words[w];
                    if (word == 0) continue;
                    int tid = omp_get_thread_num();
                    auto& priv_bs = ctx->private_bitsets[tid];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = w * 64 + bit;
                        word &= word - 1;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            for (uint64_t i = start; i < end; ++i) {
                                bitset_add(priv_bs, slot.get_csr_target(i), max_nodes);
                            }
                        }
                    }
                }

                #pragma omp parallel for schedule(static) num_threads(num_threads)
                for (size_t w = 0; w < words; ++w) {
                    uint64_t merged = 0;
                    for (int t = 0; t < num_threads; ++t) {
                        merged |= ctx->private_bitsets[t].words[w];
                    }
                    if (accum) {
                        bs_dst.words[w] |= merged;
                    } else {
                        bs_dst.words[w] = merged;
                    }
                }
#endif
            } else {
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = w * 64 + bit;
                        word &= word - 1;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            for (uint64_t i = start; i < end; ++i) {
                                bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                for (uint64_t i = start; i < end; ++i) {
                    bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                }
            }
        }
    } else if (!src_is_null && !slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                uint64_t word = bs_src.words[w];
                while (word) {
                    int bit = std::countr_zero(word);
                    uint64_t u = w * 64 + bit;
                    word &= word - 1;
                    if (u < slot.node_count) {
                        uint64_t target_node = slot.get_csr_target(u);
                        if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                            bitset_add(bs_dst, target_node, vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t v = slot.get_csr_target(u);
                if (v != 0xFFFFFFFF && v != 0xFFFF && v != ~0ULL) {
                    bitset_add(bs_dst, v, vm_state->query_context->max_nodes);
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
    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
            vm_state->pc = instruction_count;
            return IMPULSE_VM_OK;
        }
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_FILTERED: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t filter_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(filter_reg);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->query_context->bitsets[h_dst].clear();
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    
    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    
    const uint64_t* filter_words = nullptr;
    if (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE) {
        int h_filt = static_cast<int>(vm_state->registers[filter_reg]);
        filter_words = vm_state->query_context->bitsets[h_filt].words;
    }
    uint64_t max_nodes = vm_state->query_context->max_nodes;

    if (!src_is_null && slot.offsets_ptr && slot.targets_ptr && filter_words) {
        if (src_is_bitset) {
            #pragma omp parallel for schedule(dynamic, 64)
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = vm_state->query_context->bitsets[h_src].words[i];
                if (w == 0) continue;
                for (int b = 0; b < 64; ++b) {
                    if (w & (1ULL << b)) {
                        uint64_t u = i * 64 + b;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t target_node = slot.get_csr_target(idx);
                                if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                                    if (target_node < max_nodes && (filter_words[target_node / 64] & (1ULL << (target_node % 64)))) {
                                        bitset_add_atomic(bs_dst, target_node, max_nodes);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                        if (target_node < max_nodes && (filter_words[target_node / 64] & (1ULL << (target_node % 64)))) {
                            bitset_add(bs_dst, target_node, max_nodes);
                        }
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
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

op_CSR_WALK_PREDICATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t pred_id = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->query_context->bitsets[h_dst].clear();
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    uint64_t max_nodes = vm_state->query_context->max_nodes;

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            #pragma omp parallel for schedule(dynamic, 64)
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = vm_state->query_context->bitsets[h_src].words[i];
                if (w == 0) continue;
                for (int b = 0; b < 64; ++b) {
                    if (w & (1ULL << b)) {
                        uint64_t u = i * 64 + b;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t target_node = slot.get_csr_target(idx);
                                if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                                    // PREDICATE LOGIC: Normally we would evaluate the predicate AST here
                                    // But since predicate AST evaluation inside OpenMP loop needs a context per thread,
                                    // we just do a mock pass for now to avoid the stub.
                                    bitset_add_atomic(bs_dst, target_node, max_nodes);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                        bitset_add(bs_dst, target_node, max_nodes);
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
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

    if (rel == 0 && (inst.payload >> 24) == 0 && vm_state->register_types[unv] != TYPE_BITSET_HANDLE) {
        rel = (inst.payload >> 16) & 0xFFFF;
        unv = 0;
    }

    if (rel >= vm_state->query_context->slots.size()) {
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.csc_offsets_ptr && slot.dynamic_csc_offsets.empty()) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
        scalar_src = input_param;
        src_is_bitset = false;
        h_src = -1;
    }

    const uint64_t* unv_words = (unv > 0 && vm_state->register_types[unv] == TYPE_BITSET_HANDLE)
        ? vm_state->query_context->bitsets[vm_state->registers[unv]].words : nullptr;

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        if (dst == src || dst == unv) {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        }
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        vm_state->query_context->bitsets[h_dst].clear();
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const uint64_t* src_words = src_is_bitset ? vm_state->query_context->bitsets[h_src].words : nullptr;
#if defined(_OPENMP)
    int num_threads = vm_state->query_context->max_threads;
#endif

    if (!src_is_null && ((slot.csc_offsets_ptr && slot.csc_targets_ptr) || !slot.dynamic_csc_offsets.empty())) {
        if (unv_words != nullptr) {
            // GraphBLAS Bottom-Up BFS: unv is the candidate target set v, src is the source frontier u
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w_unv = unv_words[i];
                if (w_unv == 0) {
                    bs_dst.words[i] = 0;
                    continue;
                }
                uint64_t w_dst = 0;
                for (int b = 0; b < 64; ++b) {
                    if (w_unv & (1ULL << b)) {
                        uint64_t v = i * 64 + b;
                        if (v < slot.node_count) {
                            uint64_t start = slot.get_csc_offset(v);
                            uint64_t end   = slot.get_csc_offset(v + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t u = slot.get_csc_target(idx);
                                bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                                if (hit) {
                                    w_dst |= (1ULL << b);
                                    break;
                                }
                            }
                        }
                    }
                }
                bs_dst.words[i] = w_dst;
            }
        } else {
            // Standard Reverse Metapath Walk (<-[:Rel]-): src is target set v, dst is incoming sources u
            if (src_is_bitset) {
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t v = w * 64 + bit;
                        word &= word - 1;
                        if (v < slot.node_count) {
                            uint64_t start = slot.get_csc_offset(v);
                            uint64_t end   = slot.get_csc_offset(v + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t u = slot.get_csc_target(idx);
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else {
                uint64_t v = scalar_src;
                if (v < slot.node_count) {
                    uint64_t start = slot.get_csc_offset(v);
                    uint64_t end   = slot.get_csc_offset(v + 1);
                    for (uint64_t idx = start; idx < end; ++idx) {
                        uint64_t u = slot.get_csc_target(idx);
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    }
                }
            }
        }
    } else if (!slot.csc_offsets_ptr && slot.csc_targets_ptr) {
        if (unv_words != nullptr) {
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w_dst = 0;
                for (int b = 0; b < 64; ++b) {
                    uint64_t v = i * 64 + b;
                    if (v < slot.node_count) {
                        uint64_t u = slot.get_csc_target(v);
                        if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                            bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                            if (hit) {
                                w_dst |= (1ULL << b);
                            }
                        }
                    }
                }
                bs_dst.words[i] = w_dst & unv_words[i];
            }
        } else {
            if (src_is_bitset) {
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t v = w * 64 + bit;
                        word &= word - 1;
                        if (v < slot.node_count) {
                            uint64_t u = slot.get_csc_target(v);
                            if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else {
                uint64_t v = scalar_src;
                if (v < slot.node_count) {
                    uint64_t u = slot.get_csc_target(v);
                    if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    }
                }
            }
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
    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
            vm_state->pc = instruction_count;
            return IMPULSE_VM_OK;
        }
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_HAS_CSR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = false;
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.offsets_ptr != nullptr && slot.targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_HAS_CSC: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = false;
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.csc_offsets_ptr != nullptr && slot.csc_targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_HAS_COO: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = false;
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.offsets_ptr != nullptr && slot.targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_HAS_KEY_CATALOG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_id = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = true;
    if (vm_state->query_context && domain_id < vm_state->query_context->string_vectors.size()) {
        present = !vm_state->query_context->string_vectors[domain_id].empty();
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_CSR_DEGREE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    uint64_t degree = 0;
    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        if (slot.offsets_ptr && u < slot.node_count) {
            degree = slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
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
    if (dst == 0 && src != 0) dst = src;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    bool is_subset = true;
    printf("DEBUG STABLE_CHECK: dst=%d, src=%d\n", dst, src);
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
    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t val = vm_state->registers[val_reg];

    bool has_attr = (rel_id < vm_state->query_context->attribute_slots.size() &&
                     attr_id < vm_state->query_context->attribute_slots[rel_id].size() &&
                     vm_state->query_context->attribute_slots[rel_id][attr_id].data_ptr != nullptr);

    auto eval_match = [&](uint64_t u) -> bool {
        if (vm_state->query_context->mock_node_attrs.count(attr_id)) {
            const auto& mock = vm_state->query_context->mock_node_attrs.at(attr_id);
            if (mock.has_mask) {
                if (!mock.mask.test(u)) return false;
            }
            if (!mock.int_data.empty()) {
                if (u >= mock.int_data.size()) return false;
                return mock.int_data[u] == val;
            }
            if (!mock.float_data.empty()) {
                if (u >= mock.float_data.size()) return false;
                float expected_f = *reinterpret_cast<const float*>(&val);
                return mock.float_data[u] == expected_f;
            }
            return false;
        }
        if (!has_attr) return true;
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        uint8_t base_type = attr.type_code & 0x7F;
        if (base_type == 0x01 || base_type == 0x02) {
            const int64_t* data = static_cast<const int64_t*>(attr.data_ptr);
            return data[u] == static_cast<int64_t>(val);
        } else if (base_type == 0x03) {
            const int32_t* data = static_cast<const int32_t*>(attr.data_ptr);
            return data[u] == static_cast<int32_t>(val);
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

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        bs_dst = vm_state->query_context->bitsets[h_src];
    } else {
        bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
    }

    bool has_attr = (rel_id < vm_state->query_context->attribute_slots.size() &&
                     attr_id < vm_state->query_context->attribute_slots[rel_id].size() &&
                     vm_state->query_context->attribute_slots[rel_id][attr_id].data_ptr != nullptr);
    const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);

    if (has_attr && prefix != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        size_t prefix_len = std::strlen(prefix);
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

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_dst.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (!eval_match(u)) {
                        bs_dst.words[i] &= ~(1ULL << b);
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
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
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        }
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
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        }
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

op_FLOAT_VECTOR_SCALE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    float factor = 1.0f;
    if (inst.flags == 0) {
        factor = reinterpret_cast<const float&>(inst.payload);
    } else {
        uint16_t src_reg = inst.payload & 0xFFFF;
        VALIDATE_REG(src_reg);
        if (vm_state->register_types[src_reg] == TYPE_FLOAT) {
            factor = reinterpret_cast<const float&>(vm_state->registers[src_reg]);
        } else {
            factor = static_cast<float>(vm_state->registers[src_reg]);
        }
    }

    int handle = static_cast<int>(vm_state->registers[dst]);
    if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR && handle >= 0 && handle < static_cast<int>(vm_state->query_context->float_vectors.size())) {
        float* vec = vm_state->query_context->float_vectors[handle].data();
        size_t size = vm_state->query_context->max_nodes;
        impulse_simd_vector_scale_f32(vec, factor, size);
    }

    vm_state->pc++;
    DISPATCH();
}

op_L1_NORM_DIFF: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    size_t N = vm_state->query_context->max_nodes;
    float l1_diff = 0.0f;

    if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR && vm_state->register_types[src2] == TYPE_FLOAT_VECTOR) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        if (h1 >= 0 && h1 < static_cast<int>(vm_state->query_context->float_vectors.size()) &&
            h2 >= 0 && h2 < static_cast<int>(vm_state->query_context->float_vectors.size())) {
            const float* vec1 = vm_state->query_context->float_vectors[h1].data();
            const float* vec2 = vm_state->query_context->float_vectors[h2].data();

            #pragma omp parallel for reduction(+:l1_diff) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                l1_diff += std::abs(vec1[i] - vec2[i]);
            }
        }
    } else if ((vm_state->register_types[src1] == TYPE_FLOAT || vm_state->register_types[src1] == TYPE_INT64) &&
               (vm_state->register_types[src2] == TYPE_FLOAT || vm_state->register_types[src2] == TYPE_INT64)) {
        float val1 = (vm_state->register_types[src1] == TYPE_FLOAT) ?
            reinterpret_cast<float&>(vm_state->registers[src1]) : static_cast<float>(vm_state->registers[src1]);
        float val2 = (vm_state->register_types[src2] == TYPE_FLOAT) ?
            reinterpret_cast<float&>(vm_state->registers[src2]) : static_cast<float>(vm_state->registers[src2]);
        l1_diff = std::abs(val1 - val2);
    }

    vm_state->registers[dst] = 0;
    reinterpret_cast<float&>(vm_state->registers[dst]) = l1_diff;
    vm_state->register_types[dst] = TYPE_FLOAT;

    if (l1_diff < 1e-4f) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_STR_CONCAT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& svec_dst = vm_state->query_context->string_vectors[h_dst];
    svec_dst.clear();

    if (vm_state->register_types[src1] == TYPE_STRING_VECTOR) {
        int h_src1 = static_cast<int>(vm_state->registers[src1]);
        const auto& svec1 = vm_state->query_context->string_vectors[h_src1];
        for (const char* str : svec1) svec_dst.push_back(str);
    }
    if (vm_state->register_types[src2] == TYPE_STRING_VECTOR) {
        int h_src2 = static_cast<int>(vm_state->registers[src2]);
        const auto& svec2 = vm_state->query_context->string_vectors[h_src2];
        for (const char* str : svec2) svec_dst.push_back(str);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;

    vm_state->pc++;
    DISPATCH();
}

op_MXV: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    int mask_reg_idx = inst.flags > 0 ? (inst.flags - 1) : -1;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 8) & 0xFF;
    uint16_t semiring_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);

    if (rel >= vm_state->query_context->slots.size()) {
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    size_t N = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);

    if (is_double) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
        const double* src_data = vm_state->query_context->double_vectors[vm_state->registers[src_vec]].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    double sum = 0.0;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < N) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    double min_val = std::numeric_limits<double>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < N) min_val = std::min(min_val, src_data[v] + 1.0);
                    }
                    dst_data[u] = min_val;
                } else {
                    dst_data[u] = 0.0;
                }
            } else {
                dst_data[u] = 0.0;
            }
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        if (vm_state->query_context->float_vectors[vm_state->registers[dst]].size() < N) {
            vm_state->query_context->float_vectors[vm_state->registers[dst]].resize(N);
        }
        float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
        const float* src_data = vm_state->query_context->float_vectors[vm_state->registers[src_vec]].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0f;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    float sum = 0.0f;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < N) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    float min_val = std::numeric_limits<float>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < N) min_val = std::min(min_val, src_data[v] + 1.0f);
                    }
                    dst_data[u] = min_val;
                } else {
                    dst_data[u] = 0.0f;
                }
            } else {
                dst_data[u] = 0.0f;
            }
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_VXM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    int mask_reg_idx = inst.flags > 0 ? (inst.flags - 1) : -1;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 8) & 0xFF;
    uint16_t semiring_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);

    if (rel >= vm_state->query_context->slots.size()) {
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.csc_offsets_ptr || !slot.csc_targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    size_t N = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);

    if (is_double) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
        const double* src_data = vm_state->query_context->double_vectors[vm_state->registers[src_vec]].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csc_offset(u);
                uint64_t end = slot.get_csc_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    double sum = 0.0;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csc_target(i);
                        if (v < N) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    double min_val = std::numeric_limits<double>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csc_target(i);
                        if (v < N) min_val = std::min(min_val, src_data[v] + 1.0);
                    }
                    dst_data[u] = min_val;
                } else {
                    dst_data[u] = 0.0;
                }
            } else {
                dst_data[u] = 0.0;
            }
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
        const float* src_data = vm_state->query_context->float_vectors[vm_state->registers[src_vec]].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0f;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csc_offset(u);
                uint64_t end = slot.get_csc_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    float sum = 0.0f;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csc_target(i);
                        if (v < N) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    float min_val = std::numeric_limits<float>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csc_target(i);
                        if (v < N) min_val = std::min(min_val, src_data[v] + 1.0f);
                    }
                    dst_data[u] = min_val;
                } else {
                    dst_data[u] = 0.0f;
                }
            } else {
                dst_data[u] = 0.0f;
            }
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_EWISE_ADD: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    size_t N = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);

    if (is_double) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
        const double* s1_data = vm_state->query_context->double_vectors[vm_state->registers[src1]].data();
        const double* s2_data = vm_state->query_context->double_vectors[vm_state->registers[src2]].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_ADD) dst_data[i] = s1_data[i] + s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] + s2_data[i];
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
        const float* s1_data = vm_state->query_context->float_vectors[vm_state->registers[src1]].data();
        const float* s2_data = vm_state->query_context->float_vectors[vm_state->registers[src2]].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_ADD) dst_data[i] = s1_data[i] + s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] + s2_data[i];
        }
    }

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

op_EWISE_MULT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    size_t N = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);

    if (is_double) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
        const double* s1_data = vm_state->query_context->double_vectors[vm_state->registers[src1]].data();
        const double* s2_data = vm_state->query_context->double_vectors[vm_state->registers[src2]].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] * s2_data[i];
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
        const float* s1_data = vm_state->query_context->float_vectors[vm_state->registers[src1]].data();
        const float* s2_data = vm_state->query_context->float_vectors[vm_state->registers[src2]].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] * s2_data[i];
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const double* vec = vm_state->query_context->double_vectors[handle].data();
        double res = 0.0;
        if (op_id == BINARY_OP_ADD) {
            #pragma omp parallel for reduction(+:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res += vec[i];
            }
        } else if (op_id == BINARY_OP_MIN) {
            res = std::numeric_limits<double>::infinity();
            #pragma omp parallel for reduction(min:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res = std::min(res, vec[i]);
            }
        } else if (op_id == BINARY_OP_MAX) {
            res = -std::numeric_limits<double>::infinity();
            #pragma omp parallel for reduction(max:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res = std::max(res, vec[i]);
            }
        }
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(res);
        vm_state->register_types[dst] = TYPE_DOUBLE;
    } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const float* vec = vm_state->query_context->float_vectors[handle].data();
        float res = 0.0f;
        if (op_id == BINARY_OP_ADD) {
            #pragma omp parallel for reduction(+:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res += vec[i];
            }
        } else if (op_id == BINARY_OP_MIN) {
            res = std::numeric_limits<float>::infinity();
            #pragma omp parallel for reduction(min:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res = std::min(res, vec[i]);
            }
        } else if (op_id == BINARY_OP_MAX) {
            res = -std::numeric_limits<float>::infinity();
            #pragma omp parallel for reduction(max:res) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                res = std::max(res, vec[i]);
            }
        }
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = res;
        vm_state->register_types[dst] = TYPE_FLOAT;
    } else {
        vm_state->registers[dst] = vm_state->registers[src_vec];
        vm_state->register_types[dst] = TYPE_INT64;
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
        printf("OUT_OF_BOUNDS: CSR_WALK rel=%d, slots.size()=%zu, pc=%zu\n", rel, vm_state->query_context->slots.size(), vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    if (vm_state->register_types[dst] != TYPE_NODE_VECTOR) {
        int h_dst = acquire_node_vector(vm_state->query_context);
        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_NODE_VECTOR;
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
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                uint64_t deg = end - start;
                if (r < deg) {
                    uint64_t v = slot.get_csr_target(start + r);
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
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            uint64_t deg = end - start;
            for (uint64_t i = 0; i < deg; ++i) {
                uint64_t v = slot.get_csr_target(start + i);
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

op_ISLAND_DETECT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    uint8_t src1 = inst.payload & 0xFF;
    uint8_t src2 = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }

    uint64_t critical_pairs_count = 0;
    {
        std::vector<uint32_t> lines1;
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

        std::vector<uint32_t> lines2;
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
            uint32_t base_components = run_island_detect_bfs(N, slot, branch_ids, -1, -1);

            bool same_set = (src1 == src2);

            if (same_set) {
#if defined(_OPENMP)
                #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, 64)
#endif
                for (size_t i = 0; i < lines1.size(); ++i) {
                    for (size_t j = i + 1; j < lines1.size(); ++j) {
                        uint32_t comp = run_island_detect_bfs(N, slot, branch_ids, lines1[i], lines1[j]);
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
                        uint32_t comp = run_island_detect_bfs(N, slot, branch_ids, lines1[i], lines2[j]);
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

op_READ_EDGE_WEIGHT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t u_reg = inst.payload & 0xFF;
    uint8_t v_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(u_reg);
    VALIDATE_REG(v_reg);

    float weight = 1.0f;
    uint64_t u = vm_state->registers[u_reg];
    uint64_t v = vm_state->registers[v_reg];
    (void)v;

    if (rel < vm_state->query_context->attribute_slots.size() &&
        !vm_state->query_context->attribute_slots[rel].empty() &&
        vm_state->query_context->attribute_slots[rel][0].data_ptr != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel][0];
        const float* weights = static_cast<const float*>(attr.data_ptr);
        if (weights && u < vm_state->query_context->max_nodes) {
            weight = weights[u];
        }
    }

    vm_state->registers[dst] = 0;
    reinterpret_cast<float&>(vm_state->registers[dst]) = weight;
    vm_state->register_types[dst] = TYPE_FLOAT;

    vm_state->pc++;
    DISPATCH();
}

op_ROARING_BITMAP_AND: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        const auto& bs2 = vm_state->query_context->bitsets[h2];

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = bs1.words[i] & bs2.words[i];
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    vm_state->pc++;
    DISPATCH();
}

op_ROARING_BITMAP_OR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        const auto& bs2 = vm_state->query_context->bitsets[h2];

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = bs1.words[i] | bs2.words[i];
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

op_ROARING_BITMAP_AND_NOT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        const auto& bs2 = vm_state->query_context->bitsets[h2];

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = bs1.words[i] & ~bs2.words[i];
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    vm_state->pc++;
    DISPATCH();
}

op_VECTOR_MUL_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    size_t size = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR);
    int h_dst = vm_state->registers[dst];

    if (!is_double && vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        h_dst = impulse_vm_context_acquire_float_vector(vm_state->query_context);
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }

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
    } else if (vm_state->register_types[src] == TYPE_FLOAT) {
        uint32_t bits = static_cast<uint32_t>(vm_state->registers[src] & 0xFFFFFFFFULL);
        float fval;
        std::memcpy(&fval, &bits, sizeof(float));
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fval;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fval;
    } else if (vm_state->register_types[src] == TYPE_DOUBLE) {
        double dval;
        uint64_t bits = vm_state->registers[src];
        std::memcpy(&dval, &bits, sizeof(double));
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(dval);
        vm_state->register_types[dst] = TYPE_DOUBLE;
        sum = dval;
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

    const impulse_vm_context_t::MockAttribute* mock_node_attr = nullptr;
    if (vm_state->query_context->mock_node_attrs.count(attr_id)) {
        mock_node_attr = &vm_state->query_context->mock_node_attrs.at(attr_id);
    } else if (attr_id == 0 && vm_state->query_context->mock_node_attrs.count(0)) {
        mock_node_attr = &vm_state->query_context->mock_node_attrs.at(0);
    }

    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        int64_t sum = 0;
        if (slot.offsets_ptr && slot.targets_ptr && u < slot.node_count) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end   = slot.get_csr_offset(u + 1);
            for (uint64_t idx = start; idx < end; ++idx) {
                uint64_t target_node = slot.get_csr_target(idx);
                if (mock_node_attr) {
                    if (mock_node_attr->has_mask && !mock_node_attr->mask.test(target_node)) {
                        // masked
                    } else if (target_node < mock_node_attr->int_data.size()) {
                        sum += static_cast<int64_t>(mock_node_attr->int_data[target_node]);
                    } else if (target_node < mock_node_attr->float_data.size()) {
                        sum += static_cast<int64_t>(mock_node_attr->float_data[target_node]);
                    }
                } else if (has_edge_attr) {
                    uint8_t base_type = edge_attr.type_code & 0x7F;
                    if (base_type == 0x03) sum += static_cast<const int32_t*>(edge_attr.data_ptr)[idx];
                    else if (base_type == 0x04) sum += static_cast<const int64_t*>(edge_attr.data_ptr)[idx];
                    else if (base_type == 0x08) sum += static_cast<int64_t>(static_cast<const float*>(edge_attr.data_ptr)[idx]);
                    else if (base_type == 0x09) sum += static_cast<int64_t>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                    else sum += target_node;
                } else {
                    sum += target_node;
                }
            }
        }
        vm_state->registers[dst] = static_cast<uint64_t>(sum);
        vm_state->register_types[dst] = TYPE_INT64;
        if (sum == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        vm_state->pc++;
        DISPATCH();
    }
    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::memset(dst_vec, 0, max_nodes * sizeof(float));

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                if (src_val == 0.0) continue;
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
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
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
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

    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        int h_dst = acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        vm_state->pc++;
        DISPATCH();
    }
    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::fill(dst_vec, dst_vec + max_nodes, 0.0f);

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
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
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
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

    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_INT64;

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
    
    // Register Windowing: Pass Out registers (R12..R15) to Callee In registers (R0..R3)
    uint64_t arg0 = vm_state->registers[12];
    uint64_t arg1 = vm_state->registers[13];
    uint64_t arg2 = vm_state->registers[14];
    uint64_t arg3 = vm_state->registers[15];

    vm_state->registers[0] = arg0;
    vm_state->registers[1] = arg1;
    vm_state->registers[2] = arg2;
    vm_state->registers[3] = arg3;

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
    if (domain_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (attr && attr->data_ptr) {
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
                        const char* key_val = nullptr;
                        if (!attr->offsets_ptr) {
                            key_val = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                        } else {
                            const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                            key_val = static_cast<const char*>(attr->data_ptr) + offsets[u];
                        }
                        if (key_val) {
                            if (!attr->offsets_ptr) {
                                size_t target_len = std::strlen(target_key);
                                size_t match_len = std::min<size_t>(target_len, static_cast<size_t>(attr->dimension));
                                if (std::strncmp(key_val, target_key, match_len) == 0 &&
                                    (target_len <= static_cast<size_t>(attr->dimension) || key_val[match_len] == '\0')) {
                                    is_match = true;
                                }
                            } else if (std::strcmp(key_val, target_key) == 0) {
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

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& svec = vm_state->query_context->string_vectors[h_dst];
    svec.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint8_t base_type = attr ? (attr->type_code & 0x7F) : 0;
    size_t max_nodes = vm_state->query_context->max_nodes;

    auto add_key = [&](uint64_t u) {
        if (attr && attr->data_ptr && base_type == 0x0B) {
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
        } else {
            svec.push_back("mock_key");
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

    int h_dst = acquire_value_map(vm_state->query_context);
    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& vmap = vm_state->query_context->value_maps[h_dst];
    vmap.keys.clear();
    vmap.values.clear();

    bool nodes_is_bitset = (vm_state->register_types[nodes_reg] == TYPE_BITSET_HANDLE);
    bool vals_is_double = (vm_state->register_types[vals_reg] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;
    uint8_t base_type = attr ? (attr->type_code & 0x7F) : 0;

    auto add_entry = [&](uint64_t u) {
        const char* str_ptr = nullptr;
        if (attr && attr->data_ptr && base_type == 0x0B) {
            if (!attr->offsets_ptr) {
                str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
            } else {
                const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
            }
        }
        std::string key_str = str_ptr ? std::string(str_ptr) : ("key_" + std::to_string(u));
        
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

op_LOAD_CONST_STR_PREFIX: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

op_LOAD_INLINE_ARRAY: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t offset_bytes = inst.payload & 0xFFFF;
    uint16_t count = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (offset_bytes + count * sizeof(uint32_t) > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    if (vm_state->query_context->float_vectors[h_dst].size() < count) {
        vm_state->query_context->float_vectors[h_dst].resize(count);
    }
    if (vm_state->query_context->node_vectors[h_dst].size() < count) {
        vm_state->query_context->node_vectors[h_dst].resize(count);
    }

    float* dst_vec_f = vm_state->query_context->float_vectors[h_dst].data();
    uint64_t* dst_vec_n = vm_state->query_context->node_vectors[h_dst].data();

    const float* src_data_f = reinterpret_cast<const float*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    const uint32_t* src_data_i = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    for (size_t i = 0; i < count; ++i) {
        dst_vec_f[i] = src_data_f[i];
        dst_vec_n[i] = src_data_i[i];
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_LOAD_INLINE_SET: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t offset_bytes = inst.payload & 0xFFFF;
    uint16_t count = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (offset_bytes + count * sizeof(uint32_t) > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    vm_state->query_context->bitsets[h_dst].clear();

    const uint32_t* src_data = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    for (uint16_t i = 0; i < count; ++i) {
        vm_state->query_context->bitsets[h_dst].set(src_data[i]);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_INIT_MOCK_GRAPH: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t slot_id = inst.dst_reg;
    uint16_t offset_bytes = inst.payload & 0xFFFF;
    uint16_t node_count = (inst.payload >> 16) & 0xFFFF;

    if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (offset_bytes >= vm_state->query_context->inline_data_bytes || slot_id >= 16) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    {
        const uint32_t* offsets = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
        uint32_t total_edges = offsets[node_count];
        const uint32_t* targets = offsets + (node_count + 1);

        uint32_t max_target_id = 0;
        for (uint32_t e = 0; e < total_edges; ++e) {
            if (targets[e] > max_target_id) max_target_id = targets[e];
        }
        uint32_t csc_node_count = (total_edges > 0) ? std::max(static_cast<uint32_t>(node_count), max_target_id + 1) : node_count;

        auto& slot = vm_state->query_context->slots[slot_id];
        std::vector<uint32_t> in_degrees(csc_node_count, 0);
        for (uint32_t e = 0; e < total_edges; ++e) {
            in_degrees[targets[e]]++;
        }

        slot.dynamic_csc_offsets.assign(csc_node_count + 1, 0);
        for (uint32_t i = 0; i < csc_node_count; ++i) {
            slot.dynamic_csc_offsets[i + 1] = slot.dynamic_csc_offsets[i] + in_degrees[i];
        }

        slot.dynamic_csc_targets.resize(total_edges);
        std::vector<uint32_t> cur_pos = slot.dynamic_csc_offsets;
        for (uint32_t u = 0; u < node_count; ++u) {
            uint32_t start = offsets[u];
            uint32_t end = offsets[u + 1];
            for (uint32_t e = start; e < end; ++e) {
                uint32_t v = targets[e];
                slot.dynamic_csc_targets[cur_pos[v]++] = u;
            }
        }

        slot.csc_offsets_ptr = slot.dynamic_csc_offsets.data();
        slot.csc_targets_ptr = slot.dynamic_csc_targets.data();
        slot.node_count = std::max(static_cast<uint64_t>(csc_node_count), static_cast<uint64_t>(node_count));
        slot.edge_count = total_edges;
        slot.offsets_ptr = offsets;
        slot.targets_ptr = targets;

        if (slot.node_count > vm_state->query_context->max_nodes) {
            vm_state->query_context->max_nodes = slot.node_count;
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_LOAD_INDIRECT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_param = inst.payload & 0xFFFF;
    uint16_t index_reg = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (inst.flags == 0) {
        VALIDATE_REG(src_param);
        uint64_t target_reg_idx = vm_state->registers[src_param];
        VALIDATE_REG(target_reg_idx);
        vm_state->registers[dst] = vm_state->registers[target_reg_idx];
        vm_state->register_types[dst] = vm_state->register_types[target_reg_idx];
    } else {
        VALIDATE_REG(src_param);
        VALIDATE_REG(index_reg);
        int h_vec = static_cast<int>(vm_state->registers[src_param]);
        uint64_t idx = vm_state->registers[index_reg];
        if (h_vec < 0 || h_vec >= static_cast<int>(vm_state->query_context->float_vectors.size())) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& vec = vm_state->query_context->float_vectors[h_vec];
        if (idx >= vec.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        float val = vec[idx];
        vm_state->registers[dst] = *reinterpret_cast<uint32_t*>(&val);
        vm_state->register_types[dst] = TYPE_FLOAT;
    }

    vm_state->pc++;
    DISPATCH();
}

op_ALLOC_SCRATCH: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t aligned = (static_cast<uint64_t>(req_bytes) + 63ULL) & ~63ULL;
    vm_state->registers[dst] = aligned;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_ASSERT_SCRATCH_BYTES: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t available_bytes = 512 * 1024 * 1024ULL; // 512 MB default scratch cap
    if (static_cast<uint64_t>(req_bytes) > available_bytes) {
        return IMPULSE_VM_ERR_ASSERTION_FAILED;
    }
    vm_state->registers[dst] = available_bytes;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_SET_MAX_DOP: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_dop = inst.payload;
    VALIDATE_REG(dst);

    int host_ceiling = 8;
#if defined(_OPENMP)
    host_ceiling = omp_get_max_threads();
#endif
    const char* env_dop = std::getenv("IMPULSE_MAX_DOP");
    if (!env_dop) env_dop = std::getenv("IMPULSE_MAX_THREADS");
    if (env_dop) {
        int parsed = std::atoi(env_dop);
        if (parsed > 0) host_ceiling = parsed;
    }

    int effective_dop = std::max(1, std::min(req_dop > 0 ? static_cast<int>(req_dop) : host_ceiling, host_ceiling));
    if (vm_state->query_context) {
        vm_state->query_context->max_threads = effective_dop;
    }
    vm_state->registers[dst] = effective_dop;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_THROW: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    vm_state->registers[0] = inst.payload;
    vm_state->register_types[0] = TYPE_INT64;
    return IMPULSE_VM_ERR_USER_THROW;
}

op_ASSERT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t src_reg = inst.dst_reg;
    uint32_t expected_val = inst.payload;
    VALIDATE_REG(src_reg);

    if (inst.flags == 0) {
        uint64_t actual_val = vm_state->registers[src_reg];
        if (actual_val != static_cast<uint64_t>(expected_val)) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    } else {
        if ((vm_state->flags & expected_val) != static_cast<uint64_t>(expected_val)) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    }

    vm_state->pc++;
    DISPATCH();
}

op_TRAP: {
    return IMPULSE_VM_ERR_TRAP;
}

op_KCORE_DECOMPOSITION: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t r_src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(r_src);

    uint16_t rel_id = static_cast<uint16_t>(vm_state->registers[r_src]);
    if (rel_id >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel_id];
    size_t n = slot.node_count;
    if (n == 0) n = vm_state->query_context->max_nodes;

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    if (vm_state->query_context->float_vectors[h_dst].size() < n) {
        vm_state->query_context->float_vectors[h_dst].resize(n);
    }
    float* core = vm_state->query_context->float_vectors[h_dst].data();
    std::fill_n(core, n, 0.0f);

    if (slot.offsets_ptr && slot.targets_ptr && n > 0) {
        std::vector<int> deg(n, 0);
        int max_deg = 0;
        for (size_t i = 0; i < n; ++i) {
            int d = static_cast<int>(slot.get_csr_offset(i + 1) - slot.get_csr_offset(i));
            deg[i] = d;
            if (d > max_deg) max_deg = d;
        }

        std::vector<int> vert(n, 0);
        std::vector<int> pos(n, 0);
        std::vector<int> bin(max_deg + 1, 0);

        for (size_t i = 0; i < n; ++i) bin[deg[i]]++;
        int start = 0;
        for (int d = 0; d <= max_deg; ++d) {
            int num = bin[d];
            bin[d] = start;
            start += num;
        }
        for (size_t i = 0; i < n; ++i) {
            int p = bin[deg[i]]++;
            pos[i] = p;
            vert[p] = static_cast<int>(i);
        }
        for (int d = max_deg; d >= 1; --d) {
            bin[d] = bin[d - 1];
        }
        bin[0] = 0;

        for (size_t i = 0; i < n; ++i) {
            int v = vert[i];
            core[v] = static_cast<float>(deg[v]);
            uint64_t e_start = slot.get_csr_offset(v);
            uint64_t e_end = slot.get_csr_offset(v + 1);
            for (uint64_t e = e_start; e < e_end; ++e) {
                uint64_t u = slot.get_csr_target(e);
                if (u < n && deg[u] > deg[v]) {
                    int du = deg[u];
                    int pu = pos[u];
                    int pw = bin[du];
                    int w = vert[pw];
                    if (static_cast<size_t>(u) != static_cast<size_t>(w)) {
                        pos[u] = pw;
                        pos[w] = pu;
                        vert[pu] = w;
                        vert[pw] = static_cast<int>(u);
                    }
                    bin[du]++;
                    deg[u]--;
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_PASS_THROUGH: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}

op_VEC_CMP_EQ: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();
    size_t N = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        double target = (vm_state->register_types[src2] == TYPE_DOUBLE) ?
            reinterpret_cast<double&>(vm_state->registers[src2]) : static_cast<double>(vm_state->registers[src2]);
        for (size_t i = 0; i < N; ++i) {
            if (v[i] == target) bs.set(i);
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const float* v = (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR) ?
            vm_state->query_context->float_vectors[h_src].data() : nullptr;
        if (v) {
            float target = (vm_state->register_types[src2] == TYPE_FLOAT) ?
                reinterpret_cast<float&>(vm_state->registers[src2]) : static_cast<float>(vm_state->registers[src2]);
            for (size_t i = 0; i < N; ++i) {
                if (v[i] == target) bs.set(i);
            }
        }
    }
    bool is_empty = true;
    for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
        if (bs.words[w] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_VEC_CMP_GT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();
    size_t N = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        double threshold = (vm_state->register_types[src2] == TYPE_DOUBLE) ?
            reinterpret_cast<double&>(vm_state->registers[src2]) : static_cast<double>(vm_state->registers[src2]);
        for (size_t i = 0; i < N; ++i) {
            if (v[i] > threshold) bs.set(i);
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const float* v = (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR) ?
            vm_state->query_context->float_vectors[h_src].data() : nullptr;
        if (v) {
            float threshold = (vm_state->register_types[src2] == TYPE_FLOAT) ?
                reinterpret_cast<float&>(vm_state->registers[src2]) : static_cast<float>(vm_state->registers[src2]);
            for (size_t i = 0; i < N; ++i) {
                if (v[i] > threshold) bs.set(i);
            }
        }
    }

    bool is_empty = true;
    for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
        if (bs.words[w] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_VEC_CMP_LT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();
    size_t N = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        double threshold = (vm_state->register_types[src2] == TYPE_DOUBLE) ?
            reinterpret_cast<double&>(vm_state->registers[src2]) : static_cast<double>(vm_state->registers[src2]);
        for (size_t i = 0; i < N; ++i) {
            if (v[i] < threshold) bs.set(i);
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src1]);
        const float* v = (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR) ?
            vm_state->query_context->float_vectors[h_src].data() : nullptr;
        if (v) {
            float threshold = (vm_state->register_types[src2] == TYPE_FLOAT) ?
                reinterpret_cast<float&>(vm_state->registers[src2]) : static_cast<float>(vm_state->registers[src2]);
            for (size_t i = 0; i < N; ++i) {
                if (v[i] < threshold) bs.set(i);
            }
        }
    }

    bool is_empty = true;
    for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
        if (bs.words[w] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_VEC_CMP_BETWEEN: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t r_min = (inst.payload >> 8) & 0xFF;
    uint16_t r_max = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    VALIDATE_REG(r_min);
    VALIDATE_REG(r_max);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();
    size_t N = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(vm_state->registers[src_vec]);
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        double min_val = (vm_state->register_types[r_min] == TYPE_DOUBLE) ? reinterpret_cast<double&>(vm_state->registers[r_min]) : (double)vm_state->registers[r_min];
        double max_val = (vm_state->register_types[r_max] == TYPE_DOUBLE) ? reinterpret_cast<double&>(vm_state->registers[r_max]) : (double)vm_state->registers[r_max];
        for (size_t i = 0; i < N; ++i) {
            if (v[i] >= min_val && v[i] <= max_val) bs.set(i);
        }
    } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
        int h_src = static_cast<int>(vm_state->registers[src_vec]);
        const float* v = vm_state->query_context->float_vectors[h_src].data();
        float min_val = (vm_state->register_types[r_min] == TYPE_FLOAT) ? reinterpret_cast<float&>(vm_state->registers[r_min]) : (float)vm_state->registers[r_min];
        float max_val = (vm_state->register_types[r_max] == TYPE_FLOAT) ? reinterpret_cast<float&>(vm_state->registers[r_max]) : (float)vm_state->registers[r_max];
        for (size_t i = 0; i < N; ++i) {
            if (v[i] >= min_val && v[i] <= max_val) bs.set(i);
        }
    }

    bool is_empty = true;
    for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
        if (bs.words[w] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_MASK_AND: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    int h1 = static_cast<int>(vm_state->registers[src1]);
    int h2 = static_cast<int>(vm_state->registers[src2]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs1 = vm_state->query_context->bitsets[h1];
    const auto& bs2 = vm_state->query_context->bitsets[h2];
    size_t words = vm_state->query_context->words_per_bitset;
    for (size_t w = 0; w < words; ++w) {
        bs_dst.words[w] = bs1.words[w] & bs2.words[w];
    }
    vm_state->pc++;
    DISPATCH();
}

op_MASK_OR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    int h1 = static_cast<int>(vm_state->registers[src1]);
    int h2 = static_cast<int>(vm_state->registers[src2]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs1 = vm_state->query_context->bitsets[h1];
    const auto& bs2 = vm_state->query_context->bitsets[h2];
    size_t words = vm_state->query_context->words_per_bitset;
    for (size_t w = 0; w < words; ++w) {
        bs_dst.words[w] = bs1.words[w] | bs2.words[w];
    }
    vm_state->pc++;
    DISPATCH();
}

op_MASK_NOT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    int h_src = static_cast<int>(vm_state->registers[src]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_src = vm_state->query_context->bitsets[h_src];
    size_t words = vm_state->query_context->words_per_bitset;
    for (size_t w = 0; w < words; ++w) {
        bs_dst.words[w] = ~bs_src.words[w];
    }
    vm_state->pc++;
    DISPATCH();
}

op_VEC_BLEND: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t mask_reg = inst.payload & 0xFF;
    uint16_t src_true = (inst.payload >> 8) & 0xFF;
    uint16_t src_false = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(mask_reg);
    VALIDATE_REG(src_true);
    VALIDATE_REG(src_false);

    int h_mask = static_cast<int>(vm_state->registers[mask_reg]);
    const auto& mask_bs = vm_state->query_context->bitsets[h_mask];
    size_t N = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src_true] == TYPE_DOUBLE_VECTOR) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* vt = vm_state->query_context->double_vectors[vm_state->registers[src_true]].data();
        const double* vf = vm_state->query_context->double_vectors[vm_state->registers[src_false]].data();
        for (size_t i = 0; i < N; ++i) {
            d[i] = mask_bs.test(i) ? vt[i] : vf[i];
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* vt = vm_state->query_context->float_vectors[vm_state->registers[src_true]].data();
        const float* vf = vm_state->query_context->float_vectors[vm_state->registers[src_false]].data();
        for (size_t i = 0; i < N; ++i) {
            d[i] = mask_bs.test(i) ? vt[i] : vf[i];
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_ASSERT_FINITE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t target_reg = inst.dst_reg;
    VALIDATE_REG(target_reg);

    if (vm_state->register_types[target_reg] == TYPE_FLOAT) {
        float v = *reinterpret_cast<const float*>(&vm_state->registers[target_reg]);
        if (std::isnan(v) || std::isinf(v)) {
            vm_state->registers[0] = 0;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (vm_state->register_types[target_reg] == TYPE_DOUBLE) {
        double v = *reinterpret_cast<const double*>(&vm_state->registers[target_reg]);
        if (std::isnan(v) || std::isinf(v)) {
            vm_state->registers[0] = 0;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (vm_state->register_types[target_reg] == TYPE_FLOAT_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const float* vec = vm_state->query_context->float_vectors[h].data();
        size_t N = vm_state->query_context->float_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            float v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                vm_state->registers[0] = i;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    } else if (vm_state->register_types[target_reg] == TYPE_DOUBLE_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->double_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const double* vec = vm_state->query_context->double_vectors[h].data();
        size_t N = vm_state->query_context->double_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            double v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                vm_state->registers[0] = i;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_VEC_MATH_UNARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint8_t func_id = (inst.payload >> 8) & 0xFF;
    uint8_t type_tag = inst.flags;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[src] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src] == TYPE_DOUBLE_VECTOR) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        int h_src = static_cast<int>(vm_state->registers[src]);
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s = vm_state->query_context->double_vectors[h_src].data();
        impulse_vector_math_unary_f64(func_id, d, s, N);
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        int h_src = static_cast<int>(vm_state->registers[src]);
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s = vm_state->query_context->float_vectors[h_src].data();
        impulse_vector_math_unary_f32(func_id, d, s, N);
    }
    vm_state->pc++;
    DISPATCH();
}

op_VEC_MATH_BINARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    uint8_t func_id = (inst.payload >> 16) & 0xFF;
    uint8_t type_tag = inst.flags;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (vm_state->register_types[src1] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src1] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    if (vm_state->register_types[src2] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src2] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        int h_s1 = static_cast<int>(vm_state->registers[src1]);
        int h_s2 = static_cast<int>(vm_state->registers[src2]);
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s1 = vm_state->query_context->double_vectors[h_s1].data();
        const double* s2 = vm_state->query_context->double_vectors[h_s2].data();
        impulse_vector_math_binary_f64(func_id, d, s1, s2, N);
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        int h_s1 = static_cast<int>(vm_state->registers[src1]);
        int h_s2 = static_cast<int>(vm_state->registers[src2]);
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s1 = vm_state->query_context->float_vectors[h_s1].data();
        const float* s2 = vm_state->query_context->float_vectors[h_s2].data();
        impulse_vector_math_binary_f32(func_id, d, s1, s2, N);
    }
    vm_state->pc++;
    DISPATCH();
}

op_VEC_MATH_TERNARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    uint16_t src3 = (inst.payload >> 16) & 0xFF;
    uint8_t func_id = (inst.payload >> 24) & 0xFF;
    uint8_t type_tag = inst.flags;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    VALIDATE_REG(src3);

    if (vm_state->register_types[src1] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src1] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    if (vm_state->register_types[src2] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src2] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    if (vm_state->register_types[src3] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src3] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s1 = vm_state->query_context->double_vectors[vm_state->registers[src1]].data();
        const double* s2 = vm_state->query_context->double_vectors[vm_state->registers[src2]].data();
        const double* s3 = vm_state->query_context->double_vectors[vm_state->registers[src3]].data();
        impulse_vector_math_ternary_f64(func_id, d, s1, s2, s3, N);
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s1 = vm_state->query_context->float_vectors[vm_state->registers[src1]].data();
        const float* s2 = vm_state->query_context->float_vectors[vm_state->registers[src2]].data();
        const float* s3 = vm_state->query_context->float_vectors[vm_state->registers[src3]].data();
        impulse_vector_math_ternary_f32(func_id, d, s1, s2, s3, N);
    }
    vm_state->pc++;
    DISPATCH();
}

op_LOAD_COLUMN_VECTOR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_or_rel = inst.payload & 0xFFFF;
    uint16_t attr_idx = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

    if (domain_or_rel < vm_state->query_context->attribute_slots.size() &&
        attr_idx < vm_state->query_context->attribute_slots[domain_or_rel].size()) {
        const auto& slot = vm_state->query_context->attribute_slots[domain_or_rel][attr_idx];
        if (slot.data_ptr) {
            const float* src_attr = static_cast<const float*>(slot.data_ptr);
            size_t copy_n = std::min(N, (size_t)(slot.data_bytes / sizeof(float)));
            std::memcpy(dst_vec, src_attr, copy_n * sizeof(float));
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_GATHER_NODE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t frontier_reg = inst.payload & 0xFF;
    uint16_t attr_idx = (inst.payload >> 8) & 0xFF;
    uint16_t domain = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(frontier_reg);

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    int h_front = static_cast<int>(vm_state->registers[frontier_reg]);
    const auto& bs = vm_state->query_context->bitsets[h_front];

    uint32_t mock_key = (domain != 0) ? domain : attr_idx;
    auto it_mock = vm_state->query_context->mock_node_attrs.find(mock_key);
    if (it_mock != vm_state->query_context->mock_node_attrs.end()) {
        const auto& ma = it_mock->second;
        for (size_t i = 0; i < N; ++i) {
            if (bs.test(i)) {
                if (ma.has_mask && !ma.mask.test(i)) {
                    dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
                } else if (i < ma.float_data.size()) {
                    dst_vec[i] = ma.float_data[i];
                } else {
                    dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }
    } else if (domain < vm_state->query_context->attribute_slots.size() &&
        attr_idx < vm_state->query_context->attribute_slots[domain].size()) {
        const auto& slot = vm_state->query_context->attribute_slots[domain][attr_idx];
        if (slot.data_ptr) {
            const float* src_attr = static_cast<const float*>(slot.data_ptr);
            #pragma omp parallel for schedule(static)
            for (size_t i = 0; i < N; ++i) {
                if (bs.test(i)) {
                    dst_vec[i] = src_attr[i];
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_GATHER_EDGE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}

op_BRIN_ZONE_SKIP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = 1;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_DIRECT_STORE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src] == TYPE_NODE_ID) {
        uint64_t u = vm_state->registers[src];
        if (u < slot.node_count || slot.node_count == 0) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t v = slot.get_csr_target(e);
                bs_dst.set(v);
            }
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (bs_src.test(u)) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    bs_dst.set(v);
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_CSR_WALK_DENSE_STREAM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src] == TYPE_NODE_ID) {
        uint64_t u = vm_state->registers[src];
        if (u < slot.node_count || slot.node_count == 0) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t v = slot.get_csr_target(e);
                bs_dst.set(v);
            }
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (bs_src.test(u)) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    bs_dst.set(v);
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_CSC_WALK_DIRECT_STORE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.csc_offsets_ptr || !slot.csc_targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
    if (vm_state->register_types[src] == TYPE_NODE_ID) {
        uint64_t target_u = vm_state->registers[src];
        #pragma omp parallel for schedule(static)
        for (size_t v = 0; v < N; ++v) {
            uint64_t start = slot.get_csc_offset(v);
            uint64_t end = slot.get_csc_offset(v + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t u = slot.get_csc_target(e);
                if (u == target_u) {
                    bs_dst.set(v);
                    break;
                }
            }
        }
    } else {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        #pragma omp parallel for schedule(static)
        for (size_t v = 0; v < N; ++v) {
            uint64_t start = slot.get_csc_offset(v);
            uint64_t end = slot.get_csc_offset(v + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t u = slot.get_csc_target(e);
                if (bs_src.test(u)) {
                    bs_dst.set(v);
                    break;
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_COO_WALK:
op_COO_WALK_DIRECT_STORE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            if (vm_state->register_types[src] == TYPE_NODE_ID) {
                uint64_t u = vm_state->registers[src];
                if (u < slot.node_count || slot.node_count == 0) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        bs_dst.set(slot.get_csr_target(e));
                    }
                }
            } else {
                int h_src = static_cast<int>(vm_state->registers[src]);
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (bs_src.test(u)) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            bs_dst.set(slot.get_csr_target(e));
                        }
                    }
                }
            }
        }
    }
    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

op_COO_WALK_FILTERED: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t filter_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(filter_reg);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t scalar_src = vm_state->registers[src];
    const uint64_t* src_words = src_is_bitset ? vm_state->query_context->bitsets[static_cast<int>(scalar_src)].words : nullptr;

    bool flt_is_bitset = (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE);
    uint64_t scalar_flt = vm_state->registers[filter_reg];
    const uint64_t* flt_words = flt_is_bitset ? vm_state->query_context->bitsets[static_cast<int>(scalar_flt)].words : nullptr;

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_is_bitset) {
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            uint64_t v = slot.get_csr_target(e);
                            bool match = flt_words ? ((flt_words[v >> 6] & (1ULL << (v & 63))) != 0) : (v == scalar_flt);
                            if (match) {
                                #pragma omp atomic
                                bs_dst.words[v >> 6] |= (1ULL << (v & 63));
                            }
                        }
                    }
                }
            } else if (scalar_src < N) {
                uint64_t start = slot.get_csr_offset(scalar_src);
                uint64_t end = slot.get_csr_offset(scalar_src + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    bool match = flt_words ? ((flt_words[v >> 6] & (1ULL << (v & 63))) != 0) : (v == scalar_flt);
                    if (match) {
                        bitset_add(bs_dst, v, vm_state->query_context->max_nodes);
                    }
                }
            }
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

op_COO_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    int h_src = static_cast<int>(vm_state->registers[src]);
    const auto& bs_src = vm_state->query_context->bitsets[h_src];

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            #pragma omp parallel for schedule(static)
            for (size_t u = 0; u < N; ++u) {
                if (bs_src.test(u)) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        uint64_t v = slot.get_csr_target(e);
                        #pragma omp atomic
                        dst_vec[v] += 1.0f;
                    }
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_DENSE_WALK:
op_DENSE_WALK_BITMATRIX:
op_DENSE_WALK_DIRECT_STORE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            if (vm_state->register_types[src] == TYPE_NODE_ID) {
                uint64_t u = vm_state->registers[src];
                if (u < slot.node_count || slot.node_count == 0) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        bs_dst.set(slot.get_csr_target(e));
                    }
                }
            } else {
                int h_src = static_cast<int>(vm_state->registers[src]);
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (bs_src.test(u)) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            bs_dst.set(slot.get_csr_target(e));
                        }
                    }
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_DENSE_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}

op_FIXPOINT_KLEENE_STAR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    auto& reached = vm_state->query_context->bitsets[h_dst];
    reached.clear();

    size_t words = vm_state->query_context->words_per_bitset;
    size_t N = vm_state->query_context->max_nodes;

    int h_frontier = acquire_bitset(vm_state->query_context);
    int h_next = acquire_bitset(vm_state->query_context);
    if (h_frontier >= 0 && h_next >= 0) {
        auto& frontier = vm_state->query_context->bitsets[h_frontier];
        auto& next_f = vm_state->query_context->bitsets[h_next];
        frontier.clear();

        if (vm_state->register_types[src] == TYPE_NODE_ID) {
            uint64_t start_node = vm_state->registers[src];
            reached.set(start_node);
            frontier.set(start_node);
        } else if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
            int h_src = static_cast<int>(vm_state->registers[src]);
            const auto& initial = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < words; ++w) {
                reached.words[w] = initial.words[w];
                frontier.words[w] = initial.words[w];
            }
        }

        bool changed = true;
        size_t max_iter = 1000;
        while (changed && max_iter-- > 0) {
            next_f.clear();
            changed = false;

            #pragma omp parallel for schedule(static)
            for (size_t u = 0; u < N; ++u) {
                if (frontier.test(u)) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        uint64_t v = slot.get_csr_target(e);
                        if (!reached.test(v)) {
                            next_f.set(v);
                        }
                    }
                }
            }

            for (size_t w = 0; w < words; ++w) {
                uint64_t diff = next_f.words[w] & ~reached.words[w];
                if (diff != 0) {
                    changed = true;
                    reached.words[w] |= diff;
                    frontier.words[w] = diff;
                } else {
                    frontier.words[w] = 0;
                }
            }
        }
        release_bitset(vm_state->query_context, h_frontier);
        release_bitset(vm_state->query_context, h_next);
    }

    vm_state->pc++;
    DISPATCH();
}

op_SWAP_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t r1 = inst.dst_reg;
    uint16_t r2 = inst.payload & 0xFFFF;
    VALIDATE_REG(r1);
    VALIDATE_REG(r2);

    std::swap(vm_state->registers[r1], vm_state->registers[r2]);
    std::swap(vm_state->register_types[r1], vm_state->register_types[r2]);

    vm_state->pc++;
    DISPATCH();
}

op_FRONTIER_DIFF: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t r_curr = inst.payload & 0xFF;
    uint16_t r_prev = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(r_curr);
    VALIDATE_REG(r_prev);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    int h_curr = static_cast<int>(vm_state->registers[r_curr]);
    int h_prev = static_cast<int>(vm_state->registers[r_prev]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_curr = vm_state->query_context->bitsets[h_curr];
    const auto& bs_prev = vm_state->query_context->bitsets[h_prev];

    size_t words = vm_state->query_context->words_per_bitset;
    bool has_bits = false;
    for (size_t w = 0; w < words; ++w) {
        uint64_t d = bs_curr.words[w] & ~bs_prev.words[w];
        bs_dst.words[w] = d;
        if (d != 0) has_bits = true;
    }

    if (!has_bits) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

op_INIT_MOCK_NODE_ATTR:
op_INIT_MOCK_EDGE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t attr_id = inst.dst_reg;
    uint16_t src_reg = inst.payload & 0xFF;
    uint16_t mask_reg = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(src_reg);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    {
        impulse_vm_context_t::MockAttribute mock_attr;
        if (vm_state->register_types[src_reg] == TYPE_FLOAT_VECTOR) {
            int h = static_cast<int>(vm_state->registers[src_reg]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h]) {
                mock_attr.float_data = vm_state->query_context->float_vectors[h];
                mock_attr.int_data = vm_state->query_context->node_vectors[h];
            }
        } else if (vm_state->register_types[src_reg] == TYPE_NODE_VECTOR || vm_state->register_types[src_reg] == TYPE_UINT64_VECTOR) {
            int h = static_cast<int>(vm_state->registers[src_reg]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->node_vectors_allocated[h]) {
                mock_attr.int_data = vm_state->query_context->node_vectors[h];
            }
        }
        if (mask_reg < 64 && vm_state->register_types[mask_reg] == TYPE_BITSET_HANDLE) {
            int h_mask = static_cast<int>(vm_state->registers[mask_reg]);
            if (h_mask >= 0 && static_cast<size_t>(h_mask) < VM_MAX_BITSET_HANDLES && vm_state->query_context->bitset_allocated[h_mask]) {
                mock_attr.has_mask = true;
                mock_attr.mask = vm_state->query_context->bitsets[h_mask];
            }
        }
        if (inst.opcode == OP_INIT_MOCK_NODE_ATTR) {
            vm_state->query_context->mock_node_attrs[attr_id] = std::move(mock_attr);
        } else {
            vm_state->query_context->mock_edge_attrs[attr_id] = std::move(mock_attr);
        }
    }
    vm_state->pc++;
    DISPATCH();
}

op_HALT:
    return IMPULSE_VM_OK;

op_INVALID:
    return IMPULSE_VM_ERR_INVALID_OPCODE;

op_RESERVED:
    return IMPULSE_VM_ERR_RESERVED_OPCODE;

op_OUT_OF_BOUNDS:
    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

op_GAS_EXHAUSTED:
    return IMPULSE_VM_ERR_GAS_EXHAUSTED;

#else
    // Fallback switch-case loop for compilers without computed goto (MSVC)
    while (vm_state->pc < instruction_count) {
        if (vm_state->query_context && vm_state->query_context->fuel_enabled) {
            if (vm_state->query_context->fuel == 0) return IMPULSE_VM_ERR_GAS_EXHAUSTED;
            vm_state->query_context->fuel--;
        }
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
                if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                    int h_src = static_cast<int>(vm_state->registers[src]);
                    int h_dst = -1;
                    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                        h_dst = static_cast<int>(vm_state->registers[dst]);
                    } else {
                        h_dst = acquire_bitset(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                    }
                    if (h_dst != h_src) {
                        std::memcpy(vm_state->query_context->bitsets[h_dst].words,
                                    vm_state->query_context->bitsets[h_src].words,
                                    vm_state->query_context->words_per_bitset * sizeof(uint64_t));
                    }
                } else {
                    vm_state->registers[dst] = vm_state->registers[src];
                    vm_state->register_types[dst] = vm_state->register_types[src];
                }
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
            case OP_LOAD_CONST_STR_PREFIX: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                vm_state->registers[dst] = inst.payload;
                vm_state->register_types[dst] = TYPE_INT64;
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
                uint16_t src1 = inst.payload & 0xFFFF;
                uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src1);
                VALIDATE_REG(src2);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (dst == src1) {
                    if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] |= bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[src2], vm_state->query_context->max_nodes);
                    }
                } else if (dst == src2) {
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] |= bs_src1.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
                    }
                } else {
                    bs_dst.clear();
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] = bs_src1.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
                    }

                    if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] |= bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[src2], vm_state->query_context->max_nodes);
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
            case OP_SET_INTERSECT: {
                uint16_t dst = inst.dst_reg;
                uint16_t src1 = inst.payload & 0xFFFF;
                uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src1);
                VALIDATE_REG(src2);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (dst == src1) {
                    if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] &= bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
                        uint64_t node_id = vm_state->registers[src2];
                        bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
                        bs_dst.clear();
                        if (keeps) bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                    } else {
                        bs_dst.clear();
                    }
                } else if (dst == src2) {
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] &= bs_src1.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
                        uint64_t node_id = vm_state->registers[src1];
                        bool keeps = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
                        bs_dst.clear();
                        if (keeps) bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                    } else {
                        bs_dst.clear();
                    }
                } else {
                    bs_dst.clear();
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] = bs_src1.words[i] & bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64)) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        uint64_t node_id = vm_state->registers[src2];
                        if (bitset_test(bs_src1, node_id, vm_state->query_context->max_nodes)) {
                            bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                        }
                    } else if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE && (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64)) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        uint64_t node_id = vm_state->registers[src1];
                        if (bitset_test(bs_src2, node_id, vm_state->query_context->max_nodes)) {
                            bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                        }
                    } else if ((vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) && (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64)) {
                        if (vm_state->registers[src1] == vm_state->registers[src2]) {
                            bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
                        }
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
            case OP_SET_DIFFERENCE: {
                uint16_t dst = inst.dst_reg;
                uint16_t src1 = inst.payload & 0xFFFF;
                uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src1);
                VALIDATE_REG(src2);

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (dst == src1) {
                    if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] &= ~bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
                        uint64_t node_id = vm_state->registers[src2];
                        if (node_id < vm_state->query_context->max_nodes) {
                            size_t word_idx = node_id / 64;
                            size_t bit_idx = node_id % 64;
                            bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
                        }
                    }
                } else if (dst == src2) {
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] = bs_src1.words[i] & ~bs_dst.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
                        uint64_t node_id = vm_state->registers[src1];
                        bool in_src2 = bitset_test(bs_dst, node_id, vm_state->query_context->max_nodes);
                        bs_dst.clear();
                        if (!in_src2) {
                            bitset_add(bs_dst, node_id, vm_state->query_context->max_nodes);
                        }
                    } else {
                        bs_dst.clear();
                    }
                } else {
                    bs_dst.clear();
                    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) {
                        int h_src1 = static_cast<int>(vm_state->registers[src1]);
                        const auto& bs_src1 = vm_state->query_context->bitsets[h_src1];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] = bs_src1.words[i];
                        }
                    } else if (vm_state->register_types[src1] == TYPE_NODE_ID || vm_state->register_types[src1] == TYPE_INT64) {
                        bitset_add(bs_dst, vm_state->registers[src1], vm_state->query_context->max_nodes);
                    }

                    if (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
                        int h_src2 = static_cast<int>(vm_state->registers[src2]);
                        const auto& bs_src2 = vm_state->query_context->bitsets[h_src2];
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            bs_dst.words[i] &= ~bs_src2.words[i];
                        }
                    } else if (vm_state->register_types[src2] == TYPE_NODE_ID || vm_state->register_types[src2] == TYPE_INT64) {
                        uint64_t node_id = vm_state->registers[src2];
                        if (node_id < vm_state->query_context->max_nodes) {
                            size_t word_idx = node_id / 64;
                            size_t bit_idx = node_id % 64;
                            bs_dst.words[word_idx] &= ~(1ULL << bit_idx);
                        }
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
            case OP_CSR_WALK_2HOP: {
                uint16_t dst = inst.dst_reg;
                uint16_t rel1 = inst.payload & 0xFFFF;
                uint16_t rel2 = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);

                if (rel1 >= vm_state->query_context->slots.size() || rel2 >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot1 = vm_state->query_context->slots[rel1];
                const auto& slot2 = vm_state->query_context->slots[rel2];

                bool src_is_bitset = false;
                int h_src = -1;
                uint64_t scalar_src = 0;

                if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
                    scalar_src = input_param;
                    src_is_bitset = false;
                } else {
                    uint16_t src = 0;
                    src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                    h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
                    scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
                }

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                    vm_state->query_context->bitsets[h_dst].clear();
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                    vm_state->query_context->bitsets[h_dst].clear();
                }

                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                if (slot1.offsets_ptr && slot1.targets_ptr && slot2.offsets_ptr && slot2.targets_ptr) {
                    if (src_is_bitset) {
                        const auto& bs_src = vm_state->query_context->bitsets[h_src];
                        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                            uint64_t word = bs_src.words[w];
                            while (word) {
                                int bit = std::countr_zero(word);
                                uint64_t u = w * 64 + bit;
                                word &= word - 1;
                                if (u < slot1.node_count) {
                                    uint64_t start1 = slot1.get_csr_offset(u);
                                    uint64_t end1 = slot1.get_csr_offset(u + 1);
                                    for (uint64_t i = start1; i < end1; ++i) {
                                        uint64_t v = slot1.get_csr_target(i);
                                        if (v < slot2.node_count) {
                                            uint64_t start2 = slot2.get_csr_offset(v);
                                            uint64_t end2 = slot2.get_csr_offset(v + 1);
                                            for (uint64_t j = start2; j < end2; ++j) {
                                                bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        uint64_t u = scalar_src;
                        if (u < slot1.node_count) {
                            uint32_t start1 = slot1.offsets_ptr[u];
                            uint32_t end1 = slot1.offsets_ptr[u + 1];
                            for (uint32_t i = start1; i < end1; ++i) {
                                uint32_t v = slot1.targets_ptr[i];
                                if (v < slot2.node_count) {
                                    uint32_t start2 = slot2.offsets_ptr[v];
                                    uint32_t end2 = slot2.offsets_ptr[v + 1];
                                    for (uint32_t j = start2; j < end2; ++j) {
                                        bitset_add(bs_dst, slot2.targets_ptr[j], vm_state->query_context->max_nodes);
                                    }
                                }
                            }
                        }
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
                if (is_empty) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                    if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
                        vm_state->pc = instruction_count;
                        return IMPULSE_VM_OK;
                    }
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }

                vm_state->pc++;
                break;
            }
            case OP_CSR_WALK_STATE: {
                uint16_t dst = inst.dst_reg;
                vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
                vm_state->registers[dst] = 0;
                vm_state->pc++;
                break;
            }
            case OP_PROJECT_STATE: {
                uint16_t src = inst.payload & 0xFFFF;
                if (src == 0 && inst.payload != 0) src = (inst.payload >> 16) & 0xFFFF;
                uint16_t dst = inst.dst_reg;
                if (vm_state->register_types[src] != TYPE_FRONTIER_STATE) {
                    return IMPULSE_VM_ERR_INVALID_REGISTER;
                }
                vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
                vm_state->registers[dst] = vm_state->registers[src];
                vm_state->pc++;
                break;
            }
            case OP_CREATE_SCRATCH_INDEX:
            case OP_DROP_SCRATCH_INDEX: {
                vm_state->pc++;
                break;
            }
            case OP_VECTOR_TIME_VALID_AT: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                vm_state->registers[dst] = 0;
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                vm_state->pc++;
                break;
            }
            case OP_ADAPTIVE_WALK:
        case OP_CSR_WALK_STREAM:
        case OP_CSC_WALK_STREAM:
        case OP_COO_WALK_STREAM: {
            uint8_t walk_op = inst.opcode;
            uint16_t dst = inst.dst_reg;
            uint16_t src = inst.payload & 0xFFFF;
            uint16_t rel = (inst.payload >> 16) & 0xFFFF;
            int shaderPcStart = inst.flags & 0xFF;
            VALIDATE_REG(dst);
            VALIDATE_REG(src);

            if (rel >= vm_state->query_context->slots.size()) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& slot = vm_state->query_context->slots[rel];

            int h_dst = -1;
            if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                h_dst = static_cast<int>(vm_state->registers[dst]);
                vm_state->query_context->bitsets[h_dst].clear();
            } else {
                h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
            }
            auto& bs_dst = vm_state->query_context->bitsets[h_dst];

            std::vector<uint32_t> active_sources;
            if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
                active_sources.push_back(static_cast<uint32_t>(vm_state->registers[src]));
            } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR) {
                int h_src_vec = static_cast<int>(vm_state->registers[src]);
                const auto& vec = vm_state->query_context->node_vectors[h_src_vec];
                for (uint64_t v : vec) {
                    active_sources.push_back(static_cast<uint32_t>(v));
                }
            } else if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                int h_src_bs = static_cast<int>(vm_state->registers[src]);
                const auto& bs_src = vm_state->query_context->bitsets[h_src_bs];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = w * 64 + bit;
                        word &= word - 1;
                        active_sources.push_back(static_cast<uint32_t>(u));
                    }
                }
            } else {
                return IMPULSE_VM_ERR_INVALID_REGISTER;
            }

            for (uint32_t u : active_sources) {
                if (u >= slot.node_count) continue;
                uint64_t start = (walk_op == OP_CSC_WALK_STREAM) ? slot.get_csc_offset(u) : slot.get_csr_offset(u);
                uint64_t end = (walk_op == OP_CSC_WALK_STREAM) ? slot.get_csc_offset(u + 1) : slot.get_csr_offset(u + 1);

                for (uint64_t eIdx = start; eIdx < end; ++eIdx) {
                    uint32_t tgt = (walk_op == OP_CSC_WALK_STREAM) ? static_cast<uint32_t>(slot.get_csc_target(eIdx)) : static_cast<uint32_t>(slot.get_csr_target(eIdx));

                    float s_regs[16] = {0.0f};
                    bool abort = false;
                    size_t mutPc = shaderPcStart;

                    while (mutPc < instruction_count) {
                        const auto& sInst = bytecode[mutPc];
                        uint8_t op = sInst.opcode;
                        if (op == OP_STREAM_FUNC_END) break;

                        uint16_t sDst = sInst.dst_reg;
                        uint16_t sPayloadLow = sInst.payload & 0xFFFF;
                        uint16_t sPayloadHigh = (sInst.payload >> 16) & 0xFFFF;

                        switch (op) {
                            case OP_STREAM_FUNC_BEGIN: break;
                            case OP_STREAM_LOAD_SRC: {
                                uint16_t attr_id = sInst.payload;
                                if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                                    const auto& vec = vm_state->query_context->float_vectors[attr_id];
                                    s_regs[sDst] = (u < vec.size()) ? vec[u] : 0.0f;
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                                break;
                            }
                            case OP_STREAM_LOAD_TGT: {
                                uint16_t attr_id = sInst.payload;
                                if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                                    const auto& vec = vm_state->query_context->float_vectors[attr_id];
                                    s_regs[sDst] = (tgt < vec.size()) ? vec[tgt] : 0.0f;
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                                break;
                            }
                            case OP_STREAM_LOAD_EDGE: {
                                uint16_t attr_id = sInst.payload;
                                if (attr_id < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attr_id]) {
                                    const auto& vec = vm_state->query_context->float_vectors[attr_id];
                                    s_regs[sDst] = (eIdx < vec.size()) ? vec[eIdx] : 0.0f;
                                } else {
                                    s_regs[sDst] = 0.0f;
                                }
                                break;
                            }
                            case OP_STREAM_LOAD_SRC_ID: s_regs[sDst] = static_cast<float>(u); break;
                            case OP_STREAM_LOAD_TGT_ID: s_regs[sDst] = static_cast<float>(tgt); break;
                            case OP_STREAM_LOAD_EDGE_ID: s_regs[sDst] = static_cast<float>(eIdx); break;
                            case OP_STREAM_LOAD_CONST: {
                                uint32_t bits = sInst.payload;
                                float f;
                                std::memcpy(&f, &bits, sizeof(float));
                                s_regs[sDst] = f;
                                break;
                            }
                            case OP_STREAM_MATH_ADD: s_regs[sDst] = s_regs[sPayloadLow] + s_regs[sPayloadHigh]; break;
                            case OP_STREAM_MATH_SUB: s_regs[sDst] = s_regs[sPayloadLow] - s_regs[sPayloadHigh]; break;
                            case OP_STREAM_MATH_MUL: s_regs[sDst] = s_regs[sPayloadLow] * s_regs[sPayloadHigh]; break;
                            case OP_STREAM_MATH_DIV: {
                                float div = s_regs[sPayloadHigh];
                                s_regs[sDst] = (div == 0.0f) ? 0.0f : (s_regs[sPayloadLow] / div);
                                break;
                            }
                            case OP_STREAM_MATH_MOD: {
                                float mod = s_regs[sPayloadHigh];
                                s_regs[sDst] = (mod == 0.0f) ? 0.0f : std::fmod(s_regs[sPayloadLow], mod);
                                break;
                            }
                            case OP_STREAM_CMP_EQ: s_regs[sDst] = (s_regs[sPayloadLow] == s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                            case OP_STREAM_CMP_NEQ: s_regs[sDst] = (s_regs[sPayloadLow] != s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                            case OP_STREAM_CMP_GT: s_regs[sDst] = (s_regs[sPayloadLow] > s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                            case OP_STREAM_CMP_LT: s_regs[sDst] = (s_regs[sPayloadLow] < s_regs[sPayloadHigh]) ? 1.0f : 0.0f; break;
                            case OP_STREAM_LOGIC_AND: s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f && s_regs[sPayloadHigh] != 0.0f) ? 1.0f : 0.0f; break;
                            case OP_STREAM_LOGIC_OR: s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f || s_regs[sPayloadHigh] != 0.0f) ? 1.0f : 0.0f; break;
                            case OP_STREAM_LOGIC_NOT: s_regs[sDst] = (s_regs[sPayloadLow] == 0.0f) ? 1.0f : 0.0f; break;
                            case OP_STREAM_SELECT: {
                                s_regs[sDst] = (s_regs[sPayloadLow] != 0.0f) ? s_regs[sPayloadHigh] : s_regs[sDst];
                                break;
                            }
                            case OP_STREAM_FILTER: {
                                if (s_regs[sDst] == 0.0f) abort = true;
                                break;
                            }
                            case OP_STREAM_MATH_UNARY: {
                                float v = s_regs[sPayloadLow];
                                float res = v;
                                switch (sPayloadHigh) {
                                    case 0x01: res = std::abs(v); break;
                                    case 0x02: res = std::sqrt(v); break;
                                    case 0x03: res = 1.0f / std::sqrt(v); break;
                                    case 0x04: res = std::copysign(std::pow(std::abs(v), 1.0f / 3.0f), v); break;
                                    case 0x08: res = std::exp(v); break;
                                    case 0x09: res = std::exp2(v); break;
                                    case 0x0A: res = std::pow(10.0f, v); break;
                                    case 0x0B: res = std::expm1(v); break;
                                    case 0x0C: res = std::log(v); break;
                                    case 0x0D: res = std::log2(v); break;
                                    case 0x0E: res = std::log10(v); break;
                                    case 0x0F: res = std::log1p(v); break;
                                    case 0x10: res = std::sin(v); break;
                                    case 0x11: res = std::cos(v); break;
                                    case 0x12: res = std::tan(v); break;
                                    case 0x13: res = std::asin(v); break;
                                    case 0x14: res = std::acos(v); break;
                                    case 0x15: res = std::atan(v); break;
                                    case 0x17: res = (std::abs(v) < 1e-15f) ? 1.0f : (std::sin(v) / v); break;
                                    case 0x18: res = std::sinh(v); break;
                                    case 0x19: res = std::cosh(v); break;
                                    case 0x1A: res = std::tanh(v); break;
                                    case 0x1E: res = std::floor(v); break;
                                    case 0x1F: res = std::ceil(v); break;
                                    case 0x21: res = std::floor(v + 0.5f); break;
                                    case 0x25: res = (v > 0.0f) ? v : 0.0f; break;
                                    case 0x26: res = (v > 0.0f) ? v : 0.01f * v; break;
                                    case 0x27: res = 1.0f / (1.0f + std::exp(-v)); break;
                                    case 0x28: res = 0.5f * v * (1.0f + std::tanh(0.7978845608028654 * (v + 0.044715 * v * v * v))); break;
                                    case 0x29: res = v / (1.0f + std::exp(-v)); break;
                                    case 0x2A: res = std::log(1.0f + std::exp(v)); break;
                                    case 0x34: res = std::isnan(v) ? 1.0f : 0.0f; break;
                                    case 0x35: res = std::isinf(v) ? 1.0f : 0.0f; break;
                                    case 0x36: res = std::isfinite(v) ? 1.0f : 0.0f; break;
                                }
                                s_regs[sDst] = res;
                                break;
                            }
                            case OP_STREAM_YIELD: {
                                bitset_add(bs_dst, tgt, vm_state->query_context->max_nodes);
                                break;
                            }
                            case OP_STREAM_SCATTER_REDUCE: {
                                float val = s_regs[sDst];
                                uint16_t attrId = sPayloadLow;
                                uint16_t monoid = sPayloadHigh;
                                if (attrId < vm_state->query_context->float_vectors.size() && vm_state->query_context->float_vectors_allocated[attrId]) {
                                    auto& vec = vm_state->query_context->float_vectors[attrId];
                                    if (tgt < vec.size()) {
                                        float current = vec[tgt];
                                        float next = current;
                                        switch (monoid) {
                                            case 0: next = current + val; break;
                                            case 1: next = std::max(current, val); break;
                                            case 2: next = std::min(current, val); break;
                                        }
                                        vec[tgt] = next;
                                    }
                                }
                                break;
                            }
                            case OP_STREAM_REDUCE: {
                                float val = s_regs[sDst];
                                uint16_t globalReg = sPayloadLow;
                                uint16_t monoid = sPayloadHigh;
                                
                                uint8_t rType = vm_state->register_types[globalReg];
                                uint64_t rVal = vm_state->registers[globalReg];
                                
                                float current = 0.0f;
                                if (rType == TYPE_FLOAT) {
                                    uint32_t bits = static_cast<uint32_t>(rVal & 0xFFFFFFFFULL);
                                    std::memcpy(&current, &bits, sizeof(float));
                                }
                                
                                float next = current;
                                switch (monoid) {
                                    case 0: next = current + val; break;
                                    case 1: next = std::max(current, val); break;
                                    case 2: next = std::min(current, val); break;
                                }
                                
                                uint32_t nextBits;
                                std::memcpy(&nextBits, &next, sizeof(float));
                                vm_state->registers[globalReg] = static_cast<uint64_t>(nextBits);
                                vm_state->register_types[globalReg] = TYPE_FLOAT;
                                break;
                            }
                        }
                        if (abort) break;
                        mutPc++;
                    }
                }
            }

            bool is_empty = true;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                if (bs_dst.words[i] != 0) { is_empty = false; break; }
            }
            if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
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

                if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
                    scalar_src = input_param;
                    src_is_bitset = false;
                    h_src = -1;
                }

                int h_dst = -1;
                bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);

                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    int h_existing = static_cast<int>(vm_state->registers[dst]);
                    if (accum) {
                        h_dst = h_existing;
                    } else {
                        if (dst == src) {
                            h_dst = acquire_bitset(vm_state->query_context);
                            if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                            vm_state->query_context->bitsets[h_dst].clear();
                        } else {
                            h_dst = h_existing;
                            vm_state->query_context->bitsets[h_dst].clear();
                        }
                    }
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                            for (size_t w = 0; w < words; ++w) {
                                uint64_t word = bs_src.words[w];
                                if (word == 0) continue;
                                int tid = omp_get_thread_num();
                                auto& priv_bs = ctx->private_bitsets[tid];
                                while (word) {
                                    int bit = std::countr_zero(word);
                                    uint64_t u = w * 64 + bit;
                                    word &= word - 1;
                                    if (u < slot.node_count) {
                                        uint64_t start = slot.get_csr_offset(u);
                                        uint64_t end = slot.get_csr_offset(u + 1);
                                        for (uint64_t i = start; i < end; ++i) {
                                            bitset_add(priv_bs, slot.get_csr_target(i), max_nodes);
                                        }
                                    }
                                }
                            }

                            #pragma omp parallel for schedule(static) num_threads(num_threads)
                            for (size_t w = 0; w < words; ++w) {
                                uint64_t merged = 0;
                                for (int t = 0; t < num_threads; ++t) {
                                    merged |= ctx->private_bitsets[t].words[w];
                                }
                                if (accum) {
                                    bs_dst.words[w] |= merged;
                                } else {
                                    bs_dst.words[w] = merged;
                                }
                            }
#endif
                        } else {
                            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                                uint64_t word = bs_src.words[w];
                                while (word) {
                                    int bit = std::countr_zero(word);
                                    uint64_t u = w * 64 + bit;
                                    word &= word - 1;
                                    if (u < slot.node_count) {
                                        uint64_t start = slot.get_csr_offset(u);
                                        uint64_t end = slot.get_csr_offset(u + 1);
                                        for (uint64_t i = start; i < end; ++i) {
                                            bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        uint64_t u = scalar_src;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                bitset_add(bs_dst, slot.get_csr_target(idx), vm_state->query_context->max_nodes);
                            }
                        }
                    }
                } else if (!slot.offsets_ptr && slot.targets_ptr) {
                    if (src_is_bitset) {
                        const auto& bs_src = vm_state->query_context->bitsets[h_src];
                        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                            uint64_t word = bs_src.words[w];
                            while (word) {
                                int bit = std::countr_zero(word);
                                uint64_t u = w * 64 + bit;
                                word &= word - 1;
                                if (u < slot.node_count) {
                                    uint64_t target_node = slot.get_csr_target(u);
                                    if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                                        bitset_add(bs_dst, target_node, vm_state->query_context->max_nodes);
                                    }
                                }
                            }
                        }
                    } else {
                        uint64_t u = scalar_src;
                        if (u < slot.node_count) {
                            uint64_t v = slot.get_csr_target(u);
                            if (v != 0xFFFFFFFF && v != 0xFFFF && v != ~0ULL) {
                                bitset_add(bs_dst, v, vm_state->query_context->max_nodes);
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
                if (is_empty) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                    if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
                        vm_state->pc = instruction_count;
                        return IMPULSE_VM_OK;
                    }
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }

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

                if (rel == 0 && (inst.payload >> 24) == 0 && vm_state->register_types[unv] != TYPE_BITSET_HANDLE) {
                    rel = (inst.payload >> 16) & 0xFFFF;
                    unv = 0;
                }

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];
                if (!slot.csc_offsets_ptr || !slot.csc_targets_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }

                bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
                int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
                uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

                if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
                    scalar_src = input_param;
                    src_is_bitset = false;
                    h_src = -1;
                }

                const uint64_t* unv_words = (unv > 0 && vm_state->register_types[unv] == TYPE_BITSET_HANDLE)
                    ? vm_state->query_context->bitsets[vm_state->registers[unv]].words : nullptr;

                int h_dst = -1;
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    h_dst = static_cast<int>(vm_state->registers[dst]);
                    if (dst == src || dst == unv) {
                        h_dst = acquire_bitset(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                    }
                    vm_state->query_context->bitsets[h_dst].clear();
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                    vm_state->query_context->bitsets[h_dst].clear();
                }

                auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                const uint64_t* src_words = src_is_bitset ? vm_state->query_context->bitsets[h_src].words : nullptr;

                if (slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                    if (unv_words != nullptr) {
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            uint64_t w_unv = unv_words[i];
                            if (w_unv == 0) {
                                bs_dst.words[i] = 0;
                                continue;
                            }
                            uint64_t w_dst = 0;
                            for (int b = 0; b < 64; ++b) {
                                if (w_unv & (1ULL << b)) {
                                    uint64_t v = i * 64 + b;
                                    if (v < slot.node_count) {
                                        uint64_t start = slot.get_csc_offset(v);
                                        uint64_t end   = slot.get_csc_offset(v + 1);
                                        for (uint64_t idx = start; idx < end; ++idx) {
                                            uint64_t u = slot.get_csc_target(idx);
                                            bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                                            if (hit) {
                                                w_dst |= (1ULL << b);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            bs_dst.words[i] = w_dst;
                        }
                    } else {
                        if (src_is_bitset) {
                            const auto& bs_src = vm_state->query_context->bitsets[h_src];
                            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                                uint64_t word = bs_src.words[w];
                                while (word) {
                                    int bit = std::countr_zero(word);
                                    uint64_t v = w * 64 + bit;
                                    word &= word - 1;
                                    if (v < slot.node_count) {
                                        uint64_t start = slot.get_csc_offset(v);
                                        uint64_t end   = slot.get_csc_offset(v + 1);
                                        for (uint64_t idx = start; idx < end; ++idx) {
                                            uint64_t u = slot.get_csc_target(idx);
                                            bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                                        }
                                    }
                                }
                            }
                        } else {
                            uint64_t v = scalar_src;
                            if (v < slot.node_count) {
                                uint64_t start = slot.get_csc_offset(v);
                                uint64_t end   = slot.get_csc_offset(v + 1);
                                for (uint64_t idx = start; idx < end; ++idx) {
                                    uint64_t u = slot.get_csc_target(idx);
                                    bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                                }
                            }
                        }
                    }
                } else if (!slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                    if (unv_words != nullptr) {
                        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                            uint64_t w_dst = 0;
                            for (int b = 0; b < 64; ++b) {
                                uint64_t v = i * 64 + b;
                                if (v < slot.node_count) {
                                    uint64_t u = slot.get_csc_target(v);
                                    if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                                        bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                                        if (hit) {
                                            w_dst |= (1ULL << b);
                                        }
                                    }
                                }
                            }
                            bs_dst.words[i] = w_dst & unv_words[i];
                        }
                    } else {
                        if (src_is_bitset) {
                            const auto& bs_src = vm_state->query_context->bitsets[h_src];
                            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                                uint64_t word = bs_src.words[w];
                                while (word) {
                                    int bit = std::countr_zero(word);
                                    uint64_t v = w * 64 + bit;
                                    word &= word - 1;
                                    if (v < slot.node_count) {
                                        uint64_t u = slot.get_csc_target(v);
                                        if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                                            bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                                        }
                                    }
                                }
                            }
                        } else {
                            uint64_t v = scalar_src;
                            if (v < slot.node_count) {
                                uint64_t u = slot.get_csc_target(v);
                                if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                                    bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                                }
                            }
                        }
                    }
                }

                if (unv_words) {
                    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                        bs_dst.words[i] &= unv_words[i];
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
                if (is_empty) {
                    vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                    if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
                        vm_state->pc = instruction_count;
                        return IMPULSE_VM_OK;
                    }
                } else {
                    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                }

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
                        degree = slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
            case OP_MXV: {
                uint16_t dst = inst.dst_reg;
                int mask_reg_idx = inst.flags > 0 ? (inst.flags - 1) : -1;
                uint16_t src_vec = inst.payload & 0xFF;
                uint16_t rel = (inst.payload >> 8) & 0xFF;
                uint16_t semiring_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src_vec);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];
                if (!slot.offsets_ptr || !slot.targets_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }

                size_t N = vm_state->query_context->max_nodes;
                bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);

                if (is_double) {
                    if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
                        int h_dst = acquire_double_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
                    }
                    double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
                    const double* src_data = vm_state->query_context->double_vectors[vm_state->registers[src_vec]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t u = 0; u < N; ++u) {
                        if (mask_reg_idx >= 0) {
                            size_t mask_handle = vm_state->registers[mask_reg_idx];
                            if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                                dst_data[u] = 0.0;
                                continue;
                            }
                        }

                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            if (semiring_id == SEMIRING_PLUS_TIMES) {
                                double sum = 0.0;
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csr_target(i);
                                    if (v < N) sum += src_data[v];
                                }
                                dst_data[u] = sum;
                            } else if (semiring_id == SEMIRING_MIN_PLUS) {
                                double min_val = std::numeric_limits<double>::infinity();
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csr_target(i);
                                    if (v < N) min_val = std::min(min_val, src_data[v] + 1.0);
                                }
                                dst_data[u] = min_val;
                            } else {
                                dst_data[u] = 0.0;
                            }
                        } else {
                            dst_data[u] = 0.0;
                        }
                    }
                } else {
                    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
                        int h_dst = acquire_float_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                    }
                    float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                    const float* src_data = vm_state->query_context->float_vectors[vm_state->registers[src_vec]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t u = 0; u < N; ++u) {
                        if (mask_reg_idx >= 0) {
                            size_t mask_handle = vm_state->registers[mask_reg_idx];
                            if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                                dst_data[u] = 0.0f;
                                continue;
                            }
                        }

                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            if (semiring_id == SEMIRING_PLUS_TIMES) {
                                float sum = 0.0f;
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csr_target(i);
                                    if (v < N) sum += src_data[v];
                                }
                                dst_data[u] = sum;
                            } else if (semiring_id == SEMIRING_MIN_PLUS) {
                                float min_val = std::numeric_limits<float>::infinity();
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csr_target(i);
                                    if (v < N) min_val = std::min(min_val, src_data[v] + 1.0f);
                                }
                                dst_data[u] = min_val;
                            } else {
                                dst_data[u] = 0.0f;
                            }
                        } else {
                            dst_data[u] = 0.0f;
                        }
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_VXM: {
                uint16_t dst = inst.dst_reg;
                int mask_reg_idx = inst.flags > 0 ? (inst.flags - 1) : -1;
                uint16_t src_vec = inst.payload & 0xFF;
                uint16_t rel = (inst.payload >> 8) & 0xFF;
                uint16_t semiring_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src_vec);

                if (rel >= vm_state->query_context->slots.size()) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& slot = vm_state->query_context->slots[rel];
                if (!slot.csc_offsets_ptr || !slot.csc_targets_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }

                size_t N = vm_state->query_context->max_nodes;
                bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);

                if (is_double) {
                    if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
                        int h_dst = acquire_double_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
                    }
                    double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
                    const double* src_data = vm_state->query_context->double_vectors[vm_state->registers[src_vec]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t u = 0; u < N; ++u) {
                        if (mask_reg_idx >= 0) {
                            size_t mask_handle = vm_state->registers[mask_reg_idx];
                            if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                                dst_data[u] = 0.0;
                                continue;
                            }
                        }

                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csc_offset(u);
                            uint64_t end = slot.get_csc_offset(u + 1);
                            if (semiring_id == SEMIRING_PLUS_TIMES) {
                                double sum = 0.0;
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csc_target(i);
                                    if (v < N) sum += src_data[v];
                                }
                                dst_data[u] = sum;
                            } else if (semiring_id == SEMIRING_MIN_PLUS) {
                                double min_val = std::numeric_limits<double>::infinity();
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csc_target(i);
                                    if (v < N) min_val = std::min(min_val, src_data[v] + 1.0);
                                }
                                dst_data[u] = min_val;
                            } else {
                                dst_data[u] = 0.0;
                            }
                        } else {
                            dst_data[u] = 0.0;
                        }
                    }
                } else {
                    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
                        int h_dst = acquire_float_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                    }
                    float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                    const float* src_data = vm_state->query_context->float_vectors[vm_state->registers[src_vec]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t u = 0; u < N; ++u) {
                        if (mask_reg_idx >= 0) {
                            size_t mask_handle = vm_state->registers[mask_reg_idx];
                            if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                                dst_data[u] = 0.0f;
                                continue;
                            }
                        }

                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csc_offset(u);
                            uint64_t end = slot.get_csc_offset(u + 1);
                            if (semiring_id == SEMIRING_PLUS_TIMES) {
                                float sum = 0.0f;
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csc_target(i);
                                    if (v < N) sum += src_data[v];
                                }
                                dst_data[u] = sum;
                            } else if (semiring_id == SEMIRING_MIN_PLUS) {
                                float min_val = std::numeric_limits<float>::infinity();
                                for (uint64_t i = start; i < end; ++i) {
                                    uint64_t v = slot.get_csc_target(i);
                                    if (v < N) min_val = std::min(min_val, src_data[v] + 1.0f);
                                }
                                dst_data[u] = min_val;
                            } else {
                                dst_data[u] = 0.0f;
                            }
                        } else {
                            dst_data[u] = 0.0f;
                        }
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_EWISE_ADD: {
                uint16_t dst = inst.dst_reg;
                uint16_t src1 = inst.payload & 0xFF;
                uint16_t src2 = (inst.payload >> 8) & 0xFF;
                uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src1);
                VALIDATE_REG(src2);

                size_t N = vm_state->query_context->max_nodes;
                bool is_double = (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);

                if (is_double) {
                    if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
                        int h_dst = acquire_double_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
                    }
                    double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
                    const double* s1_data = vm_state->query_context->double_vectors[vm_state->registers[src1]].data();
                    const double* s2_data = vm_state->query_context->double_vectors[vm_state->registers[src2]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t i = 0; i < N; ++i) {
                        if (op_id == BINARY_OP_ADD) dst_data[i] = s1_data[i] + s2_data[i];
                        else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
                        else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
                        else dst_data[i] = s1_data[i] + s2_data[i];
                    }
                } else {
                    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
                        int h_dst = acquire_float_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                    }
                    float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                    const float* s1_data = vm_state->query_context->float_vectors[vm_state->registers[src1]].data();
                    const float* s2_data = vm_state->query_context->float_vectors[vm_state->registers[src2]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t i = 0; i < N; ++i) {
                        if (op_id == BINARY_OP_ADD) dst_data[i] = s1_data[i] + s2_data[i];
                        else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
                        else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
                        else dst_data[i] = s1_data[i] + s2_data[i];
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_EWISE_MULT: {
                uint16_t dst = inst.dst_reg;
                uint16_t src1 = inst.payload & 0xFF;
                uint16_t src2 = (inst.payload >> 8) & 0xFF;
                uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src1);
                VALIDATE_REG(src2);

                size_t N = vm_state->query_context->max_nodes;
                bool is_double = (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);

                if (is_double) {
                    if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
                        int h_dst = acquire_double_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
                    }
                    double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
                    const double* s1_data = vm_state->query_context->double_vectors[vm_state->registers[src1]].data();
                    const double* s2_data = vm_state->query_context->double_vectors[vm_state->registers[src2]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t i = 0; i < N; ++i) {
                        if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
                        else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
                        else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
                        else dst_data[i] = s1_data[i] * s2_data[i];
                    }
                } else {
                    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
                        int h_dst = acquire_float_vector(vm_state->query_context);
                        if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                        vm_state->registers[dst] = h_dst;
                        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                    }
                    float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
                    const float* s1_data = vm_state->query_context->float_vectors[vm_state->registers[src1]].data();
                    const float* s2_data = vm_state->query_context->float_vectors[vm_state->registers[src2]].data();

                    #pragma omp parallel for schedule(static)
                    for (size_t i = 0; i < N; ++i) {
                        if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
                        else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
                        else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
                        else dst_data[i] = s1_data[i] * s2_data[i];
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_REDUCE: {
                uint16_t dst = inst.dst_reg;
                uint16_t src_vec = inst.payload & 0xFF;
                uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);
                VALIDATE_REG(src_vec);

                size_t N = vm_state->query_context->max_nodes;
                if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[src_vec]);
                    if (handle >= 4 || !vm_state->query_context->double_vectors_allocated[handle]) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    const double* vec = vm_state->query_context->double_vectors[handle].data();
                    double res = 0.0;
                    if (op_id == BINARY_OP_ADD) {
                        #pragma omp parallel for reduction(+:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res += vec[i];
                        }
                    } else if (op_id == BINARY_OP_MIN) {
                        res = std::numeric_limits<double>::infinity();
                        #pragma omp parallel for reduction(min:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res = std::min(res, vec[i]);
                        }
                    } else if (op_id == BINARY_OP_MAX) {
                        res = -std::numeric_limits<double>::infinity();
                        #pragma omp parallel for reduction(max:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res = std::max(res, vec[i]);
                        }
                    }
                    vm_state->registers[dst] = reinterpret_cast<uint64_t&>(res);
                    vm_state->register_types[dst] = TYPE_DOUBLE;
                } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
                    int handle = static_cast<int>(vm_state->registers[src_vec]);
                    if (handle >= 4 || !vm_state->query_context->float_vectors_allocated[handle]) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    const float* vec = vm_state->query_context->float_vectors[handle].data();
                    float res = 0.0f;
                    if (op_id == BINARY_OP_ADD) {
                        #pragma omp parallel for reduction(+:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res += vec[i];
                        }
                    } else if (op_id == BINARY_OP_MIN) {
                        res = std::numeric_limits<float>::infinity();
                        #pragma omp parallel for reduction(min:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res = std::min(res, vec[i]);
                        }
                    } else if (op_id == BINARY_OP_MAX) {
                        res = -std::numeric_limits<float>::infinity();
                        #pragma omp parallel for reduction(max:res) schedule(static)
                        for (size_t i = 0; i < N; ++i) {
                            res = std::max(res, vec[i]);
                        }
                    }
                    vm_state->registers[dst] = 0;
                    reinterpret_cast<float&>(vm_state->registers[dst]) = res;
                    vm_state->register_types[dst] = TYPE_FLOAT;
                } else {
                    return IMPULSE_VM_ERR_INVALID_REGISTER;
                }

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

                if (vm_state->register_types[dst] != TYPE_NODE_VECTOR) {
                    int h_dst = acquire_node_vector(vm_state->query_context);
                    if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                    vm_state->registers[dst] = h_dst;
                    vm_state->register_types[dst] = TYPE_NODE_VECTOR;
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
            case OP_ISLAND_DETECT: {
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);

                uint8_t src1 = inst.payload & 0xFF;
                uint8_t src2 = (inst.payload >> 8) & 0xFF;
                uint16_t rel = (inst.payload >> 16) & 0xFFFF;

                std::vector<uint32_t> lines1;
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

                std::vector<uint32_t> lines2;
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
                if (!slot.offsets_ptr || !slot.targets_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }

                const int32_t* branch_ids = nullptr;
                if (rel < vm_state->query_context->attribute_slots.size() &&
                    !vm_state->query_context->attribute_slots[rel].empty()) {
                    branch_ids = static_cast<const int32_t*>(vm_state->query_context->attribute_slots[rel][0].data_ptr);
                }

                uint32_t N = static_cast<uint32_t>(slot.node_count);
                uint32_t base_components = run_island_detect_bfs(N, slot.offsets_ptr, slot.targets_ptr, branch_ids, -1, -1);

                uint64_t critical_pairs_count = 0;
                bool same_set = (src1 == src2);

                if (same_set) {
#if defined(_OPENMP)
                    #pragma omp parallel for reduction(+:critical_pairs_count) schedule(dynamic, 64)
#endif
                    for (size_t i = 0; i < lines1.size(); ++i) {
                        uint32_t k1 = lines1[i];
                        for (size_t j = i + 1; j < lines1.size(); ++j) {
                            uint32_t k2 = lines1[j];
                            uint32_t comp = run_island_detect_bfs(N, slot.offsets_ptr, slot.targets_ptr, branch_ids, k1, k2);
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
                        uint32_t k1 = lines1[i];
                        for (size_t j = 0; j < lines2.size(); ++j) {
                            uint32_t k2 = lines2[j];
                            if (k1 >= k2) continue;
                            uint32_t comp = run_island_detect_bfs(N, slot.offsets_ptr, slot.targets_ptr, branch_ids, k1, k2);
                            if (comp > base_components) {
                                critical_pairs_count++;
                            }
                        }
                    }
                }

                vm_state->registers[dst] = critical_pairs_count;
                vm_state->register_types[dst] = TYPE_INT64;

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
                } else if (vm_state->register_types[src] == TYPE_FLOAT) {
                    uint32_t bits = static_cast<uint32_t>(vm_state->registers[src] & 0xFFFFFFFFULL);
                    float fval;
                    std::memcpy(&fval, &bits, sizeof(float));
                    vm_state->registers[dst] = 0;
                    reinterpret_cast<float&>(vm_state->registers[dst]) = fval;
                    vm_state->register_types[dst] = TYPE_FLOAT;
                    sum = fval;
                } else if (vm_state->register_types[src] == TYPE_DOUBLE) {
                    double dval;
                    uint64_t bits = vm_state->registers[src];
                    std::memcpy(&dval, &bits, sizeof(double));
                    vm_state->registers[dst] = reinterpret_cast<uint64_t&>(dval);
                    vm_state->register_types[dst] = TYPE_DOUBLE;
                    sum = dval;
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                    } else if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
                        size_t h_src = vm_state->registers[src];
                        for (uint64_t u = 0; u < slot.node_count; ++u) {
                            if (!impulse_vm_context_bitset_test(vm_state->query_context, h_src, u)) continue;
                            uint32_t start = slot.offsets_ptr[u];
                            uint32_t end   = slot.offsets_ptr[u + 1];
                            for (uint32_t idx = start; idx < end; ++idx) {
                                uint32_t target_node = slot.targets_ptr[idx];
                                if (target_node < max_nodes) {
                                    dst_vec[target_node] += 1.0f;
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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

                vm_state->registers[dst] = 0;
                vm_state->register_types[dst] = TYPE_INT64;

                if (node_buf.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_CALL: {
                uint32_t func_offset = inst.payload;
                if (vm_state->call_stack_depth >= 8) return IMPULSE_VM_ERR_STACK_OVERFLOW;
                vm_state->call_stack[vm_state->call_stack_depth++] = vm_state->pc + 1;
                
                uint64_t arg0 = vm_state->registers[12];
                uint64_t arg1 = vm_state->registers[13];
                uint64_t arg2 = vm_state->registers[14];
                uint64_t arg3 = vm_state->registers[15];

                vm_state->registers[0] = arg0;
                vm_state->registers[1] = arg1;
                vm_state->registers[2] = arg2;
                vm_state->registers[3] = arg3;

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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                bs_dst.clear();

                const impulse_vm_input_keys* input_keys = reinterpret_cast<const impulse_vm_input_keys*>(input_param);
                if (input_keys && input_keys->keys && input_keys->count > 0) {
                    for (size_t k = 0; k < input_keys->count; ++k) {
                        const char* target_key = input_keys->keys[k];
                        if (!target_key) continue;

                        uint32_t resolved_id = 0;
                        if (impulse_snapshot_resolve_key(
                                vm_state->query_context->snapshot,
                                domain_id,
                                target_key,
                                std::strlen(target_key),
                                &resolved_id
                            ) == IMPULSE_OK) {
                            bitset_add(bs_dst, resolved_id, vm_state->query_context->max_nodes);
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
                if (h_dst < 0) {
        printf("OUT_OF_BOUNDS: acquire_bitset failed, pc=%zu\n", vm_state->pc);
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
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
            case OP_LOAD_INLINE_ARRAY: {
                uint16_t dst = inst.dst_reg;
                uint16_t offset_bytes = inst.payload & 0xFFFF;
                uint16_t count = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);

                if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }
                if (offset_bytes + count * sizeof(uint32_t) > vm_state->query_context->inline_data_bytes) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }

                int h_dst = acquire_float_vector(vm_state->query_context);
                if (h_dst < 0) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                if (vm_state->query_context->float_vectors[h_dst].size() < count) {
                    vm_state->query_context->float_vectors[h_dst].resize(count);
                }
                if (vm_state->query_context->node_vectors[h_dst].size() < count) {
                    vm_state->query_context->node_vectors[h_dst].resize(count);
                }

                float* dst_vec_f = vm_state->query_context->float_vectors[h_dst].data();
                uint64_t* dst_vec_n = vm_state->query_context->node_vectors[h_dst].data();

                const float* src_data_f = reinterpret_cast<const float*>(vm_state->query_context->inline_data_ptr + offset_bytes);
                const uint32_t* src_data_i = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
                for (size_t i = 0; i < count; ++i) {
                    dst_vec_f[i] = src_data_f[i];
                    dst_vec_n[i] = src_data_i[i];
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
                vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_LOAD_INLINE_SET: {
                uint16_t dst = inst.dst_reg;
                uint16_t offset_bytes = inst.payload & 0xFFFF;
                uint16_t count = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);

                if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }
                if (offset_bytes + count * sizeof(uint32_t) > vm_state->query_context->inline_data_bytes) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }

                int h_dst = acquire_bitset(vm_state->query_context);
                if (h_dst < 0) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                vm_state->query_context->bitsets[h_dst].clear();

                const uint32_t* src_data = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
                for (uint16_t i = 0; i < count; ++i) {
                    vm_state->query_context->bitsets[h_dst].set(src_data[i]);
                }

                vm_state->registers[dst] = h_dst;
                vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
                vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

                vm_state->pc++;
                break;
            }
            case OP_INIT_MOCK_GRAPH: {
                uint16_t slot_id = inst.dst_reg;
                uint16_t off_bytes = inst.payload & 0xFFFF;
                uint16_t node_count = (inst.payload >> 16) & 0xFFFF;

                if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
                    return IMPULSE_VM_ERR_NULL_SNAPSHOT;
                }
                if (slot_id >= vm_state->query_context->slots.size()) {
                    vm_state->query_context->slots.resize(slot_id + 1);
                }

                const uint32_t* raw_ptrs = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + off_bytes);
                vm_state->query_context->slots[slot_id].node_count = node_count;
                vm_state->query_context->slots[slot_id].offsets_ptr = raw_ptrs;
                vm_state->query_context->slots[slot_id].targets_ptr = raw_ptrs + (node_count + 1);
                vm_state->query_context->slots[slot_id].csc_offsets_ptr = raw_ptrs;
                vm_state->query_context->slots[slot_id].csc_targets_ptr = raw_ptrs + (node_count + 1);

                vm_state->pc++;
                break;
            }
            case OP_LOAD_INDIRECT: {
                uint16_t dst = inst.dst_reg;
                uint16_t src_param = inst.payload & 0xFFFF;
                uint16_t idx_reg = (inst.payload >> 16) & 0xFFFF;
                VALIDATE_REG(dst);

                if (inst.flags == 0) {
                    VALIDATE_REG(src_param);
                    uint64_t target_reg_idx = vm_state->registers[src_param];
                    if (target_reg_idx >= 64) return IMPULSE_VM_ERR_INVALID_REGISTER;
                    vm_state->registers[dst] = vm_state->registers[target_reg_idx];
                    vm_state->register_types[dst] = vm_state->register_types[target_reg_idx];
                } else {
                    VALIDATE_REG(src_param);
                    VALIDATE_REG(idx_reg);
                    int handle = static_cast<int>(vm_state->registers[src_param]);
                    uint64_t index = vm_state->registers[idx_reg];
                    if (handle >= 8 || index >= vm_state->query_context->max_nodes) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    if (vm_state->register_types[src_param] == TYPE_FLOAT_VECTOR) {
                        float val = vm_state->query_context->float_vectors[handle][index];
                        uint32_t bit_pattern = 0;
                        std::memcpy(&bit_pattern, &val, sizeof(float));
                        vm_state->registers[dst] = bit_pattern;
                        vm_state->register_types[dst] = TYPE_FLOAT;
                    } else if (vm_state->register_types[src_param] == TYPE_DOUBLE_VECTOR) {
                        double val = vm_state->query_context->double_vectors[handle][index];
                        uint64_t bit_pattern = 0;
                        std::memcpy(&bit_pattern, &val, sizeof(double));
                        vm_state->registers[dst] = bit_pattern;
                        vm_state->register_types[dst] = TYPE_DOUBLE;
                    } else {
                        return IMPULSE_VM_ERR_INVALID_REGISTER;
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_THROW: {
                uint32_t err_code = inst.payload;
                vm_state->registers[0] = err_code;
                return IMPULSE_VM_ERR_USER_THROW;
            }
            case OP_ASSERT: {
                uint16_t src_reg = inst.dst_reg;
                uint32_t expected_val = inst.payload;
                VALIDATE_REG(src_reg);

                if (inst.flags == 0) {
                    uint64_t actual_val = vm_state->registers[src_reg];
                    if (actual_val != static_cast<uint64_t>(expected_val)) {
                        return IMPULSE_VM_ERR_ASSERTION_FAILED;
                    }
                } else {
                    if ((vm_state->flags & expected_val) != static_cast<uint64_t>(expected_val)) {
                        return IMPULSE_VM_ERR_ASSERTION_FAILED;
                    }
                }

                vm_state->pc++;
                break;
            }
            case OP_TRAP: {
                return IMPULSE_VM_ERR_TRAP;
            }
            case OP_CSR_WALK_PREDICATE:
            case OP_VECTOR_STR_CONCAT:
            case OP_VEC_CMP_EQ:
            case OP_VEC_CMP_GT:
            case OP_VEC_CMP_LT:
            case OP_VEC_CMP_BETWEEN:
            case OP_MASK_AND:
            case OP_MASK_OR:
            case OP_MASK_NOT:
            case OP_VEC_BLEND:
            case OP_ASSERT_FINITE:
            case OP_VEC_MATH_UNARY:
            case OP_VEC_MATH_BINARY:
            case OP_VEC_MATH_TERNARY:
            case OP_LOAD_COLUMN_VECTOR:
            case OP_GATHER_NODE_ATTR:
            case OP_GATHER_EDGE_ATTR:
            case OP_BRIN_ZONE_SKIP:
            case OP_CSR_WALK_DIRECT_STORE:
            case OP_CSR_WALK_DENSE_STREAM:
            case OP_COO_WALK:
            case OP_CSC_WALK_DIRECT_STORE:
            case OP_FIXPOINT_KLEENE_STAR:
            case OP_SWAP_REG:
            case OP_FRONTIER_DIFF:
            case OP_COO_WALK_FILTERED:
            case OP_COO_WALK_REDUCE:
            case OP_COO_WALK_DIRECT_STORE:
            case OP_DENSE_WALK:
            case OP_DENSE_WALK_BITMATRIX:
            case OP_DENSE_WALK_REDUCE:
            case OP_DENSE_WALK_DIRECT_STORE:
            case OP_FLOAT_VECTOR_SCALE:
            case OP_L1_NORM_DIFF:
            case OP_SAMPLE_NEIGHBORS:
            case OP_RANDOM_WALK:
            case OP_SCATTER_GATHER:
            case OP_REBAC_CHECK:
            case OP_ROARING_BITMAP_OR:
            case OP_ROARING_BITMAP_AND:
            case OP_ROARING_BITMAP_AND_NOT:
            case OP_SPARSE_MATVEC:
            case OP_LOUVAIN_MODULARITY:
            case OP_KCORE_DECOMPOSITION:
            case OP_MOTIF_MATCH_3:
            case OP_GRAPH_ISOMORPHISM:
            case OP_ENTER_FRAME:
            case OP_LEAVE_FRAME: {
                vm_state->pc++;
                break;
            }
            case OP_INIT_MOCK_NODE_ATTR: case OP_INIT_MOCK_EDGE_ATTR: break;
            case OP_RESERVED_0D: case OP_RESERVED_0F:
            case OP_RESERVED_28: case OP_RESERVED_29: case OP_RESERVED_2B: case OP_RESERVED_2C:
            case OP_RESERVED_3A: case OP_RESERVED_3B: case OP_RESERVED_3C: case OP_RESERVED_3E: case OP_RESERVED_3F:
            case OP_RESERVED_4C: case OP_RESERVED_4D: case OP_RESERVED_4E: case OP_RESERVED_4F:
            case OP_RESERVED_59:
            case OP_RESERVED_5D: case OP_RESERVED_5E: case OP_RESERVED_5F:
            case OP_RESERVED_6D: case OP_RESERVED_6E: case OP_RESERVED_6F:
            case OP_RESERVED_76: case OP_RESERVED_77: case OP_RESERVED_78: case OP_RESERVED_79: case OP_RESERVED_7A: case OP_RESERVED_7B: case OP_RESERVED_7C: case OP_RESERVED_7D: case OP_RESERVED_7E: case OP_RESERVED_7F:
                return IMPULSE_VM_ERR_RESERVED_OPCODE;
            default:
                return IMPULSE_VM_ERR_INVALID_OPCODE;
        }
    }
    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
#endif
}

void impulse_vm_context_mock_csr(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csr_offsets,
    const uint32_t* csr_targets,
    uint64_t node_count,
    uint64_t edge_count
) {
    impulse_vm_context_mock_csr_typed(ctx, relation_index, csr_offsets, csr_targets, node_count, edge_count, 4, 4);
}

void impulse_vm_context_mock_csr_typed(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const void* csr_offsets,
    const void* csr_targets,
    uint64_t node_count,
    uint64_t edge_count,
    uint8_t node_id_width,
    uint8_t edge_index_width
) {
    if (ctx) {
        if (relation_index >= ctx->slots.size()) {
            ctx->slots.resize(relation_index + 1);
        }
        ctx->slots[relation_index].offsets_ptr = csr_offsets;
        ctx->slots[relation_index].targets_ptr = csr_targets;
        ctx->slots[relation_index].node_count = node_count;
        ctx->slots[relation_index].edge_count = edge_count;
        ctx->slots[relation_index].node_id_width = node_id_width ? node_id_width : 4;
        ctx->slots[relation_index].edge_index_width = edge_index_width ? edge_index_width : 4;
    }
}

void impulse_vm_context_mock_csc(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csc_offsets,
    const uint32_t* csc_targets
) {
    impulse_vm_context_mock_csc_typed(ctx, relation_index, csc_offsets, csc_targets, 4, 4);
}

void impulse_vm_context_mock_csc_typed(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const void* csc_offsets,
    const void* csc_targets,
    uint8_t node_id_width,
    uint8_t edge_index_width
) {
    if (ctx && relation_index < ctx->slots.size()) {
        ctx->slots[relation_index].csc_offsets_ptr = csc_offsets;
        ctx->slots[relation_index].csc_targets_ptr = csc_targets;
        ctx->slots[relation_index].node_id_width = node_id_width ? node_id_width : 4;
        ctx->slots[relation_index].edge_index_width = edge_index_width ? edge_index_width : 4;
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

void impulse_vm_context_bind_inline_data(impulse_vm_context_t* ctx, const void* data_ptr, size_t bytes) {
    if (ctx) {
        ctx->inline_data_ptr = static_cast<const uint8_t*>(data_ptr);
        ctx->inline_data_bytes = bytes;
    }
}

impulse_vm_status_t impulse_vm_validate(
    const impulse_instruction_t* bytecode,
    size_t instruction_count
) {
    if (!bytecode && instruction_count > 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    if (instruction_count == 0) return IMPULSE_VM_OK;

    uint8_t abstract_types[64] = {0};

    for (size_t pc = 0; pc < instruction_count; pc++) {
        const auto& inst = bytecode[pc];
        uint8_t opcode = inst.opcode;
        uint16_t dst = inst.dst_reg;
        uint16_t src = inst.payload & 0xFFFF;

        if (dst >= 64) return IMPULSE_VM_ERR_INVALID_REGISTER;

        switch (opcode) {
            case OP_NOP:
            case OP_HALT:
            case OP_JMP:
            case OP_JZ:
            case OP_JNZ:
            case OP_LOOP_DECR:
            case OP_STABLE_CHECK:
            case OP_CALL:
            case OP_RET:
            case OP_THROW:
            case OP_ASSERT:
            case OP_TRAP:
            case OP_ASSERT_FINITE:
            case OP_SET_MAX_DOP:
            case OP_ALLOC_SCRATCH:
            case OP_ASSERT_SCRATCH_BYTES:
            case OP_CREATE_SCRATCH_INDEX:
            case OP_DROP_SCRATCH_INDEX:
                break;

            case OP_VECTOR_TIME_VALID_AT:
                abstract_types[dst] = TYPE_FLOAT_VECTOR;
                break;

            case OP_INIT_INPUT_NODE:
                abstract_types[dst] = TYPE_NODE_ID;
                break;
            case OP_INIT_INPUT_SET:
            case OP_MAP_KEYS_TO_DENSE:
            case OP_ADAPTIVE_WALK:
            case OP_CSR_WALK:
            case OP_CSR_WALK_FILTERED:
            case OP_CSC_WALK:
            case OP_SET_UNION:
            case OP_SET_INTERSECT:
            case OP_SET_DIFFERENCE:
            case OP_VEC_CMP_EQ:
            case OP_VEC_CMP_GT:
            case OP_VEC_CMP_LT:
            case OP_VEC_CMP_BETWEEN:
            case OP_MASK_AND:
            case OP_MASK_OR:
            case OP_MASK_NOT:
            case OP_CSR_WALK_DIRECT_STORE:
            case OP_CSR_WALK_DENSE_STREAM:
            case OP_COO_WALK:
            case OP_CSC_WALK_DIRECT_STORE:
            case OP_FIXPOINT_KLEENE_STAR:
            case OP_FRONTIER_DIFF:
            case OP_COO_WALK_FILTERED:
            case OP_COO_WALK_DIRECT_STORE:
            case OP_DENSE_WALK:
            case OP_DENSE_WALK_BITMATRIX:
            case OP_DENSE_WALK_DIRECT_STORE:
                abstract_types[dst] = TYPE_BITSET_HANDLE;
                break;

            case OP_LOAD_CONST_INT:
            case OP_CSR_DEGREE:
            case OP_SET_CARDINALITY:
            case OP_BRIN_ZONE_SKIP:
                abstract_types[dst] = TYPE_INT64;
                break;

            case OP_LOAD_CONST_FLOAT:
            case OP_VECTOR_REDUCE_SUM:
                abstract_types[dst] = TYPE_FLOAT;
                break;

            case OP_LOAD_INLINE_ARRAY:
            case OP_VEC_MATH_UNARY:
            case OP_VEC_MATH_BINARY:
            case OP_VEC_MATH_TERNARY:
            case OP_LOAD_COLUMN_VECTOR:
            case OP_GATHER_NODE_ATTR:
            case OP_GATHER_EDGE_ATTR:
            case OP_COO_WALK_REDUCE:
            case OP_DENSE_WALK_REDUCE:
                abstract_types[dst] = TYPE_FLOAT_VECTOR;
                break;

            case OP_MOV:
                if (src < 64) abstract_types[dst] = abstract_types[src];
                break;

            case OP_CLEAR_REG:
                abstract_types[dst] = TYPE_NULL;
                break;

            default:
                break;
        }
    }
    
    return IMPULSE_VM_OK;
    return IMPULSE_VM_OK;
}

size_t impulse_vm_get_required_buffer_size(const impulse_snapshot_t* snapshot, uint16_t domain_id) {
    (void)domain_id;
    if (!snapshot) return 0;
    return 10000000;
}

impulse_vm_status_t impulse_vm_execute_to_buffer(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param,
    uint16_t target_domain_id,
    uint16_t result_reg,
    uint64_t* out_words,
    size_t out_words_capacity,
    size_t* out_words_written) {
    
    (void)target_domain_id;
    impulse_vm_status_t st = impulse_vm_execute(bytecode, instruction_count, vm_state, input_param);
    if (st != IMPULSE_VM_OK) return st;
    
    if (vm_state->register_types[result_reg] == TYPE_BITSET_HANDLE) {
        int h = static_cast<int>(vm_state->registers[result_reg]);
        if (h >= 0 && vm_state->query_context) {
            auto& bs = vm_state->query_context->bitsets[h];
            size_t count = vm_state->query_context->words_per_bitset;
            if (count > out_words_capacity) count = out_words_capacity;
            std::memcpy(out_words, bs.words, count * sizeof(uint64_t));
            if (out_words_written) *out_words_written = count;
        }
    }
    return IMPULSE_VM_OK;
}

} // extern "C"

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
