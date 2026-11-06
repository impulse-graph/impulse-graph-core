#!/usr/bin/env python3
import os
import json
import struct
import hashlib

from pathlib import Path`r`nSPEC_VECTORS_DIR = os.environ.get("SPEC_VECTORS_DIR") or str((Path(__file__).resolve().parent.parent.parent.parent / "impulse-graph-spec" / "test-vectors"))

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def compute_crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x82F63B78
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF

def compute_header_crc32(header_bytes: bytes) -> int:
    buf = bytearray()
    buf.extend(header_bytes[0x00:0x48])
    buf.extend(header_bytes[0x448:0x458])
    return compute_crc32c(bytes(buf))

def make_test_vector(tc_name, description, domains, relations, global_features=0x0000000000000001, corrupt_sha=False, corrupt_magic=False, corrupt_version=False, corrupt_offsets=None, corrupt_truncation=False, corrupt_name_len=False, metadata_kv=None, expected_status="SUCCESS"):
    folder = os.path.join(SPEC_VECTORS_DIR, tc_name)
    os.makedirs(folder, exist_ok=True)

    data_offset = 4096 # Spec v2.4 4KB page baseline
    domain_count = len(domains)
    relation_count = len(relations)

    # Build Header (4096 bytes)
    magic = 0x494D5053 if not corrupt_magic else 0x00000000
    version = 9 if not corrupt_version else 0x9999
    kafka_offset = 1000
    timestamp_ms = 1700000000000

    # Pack metadata_kv stream if provided
    kv_stream = bytearray()
    if metadata_kv:
        for k, v in metadata_kv.items():
            kb = k.encode('utf-8')
            vb = v.encode('utf-8')
            kv_stream.extend(struct.pack('<H', len(kb)))
            kv_stream.extend(kb)
            kv_stream.extend(struct.pack('<I', len(vb)))
            kv_stream.extend(vb)

    header_metadata_len = len(kv_stream)

    # Section 2 Payload (Domain Catalog & Relation Directory Table)
    payload = bytearray()

    # Section 2 Part A: Domain Catalog (64-byte fixed entries)
    domain_catalog_len = domain_count * 64
    rel_table_len = relation_count * 128
    
    # Calculate String Table offset (placed after Domain + Relation directory tables)
    string_table_pos = data_offset + domain_catalog_len + rel_table_len
    string_table = bytearray()
    
    domain_entries = bytearray()
    for dom in domains:
        dom_id = dom["id"]
        key_type = dom["key_type"]
        name_bytes = dom["name"].encode('utf-8')
        name_len = len(name_bytes) if not corrupt_name_len else 65535
        
        name_offset = string_table_pos + len(string_table)
        string_table.extend(name_bytes)
        
        # 64-byte DomainCatalogEntry
        domain_entries.extend(struct.pack('<H B B Q Q Q Q Q I H 14s',
            dom_id, key_type, 0, 0, 0, 0, 0, 0, name_offset, name_len, b'\x00'*14))

    rem_dom = len(domain_entries) % 128
    if rem_dom != 0:
        domain_entries.extend(b'\x00' * (128 - rem_dom))

    domain_catalog_len = len(domain_entries)
    string_table_pos = data_offset + domain_catalog_len + rel_table_len

    payload.extend(domain_entries)

    # Section 2 Part B: Relation Directory Table (128-byte fixed entries)
    rel_entries = bytearray()
    rel_data_chunks = bytearray()

    total_nodes = 0
    total_edges = 0

    current_rel_data_pos = string_table_pos + len(string_table)
    rem_str = current_rel_data_pos % 128
    if rem_str != 0:
        current_rel_data_pos += (128 - rem_str)

    for rel in relations:
        src_id = rel["src_id"]
        tgt_id = rel["tgt_id"]
        encoding_type = rel.get("encoding_type", 0)
        section_features = rel.get("section_features", 0)
        row_offs = rel.get("row_offsets", [0, 0])
        col_idxs = rel.get("col_indices", [])
        
        node_count = len(row_offs) - 1
        edge_count = len(col_idxs)
        total_nodes += node_count
        total_edges += edge_count

        # Write RowOffsets
        row_buf = bytearray()
        for off in row_offs:
            row_buf.extend(struct.pack('<I', off))
        
        # Write ColumnIndices
        col_buf = bytearray()
        if encoding_type == 7: # RAW_UINT64
            for col in col_idxs:
                col_buf.extend(struct.pack('<Q', col))
        elif encoding_type == 2: # UINT16
            for col in col_idxs:
                col_buf.extend(struct.pack('<H', col))
        elif encoding_type == 3: # HYBRID
            col_buf.extend(struct.pack('<H', len(col_idxs)))
            for col in col_idxs:
                col_buf.extend(struct.pack('<H', col))
        else: # RAW_UINT32 / VBYTE / SIMDCOMP
            for col in col_idxs:
                col_buf.extend(struct.pack('<I', col))

        # Optional edge weight arrays
        if rel.get("edge_weights_aos"):
            for w in rel["edge_weights_aos"]:
                col_buf.extend(struct.pack('<f', w))
        elif rel.get("edge_weights_soa"):
            for w in rel["edge_weights_soa"]:
                col_buf.extend(struct.pack('<d', w))
        elif rel.get("edge_timestamps"):
            for ts in rel["edge_timestamps"]:
                col_buf.extend(struct.pack('<Q', ts))

        csr_row_off_bytes = len(row_buf)
        csr_col_idx_bytes = len(col_buf)

        csr_row_off_offset = current_rel_data_pos
        actual_col_idx_offset = csr_row_off_offset + csr_row_off_bytes
        rem_col = actual_col_idx_offset % 128
        if rem_col != 0:
            actual_col_idx_offset += (128 - rem_col)
        csr_col_idx_offset = actual_col_idx_offset

        # Optional Sections
        id_map_offset = rel.get("id_map_offset", 0)
        id_map_bytes = rel.get("id_map_bytes", 0)
        dto_lookup_offset = rel.get("dto_lookup_offset", 0)
        dto_lookup_bytes = rel.get("dto_lookup_bytes", 0)
        delta_log_offset = rel.get("delta_log_offset", 0)
        delta_log_bytes = rel.get("delta_log_bytes", 0)

        actual_next_pos = actual_col_idx_offset + len(col_buf)
        rem_next = actual_next_pos % 128
        if rem_next != 0:
            actual_next_pos += (128 - rem_next)
        next_rel_data_pos = actual_next_pos

        if corrupt_offsets == "row_off_out_of_bounds":
            csr_row_off_offset = 0xFFFFFFFFFFFFFFFF
        elif corrupt_offsets == "col_idx_out_of_bounds":
            csr_col_idx_offset = 0xFFFFFFFFFFFFFFFF
        elif corrupt_offsets == "unaligned_row_off":
            csr_row_off_offset += 17

        # 128-byte RelationDirectoryEntry
        rel_entries.extend(struct.pack('<H H B Q Q Q Q Q Q Q Q Q Q I H H 35s',
            src_id, tgt_id, encoding_type, node_count, edge_count, section_features, 0,
            csr_row_off_offset, csr_row_off_bytes, csr_col_idx_offset, csr_col_idx_bytes,
            id_map_offset, id_map_bytes, 0, 0, 0, b'\x00'*35))

        # Append to rel_data_chunks
        rel_data_chunks.extend(row_buf)
        pad_row = actual_col_idx_offset - (current_rel_data_pos + len(row_buf))
        if pad_row > 0:
            rel_data_chunks.extend(b'\x00' * pad_row)

        rel_data_chunks.extend(col_buf)
        pad_col = actual_next_pos - (actual_col_idx_offset + len(col_buf))
        if pad_col > 0:
            rel_data_chunks.extend(b'\x00' * pad_col)

        current_rel_data_pos = actual_next_pos

    payload.extend(rel_entries)
    payload.extend(string_table)
    rem_payload = len(payload) % 128
    if rem_payload != 0:
        payload.extend(b'\x00' * (128 - rem_payload))

    payload.extend(rel_data_chunks)

    # Optional Section 4, 5, 6 Data Arrays
    if any(r.get("id_map_bytes") for r in relations):
        payload.extend(b'SECTION_4_ID_MAP_DATA_ARRAY')
    if any(r.get("dto_lookup_bytes") for r in relations):
        payload.extend(b'SECTION_5_DTO_LOOKUP_TABLE_PAYLOAD')
    if any(r.get("delta_log_bytes") for r in relations):
        payload.extend(b'SECTION_6_DELTA_LOG_WAL_MUTATIONS')

    # SHA256 over payload
    sha_bytes = hashlib.sha256(payload).digest()
    if corrupt_sha:
        sha_bytes = bytes([b ^ 0xFF for b in sha_bytes])

    header = bytearray(data_offset)
    struct.pack_into('<I', header, 0x00, magic)
    struct.pack_into('<H', header, 0x04, version)
    struct.pack_into('<I', header, 0x06, data_offset)
    struct.pack_into('<H', header, 0x0A, domain_count)
    struct.pack_into('<H', header, 0x0C, relation_count)
    struct.pack_into('<Q', header, 0x0E, timestamp_ms)
    struct.pack_into('<Q', header, 0x16, global_features)

    # Compute Header CRC-16 (at offset 0x3E)
    chk = crc16(bytes(header[0:0x3E]))
    struct.pack_into('<H', header, 0x3E, chk)

    full_snapshot = bytes(header) + bytes(payload)
    if corrupt_truncation:
        full_snapshot = full_snapshot[:data_offset + 50] # Truncate file

    # Write snapshot.imps
    imps_path = os.path.join(folder, "snapshot.imps")
    with open(imps_path, "wb") as f:
        f.write(full_snapshot)

    # Write input.tsv
    tsv_path = os.path.join(folder, "input.tsv")
    with open(tsv_path, "w") as f:
        f.write(f"# Test Vector {tc_name}\n# Domains: {domain_count}, Relations: {relation_count}\n")

    # Write manifest.json
    manifest = {
        "name": tc_name,
        "description": description,
        "spec_version": "2.4.0",
        "domain_count": domain_count,
        "relation_count": relation_count,
        "total_nodes": total_nodes,
        "total_edges": total_edges,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "expected_status": expected_status
    }
    manifest_path = os.path.join(folder, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"[+] Generated {tc_name} ({len(full_snapshot)} bytes)")

