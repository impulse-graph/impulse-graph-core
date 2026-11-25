/**
 * @file impulse_vm_v11.h
 * @brief Public C-ABI for ImpulseVM v0.11.0 (64-bit nibble ISA, 256B hot frame,
 *        caller-owned zero-allocation workspace, static verifier, memory planner,
 *        and dual ICTX v2 canonical digest).
 */
#ifndef IMPULSE_VM_V11_H
#define IMPULSE_VM_V11_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "impulse_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
  #define IMPULSE_V11_ALIGN(n) __declspec(align(n))
#else
  #define IMPULSE_V11_ALIGN(n) __attribute__((aligned(n)))
#endif

/** Forward-declared caller-owned workspace header (placement-initialized in scratch_buf). */
typedef struct impulse_vm_workspace_v11 impulse_vm_workspace_v11_t;

/** Normative status codes (matching impulse::vm::v11::Status in impulse_vm_v11_isa.hpp). */
typedef uint32_t impulse_vm_status_v11_t;

#define IMPULSE_VM_V11_OK                     0u
#define IMPULSE_VM_V11_ERR_INVALID_OPCODE     1u
#define IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS      2u
#define IMPULSE_VM_V11_ERR_NULL_SNAPSHOT      3u
#define IMPULSE_VM_V11_ERR_STACK_OVERFLOW     4u
#define IMPULSE_VM_V11_ERR_STACK_UNDERFLOW    5u
#define IMPULSE_VM_V11_ERR_INVALID_OPERAND    6u
#define IMPULSE_VM_V11_ERR_USER_THROW         7u
#define IMPULSE_VM_V11_ERR_ASSERTION_FAILED   8u
#define IMPULSE_VM_V11_ERR_TRAP               9u
#define IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH    10u
#define IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW    11u
#define IMPULSE_VM_V11_ERR_FLOATING_POINT     12u
#define IMPULSE_VM_V11_ERR_GAS_EXHAUSTED      13u
#define IMPULSE_VM_V11_ERR_TYPE_MISMATCH      14u
#define IMPULSE_VM_V11_ERR_VALIDATION_FAILED  15u

/**
 * Hot Execution State Frame (VM_ARCHITECTURE_v0_11.md §4.1).
 * Exact 256 bytes, 64-byte aligned (4 cache lines).
 */
typedef struct IMPULSE_V11_ALIGN(64) impulse_vm_state_v11 {
    /* Cache Line 0 (Bytes 0..63): Control, Flags, Type Word & R0..R3 */
    uint32_t pc;                        /* [0..3]   Current instruction word index */
    uint16_t vm_flags;                  /* [4..5]   Bit 0: ZF, Bit 1: ST, Bit 2: NF */
    uint16_t r_valid_mask;              /* [6..7]   16-bit Kleene validity mask for R0..R15 */
    uint64_t r_type_word;               /* [8..15]  16 x 4-bit scalar type tags for R0..R15 */
    uint16_t call_depth;                /* [16..17] Active call stack depth (0..16) */
    uint16_t max_dop;                   /* [18..19] Active worker parallelism (1..host_max) */
    uint32_t win_bases_packed;          /* [20..23] r_base:8 | f_base:8 | v_base:8 | spill_base:8 */
    impulse_vm_workspace_v11_t* ws;     /* [24..31] Pointer to caller-owned workspace header */
    uint64_t r_regs[4];                 /* [32..63] R0..R3 */

    /* Cache Line 1 (Bytes 64..127): Scalar Bank R4..R11 */
    uint64_t r_regs_4_11[8];            /* [64..127] */

    /* Cache Line 2 (Bytes 128..191): R12..R15 + Frontier Handle Table F0..F7 */
    uint64_t r_regs_12_15[4];           /* [128..159] */
    uint32_t f_handles[8];              /* [160..191] */

    /* Cache Line 3 (Bytes 192..255): Frontier F8..F15 + Vector Handle Table V0..V15 */
    uint32_t f_handles_8_15[8];         /* [192..223] */
    uint16_t v_handles[16];             /* [224..255] */
} impulse_vm_state_v11_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_vm_state_v11_t) == 256, "impulse_vm_state_v11_t must be exactly 256 bytes");
static_assert(alignof(impulse_vm_state_v11_t) == 64, "impulse_vm_state_v11_t must be 64-byte aligned");
#endif

/** .impb v0.11 Bytecode Container Header (32 bytes, little-endian). */
typedef struct impulse_impb_header_v11 {
    uint32_t magic;             /* 0x494D5042 ('IMPB') */
    uint16_t isa_version;       /* 0x000B (11) */
    uint8_t  header_flags;      /* Bit 0: HAS_CONST_POOL; Bits 1..7 MBZ */
    uint8_t  max_call_depth;    /* 0..16 */
    uint32_t code_word_count;
    uint32_t const_pool_bytes;
    uint32_t entry_pc;
    uint16_t required_domains;
    uint16_t reserved;          /* MBZ */
    uint64_t program_xxh3_64;
} impulse_impb_header_v11_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_impb_header_v11_t) == 32, "impulse_impb_header_v11_t must be 32 bytes");
#endif

