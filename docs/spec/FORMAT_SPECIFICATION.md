# Formal Specification: Impulse-Graph C-ABI Binary Snapshot Format

* **Specification Version**: 2.4.0
* **Document Status**: Standard Reference Specification
* **Byte Order**: Little-Endian (`LE`, IEEE 754 & x86-64 / ARM64 Native)
* **Memory Alignment**: 64-Byte Cache-Line / SIMD Boundary (`(len + 63) & ~63`) & 4KB OS Page Boundary
* **Integrity Validation**: SHA-256 (32 Bytes, calculated over `data[DataOffset .. EOF]`)

---

## 1. Specification Overview & Modular Design Principles

This document defines the formal binary protocol specification for the **Impulse-Graph C-ABI Binary Snapshot Format (Version 2.4)**. 

The format is organized into **Mandatory** and **Optional** sections positioned sequentially at 4KB OS page-aligned boundaries (`DataOffset = 4096`). This modular layout allows lightweight microservices (such as high-speed authorization query pods) to read the mandatory header and relation metadata at the start of the file and **selectively memory-map (`mmap`) only the specific CSR topology sections required**, completely bypassing gigabytes of string key mappings, entity metadata payloads, or dynamic delta logs.

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ SECTION 1 [MANDATORY]: Snapshot Header (SnapshotHeader, Size = DataOffset = 4096 Bytes) │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ SECTION 2 [MANDATORY]: Relation Metadata & Section Offset Directory                    │
│   ├── Domain Catalog (Node Types)                                                      │
│   └── Relation Directory Table (Offsets to CSR, ID Mappings, DTO Lookups, & Deltas)    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ SECTION 3 [MANDATORY/CSR]: Relation CSR Topology (RowOffsets & ColumnIndices)          │
│   ├── RowOffsets Array (uint32[] / uint64[])                                           │
│   └── ColumnIndices Array (Encoded Stream)                                             │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ SECTION 4 [OPTIONAL]: ID Mapping Section (DenseID <-> BusinessKey Mappings)            │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ SECTION 5 [OPTIONAL]: DTO & Entity Property Lookup Payload                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ SECTION 6 [OPTIONAL]: Delta Log Section (Live Edge Mutations / WAL Sinking)             │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Section 1: Snapshot Header Layout (Fixed 4KB Page Baseline)

The header occupies byte offset `0x00000000`. In Version 2.4, the baseline header occupies **4096 bytes** (`DataOffset = 4096`, `0x00001000`) to enforce 4KB OS page alignment across all memory-mapped sections.

### Header Extensibility Protocol & Compatibility Mandate
* **Variable Header Size**: The header size is variable and defined dynamically by `DataOffset`.
* **Parser Mandate**: All compliant parsers MUST read `DataOffset` from byte offset `0x06..0x09` and seek directly to byte `DataOffset` to begin unpacking Section 2.
* **Feature Compatibility Check**: All compliant parsers MUST check `(GlobalRequiredFeatures & ~SUPPORTED_GLOBAL_FEATURES) != 0` immediately upon opening Section 1 to fail fast if an unsupported global feature is present.

### Byte Offset Table

