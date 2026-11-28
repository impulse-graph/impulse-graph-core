/**
 * @file impulse_vm.h
 * @brief Public C-ABI for ImpulseVM (64-bit nibble ISA, 256B hot frame,
 *        caller-owned zero-allocation workspace, static verifier, memory planner,
 *        dual ICTX v2 canonical digest, and SQLite-style prepared statement API).
 */
#ifndef IMPULSE_VM_H
#define IMPULSE_VM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "impulse_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
  #define IMPULSE_VM_ALIGN(n) __declspec(align(n))
#else
  #define IMPULSE_VM_ALIGN(n) __attribute__((aligned(n)))
#endif

/** Little-Endian VM Bytecode Magic: 'I' 'M' 'P' 'B' (0x494D5042) */
#define IMPULSE_VM_MAGIC 0x494D5042u

/** Forward-declared caller-owned workspace header (placement-initialized in scratch_buf). */
typedef struct impulse_vm_workspace impulse_vm_workspace_t;

/** Normative status codes (matching impulse::vm::Status in impulse_vm_isa.hpp). */
typedef uint32_t impulse_vm_status_t;

enum {
    IMPULSE_VM_OK                      = 0u,
    IMPULSE_VM_ERR_INVALID_OPCODE      = 1u,
    IMPULSE_VM_ERR_OUT_OF_BOUNDS       = 2u,
    IMPULSE_VM_ERR_NULL_SNAPSHOT       = 3u,
    IMPULSE_VM_ERR_STACK_OVERFLOW      = 4u,
    IMPULSE_VM_ERR_STACK_UNDERFLOW     = 5u,
    IMPULSE_VM_ERR_INVALID_OPERAND     = 6u,
    IMPULSE_VM_ERR_USER_THROW          = 7u,
    IMPULSE_VM_ERR_ASSERTION_FAILED    = 8u,
    IMPULSE_VM_ERR_TRAP                = 9u,
    IMPULSE_VM_ERR_DOMAIN_MISMATCH     = 10u,
    IMPULSE_VM_ERR_BUFFER_OVERFLOW     = 11u,
    IMPULSE_VM_ERR_FLOATING_POINT      = 12u,
    IMPULSE_VM_ERR_GAS_EXHAUSTED       = 13u,
    IMPULSE_VM_ERR_TYPE_MISMATCH       = 14u,
    IMPULSE_VM_ERR_VALIDATION_FAILED   = 15u,
    IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW = 16u,
    IMPULSE_VM_ERR_DIV_BY_ZERO         = 17u
};

/**
 * Hot Execution State Frame.
 * Exact 256 bytes, 64-byte aligned (4 cache lines).
 */
typedef struct IMPULSE_VM_ALIGN(64) impulse_vm_state {
    /* Cache Line 0 (Bytes 0..63): Control, Flags, Type Word & R0..R3 */
    uint32_t pc;                        /* [0..3]   Current instruction word index */
    uint16_t vm_flags;                  /* [4..5]   Bit 0: ZF, Bit 1: ST, Bit 2: NF */
    uint16_t r_valid_mask;              /* [6..7]   16-bit Kleene validity mask for R0..R15 */
    uint64_t r_type_word;               /* [8..15]  16 x 4-bit scalar type tags for R0..R15 */
    uint16_t call_depth;                /* [16..17] Active call stack depth (0..16) */
    uint16_t max_dop;                   /* [18..19] Active worker parallelism (1..host_max) */
    uint32_t win_bases_packed;          /* [20..23] r_base:8 | f_base:8 | v_base:8 | spill_base:8 */
    impulse_vm_workspace_t* ws;         /* [24..31] Pointer to caller-owned workspace header */
    uint64_t r_regs[4];                 /* [32..63] R0..R3 */

    /* Cache Line 1 (Bytes 64..127): Scalar Bank R4..R11 */
    uint64_t r_regs_4_11[8];            /* [64..127] */

    /* Cache Line 2 (Bytes 128..191): R12..R15 + Frontier Handle Table F0..F7 */
    uint64_t r_regs_12_15[4];           /* [128..159] */
    uint32_t f_handles[8];              /* [160..191] */

    /* Cache Line 3 (Bytes 192..255): Frontier F8..F15 + Vector Handle Table V0..V15 */
    uint32_t f_handles_8_15[8];         /* [192..223] */
    uint16_t v_handles[16];             /* [224..255] */
} impulse_vm_state_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_vm_state_t) == 256, "impulse_vm_state_t must be exactly 256 bytes");
static_assert(alignof(impulse_vm_state_t) == 64, "impulse_vm_state_t must be 64-byte aligned");
#endif

