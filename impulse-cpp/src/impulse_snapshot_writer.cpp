/**
 * @file impulse_snapshot_writer.cpp
 * @brief Streaming Impulse Binary Snapshot (.imps) Writer, Delta Layer, and Snapshot Compaction.
 */

#include "impulse_graph_internal.hpp"

extern "C" {

impulse_writer_t* impulse_writer_create(const char* output_file_path, uint64_t global_features) {
    if (!output_file_path) return nullptr;
    auto w = std::make_unique<impulse_writer_t>();
    w->output_path = output_file_path;
    w->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
    return w.release();
}

impulse_writer_t* impulse_writer_create_stream(impulse_write_fn write_cb, void* user_data, uint64_t global_features) {
    if (!write_cb) return nullptr;
    auto w = std::make_unique<impulse_writer_t>();
    w->write_cb = write_cb;
    w->user_data = user_data;
    w->global_features = global_features | IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED | IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    return w.release();
}

impulse_status_t impulse_writer_add_domain(impulse_writer_t* writer, uint16_t domain_id, uint8_t key_type, const char* name) {
    if (!writer || !name) return IMPULSE_ERR_INVALID_ARGUMENT;
    impulse_domain_catalog_entry_t dom{};
    dom.domain_id = domain_id;
    dom.key_type = key_type;
    dom.reserved1 = 0;
    dom.name_offset = 0; // Set during finalize via string table
    dom.node_count = 0;
    dom.attr_count = 0;
    dom.key_attr_index = 0xFFFF;

    writer->domains.push_back(dom);
    writer->domain_names.push_back(name);
    writer->domain_attributes.emplace_back();
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_set_domain_node_count(impulse_writer_t* writer, uint16_t domain_index, uint64_t node_count) {
    if (!writer || domain_index >= writer->domains.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    writer->domains[domain_index].node_count = node_count;
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_set_domain_key_attr(impulse_writer_t* writer, uint16_t domain_index, uint16_t key_attr_index) {
    if (!writer || domain_index >= writer->domains.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    writer->domains[domain_index].key_attr_index = key_attr_index;
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_domain_attribute(
    impulse_writer_t* writer,
    uint16_t domain_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    const void* data, uint64_t data_bytes,
    const void* offsets, uint64_t offsets_bytes
) {
    if (!writer || !name || domain_index >= writer->domains.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (writer->domain_attributes.size() <= domain_index) {
        writer->domain_attributes.resize(writer->domains.size());
    }

    impulse_writer_attr attr;
    attr.name = name;
    attr.type_code = type_code;
    attr.dimension = dimension;

    if (data && data_bytes > 0) {
        attr.data.resize(data_bytes);
        std::memcpy(attr.data.data(), data, data_bytes);
    }
    if (offsets && offsets_bytes > 0) {
        attr.offsets.resize(offsets_bytes);
        std::memcpy(attr.offsets.data(), offsets, offsets_bytes);
    }

    writer->domain_attributes[domain_index].push_back(std::move(attr));
    writer->domains[domain_index].attr_count = static_cast<uint16_t>(writer->domain_attributes[domain_index].size());
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_relation(
    impulse_writer_t* writer,
    uint16_t src_domain_id,
    uint16_t tgt_domain_id,
    uint8_t encoding_id,
    uint64_t node_count,
    uint64_t edge_count,
    uint64_t section_features,
    const void* row_offsets_data, uint64_t row_offsets_bytes,
    const void* col_indices_data, uint64_t col_indices_bytes
) {
    if (!writer || !row_offsets_data || !col_indices_data) return IMPULSE_ERR_INVALID_ARGUMENT;

    impulse_writer_relation rel;
    rel.relation_id = static_cast<uint16_t>(writer->relations.size());
    rel.src_domain_id = src_domain_id;
    rel.tgt_domain_id = tgt_domain_id;
    rel.encoding_id = encoding_id;
    rel.node_id_width = (edge_count > 0 && col_indices_bytes == edge_count * 2) ? 2 : ((edge_count > 0 && col_indices_bytes == edge_count * 8) ? 8 : 4);
    rel.edge_index_width = (node_count > 0 && row_offsets_bytes == (node_count + 1) * 8) ? 8 : 4;
    rel.node_count = node_count;
    rel.edge_count = edge_count;
    rel.section_features = section_features;

    rel.row_offsets.resize(row_offsets_bytes);
    std::memcpy(rel.row_offsets.data(), row_offsets_data, row_offsets_bytes);

    rel.col_indices.resize(col_indices_bytes);
    std::memcpy(rel.col_indices.data(), col_indices_data, col_indices_bytes);

    writer->relations.push_back(std::move(rel));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_attribute(
    impulse_writer_t* writer,
    uint16_t relation_index,
    const char* name,
    uint8_t type_code,
    uint32_t dimension,
    const void* data, uint64_t data_bytes,
    const void* offsets, uint64_t offsets_bytes
) {
    if (!writer || !name || relation_index >= writer->relations.size()) return IMPULSE_ERR_INVALID_ARGUMENT;
    auto& rel = writer->relations[relation_index];

    impulse_writer_attr attr;
    attr.name = name;
    attr.type_code = type_code;
    attr.dimension = dimension;

    if (data && data_bytes > 0) {
        attr.data.resize(data_bytes);
        std::memcpy(attr.data.data(), data, data_bytes);
    }
    if (offsets && offsets_bytes > 0) {
        attr.offsets.resize(offsets_bytes);
        std::memcpy(attr.offsets.data(), offsets, offsets_bytes);
    }

    rel.attributes.push_back(std::move(attr));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_add_index(
    impulse_writer_t* writer,
    uint16_t domain_id,
    uint16_t relation_id,
    uint16_t attribute_index,
    uint8_t index_type,
    const char* index_name,
    const void* index_data, uint64_t index_bytes,
    uint64_t payload_feature_mask
) {
    if (!writer || !index_name) return IMPULSE_ERR_INVALID_ARGUMENT;

    impulse_writer_index idx;
    idx.index_id = static_cast<uint32_t>(writer->indexes.size());
    idx.domain_id = domain_id;
    idx.relation_id = relation_id;
    idx.attribute_index = attribute_index;
    idx.index_type = index_type;
    idx.index_name = index_name;
    idx.payload_feature_mask = payload_feature_mask;

    if (index_data && index_bytes > 0) {
        idx.data.resize(index_bytes);
        std::memcpy(idx.data.data(), index_data, index_bytes);
    }

    writer->indexes.push_back(std::move(idx));
    return IMPULSE_OK;
}

impulse_status_t impulse_writer_set_metadata(impulse_writer_t* writer, const char* key, const char* value) {
    if (!writer || !key || !value) return IMPULSE_ERR_INVALID_ARGUMENT;
    writer->metadata[key] = value;
    return IMPULSE_OK;
}


impulse_status_t impulse_writer_finalize(impulse_writer_t* writer) {
    if (!writer) return IMPULSE_ERR_INVALID_ARGUMENT;

    bool footer_catalog = (writer->global_features & IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG) != 0 || (writer->write_cb != nullptr);
    if (footer_catalog) {
        writer->global_features |= IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    }

    // Sort relations by src_domain_id primary, tgt_domain_id secondary
    std::sort(writer->relations.begin(), writer->relations.end(), [](const impulse_writer_relation& a, const impulse_writer_relation& b) {
        if (a.src_domain_id != b.src_domain_id) return a.src_domain_id < b.src_domain_id;
        return a.tgt_domain_id < b.tgt_domain_id;
    });
    for (size_t idx = 0; idx < writer->relations.size(); ++idx) {
        writer->relations[idx].relation_id = static_cast<uint16_t>(idx);
    }

    // 1. Build Shared String Table & String Pool
    std::vector<uint8_t> string_table;
    string_table.push_back('\0'); // Offset 0 = empty string ""
    std::unordered_map<std::string, uint32_t> string_map;
    string_map[""] = 0;

    auto get_or_add_string = [&](const std::string& str) -> uint32_t {
        if (str.empty()) return 0;
        auto it = string_map.find(str);
        if (it != string_map.end()) return it->second;
        uint32_t off = static_cast<uint32_t>(string_table.size());
        string_table.insert(string_table.end(), str.begin(), str.end());
        string_table.push_back('\0');
        string_map[str] = off;
        return off;
    };

    if (writer->domain_attributes.size() < writer->domains.size()) {
        writer->domain_attributes.resize(writer->domains.size());
    }

    // Collect all strings
    for (size_t i = 0; i < writer->domain_names.size(); ++i) {
        writer->domains[i].name_offset = get_or_add_string(writer->domain_names[i]);
        for (const auto& attr : writer->domain_attributes[i]) {
            get_or_add_string(attr.name);
        }
    }
    for (const auto& rel : writer->relations) {
        for (const auto& attr : rel.attributes) {
            get_or_add_string(attr.name);
        }
    }
    for (const auto& idx : writer->indexes) {
        get_or_add_string(idx.index_name);
    }

    // Infer domain node counts from registered relations if not explicitly populated
    for (const auto& rel : writer->relations) {
        if (rel.src_domain_id < writer->domains.size()) {
            writer->domains[rel.src_domain_id].node_count = std::max(
                writer->domains[rel.src_domain_id].node_count, rel.node_count
            );
        }
    }

    std::vector<uint8_t> dir_table;

    // String Table Header & Pool Blob
    uint32_t str_bytes = static_cast<uint32_t>(string_table.size());
    dir_table.resize(4);
    std::memcpy(dir_table.data(), &str_bytes, 4);
    dir_table.insert(dir_table.end(), string_table.begin(), string_table.end());

    align128(dir_table);

    // Pre-calculate exact Directory Table size (with 128B boundaries) so payload offsets are exact
    size_t sim_dir_pos = dir_table.size();
    for (size_t d = 0; d < writer->domains.size(); ++d) {
        sim_dir_pos += sizeof(impulse_domain_catalog_entry_t);
        sim_dir_pos += writer->domain_attributes[d].size() * sizeof(impulse_attribute_descriptor_t);
    }
    sim_dir_pos = (sim_dir_pos + 127) & ~static_cast<size_t>(127);
    for (const auto& rel : writer->relations) {
        sim_dir_pos += sizeof(impulse_relation_directory_entry_t);
        sim_dir_pos += rel.attributes.size() * sizeof(impulse_attribute_descriptor_t);
    }
    sim_dir_pos = (sim_dir_pos + 127) & ~static_cast<size_t>(127);
    sim_dir_pos += writer->indexes.size() * sizeof(impulse_index_directory_entry_t);

    size_t aligned_dir_table_len = (sim_dir_pos + 4095) & ~static_cast<size_t>(4095);

    uint64_t rel_blocks_base_offset = footer_catalog
        ? IMPULSE_DEFAULT_DATA_OFFSET
        : (IMPULSE_DEFAULT_DATA_OFFSET + aligned_dir_table_len);

    struct TempAttr {
        uint32_t name_off;
        uint64_t data_off, data_bytes;
        uint64_t offs_off, offs_bytes;
    };

    // 2A. Serialize Node Domain Blocks & Section 2.2 Domain Catalog Entries (Spec v0.10.0)
    std::vector<uint8_t> payload;
    for (size_t d = 0; d < writer->domains.size(); ++d) {
        auto& dom = writer->domains[d];
        const auto& dom_attrs = writer->domain_attributes[d];
        dom.attr_count = static_cast<uint16_t>(dom_attrs.size());
        if (dom.attr_count == 0 && dom.key_attr_index == 0) {
            dom.key_attr_index = 0xFFFF;
        }

        std::vector<TempAttr> temp_dom_attrs;
        temp_dom_attrs.reserve(dom_attrs.size());

        if (!dom_attrs.empty()) {
            align4096(payload);
            for (const auto& attr : dom_attrs) {
                TempAttr ta{};
                ta.name_off = get_or_add_string(attr.name);
                align128(payload);
                ta.data_off = rel_blocks_base_offset + payload.size();
                ta.data_bytes = attr.data.size();
                payload.insert(payload.end(), attr.data.begin(), attr.data.end());

                if (!attr.offsets.empty()) {
                    align128(payload);
                    ta.offs_off = rel_blocks_base_offset + payload.size();
                    ta.offs_bytes = attr.offsets.size();
                    payload.insert(payload.end(), attr.offsets.begin(), attr.offsets.end());
                } else {
                    ta.offs_off = 0;
                    ta.offs_bytes = 0;
                }
                temp_dom_attrs.push_back(ta);
            }
        }

        size_t pos = dir_table.size();
        dir_table.resize(pos + sizeof(dom));
        std::memcpy(dir_table.data() + pos, &dom, sizeof(dom));

        for (size_t a = 0; a < dom_attrs.size(); ++a) {
            const auto& attr = dom_attrs[a];
            const auto& ta = temp_dom_attrs[a];

            impulse_attribute_descriptor_t desc{};
            desc.name_offset = ta.name_off;
            desc.type_code = attr.type_code;
            desc.reserved1 = 0;
            desc.reserved2 = 0;
            desc.dimension = attr.dimension;
            desc.data_offset = ta.data_off;
            desc.data_bytes = ta.data_bytes;
            desc.offsets_offset = ta.offs_off;
            desc.offsets_bytes = ta.offs_bytes;

            dir_table.resize(dir_table.size() + sizeof(desc));
            std::memcpy(dir_table.data() + dir_table.size() - sizeof(desc), &desc, sizeof(desc));
        }
    }

    align128(dir_table);

    // 2B. Serialize Relation Blocks
    for (auto& rel : writer->relations) {
        align4096(payload);

        align128(payload);
        uint64_t csr_row_off_offset = rel_blocks_base_offset + payload.size();
        uint64_t csr_row_off_bytes = rel.row_offsets.size();
        payload.insert(payload.end(), rel.row_offsets.begin(), rel.row_offsets.end());

        align128(payload);
        uint64_t csr_col_idx_offset = rel_blocks_base_offset + payload.size();
        uint64_t csr_col_idx_bytes = rel.col_indices.size();
        payload.insert(payload.end(), rel.col_indices.begin(), rel.col_indices.end());

        std::vector<TempAttr> temp_attrs;
        temp_attrs.reserve(rel.attributes.size());

        for (auto& attr : rel.attributes) {
            TempAttr ta{};
            ta.name_off = get_or_add_string(attr.name);
            align128(payload);
            ta.data_off = rel_blocks_base_offset + payload.size();
            ta.data_bytes = attr.data.size();
            payload.insert(payload.end(), attr.data.begin(), attr.data.end());

            if (!attr.offsets.empty()) {
                align128(payload);
                ta.offs_off = rel_blocks_base_offset + payload.size();
                ta.offs_bytes = attr.offsets.size();
                payload.insert(payload.end(), attr.offsets.begin(), attr.offsets.end());
            } else {
                ta.offs_off = 0;
                ta.offs_bytes = 0;
            }
            temp_attrs.push_back(ta);
        }

        // Relation Directory Entry
        impulse_relation_directory_entry_t entry{};
        entry.relation_id = rel.relation_id;
        entry.src_domain_id = rel.src_domain_id;
        entry.tgt_domain_id = rel.tgt_domain_id;
        entry.encoding_id = rel.encoding_id;
        entry.node_id_width = rel.node_id_width;
        entry.edge_index_width = rel.edge_index_width;
        entry.name_offset = get_or_add_string("");
        entry.node_count = rel.node_count;
        entry.edge_count = rel.edge_count;
        uint64_t sec_feats = rel.section_features;
        if (!rel.attributes.empty()) sec_feats |= 0x02ULL; // IMP_REL_FEATURE_WEIGHTED
        entry.section_features = sec_feats;
        entry.csr_row_off_offset = csr_row_off_offset;
        entry.csr_row_off_bytes = csr_row_off_bytes;
        entry.csr_col_idx_offset = csr_col_idx_offset;
        entry.csr_col_idx_bytes = csr_col_idx_bytes;
        entry.csc_row_off_offset = 0;
        entry.csc_row_off_bytes = 0;
        entry.csc_col_idx_offset = 0;
        entry.csc_col_idx_bytes = 0;
        entry.attr_count = static_cast<uint16_t>(rel.attributes.size());

        dir_table.resize(dir_table.size() + sizeof(entry));
        std::memcpy(dir_table.data() + dir_table.size() - sizeof(entry), &entry, sizeof(entry));

        for (size_t a = 0; a < rel.attributes.size(); ++a) {
            const auto& attr = rel.attributes[a];
            const auto& ta = temp_attrs[a];

            impulse_attribute_descriptor_t desc{};
            desc.name_offset = ta.name_off;
            desc.type_code = attr.type_code;
            desc.reserved1 = 0;
            desc.reserved2 = 0;
            desc.dimension = attr.dimension;
            desc.data_offset = ta.data_off;
            desc.data_bytes = ta.data_bytes;
            desc.offsets_offset = ta.offs_off;
            desc.offsets_bytes = ta.offs_bytes;

            dir_table.resize(dir_table.size() + sizeof(desc));
            std::memcpy(dir_table.data() + dir_table.size() - sizeof(desc), &desc, sizeof(desc));
        }
    }

    // Serialize Secondary Index Directory Entries and Payload Data
    align128(dir_table);
    for (auto& idx : writer->indexes) {
        uint64_t data_off = 0;
        uint64_t data_bytes = idx.data.size();
        if (data_bytes > 0) {
            align128(payload);
            data_off = rel_blocks_base_offset + payload.size();
            if (idx.index_type == 8 && idx.data.size() >= 64) { // Vamana ANN relative offset fixup
                uint64_t rel_adj = 0;
                std::memcpy(&rel_adj, idx.data.data() + 40, 8);
                uint64_t abs_adj = (rel_adj == 0) ? (data_off + 128) : ((rel_adj < data_off) ? (data_off + rel_adj) : rel_adj);
                std::memcpy(idx.data.data() + 40, &abs_adj, 8);

                uint64_t rel_quant = 0;
                std::memcpy(&rel_quant, idx.data.data() + 56, 8);
                if (rel_quant > 0 && rel_quant < data_off) {
                    uint64_t abs_quant = data_off + rel_quant;
                    std::memcpy(idx.data.data() + 56, &abs_quant, 8);
                }
            }
            payload.insert(payload.end(), idx.data.begin(), idx.data.end());
        }

        impulse_index_directory_entry_t entry{};
        entry.index_id = idx.index_id;
        entry.domain_id = idx.domain_id;
        entry.relation_id = idx.relation_id;
        entry.attribute_index = idx.attribute_index;
        entry.index_type = idx.index_type;
        entry.name_offset = get_or_add_string(idx.index_name);
        entry.data_offset = data_off;
        entry.data_bytes = data_bytes;
        entry.payload_feature_mask = idx.payload_feature_mask;

        dir_table.resize(dir_table.size() + sizeof(entry));
        std::memcpy(dir_table.data() + dir_table.size() - sizeof(entry), &entry, sizeof(entry));
    }

    dir_table.resize(aligned_dir_table_len, 0x00);

    std::vector<uint8_t> footer_payload;
    uint64_t footer_dir_offset = 0;
    uint64_t footer_dir_bytes = 0;

    if (footer_catalog) {
        align4096(payload);
        footer_dir_offset = IMPULSE_DEFAULT_DATA_OFFSET + payload.size();
        footer_dir_bytes = dir_table.size();
        footer_payload.insert(footer_payload.end(), dir_table.begin(), dir_table.end());
    } else {
        std::vector<uint8_t> combined;
        combined.insert(combined.end(), dir_table.begin(), dir_table.end());
        combined.insert(combined.end(), payload.begin(), payload.end());
        payload = std::move(combined);
    }

    // 3. Serialize Footer Block at EOF
    align4096(footer_payload);
    size_t footer_start = footer_payload.size();

    // Metadata Stream
    uint32_t meta_count = static_cast<uint32_t>(writer->metadata.size());
    size_t mpos = footer_payload.size();
    footer_payload.resize(mpos + 4);
    std::memcpy(footer_payload.data() + mpos, &meta_count, 4);

    for (const auto& kv : writer->metadata) {
        uint16_t klen = static_cast<uint16_t>(kv.first.size());
        uint32_t vlen = static_cast<uint32_t>(kv.second.size());
        size_t pos = footer_payload.size();
        footer_payload.resize(pos + 2 + klen + 4 + vlen);
        uint8_t* ptr = footer_payload.data() + pos;
        std::memcpy(ptr, &klen, 2); ptr += 2;
        std::memcpy(ptr, kv.first.data(), klen); ptr += klen;
        std::memcpy(ptr, &vlen, 4); ptr += 4;
        std::memcpy(ptr, kv.second.data(), vlen);
    }

    // 16-Byte Footer Trailer
    uint64_t footer_len = static_cast<uint64_t>(footer_payload.size() + 16 - footer_start);
    impulse_footer_trailer_t trailer;
    trailer.footer_length = footer_len;
    trailer.spec_version = IMPULSE_SPEC_VERSION_PACKED;
    trailer.footer_magic = IMPULSE_MAGIC;

    mpos = footer_payload.size();
    footer_payload.resize(mpos + sizeof(trailer));
    std::memcpy(footer_payload.data() + mpos, &trailer, sizeof(trailer));

    // 4. Build Header Page 0 (4096 Bytes)
    impulse_snapshot_header_t header{};
    header.magic = IMPULSE_MAGIC;
    header.version = IMPULSE_SPEC_VERSION_PACKED;
    header.data_offset = IMPULSE_DEFAULT_DATA_OFFSET;
    header.domain_count = static_cast<uint16_t>(writer->domains.size());
    header.relation_count = static_cast<uint16_t>(writer->relations.size());
    header.index_count = static_cast<uint16_t>(writer->indexes.size());
    header.timestamp_ms = 1700000000000ULL;
    header.required_features = writer->global_features;
    header.footer_directory_offset = footer_dir_offset;
    header.footer_directory_bytes = footer_dir_bytes;

    const uint8_t* header_raw = reinterpret_cast<const uint8_t*>(&header);
    header.header_checksum = compute_crc16(header_raw, 0x3E);

    if (writer->write_cb) {
        if (writer->write_cb(reinterpret_cast<const void*>(&header), sizeof(header), writer->user_data) != 0) {
            g_last_error = "Write callback returned error for header";
            return IMPULSE_ERR_IO_FAILURE;
        }
        if (!payload.empty()) {
            if (writer->write_cb(payload.data(), payload.size(), writer->user_data) != 0) {
                g_last_error = "Write callback returned error for relation payload";
                return IMPULSE_ERR_IO_FAILURE;
            }
        }
        if (!footer_payload.empty()) {
            if (writer->write_cb(footer_payload.data(), footer_payload.size(), writer->user_data) != 0) {
                g_last_error = "Write callback returned error for footer payload";
                return IMPULSE_ERR_IO_FAILURE;
            }
        }
        return IMPULSE_OK;
    } else {
        std::ofstream ofs(writer->output_path, std::ios::binary);
        if (!ofs) {
            g_last_error = "Failed to create output snapshot file";
            return IMPULSE_ERR_IO_FAILURE;
        }

        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) {
            ofs.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        if (!footer_payload.empty()) {
            ofs.write(reinterpret_cast<const char*>(footer_payload.data()), footer_payload.size());
        }
        return IMPULSE_OK;
    }
}

void impulse_writer_destroy(impulse_writer_t* writer) {
    delete writer;
}

impulse_delta_layer_t* impulse_delta_layer_create(uint16_t src_domain_id, uint16_t tgt_domain_id, const char* relation_name) {
    auto dl = std::make_unique<impulse_delta_layer_t>();
    dl->src_domain_id = src_domain_id;
    dl->tgt_domain_id = tgt_domain_id;
    dl->relation_name = relation_name ? relation_name : "";
    return dl.release();
}

impulse_status_t impulse_delta_layer_add_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->mutex);
    delta->tombstones.erase({src_node, tgt_node});
    delta->additions[src_node].push_back(tgt_node);
    return IMPULSE_OK;
}

impulse_status_t impulse_delta_layer_tombstone_edge(impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::unique_lock<std::shared_mutex> lock(delta->mutex);
    delta->tombstones.insert({src_node, tgt_node});
    return IMPULSE_OK;
}

bool impulse_delta_layer_is_tombstoned(const impulse_delta_layer_t* delta, uint64_t src_node, uint64_t tgt_node) {
    if (!delta) return false;
    std::shared_lock<std::shared_mutex> lock(delta->mutex);
    return delta->tombstones.find({src_node, tgt_node}) != delta->tombstones.end();
}

void impulse_delta_layer_destroy(impulse_delta_layer_t* delta) {
    delete delta;
}

static void populate_writer_from_snapshot(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    impulse_writer_t* writer,
    std::vector<std::vector<uint8_t>>& owned_buffers
) {
    const uint8_t* raw = static_cast<const uint8_t*>(base_snapshot->mmap_ptr);

    for (size_t i = 0; i < base_snapshot->domains.size(); ++i) {
        const auto& dom = base_snapshot->domains[i];
        const auto& name = base_snapshot->domain_names[i];
        impulse_writer_add_domain(writer, dom.domain_id, dom.key_type, name.c_str());
        impulse_writer_set_domain_node_count(writer, static_cast<uint16_t>(i), dom.node_count);
        impulse_writer_set_domain_key_attr(writer, static_cast<uint16_t>(i), dom.key_attr_index);
        if (i < base_snapshot->domain_attributes.size()) {
            const auto& dattrs = base_snapshot->domain_attributes[i];
            const auto& dnames = base_snapshot->domain_attribute_names[i];
            for (size_t a = 0; a < dattrs.size(); ++a) {
                const auto& attr = dattrs[a];
                impulse_writer_add_domain_attribute(
                    writer,
                    static_cast<uint16_t>(i),
                    dnames[a].c_str(),
                    attr.type_code,
                    attr.dimension,
                    (attr.data_offset > 0) ? (raw + attr.data_offset) : nullptr,
                    attr.data_bytes,
                    (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr,
                    attr.offsets_bytes
                );
            }
        }
    }

    for (size_t r = 0; r < base_snapshot->relations.size(); ++r) {
        const auto& rel = base_snapshot->relations[r];
        const std::string rel_name = (r < base_snapshot->relation_names.size()) ? base_snapshot->relation_names[r] : "";

        // Collect matching deltas for this relation
        std::vector<impulse_delta_layer_t*> matching_deltas;
        if (deltas && delta_count > 0) {
            for (size_t d = 0; d < delta_count; ++d) {
                impulse_delta_layer_t* dl = deltas[d];
                if (!dl) continue;
                if (dl->src_domain_id == rel.src_domain_id &&
                    dl->tgt_domain_id == rel.tgt_domain_id &&
                    (dl->relation_name.empty() || rel_name.empty() || dl->relation_name == rel_name)) {
                    std::shared_lock<std::shared_mutex> lk(dl->mutex);
                    if (!dl->tombstones.empty() || !dl->additions.empty()) {
                        matching_deltas.push_back(dl);
                    }
                }
            }
        }

        uint64_t sec_feats = rel.section_features & ~0x01ULL; // writer currently serializes CSR only

        if (matching_deltas.empty() || rel.csr_row_off_offset == 0 || rel.csr_col_idx_offset == 0) {
            const void* row_ptr = raw + rel.csr_row_off_offset;
            const void* col_ptr = raw + rel.csr_col_idx_offset;
            impulse_writer_add_relation(
                writer, rel.src_domain_id, rel.tgt_domain_id, rel.encoding_id,
                rel.node_count, rel.edge_count, sec_feats,
                row_ptr, rel.csr_row_off_bytes,
                col_ptr, rel.csr_col_idx_bytes
            );
        } else {
            // Merge base CSR with matching delta tombstones and additions (32-bit CSR)
            const uint32_t* base_rows = reinterpret_cast<const uint32_t*>(raw + rel.csr_row_off_offset);
            const uint32_t* base_cols = reinterpret_cast<const uint32_t*>(raw + rel.csr_col_idx_offset);
            uint64_t new_node_count = rel.node_count;
            for (auto* dl : matching_deltas) {
                std::shared_lock<std::shared_mutex> lk(dl->mutex);
                for (const auto& kv : dl->additions) {
                    if (kv.first + 1 > new_node_count) new_node_count = kv.first + 1;
                }
            }
            std::vector<uint32_t> merged_rows(static_cast<size_t>(new_node_count) + 1, 0);
            std::vector<uint32_t> merged_cols;
            merged_cols.reserve(static_cast<size_t>(rel.edge_count));

            auto is_tomb = [&](uint64_t u, uint64_t v) {
                for (auto* dl : matching_deltas) {
                    std::shared_lock<std::shared_mutex> lk(dl->mutex);
                    if (dl->tombstones.find({u, v}) != dl->tombstones.end()) return true;
                }
                return false;
            };

            for (uint64_t u = 0; u < new_node_count; ++u) {
                merged_rows[static_cast<size_t>(u)] = static_cast<uint32_t>(merged_cols.size());
                if (u < rel.node_count) {
                    uint32_t start = base_rows[u];
                    uint32_t end = base_rows[u + 1];
                    for (uint32_t e = start; e < end; ++e) {
                        uint64_t v = base_cols[e];
                        if (!is_tomb(u, v)) {
                            merged_cols.push_back(static_cast<uint32_t>(v));
                        }
                    }
                }
                for (auto* dl : matching_deltas) {
                    std::shared_lock<std::shared_mutex> lk(dl->mutex);
                    auto it = dl->additions.find(u);
                    if (it != dl->additions.end()) {
                        for (uint64_t v : it->second) {
                            if (!is_tomb(u, v)) {
                                merged_cols.push_back(static_cast<uint32_t>(v));
                            }
                        }
                    }
                }
            }
            merged_rows[static_cast<size_t>(new_node_count)] = static_cast<uint32_t>(merged_cols.size());

            owned_buffers.emplace_back(merged_rows.size() * sizeof(uint32_t));
            std::memcpy(owned_buffers.back().data(), merged_rows.data(), owned_buffers.back().size());
            const void* row_ptr = owned_buffers.back().data();
            const uint64_t row_bytes = owned_buffers.back().size();

            owned_buffers.emplace_back(merged_cols.size() * sizeof(uint32_t));
            if (!merged_cols.empty()) {
                std::memcpy(owned_buffers.back().data(), merged_cols.data(), owned_buffers.back().size());
            }
            const void* col_ptr = owned_buffers.back().empty() ? nullptr : owned_buffers.back().data();
            const uint64_t col_bytes = owned_buffers.back().size();

            impulse_writer_add_relation(
                writer, rel.src_domain_id, rel.tgt_domain_id, rel.encoding_id,
                new_node_count, static_cast<uint64_t>(merged_cols.size()), sec_feats,
                row_ptr, row_bytes,
                col_ptr, col_bytes
            );
        }

        if (r < base_snapshot->relation_attributes.size()) {
            const auto& rattrs = base_snapshot->relation_attributes[r];
            const auto& rnames = base_snapshot->relation_attribute_names[r];
            for (size_t a = 0; a < rattrs.size(); ++a) {
                const auto& attr = rattrs[a];
                impulse_writer_add_attribute(
                    writer,
                    static_cast<uint16_t>(r),
                    rnames[a].c_str(),
                    attr.type_code,
                    attr.dimension,
                    (attr.data_offset > 0) ? (raw + attr.data_offset) : nullptr,
                    attr.data_bytes,
                    (attr.offsets_offset > 0) ? (raw + attr.offsets_offset) : nullptr,
                    attr.offsets_bytes
                );
            }
        }
    }

    for (size_t idx = 0; idx < base_snapshot->indexes.size(); ++idx) {
        const auto& ie = base_snapshot->indexes[idx];
        const char* iname = (idx < base_snapshot->index_names.size()) ? base_snapshot->index_names[idx].c_str() : "";
        const void* idata = (ie.data_offset > 0 && ie.data_bytes > 0) ? (raw + ie.data_offset) : nullptr;
        impulse_writer_add_index(
            writer,
            ie.domain_id,
            ie.relation_id,
            ie.attribute_index,
            ie.index_type,
            iname,
            idata,
            ie.data_bytes,
            ie.payload_feature_mask
        );
    }

    for (const auto& kv : base_snapshot->metadata) {
        impulse_writer_set_metadata(writer, kv.first.c_str(), kv.second.c_str());
    }
}

impulse_status_t impulse_snapshot_compact_to_file(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    const char* output_file_path
) {
    if (!base_snapshot || !output_file_path) return IMPULSE_ERR_INVALID_ARGUMENT;

    uint64_t gfeat = 0;
    if (base_snapshot->header.version == 9 || base_snapshot->header.version == 10) {
        gfeat = base_snapshot->header.required_features & ~IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED;
    }
    impulse_writer_t* writer = impulse_writer_create(output_file_path, gfeat);
    if (!writer) return IMPULSE_ERR_IO_FAILURE;

    std::vector<std::vector<uint8_t>> owned_buffers;
    populate_writer_from_snapshot(base_snapshot, deltas, delta_count, writer, owned_buffers);

    impulse_status_t st = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);
    return st;
}

impulse_status_t impulse_snapshot_compact_to_stream(
    const impulse_snapshot_t* base_snapshot,
    impulse_delta_layer_t** deltas,
    size_t delta_count,
    impulse_write_fn write_cb,
    void* user_data
) {
    if (!base_snapshot || !write_cb) return IMPULSE_ERR_INVALID_ARGUMENT;

    uint64_t gfeat = IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
    if (base_snapshot->header.version == 9 || base_snapshot->header.version == 10) {
        gfeat |= (base_snapshot->header.required_features & ~IMPULSE_GLOBAL_FEAT_CRYPTO_SIGNED);
    }
    impulse_writer_t* writer = impulse_writer_create_stream(write_cb, user_data, gfeat);
    if (!writer) return IMPULSE_ERR_IO_FAILURE;

    std::vector<std::vector<uint8_t>> owned_buffers;
    populate_writer_from_snapshot(base_snapshot, deltas, delta_count, writer, owned_buffers);

    impulse_status_t st = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);
    return st;
}


} // extern "C"
