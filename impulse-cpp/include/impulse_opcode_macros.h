/**
 * @file impulse_opcode_macros.h
 * @brief Self-documenting zero-overhead operand extraction, bitmask, type-code,
 *        bitset, and SQLite-style verification macros for ImpulseVM opcodes.
 *
 * Eliminates raw magic hex masks/shifts across `src/opcodes/<op>.inc` and pairs
 * instruction execution with zero-Release-cost SQLite-style invariants and
 * boundary probes (`IMPULSE_ASSERT`, `IMPULSE_WELL_FORMED_ASSERT`,
 * `IMPULSE_ALWAYS`, `IMPULSE_NEVER`, `IMPULSE_TESTCASE`, `IMP_VM_COV_TWO_WAY`).
 */
#ifndef IMPULSE_OPCODE_MACROS_H
#define IMPULSE_OPCODE_MACROS_H

#include "impulse_vm.h"
#include "impulse_assert.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Portable 64-bit unsigned overflow-checked addition & multiplication
 * (uses compiler intrinsics on Clang/GCC and defined unsigned arithmetic on MSVC).
 * Returns true if arithmetic overflow occurred, false otherwise.
 */
static inline bool impulse_u64_add_overflow(uint64_t a, uint64_t b, uint64_t* out) {
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow)
    return __builtin_add_overflow(a, b, out);
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 5)
    return __builtin_add_overflow(a, b, out);
#endif
    uint64_t sum = a + b;
    *out = sum;
    return sum < a;
}

static inline bool impulse_u64_mul_overflow(uint64_t a, uint64_t b, uint64_t* out) {
#if defined(__has_builtin)
#  if __has_builtin(__builtin_mul_overflow)
    return __builtin_mul_overflow(a, b, out);
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 5)
    return __builtin_mul_overflow(a, b, out);
#endif
    if (a != 0ULL && b > (UINT64_MAX / a)) {
        *out = 0ULL;
        return true;
    }
    *out = a * b;
    return false;
}

/* ========================================================================= */
/* 1. Register File, Stream Shader, & Multi-Lane Architectural Bounds        */
/* ========================================================================= */

#define IMP_VM_GPR_COUNT                64u
#define IMP_VM_GPR_MAX_INDEX            63u
#define IMP_VM_STREAM_REG_COUNT         16u
#define IMP_VM_STREAM_REG_MAX_INDEX     15u
#define IMP_VM_MAX_LANES                64u
#define IMP_VM_CALL_STACK_MAX_DEPTH     64u

/* ========================================================================= */
/* 2. 64-bit Instruction Word Field & Payload Unpacking Macros               */
/* ========================================================================= */

#define IMP_INST_OPCODE(inst)           ((inst).opcode)
#define IMP_INST_FLAGS(inst)            ((inst).flags)
#define IMP_INST_DST(inst)              ((inst).dst_reg)
#define IMP_INST_PAYLOAD_U32(inst)      ((inst).payload)
#define IMP_INST_PAYLOAD_I32(inst)      (static_cast<int32_t>((inst).payload))

/** Extract individual 8-bit unsigned operand bytes [7:0], [15:8], [23:16], [31:24] */
#define IMP_U32_BYTE0(val)              (static_cast<uint8_t>((static_cast<uint32_t>(val)) & 0xFFu))
#define IMP_U32_BYTE1(val)              (static_cast<uint8_t>(((static_cast<uint32_t>(val)) >> 8) & 0xFFu))
#define IMP_U32_BYTE2(val)              (static_cast<uint8_t>(((static_cast<uint32_t>(val)) >> 16) & 0xFFu))
#define IMP_U32_BYTE3(val)              (static_cast<uint8_t>(((static_cast<uint32_t>(val)) >> 24) & 0xFFu))

/** Extract 16-bit halves [15:0] and [31:16] from a 32-bit payload */
#define IMP_U32_LOW16(val)              (static_cast<uint16_t>((static_cast<uint32_t>(val)) & 0xFFFFu))
#define IMP_U32_HIGH16(val)             (static_cast<uint16_t>(((static_cast<uint32_t>(val)) >> 16) & 0xFFFFu))
#define IMP_I32_LOW16(val)              (static_cast<int16_t>((static_cast<uint32_t>(val)) & 0xFFFFu))
#define IMP_I32_HIGH16(val)             (static_cast<int16_t>(((static_cast<uint32_t>(val)) >> 16) & 0xFFFFu))