/** .impb Bytecode Container Header (32 bytes, little-endian). */
typedef struct impulse_impb_header {
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
} impulse_impb_header_t;

#ifdef __cplusplus
static_assert(sizeof(impulse_impb_header_t) == 32, "impulse_impb_header_t must be 32 bytes");
#endif

/** Configurable metamorphic frontier & morsel geometry (P0..P4). */
typedef struct impulse_vm_geometry {
    uint32_t tier0_max;         /* P0: 64, P1: 2, P2: 0 */
    uint32_t page_shift;        /* P0: 12, P1: 2, P2: 6 */
    uint32_t tier2_denom;       /* P0: 64, P1: 2, P2: 0 */
    uint32_t morsel_max;        /* P0: 2048, P3: 3, P4: 4 */
    uint32_t max_dop;           /* P0..P3: 1, P4: 4 */
} impulse_vm_geometry_t;

/** Static AOT memory planner output. */
typedef struct impulse_vm_memory_plan {
    size_t min_scratch_bytes;
    size_t guaranteed_scratch_bytes;
    size_t max_result_bytes;
} impulse_vm_memory_plan_t;

/** Dual ICTX v2 canonical digest. */
typedef struct impulse_vm_ictx_v2 {
    uint64_t logical_hash;
    uint64_t physical_hash;
} impulse_vm_ictx_v2_t;

/** Validation result detail. */
typedef struct impulse_vm_validation {
    impulse_vm_status_t status;
    uint32_t bad_pc;
} impulse_vm_validation_t;

/** Public C-ABI query parameter kind (mirrors internal ParamKind / ScalarType). */
#define IMPULSE_VM_PARAM_SCALAR_NULL  0u
#define IMPULSE_VM_PARAM_SCALAR_I64   1u
#define IMPULSE_VM_PARAM_SCALAR_U64   2u
#define IMPULSE_VM_PARAM_SCALAR_F64   3u
#define IMPULSE_VM_PARAM_SCALAR_BOOL  4u
#define IMPULSE_VM_PARAM_SCALAR_STR   5u
#define IMPULSE_VM_PARAM_FRONTIER_IDS 6u
#define IMPULSE_VM_PARAM_VEC_I64      7u
#define IMPULSE_VM_PARAM_VEC_U64      8u
#define IMPULSE_VM_PARAM_VEC_F64      9u

/** Host-bound query parameter entry passed via impulse_vm_params_t. */
typedef struct impulse_vm_param_binding {
    uint32_t    param_id;       /* Matches imm16_lo in OP_LOAD_PARAM */
    uint32_t    kind;           /* IMPULSE_VM_PARAM_* */
    uint16_t    domain_id;      /* Target domain for FRONTIER_IDS */
    uint16_t    reserved;       /* MBZ */
    uint32_t    count;          /* Element count (FRONTIER_IDS/VEC_*) or byte len (STR) */
    uint64_t    scalar_bits;    /* Raw 64-bit scalar value (i64/u64/f64 bits/bool 0|1) */
    const void* data_ptr;       /* Pointer to uint64_t[] / int64_t[] / double[] / char* */
    const uint64_t* valid_bits; /* Optional validity bitmap for VEC_* (NULL => all valid) */
} impulse_vm_param_binding_t;

