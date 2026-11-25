/**
 * @file v11_ictx_v2.cpp
 * @brief Dual ICTX v2 Wire Serializer & Canonical XXH3-64 Digest Engine.
 *
 * Bit-identical to V11_ictx_v2 (impulse-ocaml) and V11ContextHash (impulse-graph-java).
 */
#include "v11_internal.hpp"
#include "v11_xxh3.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace impulse::vm::v11 {
namespace {

inline constexpr uint64_t kLogicalSeed  = 0x494D505630313130ULL; // "IMPV0110"
inline constexpr uint64_t kPhysicalSeed = 0x5048595330313130ULL; // "PHYS0110"

struct ByteBuf {
    std::vector<uint8_t> data;

    explicit ByteBuf(size_t reserve_cap = 512) {
        data.reserve(reserve_cap);
    }

    inline void add_u8(uint8_t v) {
        data.push_back(v);
    }

    inline void add_u16_le(uint16_t v) {
        data.push_back(static_cast<uint8_t>(v & 0xFFu));
        data.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }

    inline void add_u32_le(uint32_t v) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        data.insert(data.end(), b, b + 4);
    }

    inline void add_u64_le(uint64_t v) {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        data.insert(data.end(), b, b + 8);
    }

    inline void add_raw(const void* ptr, size_t len) {
        if (len == 0) return;
        const auto* p = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), p, p + len);
    }

    inline void add_string(const char* ptr, uint32_t len) {
        add_u32_le(len);
        if (len > 0 && ptr != nullptr) {
            add_raw(ptr, len);
        }
    }
};

uint32_t count_dirty_pages(const uint32_t* sorted_nodes, uint32_t count, uint32_t page_shift) noexcept {
    if (count == 0) return 0u;
    if (page_shift >= 64u) return 1u;
    uint32_t pages = 1u;
    uint64_t prev = static_cast<uint64_t>(sorted_nodes[0]) >> page_shift;
    for (uint32_t i = 1; i < count; ++i) {
        uint64_t p = static_cast<uint64_t>(sorted_nodes[i]) >> page_shift;
        if (p != prev) {
            ++pages;
            prev = p;
        }
    }
    return pages;
}