/** Instruction-level convenience wrappers */
#define IMP_INST_U8_0(inst)             IMP_U32_BYTE0((inst).payload)
#define IMP_INST_U8_1(inst)             IMP_U32_BYTE1((inst).payload)
#define IMP_INST_U8_2(inst)             IMP_U32_BYTE2((inst).payload)
#define IMP_INST_U8_3(inst)             IMP_U32_BYTE3((inst).payload)
#define IMP_INST_LOW16(inst)            IMP_U32_LOW16((inst).payload)
#define IMP_INST_HIGH16(inst)           IMP_U32_HIGH16((inst).payload)
#define IMP_INST_I16_LOW(inst)          IMP_I32_LOW16((inst).payload)
#define IMP_INST_I16_HIGH(inst)         IMP_I32_HIGH16((inst).payload)

/** Payload / 64-bit word packing helpers */
#define IMP_PACK_U16X2(low16, high16) \
    ((static_cast<uint32_t>(low16) & 0xFFFFu) | ((static_cast<uint32_t>(high16) & 0xFFFFu) << 16))

#define IMP_PACK_U8X4(b0, b1, b2, b3) \
    ((static_cast<uint32_t>(b0) & 0xFFu) | \
     ((static_cast<uint32_t>(b1) & 0xFFu) << 8) | \
     ((static_cast<uint32_t>(b2) & 0xFFu) << 16) | \
     ((static_cast<uint32_t>(b3) & 0xFFu) << 24))

#define IMP_PACK_U64_FROM_U32(low32, high32) \
    (static_cast<uint64_t>(static_cast<uint32_t>(low32)) | \
     (static_cast<uint64_t>(static_cast<uint32_t>(high32)) << 32))

/* ========================================================================= */
/* 3. Instruction Flag Bitmasks & Predicates                                 */
/* ========================================================================= */

#define IMP_FLAG_NONE                   0x00u
#define IMP_FLAG_BIT0                   0x01u
#define IMP_FLAG_BIT1                   0x02u
#define IMP_FLAG_BIT2                   0x04u
#define IMP_FLAG_BIT3                   0x08u
#define IMP_FLAG_BIT4                   0x10u
#define IMP_FLAG_LOW2_MASK              0x03u
#define IMP_FLAG_LOW4_MASK              0x0Fu
#define IMP_FLAG_LOW7_MASK              0x7Fu
#define IMP_FLAG_EXTENDED_WORD          0x80u

/** Named semantic instruction flag aliases */
#define IMP_FLAG_HALT_ON_EMPTY          0x01u
#define IMP_FLAG_IN_PLACE               0x01u
#define IMP_FLAG_DOUBLE_PRECISION       0x01u
#define IMP_FLAG_ATTR_IS_REG            0x01u
#define IMP_FLAG_EXTENDED_QUORUM        0x01u
#define IMP_FLAG_INVERT_MASK            0x01u
#define IMP_FLAG_SCALAR_OPERAND         0x01u
#define IMP_FLAG_ACCUMULATE_DEST        0x02u
#define IMP_FLAG_DELTA_FIXPOINT         0x04u

#define IMP_INST_IS_EXTENDED(inst)      (((inst).flags & IMP_FLAG_EXTENDED_WORD) != 0u)
#define IMP_INST_HAS_FLAG(inst, mask)   (((inst).flags & (mask)) != 0u)

/* ========================================================================= */
/* 4. Bitset Word Addressing, Lane Bitmasks & Dense ID Sentinels             */
/* ========================================================================= */

#define IMP_BITS_PER_WORD               64ULL
#define IMP_WORD_BIT_SHIFT              6u
#define IMP_WORD_BIT_MASK               63u

#define IMP_WORD_INDEX(bit_idx)         (static_cast<size_t>(bit_idx) >> IMP_WORD_BIT_SHIFT)
#define IMP_BIT_IN_WORD(bit_idx)        (static_cast<uint32_t>(bit_idx) & IMP_WORD_BIT_MASK)
#define IMP_BIT_MASK64(bit_offset)      (1ULL << ((static_cast<uint32_t>(bit_offset)) & IMP_WORD_BIT_MASK))
#define IMP_WORD_BIT_TO_NODE(w_idx, b)  ((static_cast<uint64_t>(w_idx) << IMP_WORD_BIT_SHIFT) + static_cast<uint64_t>(b))
#define IMP_CLEAR_LOWEST_BIT(word)      ((word) & ((word) - 1ULL))

#define IMP_ALL_LANES_MASK64            (~0ULL)
#define IMP_LANE_MASK_FOR_COUNT(k)      ((k) >= IMP_VM_MAX_LANES ? IMP_ALL_LANES_MASK64 : (((k) == 0u) ? 0ULL : ((1ULL << (k)) - 1ULL)))