/** Optional query execution parameters & fuel override. */
typedef struct impulse_vm_params {
    uint64_t gas_limit;         /* 0 => default (100000 for fixtures, UINT64_MAX for C-ABI) */
    const impulse_vm_geometry_t* geometry_override; /* NULL => P0_DEFAULT */
    const impulse_vm_param_binding_t* bindings;     /* NULL => no host-bound params */
    uint32_t binding_count;
} impulse_vm_params_t;

/** Read-only view over a terminal OP_COLLECT output slot in workspace memory. */
typedef struct impulse_vm_output_view {
    uint16_t        slot;
    uint8_t         elem_tag;       /* 0=I64, 1=U64, 4=F64, 6=BOOL_3VL, 7=STR, 8=EMB, 9=LANE_STATE */
    uint8_t         reserved;
    uint32_t        length;
    const int64_t*  i64_data;       /* Valid for I64 / U64 */
    const double*   f64_data;       /* Valid for F64 */
    const uint64_t* valid_bits;     /* NULL => all valid */
    const uint64_t* bool_data_bits; /* Valid for BOOL_3VL */
} impulse_vm_output_view_t;

/**
 * 1. Placement-initialize a caller-owned 128-byte-aligned scratch buffer once per worker thread.
 *    Zero heap allocations; no destructor required.
 */
IMPULSE_API impulse_vm_status_t impulse_workspace_init(
    void* scratch_buf,
    size_t scratch_bytes,
    impulse_vm_workspace_t** out_ws
);

/**
 * 2. Static load-time bytecode verifier over raw 64-bit instruction words.
 */
IMPULSE_API impulse_vm_validation_t impulse_vm_validate_words(
    const uint64_t* words,
    size_t word_count
);

/**
 * 3. Static load-time bytecode verifier over a .impb container.
 */
IMPULSE_API impulse_vm_validation_t impulse_vm_validate(
    const void* impb_bytes,
    size_t impb_len
);

/**
 * 4. Static AOT memory planner (O(|code|) worst-case scratch & result bound).
 */
IMPULSE_API impulse_vm_status_t impulse_vm_plan_memory(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    uint16_t max_dop,
    impulse_vm_memory_plan_t* out_plan
);

/**
 * 4b. Pre-build a reusable snapshot view on an open .imps snapshot once
 *     before hot query loops (eliminates per-query CSR/attribute view building).
 */
IMPULSE_API impulse_vm_status_t impulse_vm_prepare_snapshot(
    const impulse_snapshot_t* snapshot
);

/**
 * 5. Zero-allocation hot-path execution over a .impb container.
 */
IMPULSE_API impulse_vm_status_t impulse_vm_execute(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    const impulse_vm_params_t* params,
    impulse_vm_workspace_t* scratch_ws,
    void* result_buf,
    size_t result_cap,
    size_t* out_result_bytes
);

/**
 * 6. Compute canonical Dual ICTX v2 digest from workspace state after execution.
 */
IMPULSE_API impulse_vm_ictx_v2_t impulse_vm_compute_ictx_v2(
    const impulse_vm_workspace_t* ws
);

/**
 * 7. Access the 256-byte hot execution state frame inside a workspace.
 */
IMPULSE_API const impulse_vm_state_t* impulse_vm_get_state(
    const impulse_vm_workspace_t* ws
);

/**
 * 7b. Inspect terminal OP_COLLECT outputs stored in the workspace after execution.
 */
IMPULSE_API uint32_t impulse_vm_get_output_count(
    const impulse_vm_workspace_t* ws
);

IMPULSE_API int impulse_vm_get_output(
    const impulse_vm_workspace_t* ws,
    uint32_t index,
    impulse_vm_output_view_t* out
);

IMPULSE_API int impulse_vm_get_output_str(
    const impulse_vm_workspace_t* ws,
    uint32_t index,
    uint32_t elem_idx,
    const char** out_ptr,
    uint32_t* out_len,
    int* out_valid
);