| Offset (Bytes) | Field Name | Type | Size | Default / Constant | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` – `0x03` | `Magic` | `uint32` | 4B | `0x494D5053` (`"IMPS"`) | Magic constant identifying Impulse-Graph binary file |
| `0x04` – `0x05` | `Version` | `uint16` | 2B | `2` (`0x0002`) | Protocol major version number (Version 2.4) |
| `0x06` – `0x09` | `DataOffset` | `uint32` | 4B | `4096` (`0x00001000`) | Byte offset where Section 2 (Payload) begins |
| `0x0A` – `0x0B` | `DomainCount` | `uint16` | 2B | Variable | Total number of domains in catalog |
| `0x0C` – `0x0D` | `RelationCount` | `uint16` | 2B | Variable | Total number of relations in matrix |
| `0x0E` – `0x15` | `KafkaOffset` | `uint64` | 8B | Variable | Kafka Write-Ahead Log (WAL) offset |
| `0x16` – `0x1D` | `TimestampMs` | `uint64` | 8B | Variable | Unix epoch timestamp (milliseconds) when created |
| `0x1E` – `0x3D` | `SHA256` | `byte[32]` | 32B | Variable | Cryptographic SHA-256 checksum over `data[DataOffset..EOF]` |
| `0x3E` – `0x3F` | `Reserved` | `byte[2]` | 2B | `0x00 0x00` | Reserved baseline alignment padding |
| `0x40` – `0x47` | `GlobalRequiredFeatures` | `uint64` | 8B | Bitmask | **Global Feature-in-Use Bitmask** (64-byte aligned boundary) |
| `0x48` – `0x0447` | `SignatureBlock` | `struct` | 1024B | `impulse_snapshot_signature_block_t` | **Cryptographic Signature Block** (Algorithm, Flags, Fingerprint, Signature, Public Key) |
| `0x0448` – `0x0FFF` | `HeaderPadding` | `byte[3000]` | 3000B | `0x00 ...` | Header padding to enforce 4KB OS page alignment |

---

## 3. Section 2 [MANDATORY]: Relation Metadata & Section Directory Table

Begins at byte offset `DataOffset` (byte 4096). Contains node type definitions (domains) and the **Section Directory Table** containing file byte offsets for selective `mmap` range loading.

### Part A: Domain Catalog (Node Types)
Contains `DomainCount` sequential domain records:
* `DomainID` (`uint16`, 2B): Zero-indexed domain identifier.
* `KeyType` (`uint8`, 1B): Key type (`0x00=INT16`, `0x01=INT32`, `0x02=INT64`, `0x03=UUID`, `0x04=STRING`).
* `NameLen` (`uint16`, 2B): Length of domain name.
* `Name` (`byte[NameLen]`): UTF-8 string (e.g. `"user"`, `"group"`).

### Part B: Relation Directory Table (Section Byte Pointers)
Contains `RelationCount` relation metadata descriptors. Each descriptor is exactly **109 bytes packed** (`impulse_relation_directory_entry_t`) and explicitly specifies absolute file offsets to allow clients to `mmap` specific sections independently:

> [!NOTE]
> **Normative Rule for Generator Tooling**: `EncodingType` (`uint8_t`) specifies the primary CSR matrix decoder enum (`0x00`..`0x08`). Generator tooling MUST also set Bit `EncodingType` (`1ULL << EncodingType`) in `SectionFeatures` to ensure instant bitwise feature compatibility checking (`(SectionFeatures & ~supported_flags) != 0`).

| Field Name | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `SrcDomainID` | `uint16` | 2B | Domain ID of source nodes |
| `TgtDomainID` | `uint16` | 2B | Domain ID of target nodes |
| `EncodingType` | `uint8` | 1B | Relation compression encoding flag (`0x00`..`0x0A`) |
| `NodeCount` | `uint64` | 8B | Number of source nodes ($N$) in relation matrix (supports $> 4.29\text{B}$ nodes) |
| `EdgeCount` | `uint64` | 8B | Total number of directed edges ($E$) in relation matrix (up to $9.22 \times 10^{18}$ edges) |
| `SectionFeatures` | `uint64` | 8B | **Per-Section Feature-in-Use Bitmask** (Encodings & Annotations) |
| `CsrRowOffOffset` | `uint64` | 8B | **Absolute File Offset** to `RowOffsets` array |
| `CsrRowOffBytes` | `uint64` | 8B | Byte size of `RowOffsets` array ($= (N + 2) \times \text{width}$) |
| `CsrColIdxOffset` | `uint64` | 8B | **Absolute File Offset** to `ColumnIndices` array |
| `CsrColIdxBytes` | `uint64` | 8B | Byte size of `ColumnIndices` stream |
| `IdMapOffset` | `uint64` | 8B | **Absolute File Offset** to Section 4 (ID Mappings, `0` if omitted) |
| `IdMapBytes` | `uint64` | 8B | Byte size of Section 4 (ID Mappings, `0` if omitted) |
| `DtoLookupOffset` | `uint64` | 8B | **Absolute File Offset** to Section 5 (DTO Entity Data, `0` if omitted) |
| `DtoLookupBytes` | `uint64` | 8B | Byte size of Section 5 (DTO Entity Data, `0` if omitted) |
| `DeltaLogOffset` | `uint64` | 8B | **Absolute File Offset** to Section 6 (Delta Log, `0` if omitted) |
| `DeltaLogBytes` | `uint64` | 8B | Byte size of Section 6 (Delta Log, `0` if omitted) |

---

## 4. Feature-in-Use Bitmasks Specification (`uint64_t` Bitfields)

Compliant parsers MUST evaluate `(file_features & ~tool_supported_features) != 0`. If true, the tool MUST reject processing with an explicit unsupported feature diagnostic error.

### 4.1 Global Feature Flags (`GlobalRequiredFeatures` — Header `0x40..0x47`)

| Bit Position | Hex Value | Constant Name | Description |
| :--- | :--- | :--- | :--- |
| Bit 0 | `0x0000000000000001ULL` | `GLOBAL_FEAT_64BIT_NODES` | Trillion-node scale graph ($> 4.29\text{B}$ nodes) |
| Bit 1 | `0x0000000000000002ULL` | `GLOBAL_FEAT_ZSTD_DICT_EMBEDDED` | Embedded 64KB Zstd dictionary at `DictionaryOffset` |
| Bit 2 | `0x0000000000000004ULL` | `GLOBAL_FEAT_DELTA_LOG_PRESENT` | Live WAL edge mutations present in Section 6 |
| Bit 3 | `0x0000000000000008ULL` | `GLOBAL_FEAT_4KB_PAGE_ALIGNED` | Enforces explicit 4KB page alignment across all sections |
| Bit 4 | `0x0000000000000010ULL` | `GLOBAL_FEAT_CRYPTO_SIGNED` | Cryptographic signature block present in Section 1 header |

---

### 4.2 Section 3 Relation Feature Flags (`SectionFeatures` in Directory Entry)

#### Category A: Topology Encodings (Bits 0..8)
| Bit Position | Hex Value | Constant Name | Description |
| :--- | :--- | :--- | :--- |
| Bit 0 | `0x0000000000000001ULL` | `RELATION_FEAT_ENC_RAW_UINT32` | Uncompressed 4-byte `uint32` target node array |
| Bit 1 | `0x0000000000000002ULL` | `RELATION_FEAT_ENC_DELTA_VBYTE` | Delta-VByte stream compression |
| Bit 2 | `0x0000000000000004ULL` | `RELATION_FEAT_ENC_RAW_UINT16` | Uncompressed 2-byte `uint16` target node array |
| Bit 3 | `0x0000000000000008ULL` | `RELATION_FEAT_ENC_HYBRID_16_32` | Partitioned hot (`uint16`) / cold (`uint32`) target array |
| Bit 4 | `0x0000000000000010ULL` | `RELATION_FEAT_ENC_SIMDCOMP` | SIMDComp / PFOR-Delta bit-packed integer stream |
| Bit 5 | `0x0000000000000020ULL` | `RELATION_FEAT_ENC_SLICED_ELLPACK` | GPU Sliced ELLPACK format for warp-coalesced access |
| Bit 6 | `0x0000000000000040ULL` | `RELATION_FEAT_ENC_TPU_BCOO` | TPU Tile Blocked COO format |
| Bit 7 | `0x0000000000000080ULL` | `RELATION_FEAT_ENC_RAW_UINT64` | Uncompressed 8-byte `uint64` target node array |
| Bit 8 | `0x0000000000000100ULL` | `RELATION_FEAT_ENC_ROARING_BITMAP` | Roaring Bitmap compressed adjacency sets |

#### Category B: Encoding Reserved Gap (Bits 9..15)
* `Bits 9..15`: Reserved for future matrix/topology encodings.

#### Category C: Relation Features & Annotations (Bits 16..31)
| Bit Position | Hex Value | Constant Name | Description |
| :--- | :--- | :--- | :--- |
| Bit 16 | `0x0000000000010000ULL` | `RELATION_FEAT_WEIGHTED_EDGES` | Float32/Float64/Int32 edge weight payload array |
| Bit 17 | `0x0000000000020000ULL` | `RELATION_FEAT_KV_LABELS` | Key-value edge attribute labels |
| Bit 18 | `0x0000000000040000ULL` | `RELATION_FEAT_DTO_EDGE_ANNOTATIONS` | Structured JSON / MessagePack / FlatBuffers edge payloads |
| Bit 19 | `0x0000000000080000ULL` | `RELATION_FEAT_TEMPORAL_TIMESTAMPS` | Per-edge uint64 creation/expiry timestamp array |
| Bit 20 | `0x0000000000100000ULL` | `RELATION_FEAT_PER_SECTION_ZSTD` | Independent RFC 8878 Zstd frame compressed stream |
| Bit 21 | `0x0000000000200000ULL` | `RELATION_FEAT_INCOMING_CSR_INDEX` | Transpose CSR index for bidirectional traversal |

#### Category D: Extension Reserved Gap (Bits 22..63)
* `Bits 22..63`: Reserved for future core spec and custom vendor extensions.

---

### 4.3 Section 4 ID Mapping Feature Flags (`SectionFeatures`)

| Bit Position | Hex Value | Constant Name | Description |
| :--- | :--- | :--- | :--- |
| Bit 0 | `0x0000000000000001ULL` | `MAPPING_FEAT_ZSTD_COMPRESSION` | Zstd dictionary string compression |
| Bit 1 | `0x0000000000000002ULL` | `MAPPING_FEAT_HUFFMAN_PREFIX` | Front-coded prefix string compression |
| Bit 2 | `0x0000000000000004ULL` | `MAPPING_FEAT_UUID128_BINARY` | Raw 16-byte fixed binary UUID keys |

---

## 5. Section 3 [CSR TOPOLOGY]: Relation CSR Arrays

Contains the core CSR matrix arrays (`RowOffsets` and `ColumnIndices`) positioned at 64-byte and 4KB-aligned file offsets as defined in the Section Directory Table.

---

## 6. Section 4 [OPTIONAL]: ID Mapping Section

Contains DenseID $\leftrightarrow$ BusinessKey mappings. Located at `IdMapOffset`.

---

## 7. Section 5 [OPTIONAL]: DTO & Entity Property Lookup Data

Contains structured JSON / MessagePack / FlatBuffers entity properties. Located at `DtoLookupOffset`.

---

## 8. Section 6 [OPTIONAL]: Delta Log Section (Live Edge Mutations / WAL Sinking)

Located at `DeltaLogOffset` (byte size `DeltaLogBytes`). Set to `0` for fully compacted static snapshots.

---

## 9. Standard Kaitai Struct (.ksy) Declarative Schema

```yaml
meta:
  id: impulse_graph_snapshot
  title: Impulse-Graph C-ABI Binary Snapshot Format
  file-extension: bin
  endian: le
  license: Apache-2.0