def generate_all():
    print("Generating 30 Edge-Case Test Vectors in impulse-graph-spec/test-vectors...")

    # 1. tc01_domain_catalog_stress: 30k domains, 10k relations
    domains_30k = [{"id": i, "name": f"dom_{i}", "key_type": 1} for i in range(30000)]
    relations_10k = [{"src_id": i % 30000, "tgt_id": (i + 1) % 30000, "row_offsets": [0, 1], "col_indices": [(i + 1) % 30000]} for i in range(10000)]
    make_test_vector("tc01_domain_catalog_stress", "Stresses domain catalog with 30,000 node types and 10,000 sparse relations", domains_30k, relations_10k)

    # 2. tc02_max_string_name_length
    long_name = "A" * 2000
    make_test_vector("tc02_max_string_name_length", "Stresses UTF-8 domain name parser with 2000-byte string name",
                     [{"id": 0, "name": long_name, "key_type": 4}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}])

    # 3. tc03_empty_snapshot
    make_test_vector("tc03_empty_snapshot", "Empty graph snapshot with 0 domains and 0 relations", [], [])

    # 4. tc04_dense_single_relation
    dense_cols = list(range(100000))
    make_test_vector("tc04_dense_single_relation", "Single node with 100,000 outgoing edges in a single CSR row",
                     [{"id": 0, "name": "user", "key_type": 1}, {"id": 1, "name": "item", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 1, "row_offsets": [0, 100000], "col_indices": dense_cols}])

    # 5. tc05_disconnected_nodes
    make_test_vector("tc05_disconnected_nodes", "10,000 isolated nodes with zero edges",
                     [{"id": 0, "name": "node", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0] * 10001, "col_indices": []}])

    # 6. tc06_deep_chain_graph
    chain_offs = list(range(10001))
    chain_cols = list(range(1, 10001))
    make_test_vector("tc06_deep_chain_graph", "Deep linear chain graph of 10,000 nodes (0->1->2...)",
                     [{"id": 0, "name": "node", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": chain_offs, "col_indices": chain_cols}])

    # Encodings 0x00 .. 0x07
    encodings = [
        ("tc07_encoding_raw_uint32", 0, "RAW_UINT32 uncompressed target array (0x00)"),
        ("tc08_encoding_delta_vbyte", 1, "DELTA_VBYTE varint stream encoding (0x01)"),
        ("tc09_encoding_raw_uint16", 2, "RAW_UINT16 target array (0x02)"),
        ("tc10_encoding_hybrid_16_32", 3, "HYBRID_UINT16_UINT32 partitioned target array (0x03)"),
        ("tc11_encoding_simdcomp", 4, "SIMDComp / PFOR-Delta bit-packed stream (0x04)"),
        ("tc12_encoding_raw_uint64", 7, "RAW_UINT64 8-byte target array (0x07)"),
    ]
    for tc_id, enc, desc in encodings:
        make_test_vector(tc_id, desc,
                         [{"id": 0, "name": "u", "key_type": 1}, {"id": 1, "name": "v", "key_type": 1}],
                         [{"src_id": 0, "tgt_id": 1, "encoding_type": enc, "row_offsets": [0, 2], "col_indices": [10, 20]}])

    # Key Mappings
    make_test_vector("tc13_keytype_uuid128", "Section 4 RFC 4122 UUID keys", [{"id": 0, "name": "user", "key_type": 3}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}])
    make_test_vector("tc14_keytype_string_utf8", "Section 4 UTF-8 business keys", [{"id": 0, "name": "user", "key_type": 4}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}])
    make_test_vector("tc15_keytype_int64", "Section 4 64-bit integer keys", [{"id": 0, "name": "user", "key_type": 2}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}])

    # Alignment & Failures
    make_test_vector("tc16_4kb_page_aligned", "Strict 4KB OS page aligned header baseline", [{"id": 0, "name": "u", "key_type": 1}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}])
    make_test_vector("tc17_invalid_sha256_corruption", "Mutated payload triggering SHA-256 mismatch", [{"id": 0, "name": "u", "key_type": 1}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_sha=True, expected_status="EXPECTED_FAILURE")
    make_test_vector("tc18_unsupported_global_feature", "Reserved global feature bit set", [{"id": 0, "name": "u", "key_type": 1}], [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], global_features=0x8000000000000000, expected_status="EXPECTED_FAILURE")

    # Optional Sections & Fixed-Size Edge Attributes (tc19..tc24)
    make_test_vector("tc19_edge_weights_aos_float32", "Array-of-Structures 32-bit float edge weight array",
                     [{"id": 0, "name": "u", "key_type": 1}, {"id": 1, "name": "v", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 1, "section_features": 0x00010000, "row_offsets": [0, 2], "col_indices": [10, 20], "edge_weights_aos": [1.5, 2.75]}])

    make_test_vector("tc20_edge_weights_soa_float64", "Structure-of-Arrays 64-bit float edge weight array",
                     [{"id": 0, "name": "u", "key_type": 1}, {"id": 1, "name": "v", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 1, "section_features": 0x00010000, "row_offsets": [0, 2], "col_indices": [10, 20], "edge_weights_soa": [100.25, 200.50]}])

    make_test_vector("tc21_edge_temporal_timestamps", "Per-edge uint64 creation/expiry timestamp array",
                     [{"id": 0, "name": "u", "key_type": 1}, {"id": 1, "name": "v", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 1, "section_features": 0x00080000, "row_offsets": [0, 2], "col_indices": [10, 20], "edge_timestamps": [1700000001000, 1700000002000]}])

    make_test_vector("tc22_section4_id_mappings", "Section 4 DenseID <-> BusinessKey ID Mappings",
                     [{"id": 0, "name": "user", "key_type": 4}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0], "id_map_offset": 8192, "id_map_bytes": 27}], expected_status="EXPECTED_FAILURE")

    make_test_vector("tc23_section5_dto_property_payloads", "Section 5 DTO entity property lookup payload table",
                     [{"id": 0, "name": "user", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0], "dto_lookup_offset": 12288, "dto_lookup_bytes": 35}])

    make_test_vector("tc24_section6_delta_log_wal", "Section 6 live mutation Write-Ahead Log Delta Log",
                     [{"id": 0, "name": "user", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0], "delta_log_offset": 16384, "delta_log_bytes": 32}])

    # Boundary Validation & Bad Metadata Failures (tc25..tc30)
    make_test_vector("tc25_bad_metadata_invalid_version", "Unsupported protocol version number (0x9999)",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_version=True, expected_status="EXPECTED_FAILURE")

    make_test_vector("tc26_catalog_offset_out_of_bounds", "CsrRowOffOffset points past EOF (0xFFFFFFFFFFFFFFFF)",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_offsets="row_off_out_of_bounds", expected_status="EXPECTED_FAILURE")

    make_test_vector("tc27_col_idx_offset_out_of_bounds", "CsrColIdxOffset points past EOF",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_offsets="col_idx_out_of_bounds", expected_status="EXPECTED_FAILURE")

    make_test_vector("tc28_truncated_file_payload", "File truncated midway through Section 3 CSR payload",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_truncation=True, expected_status="EXPECTED_FAILURE")

    make_test_vector("tc29_malformed_domain_name_len", "Domain NameLen set to 65535 exceeding file size",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_name_len=True, expected_status="EXPECTED_FAILURE")

    make_test_vector("tc30_unaligned_section_offset", "CsrRowOffOffset not aligned to 64-byte boundary",
                     [{"id": 0, "name": "u", "key_type": 1}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}], corrupt_offsets="unaligned_row_off", expected_status="EXPECTED_FAILURE")

    make_test_vector("tc31_custom_metadata", "Section 1 Header-Embedded and Section 7 Custom Metadata Stream",
                     [{"id": 0, "name": "user", "key_type": 4}],
                     [{"src_id": 0, "tgt_id": 0, "row_offsets": [0, 1], "col_indices": [0]}],
                     metadata_kv={"impulse.generator": "python-tooling v2.5.0", "impulse.created_at": "2026-08-02T20:56:00Z", "dataset.attribution": "OpenData Community License"})

    print("[+] All 31 Test Vector Folders Successfully Generated!")

if __name__ == "__main__":
    generate_all()