/**
 * 8. Optional per-opcode profiling statistics (active only when compiled with
 *    -DIMPULSE_VM_OPCODE_PROFILE=1; zero overhead & zero workspace size when off).
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
} impulse_vm_opcode_stat_t;

IMPULSE_API void impulse_vm_reset_opcode_stats(
    impulse_vm_workspace_t* ws
);

/**
 * Copies 128 opcode stat entries into out_128.
 * Returns 1 if IMPULSE_VM_OPCODE_PROFILE is enabled, 0 otherwise.
 */
IMPULSE_API int impulse_vm_get_opcode_stats(
    const impulse_vm_workspace_t* ws,
    impulse_vm_opcode_stat_t* out_128
);

/**
 * 9. Compile an ImpScheme SSA (.impscm) source string against a bound snapshot
 *    into a validated .impb container.
 *    opt_level: 0 = opt-OFF, 1 = opt-ON.
 *    Returns 0 on success, non-zero on static validation/parse error (message written to err_buf).
 *    Caller frees *out_impb via impulse_vm_free_bytes().
 */
IMPULSE_API int impulse_compile_impscm(
    const char* impscm_src,
    const impulse_snapshot_t* snapshot,
    int opt_level,
    uint8_t** out_impb,
    size_t* out_len,
    char* err_buf,
    size_t err_cap
);

IMPULSE_API void impulse_vm_free_bytes(uint8_t* ptr);

/* ---------------------------------------------------------------------------
 * Unified In-Kernel Compiler Language Enum
 * --------------------------------------------------------------------------- */

/**
 * @enum impulse_language_t
 * @brief Supported DSL frontend dialects for the unified C++20 in-kernel compiler.
 */
typedef enum {
    IMPULSE_LANG_IMPSCM = 0,  /**< ImpScheme S-Expression IR (.impscm) */
    IMPULSE_LANG_IMPK   = 1,  /**< ImpK Array/Vector DSL (.impk) */
    IMPULSE_LANG_IMPLOG = 2,  /**< ImpLog Datalog DSL (.implog) */
    IMPULSE_LANG_CYPHER = 3,  /**< openCypher Pattern Matcher */
    IMPULSE_LANG_CEL    = 4   /**< Common Expression Language */
} impulse_language_t;

/**
 * @brief Compiles a DSL query string into human-readable ImpAsm (.impas) assembly text.
 */
IMPULSE_API impulse_status_t impulse_compile_to_impas(
    const impulse_snapshot_t* snapshot,
    const char* script,
    impulse_language_t lang,
    char* out_impas_buffer,
    size_t out_capacity,
    size_t* out_bytes_written
);

/* ---------------------------------------------------------------------------
 * Canonical SQLite-Style Statement C-ABI & Projections
 * --------------------------------------------------------------------------- */

/**
 * @struct impulse_column_desc_t
 * @brief Column metadata and data pointer descriptor (Apache Arrow Compatible).
 */
typedef struct impulse_column_desc {
    char name[32];                 /**< Column identifier (e.g. "node_id", "email", "pagerank") */
    uint8_t type_code;             /**< Type code: IMPULSE_KEY_TYPE_* or primitive code */
    uint8_t element_size;          /**< Byte size of single element (e.g. 8 for uint64, 4 for float32) */
    bool is_nullable;              /**< False if column is guaranteed 100% non-null */
    uint8_t reserved;
    uint32_t dimension;            /**< 1 for scalars, vector dim for embeddings */
    size_t byte_offset_in_buf;     /**< Offset within caller buffer where column array begins */
    const void* data_ptr;          /**< Direct pointer to contiguous array in caller buffer */
    const uint64_t* null_bitmap;   /**< Arrow validity bitmap: NULL if non-nullable, else bit i=1 is valid */
} impulse_column_desc_t;

/**
 * @struct impulse_execution_result_t
 * @brief Output execution summary descriptor for one-line queries or direct calls.
 */
typedef struct impulse_execution_result {
    impulse_vm_status_t status;    /**< Execution status code (IMPULSE_VM_OK == 0) */
    size_t row_count;              /**< Total rows returned */
    uint32_t column_count;         /**< Total projected columns */
    uint32_t reserved;
    size_t total_bytes_written;    /**< Bytes written to caller buffer */
    const void* data_ptr;          /**< Fast-path pointer to column 0 array */
    uint64_t scalar_value;         /**< Fast-path scalar integer/node value if row_count == 1 */
} impulse_execution_result_t;