#define IMP_SENTINEL_U16                0xFFFFu
#define IMP_SENTINEL_U32                0xFFFFFFFFu
#define IMP_SENTINEL_U64                UINT64_MAX
#define IMP_INVALID_NODE_U64            (~0ULL)

/* ========================================================================= */
/* 5. Spec v0.10.0 Attribute Type Codes, Alignment & Validity Bitmaps        */
/* ========================================================================= */

#define IMP_ATTR_NULLABLE_BIT           0x80u
#define IMP_ATTR_BASE_TYPE_MASK         0x7Fu
#define IMP_ATTR_IS_NULLABLE(tc)        (((static_cast<uint8_t>(tc)) & IMP_ATTR_NULLABLE_BIT) != 0u)
#define IMP_ATTR_BASE_TYPE(tc)          (static_cast<uint8_t>((static_cast<uint8_t>(tc)) & IMP_ATTR_BASE_TYPE_MASK))

#define IMP_SNAP_SECTION_ALIGN          128ULL
#define IMP_ALIGN_UP_128(bytes)         (((static_cast<uint64_t>(bytes)) + (IMP_SNAP_SECTION_ALIGN - 1ULL)) & ~(IMP_SNAP_SECTION_ALIGN - 1ULL))
#define IMP_VALIDITY_RAW_BYTES(rows)    ((((static_cast<uint64_t>(rows)) + 63ULL) >> 6) << 3)
#define IMP_VALIDITY_PADDED_BYTES(rows) IMP_ALIGN_UP_128(IMP_VALIDITY_RAW_BYTES(rows))
#define IMP_VALIDITY_BYTE_OFFSET(idx)   ((static_cast<uint64_t>(idx)) >> 3)
#define IMP_VALIDITY_BIT_MASK(idx)      (static_cast<uint8_t>(1u << ((static_cast<uint32_t>(idx)) & 7u)))
#define IMP_VALIDITY_IS_VALID(ptr, idx) (((reinterpret_cast<const uint8_t*>(ptr))[IMP_VALIDITY_BYTE_OFFSET(idx)] & IMP_VALIDITY_BIT_MASK(idx)) != 0u)

/** Normative Spec v0.10.0 Scalar Attribute Type Codes (base type & 0x7F) */
#define IMP_TYPE_CODE_INT8              0x01u
#define IMP_TYPE_CODE_INT16             0x02u
#define IMP_TYPE_CODE_INT32             0x03u
#define IMP_TYPE_CODE_INT64             0x04u
#define IMP_TYPE_CODE_FLOAT16           0x05u
#define IMP_TYPE_CODE_FLOAT32           0x06u
#define IMP_TYPE_CODE_FLOAT64           0x07u
#define IMP_TYPE_CODE_TIMESTAMP_MS      0x08u
#define IMP_TYPE_CODE_TIMESTAMP_NS      0x09u
#define IMP_TYPE_CODE_FIXED_BYTES       0x0Au
#define IMP_TYPE_CODE_VAR_STRING        0x0Bu
#define IMP_TYPE_CODE_VAR_BYTES         0x0Cu
#define IMP_TYPE_CODE_INTERVAL_SEC32    0x0Du
#define IMP_TYPE_CODE_INTERVAL_MS64     0x0Eu
#define IMP_TYPE_CODE_UINT8             0x11u
#define IMP_TYPE_CODE_UINT16            0x12u
#define IMP_TYPE_CODE_UINT32            0x13u
#define IMP_TYPE_CODE_UINT64            0x14u

/** Legacy v0.9 Scalar Type Code Compatibility Constants */
#define IMP_LEGACY_V09_FLOAT32          0x05u
#define IMP_LEGACY_V09_FLOAT64          0x06u
#define IMP_LEGACY_V09_STRING           0x07u
#define IMP_LEGACY_ALT_FLOAT32          0x08u
#define IMP_LEGACY_ALT_FLOAT64          0x09u

/* ========================================================================= */
/* 6. Monoidic Reducer, Semiring & Vector Similarity Selector Constants      */
/* ========================================================================= */

#define IMP_STREAM_MONOID_SUM           0u
#define IMP_STREAM_MONOID_MAX           1u
#define IMP_STREAM_MONOID_MIN           2u

#define IMP_CSR_REDUCE_OP_MIN           0u
#define IMP_CSR_REDUCE_OP_MAX           1u
#define IMP_CSR_REDUCE_OP_SUM           2u

