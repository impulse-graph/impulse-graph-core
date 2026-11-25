/**
 * @file impulse_v11_opcode_macros.h
 * @brief Zero-overhead v0.11.0 opcode contract guards, SQLite-style assertion/testcase probes,
 *        overflow-checked arithmetic, and dispatch table registration macros.
 *
 * Preserves the v0.10 `impulse_opcode_macros.h` pattern (`IMPULSE_ASSERT`, `IMPULSE_TESTCASE`,
 * `IMPULSE_ALWAYS`, `IMPULSE_NEVER`, `IMP_VM_COV_TWO_WAY`, per-opcode `CONTRACT_DEFINED` guards)
 * for the v0.11 64-bit nibble ISA (`impulse_vm_v11_isa.hpp`).
 */
#ifndef IMPULSE_V11_OPCODE_MACROS_H
#define IMPULSE_V11_OPCODE_MACROS_H

#include "impulse_assert.h"
#include "v11/impulse_vm_v11_isa.hpp"
#include <cstdint>
#include <cstddef>

#if defined(_MSC_VER)
  #define IMPULSE_V11_NOINLINE __declspec(noinline)
  #define IMPULSE_V11_LIKELY(x)   (x)
  #define IMPULSE_V11_UNLIKELY(x) (x)
#else
  #define IMPULSE_V11_NOINLINE __attribute__((noinline))
  #define IMPULSE_V11_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define IMPULSE_V11_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

/** Compile-time check that an opcode .inc file's declared flags & subop mask match isa_v0_11.yaml. */
#define V11_OP_CONTRACT_ASSERT(OPCODE_ENUM, EXPECTED_FLAGS, EXPECTED_SUBOP_MASK) \
    static_assert(::impulse::vm::v11::kOpcodeMeta[static_cast<uint8_t>(::impulse::vm::v11::Opcode::OPCODE_ENUM)].defined, \
                  #OPCODE_ENUM " must be defined in kOpcodeMeta"); \
    static_assert(::impulse::vm::v11::kOpcodeMeta[static_cast<uint8_t>(::impulse::vm::v11::Opcode::OPCODE_ENUM)].allowed_flags == (EXPECTED_FLAGS), \
                  #OPCODE_ENUM " allowed_flags drift vs isa_v0_11.yaml"); \
    static_assert(::impulse::vm::v11::kOpcodeMeta[static_cast<uint8_t>(::impulse::vm::v11::Opcode::OPCODE_ENUM)].valid_subop_mask == (EXPECTED_SUBOP_MASK), \
                  #OPCODE_ENUM " valid_subop_mask drift vs isa_v0_11.yaml")

/** Portable 64-bit unsigned overflow-checked addition & multiplication. */
static inline bool v11_u64_add_overflow(uint64_t a, uint64_t b, uint64_t* out) noexcept {
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

static inline bool v11_u64_mul_overflow(uint64_t a, uint64_t b, uint64_t* out) noexcept {
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

/** X-macro list of implemented v0.11 opcodes for automatic dispatch table registration. */
#define V11_IMPLEMENTED_PILLAR1_OPS(X) \
    X(0x00, HALT)            \
    X(0x01, NOP)             \
    X(0x02, TRAP)            \
    X(0x03, THROW)           \
    X(0x04, ASSERT)          \
    X(0x05, LOAD_CONST)      \
    X(0x06, LOAD_CONST_POOL) \
    X(0x07, LOAD_PARAM)      \
    X(0x08, MOV)             \
    X(0x09, SWAP)            \
    X(0x0A, SPILL)           \
    X(0x0B, RELOAD)          \
    X(0x0C, SCALAR_INT_ALU)  \
    X(0x0D, SCALAR_FP_ALU)   \
    X(0x0E, SCALAR_3VL)      \
    X(0x0F, JMP)             \
    X(0x10, JCC)             \
    X(0x11, LOOP_DECR)       \
    X(0x12, CALL)            \
    X(0x13, RET)             \
    X(0x14, SET_MAX_DOP)

#define V11_IMPLEMENTED_PILLAR2_OPS(X) \
    X(0x20, FSEED)            \
    X(0x21, INIT_DOMAIN_SPAN) \
    X(0x22, SET_OP)           \
    X(0x23, SET_COMPLEMENT)   \
    X(0x24, SET_CARDINALITY)  \
    X(0x25, SET_CONTAINS)     \
    X(0x26, SET_CONVERT)      \
    X(0x27, STABLE_CHECK)

#define V11_IMPLEMENTED_OPS(X) \
    V11_IMPLEMENTED_PILLAR1_OPS(X) \
    V11_IMPLEMENTED_PILLAR2_OPS(X)

#endif /* IMPULSE_V11_OPCODE_MACROS_H */
