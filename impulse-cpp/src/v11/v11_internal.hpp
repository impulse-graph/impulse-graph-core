/**
 * @file v11_internal.hpp
 * @brief Internal C++20 runtime data structures for ImpulseVM v0.11.0.
 *
 * All execution-time allocations are carved strictly from the caller-owned
 * 128B-aligned scratch_buf via V11Arena (zero malloc/new in hot path).
 */
#pragma once

#include "impulse_vm_v11.h"
#include "v11/impulse_vm_v11_isa.hpp"
#include "v11/impulse_v11_opcode_macros.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace impulse::vm::v11 {

inline constexpr uint64_t kWorkspaceMagic   = 0x494D505753303131ULL; // "IMPWS011"
inline constexpr uint32_t kMorselMax        = 2048u;
inline constexpr uint32_t kMaxCallDepth     = 16u;
inline constexpr uint32_t kSpillPerFrame    = 16u;
inline constexpr uint32_t kTotalSpillSlots  = (kMaxCallDepth + 1u) * kSpillPerFrame;
inline constexpr uint32_t kMaxOutputs       = 64u;
inline constexpr uint32_t kMaxConstPool     = 256u;
inline constexpr uint32_t kMaxParams        = 256u;
inline constexpr uint32_t kMaxScalarStrings = 512u;

// Frontier physical tiers (VM_ARCHITECTURE_v0_11.md §5.2)
inline constexpr uint8_t kTierEmpty       = 0u;
inline constexpr uint8_t kTier0Inline     = 1u;
inline constexpr uint8_t kTier1Paged      = 2u;
inline constexpr uint8_t kTier2Dense      = 3u;
inline constexpr uint8_t kTierAllSpan     = 4u;

// Vector element tags (matching V11_types.vec_elem_tag & V11Vector.java)
inline constexpr uint8_t kVecTagI64       = 0u;
inline constexpr uint8_t kVecTagU64       = 1u;
inline constexpr uint8_t kVecTagF64       = 4u;
inline constexpr uint8_t kVecTagBool3vl   = 6u;
inline constexpr uint8_t kVecTagStr       = 7u;
inline constexpr uint8_t kVecTagEmb       = 8u;
inline constexpr uint8_t kVecTagLaneState = 9u;

// Spill kinds
inline constexpr uint8_t kSpillEmpty = 0u;
inline constexpr uint8_t kSpillR     = 1u;
inline constexpr uint8_t kSpillF     = 2u;
inline constexpr uint8_t kSpillV     = 3u;

inline constexpr impulse_vm_geometry_v11_t kGeomP0 = {64u, 12u, 64u, 2048u, 1u};
inline constexpr impulse_vm_geometry_v11_t kGeomP1 = {2u,  2u,  2u,  2048u, 1u};
inline constexpr impulse_vm_geometry_v11_t kGeomP2 = {0u,  6u,  0u,  2048u, 1u};
inline constexpr impulse_vm_geometry_v11_t kGeomP3 = {64u, 12u, 64u, 3u,    1u};
inline constexpr impulse_vm_geometry_v11_t kGeomP4 = {2u,  2u,  4u,  4u,    4u};

inline uint8_t compute_tier(const impulse_vm_geometry_v11_t& g,
                            uint64_t domain_card,
                            uint64_t card,
                            bool is_all_span) noexcept {
    if (card == 0) {
        return kTierEmpty;
    }
    if (is_all_span && card == domain_card) {
        return kTierAllSpan;
    }
    if (card <= g.tier0_max) {
        return kTier0Inline;
    }
    uint64_t page_size = (g.page_shift < 63u) ? (1ULL << g.page_shift) : UINT64_MAX;
    if (g.tier2_denom == 0u ||
        (card <= UINT64_MAX / g.tier2_denom && card * g.tier2_denom >= domain_card) ||
        (card > UINT64_MAX / (g.tier2_denom ? g.tier2_denom : 1u)) ||
        domain_card <= page_size) {
        return kTier2Dense;
    }
    return kTier1Paged;
}

/** Strict zero-malloc bump arena carved from caller scratch_buf. */
struct V11Arena {
    uint8_t* base = nullptr;
    size_t   cap  = 0;
    size_t   pos  = 0;
    size_t   mark = 0; // post-bindings reset watermark

