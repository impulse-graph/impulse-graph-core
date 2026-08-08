/**
 * @file impulse_vm.h
 * @brief Impulse Graph Bytecode Virtual Machine Engine & Instruction Set Specification.
 *
 * Defines raw 64-bit opcode instructions (`impulse_instruction_t`), hardware registers (`R0`..`R63`),
 * VM execution frame layout (`impulse_vm_state_t`), off-heap context handle pools, and native execution APIs.
 */

#ifndef IMPULSE_VM_H
#define IMPULSE_VM_H

#include "impulse_graph.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifndef __cplusplus
  #include <stdalign.h>
#endif



#ifdef __cplusplus
extern "C" {
#endif

/** Little-Endian VM Magic: 'I' 'M' 'P' 'B' (0x494D5042) */
#define IMPULSE_VM_MAGIC 0x494D5042

// Status Flags Bitmasks (FLAGS Register)
#define IMPULSE_VM_FLAG_ZF (1ULL << 0) /**< Zero Flag: set if candidate set or arithmetic result is empty / 0 */
#define IMPULSE_VM_FLAG_LT (1ULL << 1) /**< Less Than Flag: set if src1 < src2 */
#define IMPULSE_VM_FLAG_GT (1ULL << 2) /**< Greater Than Flag: set if src1 > src2 */
#define IMPULSE_VM_FLAG_EQ (1ULL << 3) /**< Equal Flag: set if src1 == src2 */
#define IMPULSE_VM_FLAG_ST (1ULL << 4) /**< Stable Flag: set by repeat checks when set generation converged */

// Opcode Modifier Flags (FLAGS field in instruction)
#define IMPULSE_VM_OP_FLAG_MODE_BITSET 0x01
#define IMPULSE_VM_OP_FLAG_ACCUMULATE  0x02
#define IMPULSE_VM_OP_FLAG_INVERT      0x04
#define IMPULSE_VM_OP_FLAG_OFFHEAP     0x08

// Opcodes Definitions
#define OP_NOP                      0x00
#define OP_INIT_INPUT_NODE          0x01
#define OP_INIT_INPUT_SET           0x02
#define OP_LOAD_CONST_INT           0x03
#define OP_MAP_KEYS_TO_DENSE        0x04
#define OP_LOAD_CONST_FLOAT         0x05
#define OP_LOAD_CONST_STR_PREFIX    0x06
#define OP_LOAD_INLINE_ARRAY        0x07
#define OP_INIT_MOCK_GRAPH          0x08

#define OP_CSR_WALK                 0x10
#define OP_CSR_WALK_FILTERED        0x11
#define OP_CSR_DEGREE               0x12
#define OP_CSR_WALK_PREDICATE       0x13
#define OP_NODE_FILTER              0x14
#define OP_NODE_FILTER_STR_PREFIX   0x15
#define OP_CSR_WALK_REDUCE_SUM      0x16
#define OP_CSR_WALK_REDUCE          0x17
#define OP_CSC_WALK                 0x18

#define OP_SET_UNION                0x30
#define OP_SET_INTERSECT            0x31
#define OP_SET_DIFFERENCE           0x32
#define OP_SET_CARDINALITY          0x33
#define OP_VECTOR_MUL_ATTR          0x34
#define OP_VECTOR_REDUCE_SUM        0x35
#define OP_VECTOR_DIV               0x36
#define OP_VECTOR_STR_CONCAT        0x37
#define OP_FLOAT_VECTOR_SCALE       0x38
#define OP_L1_NORM_DIFF             0x39

#define OP_CC_AFFOREST              0x40
#define OP_MXV                      0x41
#define OP_VXM                      0x42
#define OP_EWISE_ADD                0x43
#define OP_EWISE_MULT               0x44
#define OP_REDUCE                   0x45
#define OP_CC_HOOK_COMPRESS         0x46
#define OP_TC_SWEEP_BATCH           0x47
#define OP_BRANDES_FORWARD          0x48
#define OP_BRANDES_BACKWARD         0x49
#define OP_DELTA_STEP_RELAX         0x4A
#define OP_READ_EDGE_WEIGHT         0x4B

// Extended Domain Opcodes (0x60 - 0x6A)
#define OP_SAMPLE_NEIGHBORS         0x60
#define OP_RANDOM_WALK              0x61
#define OP_SCATTER_GATHER           0x62
#define OP_REBAC_CHECK              0x63
#define OP_ROARING_BITMAP_AND       0x64
#define OP_ISLAND_DETECT            0x65
#define OP_SPARSE_MATVEC            0x66
#define OP_LOUVAIN_MODULARITY       0x67
#define OP_KCORE_DECOMPOSITION      0x68
#define OP_MOTIF_MATCH_3            0x69
#define OP_GRAPH_ISOMORPHISM        0x6A

#define OP_JMP                      0x50
#define OP_JZ                       0x51
#define OP_JNZ                      0x52
#define OP_LOOP_DECR                0x53
#define OP_STABLE_CHECK             0x54
#define OP_CALL                     0x55
#define OP_RET                      0x56
#define OP_THROW                    0x5A
#define OP_ASSERT                   0x5B
#define OP_TRAP                     0x5C

#define OP_MOV                      0x70
#define OP_CLEAR_REG                0x71
#define OP_LOAD_INDIRECT            0x72
#define OP_ALLOC_SCRATCH            0x73
#define OP_ASSERT_SCRATCH_BYTES     0x74
#define OP_SET_MAX_DOP              0x75

// GraphBLAS Semiring IDs
#define SEMIRING_PLUS_TIMES         0
#define SEMIRING_MIN_PLUS           1
#define SEMIRING_MAX_MIN            2
#define SEMIRING_BOOL               3

// GraphBLAS Binary / Monoid Operator IDs
#define BINARY_OP_ADD               0
#define BINARY_OP_MUL               1
#define BINARY_OP_MIN               2
#define BINARY_OP_MAX               3
#define BINARY_OP_AND               4
#define BINARY_OP_OR                5

#define OP_COLLECT_BITSET           0x90
#define OP_COLLECT_ARRAY            0x91
#define OP_MAP_DENSE_TO_KEYS        0x92
#define OP_COLLECT_VALUE_MAP        0x93
#define OP_HALT                     0xFF

/** @brief Register Type Tags */
typedef enum {
    TYPE_NULL = 0x00,
    TYPE_INT64 = 0x01,
    TYPE_NODE_ID = 0x02,
    TYPE_RELATION_ID = 0x03,
    TYPE_BITSET_HANDLE = 0x04,
    TYPE_NODE_VECTOR = 0x05,
    TYPE_CSR_SPAN = 0x06,
    TYPE_BOOLEAN = 0x07,
    TYPE_FLOAT = 0x08,
    TYPE_DOUBLE = 0x09,
    TYPE_VALUE_MAP = 0x0A,
    TYPE_STRING_VECTOR = 0x0B,
    TYPE_FLOAT_VECTOR = 0x0C,
    TYPE_DOUBLE_VECTOR = 0x0D,
    TYPE_UINT64_VECTOR = 0x0E
} impulse_register_type_t;

/** @brief VM Execution Status Codes */
typedef enum {
    IMPULSE_VM_OK = 0,
    IMPULSE_VM_ERR_INVALID_OPCODE = 1,
    IMPULSE_VM_ERR_OUT_OF_BOUNDS = 2,
    IMPULSE_VM_ERR_NULL_SNAPSHOT = 3,
    IMPULSE_VM_ERR_STACK_OVERFLOW = 4,
    IMPULSE_VM_ERR_STACK_UNDERFLOW = 5,
    IMPULSE_VM_ERR_INVALID_REGISTER = 6,
    IMPULSE_VM_ERR_USER_THROW = 7,
    IMPULSE_VM_ERR_ASSERTION_FAILED = 8,
    IMPULSE_VM_ERR_TRAP = 9
} impulse_vm_status_t;

#ifndef IMPULSE_ALIGN
  #if defined(__cplusplus)
    #define IMPULSE_ALIGN(x) alignas(x)
  #elif defined(_MSC_VER)
    #define IMPULSE_ALIGN(x) __declspec(align(x))
  #else
    #define IMPULSE_ALIGN(x) __attribute__((aligned(x)))
  #endif
#endif

/**
 * @brief Fixed 64-bit Instruction Structure Layout.
 */
typedef struct IMPULSE_ALIGN(8) {
    uint8_t  opcode;    /**< Opcode byte (0x00..0xFF) */
    uint8_t  flags;     /**< Instruction modifier flags */
    uint16_t dst_reg;   /**< Destination register index (R0..R63) */
    uint32_t payload;   /**< Payload data (source register, relation ID, jump offset, or scalar constant) */
} impulse_instruction_t;

typedef struct impulse_snapshot impulse_snapshot_t;
typedef struct impulse_vm_context impulse_vm_context_t;

typedef struct {
    const char** keys;
    size_t count;
} impulse_vm_input_keys;

/**
 * @brief VM Execution State Frame (640 bytes, 64-byte aligned).
 *
 * Matches the layout of the Java 25 FFM MemorySegment representation.
 */
typedef struct IMPULSE_ALIGN(64) {
    uint32_t pc;                                  /**< Program Counter offset (0) */
    uint32_t reserved;                            /**< Alignment padding (4) */
    uint64_t flags;                               /**< Status flags register (ZF, LT, GT, EQ, ST) (8) */
    uint64_t registers[64];                       /**< 64-bit registers R0..R63 (16..527) */
    uint8_t  register_types[64];                  /**< Type tags for registers R0..R63 (528..591) */
    impulse_vm_context_t* query_context;          /**< Pointer to off-heap execution context (592..599) */
    uint32_t call_stack[8];                       /**< Subroutine return stack (600..631) */
    uint32_t call_stack_depth;                    /**< Subroutine stack depth (632..635) */
    uint32_t reserved_padding2;                   /**< Padding to exactly 640 bytes (636..639) */
} impulse_vm_state_t;

// Public Context lifecycle APIs
IMPULSE_API impulse_vm_context_t* impulse_vm_context_create(const impulse_snapshot_t* snapshot);
IMPULSE_API void impulse_vm_context_destroy(impulse_vm_context_t* ctx);
IMPULSE_API size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx);
IMPULSE_API const float* impulse_vm_context_get_float_vector(const impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API const double* impulse_vm_context_get_double_vector(const impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API int impulse_vm_context_acquire_bitset(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_bitset(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API void impulse_vm_context_bitset_add(impulse_vm_context_t* ctx, size_t handle, uint64_t node_id);
IMPULSE_API bool impulse_vm_context_bitset_test(const impulse_vm_context_t* ctx, size_t handle, uint64_t node_id);
IMPULSE_API void impulse_vm_context_bitset_fill(impulse_vm_context_t* ctx, size_t handle, uint64_t count);
IMPULSE_API uint64_t impulse_vm_context_bitset_get_word(const impulse_vm_context_t* ctx, size_t handle, size_t word_idx);
IMPULSE_API int impulse_vm_context_acquire_float_vector(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_float_vector(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API void impulse_vm_context_float_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, float val);
IMPULSE_API int impulse_vm_context_acquire_double_vector(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_double_vector(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API void impulse_vm_context_double_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, double val);
IMPULSE_API void impulse_vm_context_bind_inline_data(impulse_vm_context_t* ctx, const void* data_ptr, size_t bytes);
IMPULSE_API int impulse_vm_context_acquire_node_vector(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_node_vector(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API const uint64_t* impulse_vm_context_get_node_vector(const impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API int impulse_vm_context_acquire_string_vector(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_string_vector(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API void impulse_vm_context_string_vector_add(impulse_vm_context_t* ctx, size_t handle, const char* str);
IMPULSE_API size_t impulse_vm_context_string_vector_size(const impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API const char* impulse_vm_context_string_vector_get(const impulse_vm_context_t* ctx, size_t handle, size_t index);
IMPULSE_API int impulse_vm_context_acquire_value_map(impulse_vm_context_t* ctx);
IMPULSE_API void impulse_vm_context_release_value_map(impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API size_t impulse_vm_context_value_map_size(const impulse_vm_context_t* ctx, size_t handle);
IMPULSE_API const char* impulse_vm_context_value_map_get_key(const impulse_vm_context_t* ctx, size_t handle, size_t index);
IMPULSE_API float impulse_vm_context_value_map_get_value(const impulse_vm_context_t* ctx, size_t handle, size_t index);

IMPULSE_API void impulse_vm_context_mock_csc(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csc_offsets,
    const uint32_t* csc_targets
);

/**
 * @brief Execute a sequence of VM bytecode instructions against a VM state frame.
 * @param bytecode Pointer to array of impulse_instruction_t instructions.
 * @param instruction_count Number of instructions in array.
 * @param vm_state Pointer to aligned VM state frame.
 * @param input_param Input seed node ID or parameter value.
 * @return IMPULSE_VM_OK on successful execution, or VM error code.
 */
IMPULSE_API impulse_vm_status_t impulse_vm_execute(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_VM_H
