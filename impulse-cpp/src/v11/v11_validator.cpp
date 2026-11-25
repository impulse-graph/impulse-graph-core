/**
 * @file v11_validator.cpp
 * @brief Static Load-Time Bytecode Verifier (impulse_vm_validate_v11) for ImpulseVM v0.11.0.
 *
 * Mirrors V11_isa.decode_word / V11_validate.validate_program (impulse-ocaml) and
 * V11Validator.java (impulse-graph-java).
 */
#include "v11_internal.hpp"
#include "v11_xxh3.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace impulse::vm::v11 {
namespace {

bool is_mask_s3_only(uint8_t code) noexcept {
    switch (static_cast<Opcode>(code)) {
        case Opcode::OP_WALK:
        case Opcode::OP_WALK_FILTERED:
        case Opcode::OP_DEGREE:
        case Opcode::OP_WALK_MORSEL:
        case Opcode::OP_MORSEL_COMMIT:
        case Opcode::OP_GRAPH_KERNEL:
        case Opcode::OP_GATHER_NODE_ATTR:
        case Opcode::OP_GATHER_EDGE_ATTR:
        case Opcode::OP_GATHER_VEC:
        case Opcode::OP_VEC_CMP:
        case Opcode::OP_VEC_3VL_OP:
        case Opcode::OP_VEC_STR_PRED:
        case Opcode::OP_VEC_MATH_ARITH:
        case Opcode::OP_VEC_MATH_EXP_LOG:
        case Opcode::OP_VEC_MATH_TRIG_HYP:
        case Opcode::OP_VEC_MATH_ACT:
        case Opcode::OP_VEC_INT_BITWISE:
        case Opcode::OP_COALESCE:
        case Opcode::OP_VEC_REDUCE:
        case Opcode::OP_TOP_K:
        case Opcode::OP_COLLECT:
        case Opcode::OP_VECTOR_ANN_SEARCH:
        case Opcode::OP_VECTOR_RERANK:
            return true;
        default:
            return false;
    }
}

impulse_vm_status_v11_t validate_word(uint64_t w) noexcept {
    uint8_t code = IMP_V11_OPCODE(w);
    const OpcodeMeta& meta = kOpcodeMeta[code];
    if (!meta.defined) {
        return IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    uint8_t subop   = IMP_V11_SUBOP(w);
    uint8_t flags   = IMP_V11_FLAGS(w);
    uint8_t s1      = IMP_V11_S1(w);
    uint8_t dst     = IMP_V11_DST(w);
    uint8_t s3      = IMP_V11_S3(w);
    uint8_t s2      = IMP_V11_S2(w);
    uint32_t imm32  = IMP_V11_IMM32(w);
    uint16_t imm_lo = IMP_V11_IMM16_LO(w);
    uint16_t imm_hi = IMP_V11_IMM16_HI(w);

    if ((meta.valid_subop_mask & (static_cast<uint16_t>(1u) << subop)) == 0u) {
        return IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    if ((flags & static_cast<uint8_t>(~meta.allowed_flags & 0x0Fu)) != 0u) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }

    bool ext    = (flags & FLAG_EXTENDED) != 0u;
    bool accum  = (flags & FLAG_ACCUM) != 0u;
    bool scalar = (flags & FLAG_SCALAR_RHS) != 0u;
    bool mask   = (flags & FLAG_MASKED_S3) != 0u;

    bool mbz_ok = true;
    switch (static_cast<Opcode>(code)) {
        case Opcode::OP_HALT:
        case Opcode::OP_NOP:
        case Opcode::OP_RET:
            mbz_ok = (dst == 0 && s1 == 0 && s2 == 0 && s3 == 0 && imm32 == 0);
            break;
        case Opcode::OP_EXTENSION_PAYLOAD:
            mbz_ok = (flags == 0 && subop == 0 && dst == 0 && s1 == 0 && s2 == 0 && s3 == 0);
            break;
        case Opcode::OP_LOAD_CONST: {
            bool is_ext_subop = (subop >= 4 && subop <= 6);
            bool ext_valid = (is_ext_subop == ext);
            bool null_valid = (subop != 7) || (imm32 == 0);
            mbz_ok = ext_valid && null_valid && (s1 == 0 && s2 == 0 && s3 == 0);
            break;
        }
        case Opcode::OP_SPILL:
        case Opcode::OP_RELOAD:
            mbz_ok = (imm_lo < 16u && imm_hi == 0u);
            break;
        case Opcode::OP_SCALAR_INT_ALU:
            mbz_ok = (s3 <= 1u);
            break;
        case Opcode::OP_JMP:
            mbz_ok = (dst == 0 && s1 == 0 && s2 == 0 && s3 == 0);
            break;
        case Opcode::OP_SPMV:
            mbz_ok = (subop < 7 || !accum) && (mask || s3 == 0);
            break;
        case Opcode::OP_VEC_CMP:
            if (subop == 6) {
                mbz_ok = !mask && imm32 == 0;
            } else if (subop == 7) {
                mbz_ok = !scalar && (mask || s3 == 0);
            } else {
                mbz_ok = mask || s3 == 0;
            }
            break;
        case Opcode::OP_VEC_TERNARY:
            mbz_ok = (subop != 8) || !scalar;
            break;
        case Opcode::OP_VEC_INIT:
            mbz_ok = (s1 == 0);
            break;
        case Opcode::OP_LANE_WALK:
            mbz_ok = (subop != 8 || s2 == 0) && (mask || s3 == 0);
            break;
        case Opcode::OP_LANE_STATE_WALK: {
            bool sum_mul_ok = (subop != 3 && subop != 8) || !scalar;
            mbz_ok = sum_mul_ok && (mask || s3 == 0);
            break;
        }
        default:
            mbz_ok = !is_mask_s3_only(code) || mask || (s3 == 0);
            break;
    }

    return mbz_ok ? IMPULSE_VM_V11_OK : IMPULSE_VM_V11_ERR_INVALID_OPERAND;
}

impulse_vm_status_v11_t check_jump_target(const uint64_t* words, size_t n, size_t pc, int32_t offset) noexcept {
    if (offset == 0) {
        return IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
    }
    int64_t tgt = static_cast<int64_t>(pc) + static_cast<int64_t>(offset);
    if (tgt < 0 || static_cast<size_t>(tgt) >= n) {
        return IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
    }
    if (IMP_V11_OPCODE(words[static_cast<size_t>(tgt)]) == static_cast<uint8_t>(Opcode::OP_EXTENSION_PAYLOAD)) {
        return IMPULSE_VM_V11_ERR_VALIDATION_FAILED;
    }
    return IMPULSE_VM_V11_OK;
}

enum WriteBank : uint8_t { kWriteNone = 0, kWriteR = 1, kWriteF = 2, kWriteV = 3 };

WriteBank writes_bank_inside_morsel(uint8_t op, uint8_t subop) noexcept {
    switch (static_cast<Opcode>(op)) {
        case Opcode::OP_NOP:
        case Opcode::OP_JMP:
        case Opcode::OP_JCC:
            return kWriteNone;
        case Opcode::OP_MOV:
            if (subop == 0 || subop == 3) return kWriteR;
            if (subop == 1 || subop == 4) return kWriteF;
            return kWriteV;
        case Opcode::OP_SWAP:
            if (subop == 0) return kWriteR;
            if (subop == 1) return kWriteF;
            return kWriteV;
        case Opcode::OP_SET_OP:
        case Opcode::OP_SET_COMPLEMENT:
            return kWriteF;
        case Opcode::OP_SET_CONVERT:
            return (subop == 1 || subop == 2) ? kWriteF : kWriteV;
        case Opcode::OP_GATHER_NODE_ATTR:
            return (subop == 2) ? kWriteR : kWriteV;
        case Opcode::OP_GATHER_EDGE_ATTR:
            return (subop == 1) ? kWriteR : kWriteV;
        case Opcode::OP_VEC_3VL_OP:
            return (subop == 11) ? kWriteNone : kWriteV;
        case Opcode::OP_GATHER_VEC:
        case Opcode::OP_VEC_CMP:
        case Opcode::OP_VEC_STR_PRED:
        case Opcode::OP_VEC_MATH_ARITH:
        case Opcode::OP_VEC_MATH_EXP_LOG:
        case Opcode::OP_VEC_MATH_TRIG_HYP:
        case Opcode::OP_VEC_MATH_ACT:
        case Opcode::OP_VEC_INT_BITWISE:
        case Opcode::OP_VEC_TERNARY:
        case Opcode::OP_VEC_BLEND:
        case Opcode::OP_COALESCE:
        case Opcode::OP_VEC_POINT:
        case Opcode::OP_VECTOR_RERANK:
            return kWriteV;
        default:
            return kWriteNone;
    }
}

inline bool check_uninit(uint8_t reg, const std::array<bool, 8>& written_v) noexcept {
    return reg >= 8u && !written_v[reg - 8u];
}

bool has_uninitialized_morsel_v_read(uint8_t op, uint8_t subop, bool scalar,
                                     uint8_t dst, uint8_t s1, uint8_t s2, uint8_t s3,
                                     const std::array<bool, 8>& wv) noexcept {
    switch (static_cast<Opcode>(op)) {
        case Opcode::OP_MOV:
            return subop == 2 && check_uninit(s1, wv);
        case Opcode::OP_SWAP:
            return subop == 2 && (check_uninit(dst, wv) || check_uninit(s1, wv));
        case Opcode::OP_SPILL:
            return subop == 2 && check_uninit(s1, wv);
        case Opcode::OP_SET_CONVERT:
            return (subop == 1 || subop == 2) && check_uninit(s1, wv);
        case Opcode::OP_GATHER_NODE_ATTR:
        case Opcode::OP_GATHER_EDGE_ATTR:
            return subop == 0 && check_uninit(s1, wv);
        case Opcode::OP_GATHER_VEC:
            return check_uninit(s1, wv) || check_uninit(s2, wv);
        case Opcode::OP_VEC_CMP:
            return check_uninit(s1, wv)
                || (subop != 7 && !scalar && check_uninit(s2, wv))
                || (subop == 6 && !scalar && check_uninit(s3, wv));
        case Opcode::OP_VEC_3VL_OP:
            return check_uninit(s1, wv)
                || ((subop == 0 || subop == 1 || subop == 3) && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_STR_PRED:
        case Opcode::OP_COALESCE:
            return check_uninit(s1, wv) || (!scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_MATH_ARITH:
            return check_uninit(s1, wv) || (subop <= 7 && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_MATH_EXP_LOG:
            return check_uninit(s1, wv) || ((subop == 8 || subop == 10) && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_MATH_TRIG_HYP:
            return check_uninit(s1, wv) || (subop == 6 && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_MATH_ACT:
            return check_uninit(s1, wv) || (subop == 1 && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_INT_BITWISE:
            return check_uninit(s1, wv)
                || ((subop <= 2 || (subop >= 4 && subop <= 9)) && !scalar && check_uninit(s2, wv));
        case Opcode::OP_VEC_TERNARY:
            return check_uninit(s1, wv)
                || (!scalar && check_uninit(s2, wv))
                || ((subop == 0 || subop == 2 || subop == 4 || subop == 6) && check_uninit(s3, wv));
        case Opcode::OP_VEC_BLEND:
            return check_uninit(s1, wv)
                || (!scalar && check_uninit(s2, wv))
                || ((subop == 0 || subop == 2) && check_uninit(s3, wv));
        case Opcode::OP_VECTOR_RERANK:
            return subop >= 4 && (check_uninit(s1, wv) || check_uninit(s2, wv));
        case Opcode::OP_VEC_POINT:
            return subop == 2 && check_uninit(s1, wv);
        case Opcode::OP_MORSEL_COMMIT:
            return (subop <= 7 && check_uninit(s1, wv))
                || (subop >= 1 && subop <= 7 && !scalar && check_uninit(s2, wv));
        default:
            return false;
    }
}

bool is_in_cfg_cycle(const uint64_t* words, size_t n, size_t target_pc, V11Arena* arena, bool* out_oom) {
    uint8_t stack_vis[2048];
    size_t stack_dfs[2048];
    std::vector<uint8_t> heap_vis;
    std::vector<size_t> heap_dfs;
    uint8_t* visited = nullptr;
    size_t* stack_buf = nullptr;
    size_t saved_pos = arena ? arena->pos : 0u;

    if (n <= 2048u) {
        std::memset(stack_vis, 0, n);
        visited = stack_vis;
        stack_buf = stack_dfs;
    } else if (arena != nullptr) {
        visited = arena->alloc_array<uint8_t>(n, 8);
        stack_buf = arena->alloc_array<size_t>(n, 8);
        if (!visited || !stack_buf) {
            arena->pos = saved_pos;
            if (out_oom) *out_oom = true;
            return false;
        }
        std::memset(visited, 0, n);
    } else {
        heap_vis.assign(n, 0);
        heap_dfs.resize(n);
        visited = heap_vis.data();
        stack_buf = heap_dfs.data();
    }
    size_t sp = 0u;

    auto finish = [&](bool r) -> bool {
        if (arena != nullptr) arena->pos = saved_pos;
        return r;
    };

    bool ext0 = (IMP_V11_FLAGS(words[target_pc]) & FLAG_EXTENDED) != 0u;
    size_t start = target_pc + (ext0 ? 2u : 1u);
    if (start < n) {
        if (start == target_pc) return finish(true);
        visited[start] = 1;
        stack_buf[sp++] = start;
    }

    while (sp > 0u) {
        size_t cur = stack_buf[--sp];
        uint64_t w = words[cur];
        uint8_t op = IMP_V11_OPCODE(w);
        bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;
        size_t next_pc = cur + (ext ? 2u : 1u);
        int32_t imm32 = IMP_V11_IMM32_S(w);

        switch (static_cast<Opcode>(op)) {
            case Opcode::OP_HALT:
            case Opcode::OP_TRAP:
            case Opcode::OP_THROW:
            case Opcode::OP_RET:
                break;
            case Opcode::OP_JMP: {
                int64_t t = static_cast<int64_t>(cur) + imm32;
                if (t == static_cast<int64_t>(target_pc)) return finish(true);
                if (t >= 0 && static_cast<size_t>(t) < n && !visited[static_cast<size_t>(t)]) {
                    visited[static_cast<size_t>(t)] = 1;
                    stack_buf[sp++] = static_cast<size_t>(t);
                }
                break;
            }
            case Opcode::OP_JCC:
            case Opcode::OP_LOOP_DECR:
            case Opcode::OP_CALL: {
                int64_t t = static_cast<int64_t>(cur) + imm32;
                if (t == static_cast<int64_t>(target_pc) || next_pc == target_pc) return finish(true);
                if (next_pc < n && !visited[next_pc]) {
                    visited[next_pc] = 1;
                    stack_buf[sp++] = next_pc;
                }
                if (t >= 0 && static_cast<size_t>(t) < n && !visited[static_cast<size_t>(t)]) {
                    visited[static_cast<size_t>(t)] = 1;
                    stack_buf[sp++] = static_cast<size_t>(t);
                }
                break;
            }
            default: {
                if (next_pc == target_pc) return finish(true);
                if (next_pc < n && !visited[next_pc]) {
                    visited[next_pc] = 1;
                    stack_buf[sp++] = next_pc;
                }
                break;
            }
        }
    }
    return finish(false);
}

} // namespace

impulse_vm_validation_v11_t validate_words_in_arena(
    const uint64_t* words,
    size_t word_count,
    V11Arena* arena
) noexcept {
    if (!words || word_count == 0) {
        return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, 0u};
    }
    const size_t saved_arena_pos = arena ? arena->pos : 0u;
    struct ArenaRewind {
        V11Arena* a;
        size_t pos;
        ~ArenaRewind() noexcept { if (a) a->pos = pos; }
    } rewind_guard{arena, saved_arena_pos};
    auto ret = [&](impulse_vm_validation_v11_t v) noexcept {
        return v;
    };

    // Pass 1: Decode words and verify 128-bit FLAG_EXTENDED <-> 0xFF pairing
    size_t pc = 0;
    while (pc < word_count) {
        uint64_t w = words[pc];
        impulse_vm_status_v11_t st = validate_word(w);
        if (st != IMPULSE_VM_V11_OK) {
            return {st, static_cast<uint32_t>(pc)};
        }
        uint8_t op = IMP_V11_OPCODE(w);
        bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;
        if (op == static_cast<uint8_t>(Opcode::OP_EXTENSION_PAYLOAD)) {
            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
        }
        if (ext) {
            if (pc + 1 >= word_count) {
                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
            }
            uint64_t w2 = words[pc + 1];
            impulse_vm_status_v11_t st2 = validate_word(w2);
            if (st2 != IMPULSE_VM_V11_OK) {
                return {st2, static_cast<uint32_t>(pc + 1)};
            }
            if (IMP_V11_OPCODE(w2) != static_cast<uint8_t>(Opcode::OP_EXTENSION_PAYLOAD)) {
                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc + 1)};
            }
            pc += 2;
        } else {
            pc += 1;
        }
    }

    // Pass 2: Verify CFG targets and lexical morsel blocks
    int64_t m_hdr_pc = -1;
    size_t m_end_pc = 0;
    uint8_t m_outer_src_f = 0;
    int m_outer_mask_f = -1;
    uint8_t stack_mj[256];
    uint8_t stack_mw[256];
    std::vector<uint8_t> heap_mj;
    std::vector<uint8_t> heap_mw;
    uint8_t* m_has_join = nullptr;
    uint8_t* m_join_written = nullptr;
    std::array<bool, 8> m_written_v{};

    uint8_t stack_mi[2048];
    std::vector<uint8_t> heap_mi;
    uint8_t* in_morsel_interior = stack_mi;
    if (word_count <= 2048u) {
        std::memset(stack_mi, 0, word_count);
    } else if (arena != nullptr) {
        in_morsel_interior = arena->alloc_array<uint8_t>(word_count, 8);
        if (!in_morsel_interior) return ret({IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW, 0u});
        std::memset(in_morsel_interior, 0, word_count);
    } else {
        heap_mi.assign(word_count, 0);
        in_morsel_interior = heap_mi.data();
    }

    pc = 0;
    while (pc < word_count) {
        uint64_t w = words[pc];
        uint8_t op    = IMP_V11_OPCODE(w);
        uint8_t subop = IMP_V11_SUBOP(w);
        uint8_t flags = IMP_V11_FLAGS(w);
        uint8_t s1    = IMP_V11_S1(w);
        uint8_t dst   = IMP_V11_DST(w);
        uint8_t s3    = IMP_V11_S3(w);
        uint8_t s2    = IMP_V11_S2(w);
        int32_t imm32 = IMP_V11_IMM32_S(w);
        uint16_t imm_hi = IMP_V11_IMM16_HI(w);
        bool ext    = (flags & FLAG_EXTENDED) != 0u;
        bool mask   = (flags & FLAG_MASKED_S3) != 0u;
        bool scalar = (flags & FLAG_SCALAR_RHS) != 0u;
        size_t next_pc = ext ? pc + 2 : pc + 1;

        if (m_hdr_pc < 0) {
            if (op == static_cast<uint8_t>(Opcode::OP_MORSEL_COMMIT) ||
                op == static_cast<uint8_t>(Opcode::OP_MORSEL_END)) {
                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
            } else if (op == static_cast<uint8_t>(Opcode::OP_WALK_MORSEL) ||
                       op == static_cast<uint8_t>(Opcode::OP_FILTER_MORSEL)) {
                uint32_t end_off = imm_hi;
                size_t end_pc = pc + end_off;
                if (end_off < 1u || end_pc >= word_count || s1 >= 8u || (mask && s3 >= 8u)) {
                    return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                }
                uint64_t end_w = words[end_pc];
                if (IMP_V11_OPCODE(end_w) != static_cast<uint8_t>(Opcode::OP_MORSEL_END) ||
                    IMP_V11_IMM16_HI(end_w) != end_off) {
                    return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                }
                for (size_t mp = pc + 1; mp <= end_pc; ++mp) {
                    in_morsel_interior[mp] = 1;
                }
                bool is_filter = (op == static_cast<uint8_t>(Opcode::OP_FILTER_MORSEL));
                m_hdr_pc = static_cast<int64_t>(pc);
                m_end_pc = end_pc;
                m_outer_src_f = s1;
                m_outer_mask_f = mask ? static_cast<int>(s3) : -1;
                size_t m_span = end_pc - pc + 1u;
                if (m_span <= 256u) {
                    std::memset(stack_mj, 0, m_span);
                    std::memset(stack_mw, 0, m_span);
                    m_has_join = stack_mj;
                    m_join_written = stack_mw;
                } else if (arena != nullptr) {
                    m_has_join = arena->alloc_array<uint8_t>(m_span, 8);
                    m_join_written = arena->alloc_array<uint8_t>(m_span, 8);
                    if (!m_has_join || !m_join_written) return ret({IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW, static_cast<uint32_t>(pc)});
                    std::memset(m_has_join, 0, m_span);
                    std::memset(m_join_written, 0, m_span);
                } else {
                    heap_mj.assign(m_span, 0);
                    heap_mw.assign(m_span, 0);
                    m_has_join = heap_mj.data();
                    m_join_written = heap_mw.data();
                }
                m_written_v.fill(false);
                m_written_v[0] = true; // M0
                m_written_v[3] = true; // M3
                if (!is_filter) {
                    m_written_v[1] = true; // M1
                    m_written_v[2] = true; // M2
                }
            } else if (op == static_cast<uint8_t>(Opcode::OP_JMP) ||
                       op == static_cast<uint8_t>(Opcode::OP_JCC) ||
                       op == static_cast<uint8_t>(Opcode::OP_LOOP_DECR) ||
                       op == static_cast<uint8_t>(Opcode::OP_CALL)) {
                impulse_vm_status_v11_t st = check_jump_target(words, word_count, pc, imm32);
                if (st != IMPULSE_VM_V11_OK) {
                    return {st, static_cast<uint32_t>(pc)};
                }
            }
        } else {
            if (pc == m_end_pc) {
                m_hdr_pc = -1;
                m_end_pc = 0;
            } else {
                size_t rel_cur = pc - static_cast<size_t>(m_hdr_pc);
                if (m_has_join[rel_cur]) {
                    uint8_t jm = m_join_written[rel_cur];
                    for (size_t k = 0; k < 8; ++k) {
                        m_written_v[k] = m_written_v[k] && (((jm >> k) & 1u) != 0u);
                    }
                }
                if (has_uninitialized_morsel_v_read(op, subop, scalar, dst, s1, s2, s3, m_written_v)) {
                    return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                }
                MorselRole role = kOpcodeMeta[op].morsel_role;
                switch (role) {
                    case MorselRole::Forbidden:
                    case MorselRole::Header:
                    case MorselRole::EndOnly:
                        return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                    case MorselRole::FwdJumpOnly: {
                        bool jcc_ok = (op == static_cast<uint8_t>(Opcode::OP_JMP))
                            || (op == static_cast<uint8_t>(Opcode::OP_JCC) && subop >= 6 && subop <= 9 && (subop < 8 || s1 >= 8));
                        if (!jcc_ok || imm32 <= 0) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        impulse_vm_status_v11_t st = check_jump_target(words, word_count, pc, imm32);
                        int64_t tgt = static_cast<int64_t>(pc) + imm32;
                        if (st != IMPULSE_VM_V11_OK || tgt > static_cast<int64_t>(m_end_pc)) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        size_t rel = static_cast<size_t>(tgt) - static_cast<size_t>(m_hdr_pc);
                        uint8_t cur_mask = 0u;
                        for (size_t k = 0; k < 8; ++k) if (m_written_v[k]) cur_mask |= static_cast<uint8_t>(1u << k);
                        if (!m_has_join[rel]) {
                            m_has_join[rel] = 1;
                            m_join_written[rel] = cur_mask;
                        } else {
                            m_join_written[rel] &= cur_mask;
                        }
                        break;
                    }
                    case MorselRole::CommitOnly: {
                        bool s1_ok = (subop == 8) || (s1 >= 8);
                        bool s2_ok = (subop == 0) || (subop == 8) || scalar || (s2 >= 8);
                        bool s3_ok = !mask || (s3 >= 8);
                        bool aliases_outer_f = (subop == 0 || subop == 1)
                            && (dst == m_outer_src_f || static_cast<int>(dst) == m_outer_mask_f);
                        if (dst >= 8 || !s1_ok || !s2_ok || !s3_ok || aliases_outer_f) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        break;
                    }
                    case MorselRole::BodyVOnly: {
                        if (subop != 2) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        if (op == static_cast<uint8_t>(Opcode::OP_SPILL)) {
                            if (s1 < 8) {
                                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                            }
                        } else if (op == static_cast<uint8_t>(Opcode::OP_RELOAD)) {
                            if (dst < 8) {
                                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                            }
                            m_written_v[dst - 8] = true;
                        } else {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        break;
                    }
                    case MorselRole::BodyPairOnly:
                    case MorselRole::BodyCastOnly:
                    case MorselRole::BodyOk: {
                        if (role == MorselRole::BodyPairOnly && subop < 4) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        if (role == MorselRole::BodyCastOnly && subop != 2) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        if (mask && s3 < 8) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        }
                        WriteBank wb = writes_bank_inside_morsel(op, subop);
                        if (wb == kWriteR) {
                            return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                        } else if (wb == kWriteF) {
                            if (dst < 8 || (op == static_cast<uint8_t>(Opcode::OP_SWAP) && s1 < 8)) {
                                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                            }
                        } else if (wb == kWriteV) {
                            if (dst < 8 || (op == static_cast<uint8_t>(Opcode::OP_SWAP) && s1 < 8)) {
                                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
                            }
                            m_written_v[dst - 8] = true;
                        }
                        break;
                    }
                }
            }
        }
        pc = next_pc;
    }
    if (m_hdr_pc >= 0) {
        return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(m_hdr_pc)};
    }

    // Pass 2b: Forbid outer branches/calls from jumping into the interior of any morsel block
    pc = 0;
    while (pc < word_count) {
        uint64_t w = words[pc];
        uint8_t op = IMP_V11_OPCODE(w);
        bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;
        if (!in_morsel_interior[pc] &&
            (op == static_cast<uint8_t>(Opcode::OP_JMP) ||
             op == static_cast<uint8_t>(Opcode::OP_JCC) ||
             op == static_cast<uint8_t>(Opcode::OP_LOOP_DECR) ||
             op == static_cast<uint8_t>(Opcode::OP_CALL))) {
            int64_t tgt = static_cast<int64_t>(pc) + IMP_V11_IMM32_S(w);
            if (tgt >= 0 && static_cast<size_t>(tgt) < word_count && in_morsel_interior[static_cast<size_t>(tgt)]) {
                return {IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)};
            }
        }
        pc += ext ? 2u : 1u;
    }

    // Pass 3: Reject OP_SAMPLE_NEIGHBORS.vec_wr (subop=1) inside any CFG loop cycle
    pc = 0;
    while (pc < word_count) {
        uint64_t w = words[pc];
        uint8_t op = IMP_V11_OPCODE(w);
        uint8_t subop = IMP_V11_SUBOP(w);
        bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;
        if (op == static_cast<uint8_t>(Opcode::OP_SAMPLE_NEIGHBORS) && subop == 1u) {
            bool oom = false;
            if (is_in_cfg_cycle(words, word_count, pc, arena, &oom)) {
                return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, static_cast<uint32_t>(pc)});
            }
            if (oom) {
                return ret({IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW, static_cast<uint32_t>(pc)});
            }
        }
        pc += ext ? 2u : 1u;
    }