    void* alloc(size_t bytes, size_t align = 64) noexcept {
        if (bytes == 0) {
            bytes = 1;
        }
        size_t mask = align - 1;
        if (pos > cap) return nullptr;
        size_t aligned = (pos + mask) & ~mask;
        if (aligned < pos || aligned > cap || bytes > cap - aligned) {
            return nullptr;
        }
        void* ptr = base + aligned;
        pos = aligned + bytes;
        return ptr;
    }

    template <typename T>
    T* alloc_array(size_t count, size_t align = alignof(T)) noexcept {
        if (count == 0) return nullptr;
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(T)) return nullptr;
        return static_cast<T*>(alloc(count * sizeof(T), align < 8 ? 8 : align));
    }

    void reset_to_mark() noexcept {
        pos = mark;
    }
};

/** 16-byte string slice pointing into snapshot pool, const pool, or workspace arena. */
struct V11StrSlice {
    const char* ptr  = nullptr;
    uint32_t    len  = 0;
    uint8_t     valid = 0;
};

/** Typed nullable vector (V0..V15, const pool, params, outputs). */
struct V11Vector {
    uint8_t  elem_tag = kVecTagI64;
    uint32_t length   = 0;

    int64_t*     i64_data       = nullptr; // TAG_I64, TAG_U64
    double*      f64_data       = nullptr; // TAG_F64
    uint64_t*    valid_bits     = nullptr; // nullptr => all valid
    uint64_t*    bool_data_bits = nullptr; // TAG_BOOL_3VL (data & ~valid == 0)
    V11StrSlice* str_data       = nullptr; // TAG_STR

    uint32_t     emb_dim        = 0;
    double**     emb_rows       = nullptr; // TAG_EMB (nullptr row => NULL)

    // TAG_LANE_STATE (MITra)
    uint16_t     ls_dom           = 0;
    uint16_t     ls_k             = 0;
    double*      ls_vals          = nullptr; // flat [length * ls_k]
    uint8_t*     ls_valid         = nullptr; // flat [length * ls_k]
    int64_t*     ls_witness       = nullptr; // flat [length * ls_k]
    uint8_t*     ls_witness_valid = nullptr;

    inline bool is_valid_at(uint32_t idx) const noexcept {
        if (elem_tag == kVecTagStr) {
            return str_data != nullptr && str_data[idx].valid != 0;
        }
        if (elem_tag == kVecTagEmb) {
            return emb_rows != nullptr && emb_rows[idx] != nullptr;
        }
        if (valid_bits == nullptr) {
            return true;
        }
        return ((valid_bits[idx >> 6] >> (idx & 63u)) & 1ULL) != 0ULL;
    }

    inline bool bool_data_at(uint32_t idx) const noexcept {
        return bool_data_bits != nullptr &&
               ((bool_data_bits[idx >> 6] >> (idx & 63u)) & 1ULL) != 0ULL;
    }

    inline bool matches_expected_param_tag(uint16_t tag) const noexcept {
        if (tag == 0xFFFFu) return true;
        switch (elem_tag) {
            case kVecTagI64:     return tag == 1u;
            case kVecTagU64:     return tag == 2u;
            case kVecTagF64:     return tag == 3u || tag == 6u;
            case kVecTagBool3vl: return tag == 4u;
            case kVecTagStr:     return tag == 5u;
            default:             return false;
        }
    }
};

/** Unified 3-Tier Adaptive Frontier (F0..F15, F8..F15 = MF0..MF7). */
struct V11Frontier {
    uint16_t domain_id         = 0;
    uint64_t domain_card       = 0;
    uint8_t  tier              = kTierEmpty;
    bool     multi_lane        = false;
    uint8_t  alloc_width       = 1;
    uint8_t  active_lane_count = 1;
    int64_t  cached_card       = 0;

    // Single-lane sorted node IDs (canonical representation backed by arena)
    uint32_t* nodes      = nullptr;
    uint32_t  node_count = 0;

    // Dense bitset representation (used by TIER2_DENSE & fast set ops in Pillar 2+)
    uint64_t* dense_bits  = nullptr;
    uint32_t  dense_words = 0;