/** Opaque Statement Handle */
typedef struct impulse_stmt impulse_stmt_t;

/* Lifecycle & Sizing */
IMPULSE_API impulse_status_t impulse_stmt_prepare(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    impulse_stmt_t** out_stmt
);

IMPULSE_API size_t impulse_stmt_buffer_size(const impulse_stmt_t* stmt);
IMPULSE_API void impulse_stmt_finalize(impulse_stmt_t* stmt);

/* Parameter Bindings */
IMPULSE_API impulse_status_t impulse_stmt_bind_node(impulse_stmt_t* stmt, const char* param, uint64_t node_id);
IMPULSE_API impulse_status_t impulse_stmt_bind_nodes(impulse_stmt_t* stmt, const char* param, const uint64_t* node_ids, size_t count);
IMPULSE_API impulse_status_t impulse_stmt_bind_bitset(impulse_stmt_t* stmt, const char* param, const uint64_t* words, size_t word_count);
IMPULSE_API impulse_status_t impulse_stmt_bind_roaring(impulse_stmt_t* stmt, const char* param, const uint8_t* bytes, size_t len);
IMPULSE_API impulse_status_t impulse_stmt_bind_int(impulse_stmt_t* stmt, const char* param, int64_t val);
IMPULSE_API impulse_status_t impulse_stmt_bind_uint(impulse_stmt_t* stmt, const char* param, uint64_t val);
IMPULSE_API impulse_status_t impulse_stmt_bind_float(impulse_stmt_t* stmt, const char* param, double val);
IMPULSE_API impulse_status_t impulse_stmt_bind_str(impulse_stmt_t* stmt, const char* param, const char* str);
IMPULSE_API impulse_status_t impulse_stmt_bind_uuid(impulse_stmt_t* stmt, const char* param, const uint8_t uuid_bytes[16]);
IMPULSE_API impulse_status_t impulse_stmt_bind_vector(impulse_stmt_t* stmt, const char* param, const float* data, size_t dim);

/* Execution */
IMPULSE_API impulse_status_t impulse_stmt_execute(impulse_stmt_t* stmt, void* buffer, size_t buffer_size);

/* Column Result Accessors (Zero-Copy Arrow-Style) */
IMPULSE_API size_t impulse_stmt_row_count(const impulse_stmt_t* stmt);
IMPULSE_API uint32_t impulse_stmt_column_count(const impulse_stmt_t* stmt);
IMPULSE_API const char* impulse_stmt_column_name(const impulse_stmt_t* stmt, uint32_t col_idx);
IMPULSE_API uint8_t impulse_stmt_column_type(const impulse_stmt_t* stmt, uint32_t col_idx);
IMPULSE_API uint32_t impulse_stmt_column_dim(const impulse_stmt_t* stmt, uint32_t col_idx);
IMPULSE_API const void* impulse_stmt_column_data(const impulse_stmt_t* stmt, uint32_t col_idx);
IMPULSE_API bool impulse_stmt_column_is_null(const impulse_stmt_t* stmt, uint32_t col_idx, size_t row_idx);

/**
 * @brief One-line convenience wrapper: prepare, bind `$seed`, execute, and finalize.
 *
 * @note Ownership & Lifetime: `out_result->data_ptr` points into an internal
 *       thread-local execution buffer owned by the library. Callers MUST NOT
 *       `free()` `out_result->data_ptr`. The buffer remains valid until the
 *       next `impulse_exec()` call on the same thread or thread exit.
 *       For concurrent/persistent result buffers or zero-compile hot paths,
 *       use `impulse_stmt_prepare()` + `impulse_stmt_execute()`.
 */
IMPULSE_API impulse_status_t impulse_exec(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    uint64_t seed_node,
    impulse_execution_result_t* out_result
);

#ifdef __cplusplus
}
#endif

#endif /* IMPULSE_VM_H */