    return ret({IMPULSE_VM_V11_OK, 0u});
}

impulse_vm_validation_v11_t validate_impb_in_arena(
    const void* impb_bytes,
    size_t impb_len,
    V11Arena* arena
) noexcept {
    const size_t saved_arena_pos = arena ? arena->pos : 0u;
    struct ArenaRewind {
        V11Arena* a;
        size_t pos;
        ~ArenaRewind() noexcept { if (a) a->pos = pos; }
    } rewind_guard{arena, saved_arena_pos};
    auto ret = [&](impulse_vm_validation_v11_t v) noexcept {
        return v;
    };
    try {
        if (!impb_bytes || impb_len < sizeof(impulse_impb_header_v11_t)) {
            return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, 0u});
        }
        impulse_impb_header_v11_t hdr{};
        std::memcpy(&hdr, impb_bytes, sizeof(hdr));
        if (hdr.magic != kImpbMagic || hdr.isa_version != kImpbVersion ||
            (hdr.header_flags & 0xFEu) != 0u || hdr.max_call_depth > kMaxCallDepth ||
            hdr.reserved != 0u || hdr.code_word_count == 0u) {
            return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, 0u});
        }
        size_t code_bytes = static_cast<size_t>(hdr.code_word_count) * sizeof(uint64_t);
        size_t payload_bytes = code_bytes + static_cast<size_t>(hdr.const_pool_bytes);
        if (payload_bytes < code_bytes || sizeof(hdr) + payload_bytes > impb_len) {
            return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, 0u});
        }
        if (hdr.entry_pc >= hdr.code_word_count) {
            return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, hdr.entry_pc});
        }
        const auto* payload_ptr = static_cast<const uint8_t*>(impb_bytes) + sizeof(hdr);
        if (hdr.program_xxh3_64 != 0ULL) {
            uint64_t actual = V11Xxh3::hash64(payload_ptr, payload_bytes);
            if (actual != hdr.program_xxh3_64) {
                return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, 0u});
            }
        }
        uint64_t stack_words[2048];
        std::vector<uint64_t> heap_words;
        uint64_t* words_ptr = stack_words;
        if (hdr.code_word_count > 2048u) {
            if (arena != nullptr) {
                words_ptr = arena->alloc_array<uint64_t>(hdr.code_word_count, 8);
                if (!words_ptr) return ret({IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW, 0u});
            } else {
                heap_words.resize(hdr.code_word_count);
                words_ptr = heap_words.data();
            }
        }
        std::memcpy(words_ptr, payload_ptr, code_bytes);
        impulse_vm_validation_v11_t vr = validate_words_in_arena(words_ptr, hdr.code_word_count, arena);
        if (vr.status != IMPULSE_VM_V11_OK) {
            return ret(vr);
        }
        // Reject entry_pc landing on 0xFF OP_EXTENSION_PAYLOAD or inside a morsel interior
        if (IMP_V11_OPCODE(words_ptr[hdr.entry_pc]) == static_cast<uint8_t>(Opcode::OP_EXTENSION_PAYLOAD)) {
            return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, hdr.entry_pc});
        }
        for (size_t pc = 0; pc < hdr.code_word_count; ) {
            uint64_t w = words_ptr[pc];
            uint8_t op = IMP_V11_OPCODE(w);
            bool ext = (IMP_V11_FLAGS(w) & FLAG_EXTENDED) != 0u;
            if (op == static_cast<uint8_t>(Opcode::OP_WALK_MORSEL) ||
                op == static_cast<uint8_t>(Opcode::OP_FILTER_MORSEL)) {
                size_t end_pc = pc + IMP_V11_IMM16_HI(w);
                if (hdr.entry_pc > pc && hdr.entry_pc <= end_pc) {
                    return ret({IMPULSE_VM_V11_ERR_VALIDATION_FAILED, hdr.entry_pc});
                }
                pc = end_pc + 1u;
            } else {
                pc += ext ? 2u : 1u;
            }
        }
        return ret({IMPULSE_VM_V11_OK, 0u});
    } catch (...) {
        return ret({IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW, 0u});
    }
}

} // namespace impulse::vm::v11

extern "C" IMPULSE_API impulse_vm_validation_v11_t impulse_vm_validate_words_v11(
    const uint64_t* words,
    size_t word_count
) {
    return impulse::vm::v11::validate_words_in_arena(words, word_count, nullptr);
}

extern "C" IMPULSE_API impulse_vm_validation_v11_t impulse_vm_validate_v11(
    const void* impb_bytes,
    size_t impb_len
) {
    return impulse::vm::v11::validate_impb_in_arena(impb_bytes, impb_len, nullptr);
}