seq:
  - id: header
    type: snapshot_header
  - id: metadata_section
    type: metadata_section
  - id: relation_data_section
    type: relation_data_section

types:
  snapshot_header:
    seq:
      - id: magic
        contents: [0x53, 0x50, 0x4d, 0x49] # "IMPS" Little-Endian
      - id: version
        type: u2
      - id: data_offset
        type: u4
      - id: domain_count
        type: u2
      - id: relation_count
        type: u2
      - id: kafka_offset
        type: u8
      - id: timestamp_ms
        type: u8
      - id: sha256_checksum
        size: 32
      - id: reserved
        size: 2
      - id: global_required_features
        type: u8
      - id: header_padding
        size: 4024

  metadata_section:
    seq:
      - id: domains
        type: domain_catalog_entry
        repeat: expr
        repeat-expr: _root.header.domain_count
      - id: relation_directory
        type: relation_directory_entry
        repeat: expr
        repeat-expr: _root.header.relation_count

  domain_catalog_entry:
    seq:
      - id: domain_id
        type: u2
      - id: key_type
        type: u1
      - id: name_len
        type: u2
      - id: name
        type: str
        size: name_len
        encoding: UTF-8

  relation_directory_entry:
    seq:
      - id: src_domain_id
        type: u2
      - id: tgt_domain_id
        type: u2
      - id: encoding_type
        type: u1
        enum: relation_encoding
      - id: node_count
        type: u4
      - id: edge_count
        type: u8
      - id: section_features
        type: u8
      - id: csr_row_off_offset
        type: u8
      - id: csr_row_off_bytes
        type: u8
      - id: csr_col_idx_offset
        type: u8
      - id: csr_col_idx_bytes
        type: u8
      - id: id_map_offset
        type: u8
      - id: id_map_bytes
        type: u8
      - id: dto_lookup_offset
        type: u8
      - id: dto_lookup_bytes
        type: u8
      - id: delta_log_offset
        type: u8
      - id: delta_log_bytes
        type: u8

enums:
  relation_encoding:
    0: raw_uint32
    1: delta_vbyte
    2: raw_uint16
    3: hybrid_uint16_uint32
    4: simdcomp_bitpacked
    5: sliced_ellpack
    6: tpu_tile_bcoo
    7: raw_uint64
    8: roaring_bitmap
```