    // Multi-lane sorted (node_id, lane_mask) pairs
    uint64_t* ml_nodes = nullptr;
    uint64_t* ml_masks = nullptr;
    uint32_t  ml_count = 0;

    inline bool is_empty() const noexcept {
        if (multi_lane) return ml_count == 0;
        if (tier == kTierEmpty) return true;
        if (tier == kTierAllSpan) return domain_card == 0;
        return node_count == 0;
    }

    inline void reset_empty(uint16_t dom_id, uint64_t dom_card) noexcept {
        domain_id = dom_id;
        domain_card = dom_card;
        tier = kTierEmpty;
        multi_lane = false;
        alloc_width = 1;
        active_lane_count = 1;
        cached_card = 0;
        nodes = nullptr;
        node_count = 0;
        dense_bits = nullptr;
        dense_words = 0;
        ml_nodes = nullptr;
        ml_masks = nullptr;
        ml_count = 0;
    }

    inline void init_all_span(uint16_t dom_id, uint64_t dom_card) noexcept {
        reset_empty(dom_id, dom_card);
        tier = (dom_card > 0ULL) ? kTierAllSpan : kTierEmpty;
        cached_card = static_cast<int64_t>(dom_card);
    }

    inline uint64_t cardinality() noexcept {
        if (multi_lane) {
            cached_card = static_cast<int64_t>(ml_count);
            return static_cast<uint64_t>(ml_count);
        }
        if (tier == kTierEmpty) {
            cached_card = 0;
            return 0ULL;
        }
        if (tier == kTierAllSpan) {
            cached_card = static_cast<int64_t>(domain_card);
            return domain_card;
        }
        cached_card = static_cast<int64_t>(node_count);
        return static_cast<uint64_t>(node_count);
    }

    inline bool contains_1bit(uint32_t u) const noexcept {
        if (tier == kTierEmpty) return false;
        if (tier == kTierAllSpan) return static_cast<uint64_t>(u) < domain_card;
        if (node_count == 0u || nodes == nullptr) return false;
        return std::binary_search(nodes, nodes + node_count, u);
    }

    bool materialize_1bit(V11Arena& arena, const uint32_t** out_ptr, uint32_t* out_len) const noexcept {
        if (tier == kTierEmpty) {
            *out_ptr = nullptr;
            *out_len = 0u;
            return true;
        }
        if (tier == kTierAllSpan) {
            if (domain_card > UINT32_MAX) return false;
            uint32_t n = static_cast<uint32_t>(domain_card);
            uint32_t* buf = (n > 0u) ? arena.alloc_array<uint32_t>(n, 8) : nullptr;
            if (n > 0u && !buf) return false;
            for (uint32_t i = 0; i < n; ++i) buf[i] = i;
            *out_ptr = buf;
            *out_len = n;
            return true;
        }
        *out_ptr = nodes;
        *out_len = node_count;
        return true;
    }

    bool init_from_sorted_ids(V11Arena& arena,
                              uint16_t dom_id,
                              uint64_t dom_card,
                              const uint32_t* sorted_ids,
                              uint32_t count,
                              const impulse_vm_geometry_v11_t& geom,
                              bool know_card) noexcept {
        uint32_t* dst = nullptr;
        if (count > 0u) {
            dst = arena.alloc_array<uint32_t>(count, 8);
            if (!dst) return false;
            std::memcpy(dst, sorted_ids, static_cast<size_t>(count) * sizeof(uint32_t));
        }
        reset_empty(dom_id, dom_card);
        if (count == 0u) {
            cached_card = 0;
            return true;
        }
        nodes = dst;
        node_count = count;
        tier = compute_tier(geom, dom_card, count, false);
        cached_card = (count == 0u || tier == kTier0Inline || tier == kTierAllSpan || know_card)
            ? static_cast<int64_t>(count) : -1;
        return true;
    }

