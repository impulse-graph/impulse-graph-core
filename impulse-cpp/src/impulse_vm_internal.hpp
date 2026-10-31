/**
 * @file impulse_vm_internal.hpp
 * @brief Internal data structures, off-heap handle pool helpers, and force-inline utilities
 *        shared between impulse_vm.cpp, impulse_vm_context.cpp, and per-opcode implementations.
 */
#ifndef IMPULSE_VM_INTERNAL_HPP
#define IMPULSE_VM_INTERNAL_HPP

#include "impulse_vm.h"
#include "impulse_assert.h"
#include "impulse_graph.h"
#include "impulse_simd.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <bit>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <string>
#include <cstdarg>
#include <atomic>
#include <mutex>
#include <map>

#if defined(_OPENMP)
#include <omp.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(IMPULSE_DISABLE_COMPUTED_GOTO)
  #define HAS_COMPUTED_GOTO 1
#else
  #define HAS_COMPUTED_GOTO 0
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define IMPULSE_PREFETCH_L2(ptr) __builtin_prefetch((ptr), 0, 1)
  #define IMPULSE_VM_FORCE_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
  #if defined(_M_X64) || defined(_M_IX86)
    #include <xmmintrin.h>
    #define IMPULSE_PREFETCH_L2(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T2)
  #else
    #define IMPULSE_PREFETCH_L2(ptr) ((void)(ptr))
  #endif
  #define IMPULSE_VM_FORCE_INLINE __forceinline
#else
  #define IMPULSE_PREFETCH_L2(ptr) ((void)(ptr))
  #define IMPULSE_VM_FORCE_INLINE inline
#endif

#define VALIDATE_REG(r) \
    do { \
        if ((r) >= 64) return IMPULSE_VM_ERR_INVALID_REGISTER; \
    } while(0)