#define IMP_LANE_REDUCE_SUM             0u
#define IMP_LANE_REDUCE_MUL             1u
#define IMP_LANE_REDUCE_MIN             2u
#define IMP_LANE_REDUCE_MAX             3u
#define IMP_LANE_REDUCE_COUNT           4u
#define IMP_LANE_REDUCE_ARGMIN          6u
#define IMP_LANE_REDUCE_ARGMAX          7u

#define IMP_VEC_METRIC_DOT              0u
#define IMP_VEC_METRIC_NEG_L2_SQ        1u
#define IMP_VEC_METRIC_COSINE           2u

/* ========================================================================= */
/* 7. SQLite-Style Opcode Guard, Status Flag & Branch Coverage Macros        */
/* ========================================================================= */

#define IMP_VALIDATE_GPR(r) \
    do { \
        IMPULSE_TESTCASE((r) == IMP_VM_GPR_MAX_INDEX); \
        IMPULSE_TESTCASE((r) >= IMP_VM_GPR_COUNT); \
        if (IMPULSE_UNLIKELY((r) >= IMP_VM_GPR_COUNT)) { \
            return IMPULSE_VM_ERR_INVALID_REGISTER; \
        } \
    } while (0)

#define IMP_VALIDATE_STREAM_REG(sr) \
    do { \
        IMPULSE_TESTCASE((sr) == IMP_VM_STREAM_REG_MAX_INDEX); \
        IMPULSE_TESTCASE((sr) >= IMP_VM_STREAM_REG_COUNT); \
        if (IMPULSE_UNLIKELY((sr) >= IMP_VM_STREAM_REG_COUNT)) { \
            return IMPULSE_VM_ERR_INVALID_REGISTER; \
        } \
    } while (0)

#define IMP_REQUIRE_CONTEXT(vm_state) \
    do { \
        IMPULSE_TESTCASE((vm_state)->query_context == nullptr); \
        if (IMPULSE_UNLIKELY(!(vm_state)->query_context)) { \
            return IMPULSE_VM_ERR_NULL_SNAPSHOT; \
        } \
    } while (0)

#define IMP_REQUIRE_REL_SLOT(vm_state, rel_id) \
    do { \
        IMPULSE_TESTCASE((rel_id) >= (vm_state)->query_context->slots.size()); \
        if (IMPULSE_UNLIKELY((rel_id) >= (vm_state)->query_context->slots.size())) { \
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS; \
        } \
    } while (0)

#define IMP_VM_SET_ZF(vm_state, zero_cond) \
    do { \
        const bool _imp_zf = static_cast<bool>(zero_cond); \
        IMPULSE_TESTCASE(_imp_zf); \
        if (_imp_zf) { \
            (vm_state)->flags |= IMPULSE_VM_FLAG_ZF; \
        } else { \
            (vm_state)->flags &= static_cast<uint8_t>(~IMPULSE_VM_FLAG_ZF); \
        } \
    } while (0)

#define IMP_VM_SET_ST(vm_state, stable_cond) \
    do { \
        const bool _imp_st = static_cast<bool>(stable_cond); \
        IMPULSE_TESTCASE(_imp_st); \
        if (_imp_st) { \
            (vm_state)->flags |= IMPULSE_VM_FLAG_ST; \
        } else { \
            (vm_state)->flags &= static_cast<uint8_t>(~IMPULSE_VM_FLAG_ST); \
        } \
    } while (0)

#define IMP_VM_IS_ZF(vm_state)          (((vm_state)->flags & IMPULSE_VM_FLAG_ZF) != 0u)
#define IMP_VM_IS_ST(vm_state)          (((vm_state)->flags & IMPULSE_VM_FLAG_ST) != 0u)

/**
 * SQLite VdbeCoverage equivalent for conditional control-flow / early-exit VM branches.
 * Records branch outcome (0 = taken, 1 = fallthrough) when g_vm_branch_cov_fast is enabled.
 */
#define IMP_VM_COV_BRANCH(vm_state, opcode_id, site_id, taken_cond) \
    do { \
        const bool _imp_branch_taken = static_cast<bool>(taken_cond); \
        if (IMPULSE_UNLIKELY(g_vm_branch_cov_fast)) { \
            impulse_vm_branch_coverage_record( \
                static_cast<uint32_t>((vm_state)->pc), \
                static_cast<uint8_t>(opcode_id), \
                static_cast<uint32_t>(site_id), \
                IMPULSE_COV_KIND_TWO_WAY, \
                IMPULSE_COV_BOTH, \
                _imp_branch_taken ? 0u : 1u); \
        } \
    } while (0)

#endif /* IMPULSE_OPCODE_MACROS_H */