    bool init_multi_lane(V11Arena& arena,
                         uint16_t dom_id,
                         uint64_t dom_card,
                         uint8_t width,
                         uint8_t active_lanes,
                         const uint64_t* sorted_nodes,
                         const uint64_t* sorted_masks,
                         uint32_t count,
                         const impulse_vm_geometry_v11_t& geom) noexcept {
        uint64_t* dn = nullptr;
        uint64_t* dm = nullptr;
        if (count > 0u) {
            dn = arena.alloc_array<uint64_t>(count, 8);
            dm = arena.alloc_array<uint64_t>(count, 8);
            if (!dn || !dm) return false;
            std::memcpy(dn, sorted_nodes, static_cast<size_t>(count) * sizeof(uint64_t));
            std::memcpy(dm, sorted_masks, static_cast<size_t>(count) * sizeof(uint64_t));
        }
        reset_empty(dom_id, dom_card);
        multi_lane = true;
        alloc_width = width;
        active_lane_count = active_lanes;
        ml_count = count;
        ml_nodes = dn;
        ml_masks = dm;
        tier = compute_tier(geom, dom_card, count, false);
        cached_card = static_cast<int64_t>(count);
        return true;
    }

    bool copy_from(V11Arena& arena, const V11Frontier& src) noexcept {
        domain_id = src.domain_id;
        domain_card = src.domain_card;
        tier = src.tier;
        multi_lane = src.multi_lane;
        alloc_width = src.alloc_width;
        active_lane_count = src.active_lane_count;
        cached_card = src.cached_card;
        dense_bits = nullptr;
        dense_words = 0;
        if (!src.multi_lane) {
            ml_nodes = nullptr;
            ml_masks = nullptr;
            ml_count = 0;
            node_count = src.node_count;
            if (src.node_count > 0 && src.nodes != nullptr) {
                uint32_t* dst = arena.alloc_array<uint32_t>(src.node_count, 8);
                if (!dst) return false;
                std::memcpy(dst, src.nodes, static_cast<size_t>(src.node_count) * sizeof(uint32_t));
                nodes = dst;
            } else {
                nodes = nullptr;
            }
        } else {
            nodes = nullptr;
            node_count = 0;
            ml_count = src.ml_count;
            if (src.ml_count > 0 && src.ml_nodes != nullptr && src.ml_masks != nullptr) {
                uint64_t* dn = arena.alloc_array<uint64_t>(src.ml_count, 8);
                uint64_t* dm = arena.alloc_array<uint64_t>(src.ml_count, 8);
                if (!dn || !dm) return false;
                std::memcpy(dn, src.ml_nodes, static_cast<size_t>(src.ml_count) * sizeof(uint64_t));
                std::memcpy(dm, src.ml_masks, static_cast<size_t>(src.ml_count) * sizeof(uint64_t));
                ml_nodes = dn;
                ml_masks = dm;
            } else {
                ml_nodes = nullptr;
                ml_masks = nullptr;
            }
        }
        return true;
    }
};

/** Const pool & param bindings stored in caller workspace. */
enum class ConstPoolKind : uint8_t { Empty = 0, Vec = 1, Frontier = 2, Str = 3 };

struct V11ConstPoolEntry {
    ConstPoolKind kind = ConstPoolKind::Empty;
    uint32_t      id   = 0;
    V11Vector*    vec  = nullptr;
    uint64_t*     f_ids = nullptr;
    uint32_t      f_len = 0;
    V11StrSlice   str{};
};

enum class ParamKind : uint8_t { Empty = 0, Scalar = 1, Str = 2, Frontier = 3, MultiLane = 4, Vec = 5 };

struct V11ParamEntry {
    ParamKind   kind         = ParamKind::Empty;
    uint32_t    id           = 0;
    uint8_t     scalar_type  = 0;
    bool        scalar_valid = false;
    uint64_t    scalar_bits  = 0;
    V11StrSlice str{};
    uint16_t    dom_id       = 0;
    uint64_t*   f_ids        = nullptr;
    uint32_t    f_len        = 0;
    uint8_t     ml_width     = 64;
    uint8_t     ml_active    = 0;
    uint64_t*   ml_nodes     = nullptr;
    uint64_t*   ml_masks     = nullptr;
    uint32_t    ml_len       = 0;
    V11Vector*  vec          = nullptr;
};

/** Unified Snapshot View (adapts both .imps impulse_snapshot_t* and compliance fixtures). */
struct V11RelView {
    uint16_t   rel_id       = 0;
    uint16_t   src_dom      = 0;
    uint16_t   tgt_dom      = 0;
    const uint32_t* row_offsets = nullptr;
    uint32_t   row_off_len  = 0;
    const uint64_t* col_targets = nullptr;
    uint32_t   col_tgt_len  = 0;
    bool       has_csr      = false;
    bool       has_csc      = false;
    bool       has_coo      = false;
    bool       has_dense    = false;
    // Edge attributes (indexed by attr_id)
    const uint32_t*  edge_attr_ids  = nullptr;
    V11Vector* const* edge_attrs    = nullptr;
    uint32_t   edge_attr_count = 0;
};