void serialize_vec(ByteBuf& b, const V11Vector& v) {
    b.add_u8(v.elem_tag);
    b.add_u64_le(static_cast<uint64_t>(v.length));
    switch (v.elem_tag) {
        case kVecTagI64:
        case kVecTagU64: {
            for (uint32_t i = 0; i < v.length; ++i) {
                if (!v.is_valid_at(i)) {
                    b.add_u8(0u);
                } else {
                    b.add_u8(1u);
                    b.add_u64_le(static_cast<uint64_t>(v.i64_data[i]));
                }
            }
            break;
        }
        case kVecTagF64: {
            for (uint32_t i = 0; i < v.length; ++i) {
                if (!v.is_valid_at(i)) {
                    b.add_u8(0u);
                } else {
                    b.add_u8(1u);
                    uint64_t bits = 0;
                    std::memcpy(&bits, &v.f64_data[i], 8);
                    b.add_u64_le(bits);
                }
            }
            break;
        }
        case kVecTagBool3vl: {
            for (uint32_t i = 0; i < v.length; ++i) {
                if (!v.is_valid_at(i)) {
                    b.add_u8(0u);
                } else {
                    b.add_u8(1u);
                    b.add_u8(v.bool_data_at(i) ? 1u : 0u);
                }
            }
            break;
        }
        case kVecTagStr: {
            for (uint32_t i = 0; i < v.length; ++i) {
                if (v.str_data == nullptr || v.str_data[i].valid == 0) {
                    b.add_u8(0u);
                } else {
                    b.add_u8(1u);
                    b.add_string(v.str_data[i].ptr, v.str_data[i].len);
                }
            }
            break;
        }
        case kVecTagEmb: {
            b.add_u32_le(v.emb_dim);
            for (uint32_t i = 0; i < v.length; ++i) {
                if (v.emb_rows == nullptr || v.emb_rows[i] == nullptr) {
                    b.add_u8(0u);
                } else {
                    b.add_u8(1u);
                    for (uint32_t d = 0; d < v.emb_dim; ++d) {
                        uint64_t bits = 0;
                        std::memcpy(&bits, &v.emb_rows[i][d], 8);
                        b.add_u64_le(bits);
                    }
                }
            }
            break;
        }
        case kVecTagLaneState: {
            b.add_u16_le(v.ls_dom);
            b.add_u16_le(v.ls_k);
            for (uint32_t u = 0; u < v.length; ++u) {
                for (uint16_t l = 0; l < v.ls_k; ++l) {
                    size_t idx = static_cast<size_t>(u) * v.ls_k + l;
                    bool valid = (v.ls_valid != nullptr && v.ls_valid[idx] != 0);
                    if (!valid) {
                        b.add_u8(0u);
                    } else {
                        b.add_u8(1u);
                        uint64_t bits = 0;
                        std::memcpy(&bits, &v.ls_vals[idx], 8);
                        b.add_u64_le(bits);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

} // namespace
} // namespace impulse::vm::v11

extern "C" IMPULSE_API impulse_vm_ictx_v2_t impulse_vm_v11_compute_ictx_v2(
    const impulse_vm_workspace_v11_t* ws
) {
    using namespace impulse::vm::v11;
    if (!ws) {
        return {0ULL, 0ULL};
    }

    ByteBuf bl(512);
    ByteBuf bp(512);

    // 1. Header (24 B)
    static constexpr uint8_t kMagic[8] = {'I', 'C', 'T', 'X', 'v', '2', 'L', 0};
    bl.add_raw(kMagic, 8);
    bp.add_raw(kMagic, 8);

    bl.add_u32_le(ws->status);
    bp.add_u32_le(ws->status);

    bl.add_u32_le(ws->state.pc);
    bp.add_u32_le(ws->state.pc);

    uint16_t flags_raw = ws->state.vm_flags & 0x0007u;
    bl.add_u16_le(flags_raw);
    bp.add_u16_le(flags_raw);

    uint16_t valid_mask = ws->state.r_valid_mask;
    bl.add_u16_le(valid_mask);
    bp.add_u16_le(valid_mask);

    uint64_t r_type_word = ws->canonical_r_type_word();
    bl.add_u64_le(r_type_word);
    bp.add_u64_le(r_type_word);

    // 2. Scalar Bank R0..R15
    for (uint8_t i = 0; i < 16; ++i) {
        if (((valid_mask >> i) & 1u) == 0u) {
            bl.add_u8(0u);
            bp.add_u8(0u);
        } else {
            bl.add_u8(1u);
            bp.add_u8(1u);
            uint8_t ty = ws->read_r_type(i);
            uint64_t rv = ws->read_r_raw(i);
            if (ty == static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE)) {
                V11StrSlice s = ws->resolve_scalar_str(rv);
                bl.add_string(s.valid ? s.ptr : nullptr, s.valid ? s.len : 0u);
                bp.add_string(s.valid ? s.ptr : nullptr, s.valid ? s.len : 0u);
            } else {
                bl.add_u64_le(rv);
                bp.add_u64_le(rv);
            }
        }
    }

    // 3. Frontier Bank F0..F15
    for (uint8_t i = 0; i < 16; ++i) {
        if (!ws->is_f_init(i)) {
            bl.add_u8(0u);
            bp.add_u8(0u);
        } else {
            const V11Frontier& f = ws->f_regs[i];
            bl.add_u8(1u);
            bp.add_u8(1u);
            bl.add_u16_le(f.domain_id);
            bp.add_u16_le(f.domain_id);
            if (!f.multi_lane) {
                bl.add_u8(1u);
                bp.add_u8(1u);
                bl.add_u8(1u);
                bp.add_u8(1u);
                if (f.tier == kTierAllSpan) {
                    bl.add_u64_le(f.domain_card);
                    bp.add_u64_le(f.domain_card);
                    for (uint64_t u = 0; u < f.domain_card; ++u) {
                        bl.add_u64_le(u);
                        bp.add_u64_le(u);
                    }
                    bp.add_u8(f.tier);
                    bp.add_u8(1u);
                    bp.add_u32_le(0u);
                    bp.add_u8(f.cached_card >= 0 ? 1u : 0u);
                } else {
                    bl.add_u64_le(static_cast<uint64_t>(f.node_count));
                    bp.add_u64_le(static_cast<uint64_t>(f.node_count));
                    for (uint32_t k = 0; k < f.node_count; ++k) {
                        uint64_t u = static_cast<uint64_t>(f.nodes[k]);
                        bl.add_u64_le(u);
                        bp.add_u64_le(u);
                    }
                    bp.add_u8(f.tier);
                    bp.add_u8(1u);
                    uint32_t dp = (f.tier == kTier1Paged)
                        ? count_dirty_pages(f.nodes, f.node_count, ws->geometry.page_shift)
                        : 0u;
                    bp.add_u32_le(dp);
                    bp.add_u8(f.cached_card >= 0 ? 1u : 0u);
                }
            } else {
                bl.add_u8(64u);
                bp.add_u8(64u);
                bl.add_u8(f.active_lane_count);
                bp.add_u8(f.active_lane_count);
                bl.add_u64_le(static_cast<uint64_t>(f.ml_count));
                bp.add_u64_le(static_cast<uint64_t>(f.ml_count));
                for (uint32_t k = 0; k < f.ml_count; ++k) {
                    bl.add_u64_le(f.ml_nodes[k]);
                    bp.add_u64_le(f.ml_nodes[k]);
                    bl.add_u64_le(f.ml_masks[k]);
                    bp.add_u64_le(f.ml_masks[k]);
                }
                bp.add_u8(f.tier);
                bp.add_u8(f.alloc_width);
                bp.add_u8(f.cached_card >= 0 ? 1u : 0u);
            }
        }
    }

    // 4. Vector Bank V0..V15
    for (uint8_t i = 0; i < 16; ++i) {
        const V11Vector* v = ws->v_regs[i];
        if (!v) {
            bl.add_u8(0u);
            bp.add_u8(0u);
        } else {
            bl.add_u8(1u);
            bp.add_u8(1u);
            serialize_vec(bl, *v);
            serialize_vec(bp, *v);
        }
    }

    // 5. Terminal COLLECT Outputs (sorted by slot)
    uint32_t n_out = ws->output_count;
    uint32_t order[kMaxOutputs];
    for (uint32_t i = 0; i < n_out && i < kMaxOutputs; ++i) {
        order[i] = i;
    }
    uint32_t clamped_out = (n_out < kMaxOutputs) ? n_out : kMaxOutputs;
    std::sort(order, order + clamped_out, [&](uint32_t a, uint32_t b) {
        return ws->outputs[a].slot < ws->outputs[b].slot;
    });
    bl.add_u32_le(clamped_out);
    bp.add_u32_le(clamped_out);
    for (uint32_t i = 0; i < clamped_out; ++i) {
        const V11OutputSlot& out = ws->outputs[order[i]];
        bl.add_u16_le(out.slot);
        bp.add_u16_le(out.slot);
        if (out.vec) {
            serialize_vec(bl, *out.vec);
            serialize_vec(bp, *out.vec);
        }
    }

    impulse_vm_ictx_v2_t digest{};
    digest.logical_hash = V11Xxh3::hash64_with_seed(bl.data.data(), bl.data.size(), kLogicalSeed);
    digest.physical_hash = V11Xxh3::hash64_with_seed(bp.data.data(), bp.data.size(), kPhysicalSeed);
    return digest;
}