/** Configurable metamorphic frontier & morsel geometry (P0..P4). */
typedef struct impulse_vm_geometry_v11 {
    uint32_t tier0_max;         /* P0: 64, P1: 2, P2: 0 */
    uint32_t page_shift;        /* P0: 12, P1: 2, P2: 6 */
    uint32_t tier2_denom;       /* P0: 64, P1: 2, P2: 0 */
    uint32_t morsel_max;        /* P0: 2048, P3: 3, P4: 4 */
    uint32_t max_dop;           /* P0..P3: 1, P4: 4 */
} impulse_vm_geometry_v11_t;

/** Static AOT memory planner output (VM_ARCHITECTURE_v0_11.md §5.1). */
typedef struct impulse_vm_memory_plan {
    size_t min_scratch_bytes;
    size_t guaranteed_scratch_bytes;
    size_t max_result_bytes;
} impulse_vm_memory_plan_t;

/** Dual ICTX v2 canonical digest (TESTING_AND_BENCHMARK_STRATEGY.md §1). */
typedef struct impulse_vm_ictx_v2 {
    uint64_t logical_hash;
    uint64_t physical_hash;
} impulse_vm_ictx_v2_t;

/** Validation result detail. */
typedef struct impulse_vm_validation_v11 {
    impulse_vm_status_v11_t status;
    uint32_t bad_pc;
} impulse_vm_validation_v11_t;

/** Public C-ABI query parameter kind (mirrors internal ParamKind / ScalarType). */
#define IMPULSE_VM_V11_PARAM_SCALAR_NULL  0u
#define IMPULSE_VM_V11_PARAM_SCALAR_I64   1u
#define IMPULSE_VM_V11_PARAM_SCALAR_U64   2u
#define IMPULSE_VM_V11_PARAM_SCALAR_F64   3u
#define IMPULSE_VM_V11_PARAM_SCALAR_BOOL  4u
#define IMPULSE_VM_V11_PARAM_SCALAR_STR   5u
#define IMPULSE_VM_V11_PARAM_FRONTIER_IDS 6u
#define IMPULSE_VM_V11_PARAM_VEC_I64      7u
#define IMPULSE_VM_V11_PARAM_VEC_U64      8u
#define IMPULSE_VM_V11_PARAM_VEC_F64      9u

/** Host-bound query parameter entry passed via impulse_vm_params_v11_t. */
typedef struct impulse_vm_param_binding_v11 {
    uint32_t    param_id;       /* Matches imm16_lo in OP_LOAD_PARAM */
    uint32_t    kind;           /* IMPULSE_VM_V11_PARAM_* */
    uint16_t    domain_id;      /* Target domain for FRONTIER_IDS */
    uint16_t    reserved;       /* MBZ */
    uint32_t    count;          /* Element count (FRONTIER_IDS/VEC_*) or byte len (STR) */
    uint64_t    scalar_bits;    /* Raw 64-bit scalar value (i64/u64/f64 bits/bool 0|1) */
    const void* data_ptr;       /* Pointer to uint64_t[] / int64_t[] / double[] / char* */
    const uint64_t* valid_bits; /* Optional validity bitmap for VEC_* (NULL => all valid) */
} impulse_vm_param_binding_v11_t;

/** Optional query execution parameters & fuel override. */
typedef struct impulse_vm_params_v11 {
    uint64_t gas_limit;         /* 0 => default (100000 for fixtures, UINT64_MAX for C-ABI) */
    const impulse_vm_geometry_v11_t* geometry_override; /* NULL => P0_DEFAULT */
    const impulse_vm_param_binding_v11_t* bindings;     /* NULL => no host-bound params */
    uint32_t binding_count;
} impulse_vm_params_v11_t;

/** Read-only view over a terminal OP_COLLECT output slot in workspace memory. */
typedef struct impulse_vm_output_view_v11 {
    uint16_t        slot;
    uint8_t         elem_tag;       /* 0=I64, 1=U64, 4=F64, 6=BOOL_3VL, 7=STR, 8=EMB, 9=LANE_STATE */
    uint8_t         reserved;
    uint32_t        length;
    const int64_t*  i64_data;       /* Valid for I64 / U64 */
    const double*   f64_data;       /* Valid for F64 */
    const uint64_t* valid_bits;     /* NULL => all valid */
    const uint64_t* bool_data_bits; /* Valid for BOOL_3VL */
} impulse_vm_output_view_v11_t;

/**
 * 1. Placement-initialize a caller-owned 128-byte-aligned scratch buffer once per worker thread.
 *    Zero heap allocations; no destructor required.
 */