struct V11DomAttrEntry {
    uint16_t   dom_id  = 0;
    uint16_t   attr_id = 0;
    V11Vector* vec     = nullptr;
};

struct V11KeyCatalog {
    uint16_t           dom_id = 0;
    const V11StrSlice* keys   = nullptr;
    uint32_t           count  = 0;
};

struct V11IndexEntry {
    uint16_t    dom_id  = 0;
    uint16_t    attr_id = 0;
    V11StrSlice kind{};
};

struct V11SnapshotView {
    const uint64_t*        domain_cards      = nullptr;
    uint32_t               domain_count      = 0;
    const V11RelView*      relations         = nullptr;
    uint32_t               relation_count    = 0;
    const V11DomAttrEntry* node_attrs        = nullptr;
    uint32_t               node_attr_count   = 0;
    const V11KeyCatalog*   key_catalogs      = nullptr;
    uint32_t               key_catalog_count = 0;
    const V11IndexEntry*   indexes           = nullptr;
    uint32_t               index_count       = 0;
    const impulse_snapshot_t* raw_snapshot   = nullptr;

    inline bool get_domain_card(uint16_t dom_id, uint64_t* out_card) const noexcept {
        if (dom_id >= domain_count) return false;
        *out_card = domain_cards[dom_id];
        return true;
    }
};

struct V11SpillSlot {
    uint8_t     kind    = kSpillEmpty;
    uint64_t    r_val   = 0;
    bool        r_valid = false;
    uint8_t     r_type  = 0;
    V11Frontier f_val{};
    V11Vector*  v_val   = nullptr;
};

struct V11CallFrame {
    uint32_t    return_pc      = 0;
    uint8_t     r_stride       = 0;
    uint8_t     f_stride       = 0;
    uint8_t     v_stride       = 0;
    uint64_t    saved_r_val[16]{};
    uint16_t    saved_r_valid  = 0;
    uint64_t    saved_r_type   = 0;
    uint16_t    saved_f_valid  = 0;
    V11Frontier saved_f[16]{};
    V11Vector*  saved_v[16]{};
};

struct V11OutputSlot {
    uint16_t   slot = 0;
    V11Vector* vec  = nullptr;
};

} // namespace impulse::vm::v11

/** Concrete definition of caller-owned workspace header (placement-initialized in scratch_buf). */
struct impulse_vm_workspace_v11 {
    impulse_vm_state_v11_t state; // Exact 256B, 64B-aligned at offset 0

    uint64_t              magic;
    size_t                total_capacity;
    std::atomic<uint32_t> in_use;
    uint64_t              frontier_epoch;

    impulse_vm_status_v11_t   status;
    uint32_t                  user_throw_code;
    bool                      has_user_throw_payload;
    uint64_t                  user_throw_payload;
    uint16_t                  host_max_threads;
    uint8_t                   max_call_depth;
    uint64_t                  gas_remaining;
    impulse_vm_geometry_v11_t geometry;

    // Frontier & Vector register backing tables
    impulse::vm::v11::V11Frontier f_regs[16];
    uint16_t                      f_valid_mask;
    impulse::vm::v11::V11Vector*  v_regs[16];

    // 16-deep sliding register window call stack & 256 spill slots
    impulse::vm::v11::V11CallFrame call_frames[impulse::vm::v11::kMaxCallDepth];
    impulse::vm::v11::V11SpillSlot spill_slots[impulse::vm::v11::kTotalSpillSlots];

    // Bindings (const pool, params, interned scalar strings, terminal outputs)
    impulse::vm::v11::V11ConstPoolEntry const_pool[impulse::vm::v11::kMaxConstPool];
    uint32_t                            const_pool_count;
    impulse::vm::v11::V11ParamEntry     params[impulse::vm::v11::kMaxParams];
    uint32_t                            param_count;
    impulse::vm::v11::V11StrSlice       str_table[impulse::vm::v11::kMaxScalarStrings];
    impulse::vm::v11::V11StrSlice*      str_table_ptr;
    uint32_t                            str_count;
    uint32_t                            str_cap;
    impulse::vm::v11::V11OutputSlot     outputs[impulse::vm::v11::kMaxOutputs];
    uint32_t                            output_count;

