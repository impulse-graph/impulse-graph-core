#ifndef IMPULSE_VM_H
#define IMPULSE_VM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Little-Endian VM Magic: 'I' 'M' 'P' 'B' (0x494D5042)
#define IMPULSE_VM_MAGIC 0x494D5042

// Status Flags Bitmasks (FLAGS Register)
#define IMPULSE_VM_FLAG_ZF (1ULL << 0) // Zero Flag: set if candidate set or arithmetic result is empty / 0
#define IMPULSE_VM_FLAG_LT (1ULL << 1) // Less Than Flag: set if src1 < src2
#define IMPULSE_VM_FLAG_GT (1ULL << 2) // Greater Than Flag: set if src1 > src2
#define IMPULSE_VM_FLAG_EQ (1ULL << 3) // Equal Flag: set if src1 == src2
#define IMPULSE_VM_FLAG_ST (1ULL << 4) // Stable Flag: set by repeat checks when set generation converged

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
#define OP_VEC_GET                  0x38
#define OP_VEC_SET                  0x39
#define OP_VEC_SEQUENCE             0x3A
#define OP_CSR_GET_NBR              0x3B
#define OP_CC_AFFOREST              0x40

#define OP_JMP                      0x50
#define OP_JZ                       0x51
#define OP_JNZ                      0x52
#define OP_LOOP_DECR                0x53
#define OP_STABLE_CHECK             0x54
#define OP_CALL                     0x55
#define OP_RET                      0x56

#define OP_MOV                      0x70
#define OP_CLEAR_REG                0x71
#define OP_CMP                      0x72

#define OP_COLLECT_BITSET           0x90
#define OP_COLLECT_ARRAY            0x91
#define OP_MAP_DENSE_TO_KEYS        0x92
#define OP_COLLECT_VALUE_MAP        0x93
#define OP_HALT                     0xFF

// Register Type Tags
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

// VM Execution result status
typedef enum {
    IMPULSE_VM_OK = 0,
    IMPULSE_VM_ERR_INVALID_OPCODE = 1,
    IMPULSE_VM_ERR_OUT_OF_BOUNDS = 2,
    IMPULSE_VM_ERR_NULL_SNAPSHOT = 3,
    IMPULSE_VM_ERR_STACK_OVERFLOW = 4,
    IMPULSE_VM_ERR_STACK_UNDERFLOW = 5,
    IMPULSE_VM_ERR_INVALID_REGISTER = 6
} impulse_vm_status_t;

// Fixed 64-bit Instruction Encoding
typedef struct alignas(8) {
    uint8_t  opcode;    // 0x00
    uint8_t  flags;     // 0x01
    uint16_t dst_reg;   // 0x02..0x03
    uint32_t payload;   // 0x04..0x07 (src_reg, rel_id, offset, etc.)
} impulse_instruction_t;

// Forward declaration of snapshot and execution context
typedef struct impulse_snapshot impulse_snapshot_t;
typedef struct impulse_vm_context impulse_vm_context_t;

// Input keys list structure for key mapping opcodes
typedef struct {
    const char** keys;
    size_t count;
} impulse_vm_input_keys;

// VM State execution frame - aligned to 64 bytes and sized to exactly 640 bytes
// to match the Java FFM MemorySegment representation
typedef struct alignas(64) {
    uint32_t pc;                                  // Offset 0
    uint32_t reserved;                            // Offset 4 (Alignment padding)
    uint64_t flags;                               // Offset 8 (ZF, LT, GT, EQ, ST flags)
    uint64_t registers[64];                       // Offset 16..527
    uint8_t  register_types[64];                  // Offset 528..591 (impulse_register_type_t values)
    impulse_vm_context_t* query_context;          // Offset 592..599
    uint32_t call_stack[8];                       // Offset 600..631 (8 levels of subroutine return PC)
    uint32_t call_stack_depth;                    // Offset 632..635
    uint32_t reserved_padding2;                   // Offset 636..639 (Pads struct to exactly 640 bytes)
} impulse_vm_state_t;

// Public Context lifecycle APIs
impulse_vm_context_t* impulse_vm_context_create(const impulse_snapshot_t* snapshot);
void impulse_vm_context_destroy(impulse_vm_context_t* ctx);
size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx);
const float* impulse_vm_context_get_float_vector(const impulse_vm_context_t* ctx, size_t handle);
const double* impulse_vm_context_get_double_vector(const impulse_vm_context_t* ctx, size_t handle);
int impulse_vm_context_acquire_bitset(impulse_vm_context_t* ctx);
void impulse_vm_context_release_bitset(impulse_vm_context_t* ctx, size_t handle);
void impulse_vm_context_bitset_add(impulse_vm_context_t* ctx, size_t handle, uint64_t node_id);
bool impulse_vm_context_bitset_test(const impulse_vm_context_t* ctx, size_t handle, uint64_t node_id);
void impulse_vm_context_bitset_fill(impulse_vm_context_t* ctx, size_t handle, uint64_t count);
uint64_t impulse_vm_context_bitset_get_word(const impulse_vm_context_t* ctx, size_t handle, size_t word_idx);
int impulse_vm_context_acquire_float_vector(impulse_vm_context_t* ctx);
void impulse_vm_context_release_float_vector(impulse_vm_context_t* ctx, size_t handle);
void impulse_vm_context_float_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, float val);
int impulse_vm_context_acquire_double_vector(impulse_vm_context_t* ctx);
void impulse_vm_context_release_double_vector(impulse_vm_context_t* ctx, size_t handle);
void impulse_vm_context_double_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, double val);
int impulse_vm_context_acquire_node_vector(impulse_vm_context_t* ctx);
void impulse_vm_context_release_node_vector(impulse_vm_context_t* ctx, size_t handle);
const uint64_t* impulse_vm_context_get_node_vector(const impulse_vm_context_t* ctx, size_t handle);
int impulse_vm_context_acquire_string_vector(impulse_vm_context_t* ctx);
void impulse_vm_context_release_string_vector(impulse_vm_context_t* ctx, size_t handle);
void impulse_vm_context_string_vector_add(impulse_vm_context_t* ctx, size_t handle, const char* str);
size_t impulse_vm_context_string_vector_size(const impulse_vm_context_t* ctx, size_t handle);
const char* impulse_vm_context_string_vector_get(const impulse_vm_context_t* ctx, size_t handle, size_t index);
int impulse_vm_context_acquire_value_map(impulse_vm_context_t* ctx);
void impulse_vm_context_release_value_map(impulse_vm_context_t* ctx, size_t handle);
size_t impulse_vm_context_value_map_size(const impulse_vm_context_t* ctx, size_t handle);
const char* impulse_vm_context_value_map_get_key(const impulse_vm_context_t* ctx, size_t handle, size_t index);
float impulse_vm_context_value_map_get_value(const impulse_vm_context_t* ctx, size_t handle, size_t index);

void impulse_vm_context_mock_csc(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csc_offsets,
    const uint32_t* csc_targets
);

// Public VM execution API
impulse_vm_status_t impulse_vm_execute(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_VM_H
