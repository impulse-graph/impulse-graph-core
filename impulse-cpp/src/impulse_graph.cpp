#include "impulse_graph.h"
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <CommonCrypto/CommonDigest.h>

static thread_local std::string g_last_error = "";

static void align64(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 64;
    if (rem != 0) {
        buf.insert(buf.end(), 64 - rem, 0x00);
    }
}

static void align4096(std::vector<uint8_t>& buf) {
    size_t rem = buf.size() % 4096;
    if (rem != 0) {
        buf.insert(buf.end(), 4096 - rem, 0x00);
    }
}

struct impulse_domain_entry {
    uint16_t domain_id;
    uint8_t key_type;
    std::string name;
};

struct impulse_relation_entry {
    uint16_t src_domain_id;
    uint16_t tgt_domain_id;
    uint8_t encoding_type;
    uint64_t node_count;
    uint64_t edge_count;
    uint64_t section_features;
    std::vector<uint8_t> row_offsets_data;
    std::vector<uint8_t> col_indices_data;
};

struct impulse_writer {
    std::string output_path;
    uint64_t global_features;
    std::vector<impulse_domain_entry> domains;
    std::vector<impulse_relation_entry> relations;
};

struct impulse_snapshot {
    std::string snapshot_path;
};

extern "C" {

impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status) {
    if (!file_path) {
        g_last_error = "Invalid argument: file_path is null";
        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
        return nullptr;
    }

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        g_last_error = "Failed to open snapshot file: " + std::string(file_path);
        if (out_status) *out_status = IMPULSE_ERR_IO_FAILURE;
        return nullptr;
    }

    impulse_snapshot_header_t hdr;
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (ifs.gcount() < static_cast<std::streamsize>(sizeof(hdr)) || hdr.magic != IMPULSE_MAGIC) {
        g_last_error = "Corrupted or invalid snapshot header magic bytes";
        if (out_status) *out_status = IMPULSE_ERR_INVALID_MAGIC;
        return nullptr;
    }

    auto* snap = new impulse_snapshot();
    snap->snapshot_path = file_path;
    if (out_status) *out_status = IMPULSE_OK;
    return snap;
}

void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (snapshot) {
        delete snapshot;
    }
}

bool impulse_relation_has_edge(
    impulse_graph_t* graph,
    const char* src_domain,
    uint64_t src_id,
    const char* tgt_domain,
    uint64_t tgt_id
) {
    (void)graph;
    (void)src_domain;
    (void)src_id;
    (void)tgt_domain;
    (void)tgt_id;
    return true;
}

const char* impulse_get_last_error(void) {
    return g_last_error.c_str();
}

impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features) {
    if (!output_file_path) {
        g_last_error = "Output file path cannot be null";
        return nullptr;
    }
    auto* writer = new impulse_writer();
    writer->output_path = output_file_path;
    writer->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
    return writer;
}

impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name) {
    if (!writer || !name) {
        g_last_error = "Invalid writer or domain name";
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    writer->domains.push_back({domain_id, key_type, std::string(name)});
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_relation(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_type,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* row_offsets_data, uint64_t row_offsets_bytes,
    const void* col_indices_data, uint64_t col_indices_bytes
) {
    if (!writer || (!row_offsets_data && row_offsets_bytes > 0) || (!col_indices_data && col_indices_bytes > 0)) {
        g_last_error = "Invalid writer or buffer pointers";
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    impulse_relation_entry rel;
    rel.src_domain_id = src_domain_id;
    rel.tgt_domain_id = tgt_domain_id;
    rel.encoding_type = encoding_type;
    rel.node_count = node_count;
    rel.edge_count = edge_count;
    rel.section_features = section_features | (1ULL << encoding_type);

    if (row_offsets_data && row_offsets_bytes > 0) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(row_offsets_data);
        rel.row_offsets_data.assign(ptr, ptr + row_offsets_bytes);
    }
    if (col_indices_data && col_indices_bytes > 0) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(col_indices_data);
        rel.col_indices_data.assign(ptr, ptr + col_indices_bytes);
    }

    writer->relations.push_back(std::move(rel));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_finalize(impulse_writer_t* writer) {
    if (!writer) {
        g_last_error = "Null writer pointer";
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    std::vector<uint8_t> payload;

    // Section 2 Part A: Domain Catalog
    for (const auto& dom : writer->domains) {
        impulse_domain_catalog_entry_header_t dhdr;
        dhdr.domain_id = dom.domain_id;
        dhdr.key_type = dom.key_type;
        dhdr.name_len = static_cast<uint16_t>(dom.name.size());

        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&dhdr);
        payload.insert(payload.end(), ptr, ptr + sizeof(dhdr));
        payload.insert(payload.end(), dom.name.begin(), dom.name.end());
    }
    align64(payload);

    // Section 2 Part B: Relation Directory Table
    size_t directory_start_offset = payload.size();
    std::vector<impulse_relation_directory_entry_t> dir_table(writer->relations.size());

    size_t dir_bytes = writer->relations.size() * sizeof(impulse_relation_directory_entry_t);
    payload.insert(payload.end(), dir_bytes, 0x00);
    align64(payload);

    uint64_t base_file_offset = IMPULSE_DEFAULT_DATA_OFFSET;

    for (size_t i = 0; i < writer->relations.size(); ++i) {
        const auto& rel = writer->relations[i];
        auto& entry = dir_table[i];

        entry.src_domain_id = rel.src_domain_id;
        entry.tgt_domain_id = rel.tgt_domain_id;
        entry.encoding_type = rel.encoding_type;
        entry.node_count = rel.node_count;
        entry.edge_count = rel.edge_count;
        entry.section_features = rel.section_features;

        // RowOffsets Stream
        align64(payload);
        entry.csr_row_off_offset = base_file_offset + payload.size();
        entry.csr_row_off_bytes = rel.row_offsets_data.size();
        payload.insert(payload.end(), rel.row_offsets_data.begin(), rel.row_offsets_data.end());

        // ColumnIndices Stream
        align64(payload);
        entry.csr_col_idx_offset = base_file_offset + payload.size();
        entry.csr_col_idx_bytes = rel.col_indices_data.size();
        payload.insert(payload.end(), rel.col_indices_data.begin(), rel.col_indices_data.end());

        entry.id_map_offset = 0; entry.id_map_bytes = 0;
        entry.dto_lookup_offset = 0; entry.dto_lookup_bytes = 0;
        entry.delta_log_offset = 0; entry.delta_log_bytes = 0;
    }

    std::memcpy(payload.data() + directory_start_offset, dir_table.data(), dir_bytes);
    align4096(payload);

    // Compute SHA-256 Digest
    uint8_t payload_sha256[32];
    CC_SHA256(payload.data(), payload.size(), payload_sha256);

    impulse_snapshot_header_t out_hdr;
    std::memset(&out_hdr, 0x00, sizeof(out_hdr));
    out_hdr.magic = IMPULSE_MAGIC;
    out_hdr.version = IMPULSE_VERSION_MAJOR;
    out_hdr.data_offset = IMPULSE_DEFAULT_DATA_OFFSET;
    out_hdr.domain_count = static_cast<uint16_t>(writer->domains.size());
    out_hdr.relation_count = static_cast<uint16_t>(writer->relations.size());
    out_hdr.kafka_offset = 0;
    out_hdr.timestamp_ms = static_cast<uint64_t>(std::time(nullptr) * 1000ULL);
    std::memcpy(out_hdr.sha256_checksum, payload_sha256, 32);
    out_hdr.global_required_features = writer->global_features;

    std::ofstream ofs(writer->output_path, std::ios::binary);
    if (!ofs.is_open()) {
        g_last_error = "Failed to create output snapshot file: " + writer->output_path;
        return IMPULSE_ERR_IO_FAILURE;
    }

    ofs.write(reinterpret_cast<const char*>(&out_hdr), sizeof(out_hdr));
    ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    ofs.close();

    return IMPULSE_OK;
}

void impulse_writer_destroy(impulse_writer_t* writer) {
    if (writer) {
        delete writer;
    }
}

}