    // Caller-owned bump arena (starts right after this struct, 128B aligned)
    impulse::vm::v11::V11Arena arena;

    // UB-free accessors for R0..R15 across the 3 cache-line sub-arrays
    inline uint64_t read_r_raw(uint8_t reg) const noexcept {
        if (reg < 4u)  return state.r_regs[reg];
        if (reg < 12u) return state.r_regs_4_11[reg - 4u];
        return state.r_regs_12_15[reg - 12u];
    }

    inline void write_r_raw(uint8_t reg, uint64_t val) noexcept {
        if (reg < 4u)       state.r_regs[reg] = val;
        else if (reg < 12u) state.r_regs_4_11[reg - 4u] = val;
        else                state.r_regs_12_15[reg - 12u] = val;
    }

    inline void clear_r_bank() noexcept {
        std::memset(state.r_regs, 0, sizeof(state.r_regs));
        std::memset(state.r_regs_4_11, 0, sizeof(state.r_regs_4_11));
        std::memset(state.r_regs_12_15, 0, sizeof(state.r_regs_12_15));
    }

    inline bool is_r_valid(uint8_t reg) const noexcept {
        return ((state.r_valid_mask >> reg) & 1u) != 0u;
    }

    inline uint8_t read_r_type(uint8_t reg) const noexcept {
        if (!is_r_valid(reg)) return 0u;
        return static_cast<uint8_t>((state.r_type_word >> (reg * 4u)) & 0xFULL);
    }

    inline void write_r_null(uint8_t reg) noexcept {
        write_r_raw(reg, 0ULL);
        state.r_valid_mask = static_cast<uint16_t>(state.r_valid_mask & ~(1u << reg));
        state.r_type_word &= ~(0xFULL << (reg * 4u));
    }

    inline void write_r(uint8_t reg, uint8_t scalar_type, uint64_t value) noexcept {
        write_r_raw(reg, value);
        state.r_valid_mask = static_cast<uint16_t>(state.r_valid_mask | (1u << reg));
        state.r_type_word = (state.r_type_word & ~(0xFULL << (reg * 4u))) |
                            (static_cast<uint64_t>(scalar_type & 0xFu) << (reg * 4u));
    }

    inline uint64_t canonical_r_type_word() const noexcept {
        uint64_t w = 0ULL;
        for (uint8_t i = 0; i < 16; ++i) {
            uint64_t nib = is_r_valid(i) ? read_r_type(i) : 0ULL;
            w |= (nib << (i * 4u));
        }
        return w;
    }

    inline bool flag_zf() const noexcept { return (state.vm_flags & impulse::vm::v11::VM_FLAG_ZF) != 0u; }
    inline bool flag_st() const noexcept { return (state.vm_flags & impulse::vm::v11::VM_FLAG_ST) != 0u; }
    inline bool flag_nf() const noexcept { return (state.vm_flags & impulse::vm::v11::VM_FLAG_NF) != 0u; }

    inline void set_zf(bool v) noexcept {
        if (v) state.vm_flags |= impulse::vm::v11::VM_FLAG_ZF;
        else   state.vm_flags &= static_cast<uint16_t>(~impulse::vm::v11::VM_FLAG_ZF);
    }
    inline void set_st(bool v) noexcept {
        if (v) state.vm_flags |= impulse::vm::v11::VM_FLAG_ST;
        else   state.vm_flags &= static_cast<uint16_t>(~impulse::vm::v11::VM_FLAG_ST);
    }
    inline void set_nf(bool v) noexcept {
        if (v) state.vm_flags |= impulse::vm::v11::VM_FLAG_NF;
        else   state.vm_flags &= static_cast<uint16_t>(~impulse::vm::v11::VM_FLAG_NF);
    }