extern int g_vm_branch_cov_fast;

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
    uint8_t     csc_node_id_width = 4;
    uint8_t     csc_edge_index_width = 4;
    uint64_t    node_count = 0;
    uint64_t    csc_node_count = 0;
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
        if (csc_edge_index_width == 8 || csc_edge_index_width == 64) {
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
        if (csc_node_id_width == 2 || csc_node_id_width == 16) {
            return static_cast<const uint16_t*>(csc_targets_ptr)[edge_idx];
        } else if (csc_node_id_width == 8 || csc_node_id_width == 64) {
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
constexpr size_t VM_MAX_LANE_FRONTIER_HANDLES = 32;
constexpr size_t VM_MAX_REL_SLOTS = 16;
constexpr size_t VM_MAX_MOCK_ATTRS = 16;
constexpr size_t VM_MOCK_INIT_CAP = 4096;

struct VmLaneFrontier {
    std::vector<uint64_t> lanes;
    std::vector<uint64_t> active_words;
    uint32_t num_lanes = 0;
    impulse_lane_width_t width = IMPULSE_LANE_WIDTH_8;
    uint64_t active_mask = 0;
    uint16_t edge_trip_active_mask = 0;
    std::array<std::vector<uint64_t>, VM_MAX_REL_SLOTS> edge_trip_masks;
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
    std::array<size_t, VM_MAX_VECTOR_HANDLES> float_vector_lengths{};
    std::array<std::vector<double>, VM_MAX_VECTOR_HANDLES> double_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> double_vectors_allocated;
    std::array<size_t, VM_MAX_VECTOR_HANDLES> double_vector_lengths{};
    std::array<std::vector<uint64_t>, VM_MAX_VECTOR_HANDLES> node_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> node_vectors_allocated;
    std::array<size_t, VM_MAX_VECTOR_HANDLES> node_vector_lengths{};
    std::array<bool, VM_MAX_VECTOR_HANDLES> node_vector_length_explicit{};
    std::array<std::vector<const char*>, VM_MAX_VECTOR_HANDLES> string_vectors;
    std::array<bool, VM_MAX_VECTOR_HANDLES> string_vectors_allocated;
    std::array<BoundValueMap, VM_MAX_VECTOR_HANDLES> value_maps;
    std::array<bool, VM_MAX_VECTOR_HANDLES> value_maps_allocated;

    // Pre-allocated multi-lane bit-sliced frontiers
    std::array<VmLaneFrontier, VM_MAX_LANE_FRONTIER_HANDLES> lane_frontiers;
    size_t lane_frontiers_count = 0;
    std::vector<uint64_t> scratch_lanes;
    std::vector<uint64_t> scratch_lanes_b;
    std::vector<uint64_t> scratch_active_words;
    std::vector<uint64_t> scratch_active_words_b;

    // Pre-allocated scratch buffers for zero-allocation hot-path execution
    std::vector<uint32_t> scratch_u32_a;
    std::vector<uint32_t> scratch_u32_b;
    std::vector<uint64_t> scratch_u64_a;
    std::vector<float> scratch_f32_a;
    std::vector<int> scratch_int_deg;
    std::vector<int> scratch_int_vert;
    std::vector<int> scratch_int_pos;
    std::vector<int> scratch_int_bin;
    std::vector<std::vector<uint64_t>> bfs_visited_words;
    std::vector<std::vector<uint32_t>> bfs_queues;

    // Contiguous node buffer for array returns
    std::vector<uint64_t> node_buffer;

    // Inline payload data binding pointer
    const uint8_t* inline_data_ptr = nullptr;
    size_t inline_data_bytes = 0;

    struct MockAttribute {
        uint32_t key = 0;
        bool active = false;
        std::vector<float> float_data;
        std::vector<uint64_t> int_data;
        bool has_mask = false;
        VmBitSet mask;
    };
    std::array<MockAttribute, VM_MAX_MOCK_ATTRS> mock_node_attrs;
    std::array<MockAttribute, VM_MAX_MOCK_ATTRS> mock_edge_attrs;

    // Fuel counter for unbounded loop & gas protection
    uint64_t fuel = 0;
    bool fuel_enabled = false;
};

// ============================================================================
// SQLite-Inspired Runtime Test Control, Fault Simulator & Coverage State

inline int acquire_bitset(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    if (impulse_faultsim_step() != 0) return -1;
    for (size_t i = 0; i < VM_MAX_BITSET_HANDLES; ++i) {
        if (!ctx->bitset_allocated[i]) {
            IMPULSE_TESTCASE(i == 0);
            IMPULSE_TESTCASE(i == VM_MAX_BITSET_HANDLES - 1);
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
    if (impulse_faultsim_step() != 0) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->float_vectors_allocated[i]) {
            ctx->float_vectors_allocated[i] = true;
            ctx->float_vector_lengths[i] = ctx->max_nodes;
            std::memset(ctx->float_vectors[i].data(), 0, ctx->float_vectors[i].size() * sizeof(float));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_float_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->float_vectors_allocated[handle] = false;
        ctx->float_vector_lengths[handle] = 0;
    }
}

inline int acquire_double_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    if (impulse_faultsim_step() != 0) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->double_vectors_allocated[i]) {
            ctx->double_vectors_allocated[i] = true;
            ctx->double_vector_lengths[i] = ctx->max_nodes;
            std::memset(ctx->double_vectors[i].data(), 0, ctx->double_vectors[i].size() * sizeof(double));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_double_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->double_vectors_allocated[handle] = false;
        ctx->double_vector_lengths[handle] = 0;
    }
}

inline int acquire_node_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    if (impulse_faultsim_step() != 0) return -1;
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        if (!ctx->node_vectors_allocated[i]) {
            ctx->node_vectors_allocated[i] = true;
            ctx->node_vector_lengths[i] = ctx->max_nodes;
            ctx->node_vector_length_explicit[i] = false;
            std::memset(ctx->node_vectors[i].data(), 0, ctx->node_vectors[i].size() * sizeof(uint64_t));
            return static_cast<int>(i);
        }
    }
    return -1; // Pool exhausted
}

inline void release_node_vector(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES) {
        ctx->node_vectors_allocated[handle] = false;
        ctx->node_vector_lengths[handle] = 0;
        ctx->node_vector_length_explicit[handle] = false;
    }
}

inline impulse_lane_width_t select_lane_width(uint32_t k) {
    IMPULSE_TESTCASE(k == 0);
    IMPULSE_TESTCASE(k == 1);
    IMPULSE_TESTCASE(k == 7);
    IMPULSE_TESTCASE(k == 8);
    IMPULSE_TESTCASE(k == 9);
    IMPULSE_TESTCASE(k == 15);
    IMPULSE_TESTCASE(k == 16);
    IMPULSE_TESTCASE(k == 17);
    IMPULSE_TESTCASE(k == 31);
    IMPULSE_TESTCASE(k == 32);
    IMPULSE_TESTCASE(k == 33);
    IMPULSE_TESTCASE(k == 63);
    IMPULSE_TESTCASE(k == 64);
    if (k <= 8) return IMPULSE_LANE_WIDTH_8;
    if (k <= 16) return IMPULSE_LANE_WIDTH_16;
    if (k <= 32) return IMPULSE_LANE_WIDTH_32;
    return IMPULSE_LANE_WIDTH_64;
}