IMPULSE_API impulse_vm_status_v11_t impulse_workspace_init(
    void* scratch_buf,
    size_t scratch_bytes,
    impulse_vm_workspace_v11_t** out_ws
);

/**
 * 2. Static load-time bytecode verifier over raw 64-bit instruction words.
 */
IMPULSE_API impulse_vm_validation_v11_t impulse_vm_validate_words_v11(
    const uint64_t* words,
    size_t word_count
);

/**
 * 3. Static load-time bytecode verifier over a .impb v0.11 container.
 */
IMPULSE_API impulse_vm_validation_v11_t impulse_vm_validate_v11(
    const void* impb_bytes,
    size_t impb_len
);

/**
 * 4. Static AOT memory planner (O(|code|) worst-case scratch & result bound).
 */
IMPULSE_API impulse_vm_status_v11_t impulse_vm_plan_memory(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    uint16_t max_dop,
    impulse_vm_memory_plan_t* out_plan
);

/**
 * 4b. Pre-build a reusable v0.11 snapshot view on an open .imps snapshot once
 *     before hot query loops (eliminates per-query CSR/attribute view building).
 */
IMPULSE_API impulse_vm_status_v11_t impulse_vm_v11_prepare_snapshot(
    const impulse_snapshot_t* snapshot
);

/**
 * 5. Zero-allocation hot-path execution over a .impb v0.11 container.
 */
IMPULSE_API impulse_vm_status_v11_t impulse_vm_execute_v11(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    const impulse_vm_params_v11_t* params,
    impulse_vm_workspace_v11_t* scratch_ws,
    void* result_buf,
    size_t result_cap,
    size_t* out_result_bytes
);

/**
 * 6. Compute canonical Dual ICTX v2 digest from workspace state after execution.
 */
IMPULSE_API impulse_vm_ictx_v2_t impulse_vm_v11_compute_ictx_v2(
    const impulse_vm_workspace_v11_t* ws
);

/**
 * 7. Access the 256-byte hot execution state frame inside a workspace.
 */
IMPULSE_API const impulse_vm_state_v11_t* impulse_vm_v11_get_state(
    const impulse_vm_workspace_v11_t* ws
);

/**
 * 7b. Inspect terminal OP_COLLECT outputs stored in the workspace after execution.
 */
IMPULSE_API uint32_t impulse_vm_v11_get_output_count(
    const impulse_vm_workspace_v11_t* ws
);

IMPULSE_API int impulse_vm_v11_get_output(
    const impulse_vm_workspace_v11_t* ws,
    uint32_t index,
    impulse_vm_output_view_v11_t* out
);

IMPULSE_API int impulse_vm_v11_get_output_str(
    const impulse_vm_workspace_v11_t* ws,
    uint32_t index,
    uint32_t elem_idx,
    const char** out_ptr,
    uint32_t* out_len,
    int* out_valid
);

/**
 * 8. Optional per-opcode profiling statistics (active only when compiled with
 *    -DIMPULSE_V11_OPCODE_PROFILE=1; zero overhead & zero workspace size when off).
 *    - calls:    exact invocation count for all opcodes 0x00..0x7F.
 *    - total_ns / min_ns / max_ns: populated for bulk opcodes (op >= 0x20, Pillars 2-5,
 *      including morsel-body ops); 0 for Pillar 1 scalar ops (op < 0x20) to avoid
 *      timer-read distortion on sub-10ns instructions.
 */
typedef struct {
    uint64_t calls;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
} impulse_vm_v11_opcode_stat_t;

IMPULSE_API void impulse_vm_v11_reset_opcode_stats(
    impulse_vm_workspace_v11_t* ws
);

/**
 * Copies 128 opcode stat entries into out_128.
 * Returns 1 if IMPULSE_V11_OPCODE_PROFILE is enabled, 0 otherwise.
 */
IMPULSE_API int impulse_vm_v11_get_opcode_stats(
    const impulse_vm_workspace_v11_t* ws,
    impulse_vm_v11_opcode_stat_t* out_128
);

/**
 * 9. Compile an ImpScheme v0.11 SSA (.impscm) source string against a bound snapshot
 *    into a validated .impb v0.11 container.
 *    opt_level: 0 = opt-OFF, 1 = opt-ON.
 *    Returns 0 on success, non-zero on static validation/parse error (message written to err_buf).
 *    Caller frees *out_impb via impulse_vm_v11_free_bytes().
 */
IMPULSE_API int impulse_compile_impscm_v11(
    const char* impscm_src,
    const impulse_snapshot_t* snapshot,
    int opt_level,
    uint8_t** out_impb,
    size_t* out_len,
    char* err_buf,
    size_t err_cap
);

IMPULSE_API void impulse_vm_v11_free_bytes(uint8_t* ptr);

#ifdef __cplusplus
}
#endif

#endif /* IMPULSE_VM_V11_H */