    inline bool is_f_init(uint8_t reg) const noexcept {
        return ((f_valid_mask >> reg) & 1u) != 0u;
    }
    inline void set_f_init(uint8_t reg, bool v) noexcept {
        if (v) f_valid_mask = static_cast<uint16_t>(f_valid_mask | (1u << reg));
        else   f_valid_mask = static_cast<uint16_t>(f_valid_mask & ~(1u << reg));
    }

    inline uint64_t intern_scalar_str(const char* ptr, uint32_t len) noexcept {
        if (!str_table_ptr || str_cap == 0u) {
            str_table_ptr = str_table;
            str_cap = impulse::vm::v11::kMaxScalarStrings;
        }
        if (str_count >= str_cap) {
            uint32_t next_cap = str_cap * 2u;
            if (next_cap <= str_cap) return 0ULL;
            auto* grown = arena.alloc_array<impulse::vm::v11::V11StrSlice>(next_cap, 8);
            if (!grown) return 0ULL;
            std::memcpy(grown, str_table_ptr, static_cast<size_t>(str_count) * sizeof(impulse::vm::v11::V11StrSlice));
            str_table_ptr = grown;
            str_cap = next_cap;
        }
        char* copy = nullptr;
        if (len > 0) {
            copy = static_cast<char*>(arena.alloc(len, 1));
            if (!copy) return 0ULL;
            std::memcpy(copy, ptr, len);
        }
        uint32_t idx = str_count++;
        str_table_ptr[idx] = {copy, len, 1u};
        return static_cast<uint64_t>(idx + 1u);
    }

    inline impulse::vm::v11::V11StrSlice resolve_scalar_str(uint64_t handle) const noexcept {
        if (handle == 0ULL || handle > str_count || !str_table_ptr) {
            return {nullptr, 0u, 0u};
        }
        return str_table_ptr[handle - 1u];
    }

    inline const impulse::vm::v11::V11ConstPoolEntry* find_const_pool(uint32_t id) const noexcept {
        for (uint32_t i = 0; i < const_pool_count; ++i) {
            if (const_pool[i].id == id) return &const_pool[i];
        }
        return nullptr;
    }

    inline const impulse::vm::v11::V11ParamEntry* find_param(uint32_t id) const noexcept {
        for (uint32_t i = 0; i < param_count; ++i) {
            if (params[i].id == id) return &params[i];
        }
        return nullptr;
    }

    inline void reset_execution_state() noexcept {
        state.pc = 0u;
        state.vm_flags = 0u;
        state.r_valid_mask = 0u;
        state.r_type_word = 0ULL;
        state.call_depth = 0u;
        state.max_dop = 1u;
        state.win_bases_packed = 0u;
        state.ws = this;
        clear_r_bank();
        std::memset(state.f_handles, 0, sizeof(state.f_handles));
        std::memset(state.f_handles_8_15, 0, sizeof(state.f_handles_8_15));
        std::memset(state.v_handles, 0, sizeof(state.v_handles));
        status = IMPULSE_VM_V11_OK;
        user_throw_code = 0u;
        has_user_throw_payload = false;
        user_throw_payload = 0ULL;
        f_valid_mask = 0u;
        for (uint8_t i = 0; i < 16; ++i) {
            f_regs[i].reset_empty(0u, 0u);
            v_regs[i] = nullptr;
        }
        for (uint32_t i = 0; i < impulse::vm::v11::kSpillPerFrame; ++i) {
            spill_slots[i].kind = impulse::vm::v11::kSpillEmpty;
        }
        output_count = 0u;
        str_table_ptr = str_table;
        str_count = 0u;
        str_cap = impulse::vm::v11::kMaxScalarStrings;
        frontier_epoch++;
        arena.reset_to_mark();
    }
};

namespace impulse::vm::v11 {

impulse_vm_validation_v11_t validate_words_in_arena(
    const uint64_t* words,
    size_t word_count,
    V11Arena* arena) noexcept;

impulse_vm_validation_v11_t validate_impb_in_arena(
    const void* impb_bytes,
    size_t impb_len,
    V11Arena* arena) noexcept;

impulse_vm_status_v11_t execute_words_internal(
    const uint64_t* words,
    size_t word_count,
    const V11SnapshotView* snap,
    impulse_vm_workspace_v11_t& ws,
    uint64_t max_steps,
    uint32_t entry_pc = 0u,
    bool already_validated = false) noexcept;

} // namespace impulse::vm::v11