inline const impulse_vm_context_t::MockAttribute* find_mock_node_attr(const impulse_vm_context_t* ctx, uint32_t key) {
    if (!ctx) return nullptr;
    for (const auto& ma : ctx->mock_node_attrs) {
        if (ma.active && ma.key == key) return &ma;
    }
    return nullptr;
}

inline const impulse_vm_context_t::MockAttribute* find_mock_edge_attr(const impulse_vm_context_t* ctx, uint32_t key) {
    if (!ctx) return nullptr;
    for (const auto& ma : ctx->mock_edge_attrs) {
        if (ma.active && ma.key == key) return &ma;
    }
    return nullptr;
}

inline impulse_vm_context_t::MockAttribute* upsert_mock_node_attr(impulse_vm_context_t* ctx, uint32_t key) {
    for (auto& ma : ctx->mock_node_attrs) {
        if (ma.active && ma.key == key) return &ma;
    }
    for (auto& ma : ctx->mock_node_attrs) {
        if (!ma.active) {
            ma.active = true;
            ma.key = key;
            return &ma;
        }
    }
    ctx->mock_node_attrs[0].active = true;
    ctx->mock_node_attrs[0].key = key;
    return &ctx->mock_node_attrs[0];
}

inline impulse_vm_context_t::MockAttribute* upsert_mock_edge_attr(impulse_vm_context_t* ctx, uint32_t key) {
    for (auto& ma : ctx->mock_edge_attrs) {
        if (ma.active && ma.key == key) return &ma;
    }
    for (auto& ma : ctx->mock_edge_attrs) {
        if (!ma.active) {
            ma.active = true;
            ma.key = key;
            return &ma;
        }
    }
    ctx->mock_edge_attrs[0].active = true;
    ctx->mock_edge_attrs[0].key = key;
    return &ctx->mock_edge_attrs[0];
}

inline int acquire_lane_frontier(impulse_vm_context_t* ctx, size_t node_count, uint32_t num_lanes) {
    if (!ctx) return -1;
    if (impulse_faultsim_step() != 0) return -1;
    size_t idx = ctx->lane_frontiers_count;
    if (idx >= VM_MAX_LANE_FRONTIER_HANDLES) {
        idx = (idx % (VM_MAX_LANE_FRONTIER_HANDLES / 2)) + (VM_MAX_LANE_FRONTIER_HANDLES / 2);
        ctx->lane_frontiers_count++;
    } else {
        ctx->lane_frontiers_count++;
    }
    uint32_t clamped_k = (num_lanes > 64u) ? 64u : num_lanes;
    auto& lf = ctx->lane_frontiers[idx];
    lf.lanes.assign(node_count, 0ULL);
    lf.active_words.assign((node_count + 63) / 64, 0ULL);
    lf.num_lanes = clamped_k;
    lf.width = select_lane_width(clamped_k);
    lf.active_mask = impulse_make_active_mask_u64(clamped_k);
    if (lf.edge_trip_active_mask != 0) {
        for (size_t r = 0; r < VM_MAX_REL_SLOTS; ++r) {
            if (lf.edge_trip_active_mask & (1u << r)) {
                lf.edge_trip_masks[r].clear();
            }
        }
        lf.edge_trip_active_mask = 0;
    }
    return static_cast<int>(idx);
}

