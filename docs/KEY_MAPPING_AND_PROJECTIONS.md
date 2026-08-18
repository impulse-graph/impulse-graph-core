# Impulse Graph C-ABI: Domain Key Resolution, Projections & Memory Model

This document serves as the **normative architectural reference** for AI Coding Assistants and engine implementers regarding domain key resolution, columnar projections, nullability bitmaps, parameter bindings, and zero-allocation memory lifecycle across the Impulse Graph Engine.

---

## 1. Domain Key System (Section 4 & Section 2)

Impulse Graph strictly decouples **Physical Dense Node IDs** from **External Domain Keys**:

* **Dense Node IDs (`0..N-1`)**: The internal VM, bitsets, CSR row offsets, and SIMD instructions operate exclusively on dense, contiguous `uint32_t` / `uint64_t` indices for $O(1)$ L1 cache access.
* **Domain Keys**: External identifiers (UUIDs, Strings, SQL `BIGINT`s, floats) mapped to dense node IDs via Minimal Perfect Hash Functions (MPHF), binary search tables, or direct arrays in Section 4/5 of the `.imps` binary snapshot.

### 1.1 Supported Domain Key Types (Normative Specification v0.9.0)

| Type Code | Constant | Spec Byte Size | Input Binding API | Column Projection C Type |
| :---: | :--- | :---: | :--- | :--- |
| `0x01` | `IMPULSE_KEY_TYPE_INT8` | 1 byte | `impulse_stmt_bind_int(stmt, "$k", val)` | `const int8_t*` |
| `0x02` | `IMPULSE_KEY_TYPE_INT16` | 2 bytes | `impulse_stmt_bind_int(stmt, "$k", val)` | `const int16_t*` |
| `0x03` | `IMPULSE_KEY_TYPE_INT32` | 4 bytes | `impulse_stmt_bind_int(stmt, "$k", val)` | `const int32_t*` |
| `0x04` | `IMPULSE_KEY_TYPE_INT64` | 8 bytes | `impulse_stmt_bind_int(stmt, "$k", val)` | `const int64_t*` |
| `0x05` | `IMPULSE_KEY_TYPE_UINT8` | 1 byte | `impulse_stmt_bind_uint(stmt, "$k", val)` | `const uint8_t*` |
| `0x06` | `IMPULSE_KEY_TYPE_UINT16` | 2 bytes | `impulse_stmt_bind_uint(stmt, "$k", val)` | `const uint16_t*` |
| `0x07` | `IMPULSE_KEY_TYPE_UINT32` | 4 bytes | `impulse_stmt_bind_uint(stmt, "$k", val)` | `const uint32_t*` |
| `0x08` | `IMPULSE_KEY_TYPE_UINT64` | 8 bytes | `impulse_stmt_bind_node(stmt, "$k", val)` | `const uint64_t*` |
| `0x09` | `IMPULSE_KEY_TYPE_FLOAT32` | 4 bytes | `impulse_stmt_bind_float(stmt, "$k", val)` | `const float*` |
| `0x0A` | `IMPULSE_KEY_TYPE_FLOAT64` | 8 bytes | `impulse_stmt_bind_float(stmt, "$k", val)` | `const double*` |
| `0x0B` | `IMPULSE_KEY_TYPE_VAR_STRING`| Variable | `impulse_stmt_bind_str(stmt, "$k", "str")` | `const char* const*` (Pointers into mmap Section 2) |
| `0x0C` | `IMPULSE_KEY_TYPE_UUID128` | 16 bytes | `impulse_stmt_bind_uuid(stmt, "$k", raw16)`| `const uint8_t (*)[16]` (Contiguous 16B UUIDs) |

### 1.2 Core Resolution Functions in `impulse_graph.h`

```c
// 1. Forward Lookup: Domain Key -> Dense Node ID (O(1) MPHF)
IMPULSE_API impulse_status_t impulse_snapshot_resolve_key(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    const void* key_bytes,
    size_t key_len,
    uint32_t* out_node_id
);

// 2. Reverse Lookup: Dense Node ID -> Domain Key (Zero-Copy mmap pointer)
IMPULSE_API impulse_status_t impulse_snapshot_resolve_dense_id(
    const impulse_snapshot_t* snapshot,
    uint16_t domain_id,
    uint32_t node_id,
    const void** out_key_bytes,
    size_t* out_key_len
);
```

