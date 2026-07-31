#include "impulse_graph.h"
#include <iostream>
#include <string>

struct impulse_snapshot {
    std::string path;
};

struct impulse_writer {
    std::string output_path;
    uint64_t global_features;
};

static thread_local std::string g_last_error;

extern "C" {

impulse_snapshot_t* impulse_snapshot_open(const char* file_path, impulse_status_t* out_status) {
    if (!file_path) {
        g_last_error = "File path cannot be null";
        if (out_status) *out_status = IMPULSE_ERR_INVALID_ARGUMENT;
        return nullptr;
    }
    auto* snapshot = new impulse_snapshot();
    snapshot->path = file_path;
    if (out_status) *out_status = IMPULSE_OK;
    return snapshot;
}

void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (snapshot) {
        delete snapshot;
    }
}

bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
) {
    (void)snapshot;
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
    writer->global_features = global_features;
    return writer;
}

impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name) {
    (void)writer;
    (void)domain_id;
    (void)key_type;
    (void)name;
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
    (void)writer;
    (void)src_domain_id;
    (void)tgt_domain_id;
    (void)encoding_type;
    (void)node_count;
    (void)edge_count;
    (void)section_features;
    (void)row_offsets_data;
    (void)row_offsets_bytes;
    (void)col_indices_data;
    (void)col_indices_bytes;
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_finalize(impulse_writer_t* writer) {
    (void)writer;
    return IMPULSE_OK;
}

void impulse_writer_destroy(impulse_writer_t* writer) {
    if (writer) {
        delete writer;
    }
}

}