inline int clone_lane_frontier(impulse_vm_context_t* ctx, size_t src_handle) {
    if (!ctx || src_handle >= VM_MAX_LANE_FRONTIER_HANDLES || src_handle >= ctx->lane_frontiers_count) return -1;
    const auto& src = ctx->lane_frontiers[src_handle];
    int dst_handle = acquire_lane_frontier(ctx, src.lanes.size(), src.num_lanes);
    if (dst_handle < 0) return -1;
    auto& dst = ctx->lane_frontiers[dst_handle];
    if (!src.lanes.empty()) {
        std::memcpy(dst.lanes.data(), src.lanes.data(), src.lanes.size() * sizeof(uint64_t));
    }
    if (!src.active_words.empty()) {
        dst.active_words.resize(src.active_words.size(), 0ULL);
        std::memcpy(dst.active_words.data(), src.active_words.data(), src.active_words.size() * sizeof(uint64_t));
    }
    dst.width = src.width;
    dst.active_mask = src.active_mask;
    dst.edge_trip_active_mask = src.edge_trip_active_mask;
    if (src.edge_trip_active_mask != 0) {
        for (size_t r = 0; r < VM_MAX_REL_SLOTS; ++r) {
            if (src.edge_trip_active_mask & (1u << r)) {
                const auto& s_trip = src.edge_trip_masks[r];
                auto& d_trip = dst.edge_trip_masks[r];
                d_trip.clear();
                d_trip.resize(s_trip.size(), 0ULL);
                if (!s_trip.empty()) {
                    std::memcpy(d_trip.data(), s_trip.data(), s_trip.size() * sizeof(uint64_t));
                }
            }
        }
    }
    return dst_handle;
}

inline size_t resolve_multilane_node_count(const impulse_vm_context_t* ctx, int rel_id, size_t min_needed) {
    if (!ctx) return std::max<size_t>(64, min_needed);
    size_t best = 0;
    if (rel_id >= 0 && static_cast<size_t>(rel_id) < ctx->slots.size()) {
        best = std::max<size_t>(best, ctx->slots[rel_id].node_count);
        best = std::max<size_t>(best, ctx->slots[rel_id].csc_node_count);
    }
    if (best == 0) {
        for (const auto& slot : ctx->slots) {
            best = std::max<size_t>(best, slot.node_count);
            best = std::max<size_t>(best, slot.csc_node_count);
        }
        if (best == 0 && ctx->snapshot) {
            best = std::max<size_t>(best, impulse_snapshot_max_node_count(ctx->snapshot));
        }
        if (best == 0) {
            for (const auto& ma : ctx->mock_node_attrs) {
                if (ma.active) {
                    best = std::max<size_t>(best, ma.float_data.size());
                    best = std::max<size_t>(best, ma.int_data.size());
                }
            }
        }
    }
    return (best > 0) ? std::max(best, min_needed) : std::max<size_t>(64, min_needed);
}

inline bool get_node_vector_slice(
    const impulse_vm_context_t* ctx,
    uint64_t reg_val,
    const uint64_t** out_ptr,
    size_t* out_len
) {
    if (!ctx || !out_ptr || !out_len) return false;
    if (reg_val < VM_MAX_VECTOR_HANDLES && ctx->node_vectors_allocated[reg_val]) {
        *out_ptr = ctx->node_vectors[reg_val].data();
        if (ctx->node_vector_length_explicit[reg_val]) {
            *out_len = ctx->node_vector_lengths[reg_val];
        } else if (ctx->node_vector_lengths[reg_val] > 0) {
            *out_len = ctx->node_vector_lengths[reg_val];
        } else {
            *out_len = ctx->node_vectors[reg_val].size();
        }
        return true;
    }
    if (!ctx->node_buffer.empty() && reg_val == reinterpret_cast<uint64_t>(ctx->node_buffer.data())) {
        *out_ptr = ctx->node_buffer.data();
        *out_len = ctx->node_buffer.size();
        return true;
    }
    *out_ptr = nullptr;
    *out_len = 0;
    return false;
}

inline int acquire_string_vector(impulse_vm_context_t* ctx) {
    if (!ctx) return -1;
    if (impulse_faultsim_step() != 0) return -1;
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
    if (impulse_faultsim_step() != 0) return -1;
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

inline uint32_t run_island_detect_bfs(
    impulse_vm_context_t* ctx,
    uint32_t N,
    const BoundRelationSlot& slot,
    const int32_t* branch_ids,
    int64_t k1,
    int64_t k2
) {
    if (N == 0 || !ctx) return 0;
    size_t num_words = (N + 63) / 64;
    size_t tid = 0;
#if defined(_OPENMP)
    tid = static_cast<size_t>(omp_get_thread_num()) % ctx->bfs_visited_words.size();
#endif
    auto& visited_words = ctx->bfs_visited_words[tid];
    visited_words.clear();
    visited_words.resize(num_words, 0ULL);

    auto& queue = ctx->bfs_queues[tid];
    queue.clear();

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

inline void extract_active_bits(const VmBitSet& bs, std::vector<uint32_t>& out_bits) {
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


#endif // IMPULSE_VM_INTERNAL_HPP