---

## 2. The SQLite-Style Statement Execution Lifecycle (`impulse_stmt_*`)

```
   1. Prepare (< 3 µs)
      impulse_stmt_prepare(snap, query_str, &stmt)
      └─ Compiles AST -> Emits register-allocated impOps -> Inspects snapshot catalog statistics.

   2. Query Sizing
      size_t buf_len = impulse_stmt_buffer_size(stmt)
      └─ Returns exact bytes needed for Scratch Memory + Columnar Output Arrays.

   3. Allocate Caller Buffer
      void* buf = malloc(buf_len); // Or stack alloca(), caller arena, hugepages

   4. Bind Input Parameters & Frontiers
      impulse_stmt_bind_node(stmt, "$seed", 14726);
      impulse_stmt_bind_nodes(stmt, "$frontier", seed_array, seed_count);
      impulse_stmt_bind_bitset(stmt, "$mask", words, word_count);
      impulse_stmt_bind_roaring(stmt, "$compressed", roaring_bytes, len);
      impulse_stmt_bind_uuid(stmt, "$tenant", uuid16);

   5. Execute in One Vectorized SIMD Pass
      impulse_stmt_execute(stmt, buf, buf_len);

   6. Read Apache Arrow-Style Columnar Results
      size_t rows = impulse_stmt_row_count(stmt);
      uint32_t cols = impulse_stmt_column_count(stmt);
      for (uint32_t c = 0; c < cols; ++c) {
          const char* name    = impulse_stmt_column_name(stmt, c);
          uint8_t type        = impulse_stmt_column_type(stmt, c);
          const void* data    = impulse_stmt_column_data(stmt, c);
          bool is_null        = impulse_stmt_column_is_null(stmt, c, row_idx);
      }

   7. Finalize Statement
      impulse_stmt_finalize(stmt);
```

---

## 3. Caller Memory Buffer Layout (Scratch + Return Area)

When executing `impulse_stmt_execute(stmt, buffer, buffer_size)`, the caller provides a single contiguous memory block partitioned internally with 128-byte alignment:

```
Caller-Provided Memory Buffer:
┌─────────────────────────┬─────────────────────────┬─────────────────────────┬─────────────────────────┐
│ Section A: Scratch Area │ Section B: Col 0 Data   │ Section C: Col 1 Data   │ Section D: Null Bitmaps │
│ Internal VM Bitsets &   │ Array of uint64_t       │ Array of float32        │ Validity Bitmaps        │
│ Vector Scratch Arenas   │ (e.g. Node IDs)         │ (e.g. PageRank Scores)  │ (1 bit per row)         │
│ (128-Byte Aligned)      │ (128-Byte Aligned)      │ (128-Byte Aligned)      │ (64-Byte Aligned)       │
└─────────────────────────┴─────────────────────────┴─────────────────────────┴─────────────────────────┘
```

---

## 4. Nullability & Validity Bitmaps (Apache Arrow Compatible)

Inside `impulse_column_descriptor_t`, nullability is represented via an **Arrow-Compatible Validity Bitmap**:

1. **Non-Nullable Columns (`is_nullable == false`)**:
   * `null_bitmap == NULL`.
   * 100% of rows are guaranteed non-null. Zero memory overhead.
2. **Nullable Columns (`is_nullable == true`)**:
   * `null_bitmap` points to an array of 64-bit words in the buffer.
   * **Bit `i == 1`**: Row `i` contains a valid value.
   * **Bit `i == 0`**: Row `i` is `NULL`.
3. **Checking Nullability**:
   ```c
   bool is_null = impulse_stmt_column_is_null(stmt, col_idx, row_idx);
   ```

---

## 5. Summary of Guarantees for AI Agents & Implementers

1. **Zero Heap Allocation During Query Execution**:
   * `impulse_stmt_execute()` MUST NOT call `malloc`, `new`, or trigger garbage collection.
   * All intermediate bitsets and output vectors live strictly inside the caller-provided buffer.
2. **Zero String Copies on Projection**:
   * String columns project `const char*` pointers pointing directly into the immutable memory-mapped string table in Section 2.
3. **Single In-Kernel Compiler**:
   * All queries (Cypher, ImpLog, ImpK, ImpScheme) compile through the C++20 engine in `< 3 µs`. No separate compiler process or secondary runtime required.
