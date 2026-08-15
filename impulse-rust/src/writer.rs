//! Spec v0.9.0 Binary Snapshot Generator / Writer

use crate::spec::*;
use std::fs::File;
use std::io::Write;

pub struct AttributeField {
    pub name: String,
    pub type_code: u8,
    pub dimension: u32,
    pub data: Vec<u8>,
    pub offsets: Option<Vec<u32>>,
    pub data_offset: u64,
    pub data_bytes: u64,
    pub offsets_offset: u64,
    pub offsets_bytes: u64,
}

pub struct WriterIndex {
    pub domain_id: u16,
    pub relation_id: u16,
    pub attribute_index: u16,
    pub index_type: u8,
    pub name: String,
    pub data: Vec<u8>,
    pub data_offset: u64,
    pub data_bytes: u64,
}

pub struct WriterDomain {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub name: String,
    pub node_count: u64,
}

pub struct WriterRelation {
    pub relation_id: u16,
    pub src_domain_id: u16,
    pub tgt_domain_id: u16,
    pub encoding_id: u8,
    pub node_id_width: u8,
    pub edge_index_width: u8,
    pub name: String,
    pub node_count: u64,
    pub edge_count: u64,
    pub row_offsets: Vec<u32>,
    pub col_indices: Vec<u32>,
    pub include_csc: bool,
    pub attributes: Vec<AttributeField>,
    pub csr_row_off_offset: u64,
    pub csr_row_off_bytes: u64,
    pub csr_col_idx_offset: u64,
    pub csr_col_idx_bytes: u64,
    pub csc_row_off_offset: u64,
    pub csc_row_off_bytes: u64,
    pub csc_col_idx_offset: u64,
    pub csc_col_idx_bytes: u64,
}

pub struct SnapshotWriter {
    output_path: String,
    global_required_features: u64,
    domains: Vec<WriterDomain>,
    relations: Vec<WriterRelation>,
    indexes: Vec<WriterIndex>,
    metadata: std::collections::HashMap<String, String>,
}

impl SnapshotWriter {
    pub fn new(output_path: &str) -> Self {
        Self {
            output_path: output_path.to_string(),
            global_required_features: IMPULSE_FEAT_4KB_PAGE_ALIGNED,
            domains: Vec::new(),
            relations: Vec::new(),
            indexes: Vec::new(),
            metadata: std::collections::HashMap::new(),
        }
    }

    pub fn set_metadata(&mut self, metadata: std::collections::HashMap<String, String>) {
        self.metadata = metadata;
    }

    pub fn add_domain(&mut self, domain_id: u16, key_type: KeyType, name: &str) {
        if let Some(existing) = self.domains.iter_mut().find(|d| d.domain_id == domain_id) {
            existing.key_type = key_type;
            existing.name = name.to_string();
        } else {
            self.domains.push(WriterDomain {
                domain_id,
                key_type,
                name: name.to_string(),
                node_count: 0,
            });
        }
    }

    pub fn add_relation(
        &mut self,
        src_domain_id: u16,
        tgt_domain_id: u16,
        node_count: u64,
        edge_count: u64,
        row_offsets: Vec<u32>,
        col_indices: Vec<u32>,
    ) {
        let relation_id = self.relations.len() as u16;
        self.relations.push(WriterRelation {
            relation_id,
            src_domain_id,
            tgt_domain_id,
            encoding_id: EncodingType::Raw.to_u8(),
            node_id_width: 4,
            edge_index_width: 4,
            name: String::new(),
            node_count,
            edge_count,
            row_offsets,
            col_indices,
            include_csc: false,
            attributes: Vec::new(),
            csr_row_off_offset: 0,
            csr_row_off_bytes: 0,
            csr_col_idx_offset: 0,
            csr_col_idx_bytes: 0,
            csc_row_off_offset: 0,
            csc_row_off_bytes: 0,
            csc_col_idx_offset: 0,
            csc_col_idx_bytes: 0,
        });
    }

    pub fn add_attribute_to_relation(
        &mut self,
        relation_index: usize,
        name: &str,
        type_code: u8,
        dimension: u32,
        data: Vec<u8>,
        offsets: Option<Vec<u32>>,
    ) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].attributes.push(AttributeField {
                name: name.to_string(),
                type_code,
                dimension,
                data,
                offsets,
                data_offset: 0,
                data_bytes: 0,
                offsets_offset: 0,
                offsets_bytes: 0,
            });
        }
    }

    fn align_buffer(buf: &mut Vec<u8>, align: usize) {
        let rem = buf.len() % align;
        if rem != 0 {
            buf.resize(buf.len() + (align - rem), 0x00);
        }
    }

    pub fn add_index(
        &mut self,
        domain_id: u16,
        relation_id: u16,
        attribute_index: u16,
        index_type: u8,
        name: &str,
        data: Vec<u8>,
    ) {
        self.indexes.push(WriterIndex {
            domain_id,
            relation_id,
            attribute_index,
            index_type,
            name: name.to_string(),
            data,
            data_offset: 0,
            data_bytes: 0,
        });
    }

    pub fn finalize_to_writer(&mut self, writer: &mut impl Write) -> Result<(), ImpulseError> {

        // Sort relations primary by src_domain_id, secondary by tgt_domain_id
        self.relations.sort_by(|a, b| {
            a.src_domain_id
                .cmp(&b.src_domain_id)
                .then_with(|| a.tgt_domain_id.cmp(&b.tgt_domain_id))
        });
        for (idx, rel) in self.relations.iter_mut().enumerate() {
            rel.relation_id = idx as u16;
        }

        // 1. Build Shared String Table & Pool Blob
        let mut string_pool = vec![0u8]; // Offset 0 = empty string ""
        let mut string_map = std::collections::HashMap::new();
        string_map.insert(String::new(), 0u32);

        let mut get_or_add_string = |s: &str| -> u32 {
            if s.is_empty() {
                return 0;
            }
            if let Some(&off) = string_map.get(s) {
                return off;
            }
            let off = string_pool.len() as u32;
            string_pool.extend_from_slice(s.as_bytes());
            string_pool.push(0); // Null terminator
            string_map.insert(s.to_string(), off);
            off
        };

        // Collect string offsets
        let domain_name_offsets: Vec<u32> = self.domains.iter().map(|d| get_or_add_string(&d.name)).collect();
        let rel_name_offsets: Vec<u32> = self.relations.iter().map(|r| get_or_add_string(&r.name)).collect();

        let mut attr_name_offsets: Vec<Vec<u32>> = Vec::new();
        for rel in &self.relations {
            let mut a_offs = Vec::new();
            for attr in &rel.attributes {
                a_offs.push(get_or_add_string(&attr.name));
            }
            attr_name_offsets.push(a_offs);
        }

        let index_name_offsets: Vec<u32> = self.indexes.iter().map(|i| get_or_add_string(&i.name)).collect();

        let mut dir_table_bytes = Vec::new();

        // Write String Table Header & Pool Blob
        let str_pool_len = string_pool.len() as u32;
        dir_table_bytes.extend_from_slice(&str_pool_len.to_le_bytes());
        dir_table_bytes.extend_from_slice(&string_pool);

        Self::align_buffer(&mut dir_table_bytes, 128);

        // Domain Catalog Entries Array
        for (dom, &name_off) in self.domains.iter().zip(domain_name_offsets.iter()) {
            let dom_entry = DomainCatalogEntry {
                domain_id: dom.domain_id,
                key_type: dom.key_type as u8,
                reserved: 0,
                name_offset: name_off,
                node_count: dom.node_count,
            };
            let entry_bytes = unsafe {
                std::slice::from_raw_parts(
                    &dom_entry as *const DomainCatalogEntry as *const u8,
                    std::mem::size_of::<DomainCatalogEntry>(),
                )
            };
            dir_table_bytes.extend_from_slice(entry_bytes);
        }

        Self::align_buffer(&mut dir_table_bytes, 128);

        // Calculate Relation Directory Table size
        let mut rel_dir_size = 0;
        for rel in &self.relations {
            rel_dir_size += std::mem::size_of::<RelationDirectoryEntry>();
            rel_dir_size += rel.attributes.len() * std::mem::size_of::<AttributeDescriptor>();
        }
        let mut idx_dir_size = 128; // alignment padding
        idx_dir_size += self.indexes.len() * std::mem::size_of::<IndexDirectoryEntry>();

        let total_dir_table_len = dir_table_bytes.len() + rel_dir_size + idx_dir_size;
        let aligned_dir_table_len = align_4k(total_dir_table_len as u64) as usize;

        // Base offset where Relation Blocks payload begins
        let rel_blocks_base_offset = IMPULSE_DEFAULT_DATA_OFFSET as u64 + aligned_dir_table_len as u64;

        // 2. Serialize Relation Blocks
        let mut payload = Vec::new();
        for rel in &mut self.relations {
            Self::align_buffer(&mut payload, 4096);

            // Align csrRowOffsets to 128B
            Self::align_buffer(&mut payload, 128);
            rel.csr_row_off_offset = rel_blocks_base_offset + payload.len() as u64;
            let row_bytes: Vec<u8> = rel.row_offsets.iter().flat_map(|v| v.to_le_bytes()).collect();
            rel.csr_row_off_bytes = row_bytes.len() as u64;
            payload.extend_from_slice(&row_bytes);

            // Align csrColumnIndices to 128B
            Self::align_buffer(&mut payload, 128);
            rel.csr_col_idx_offset = rel_blocks_base_offset + payload.len() as u64;
            let col_bytes: Vec<u8> = rel.col_indices.iter().flat_map(|v| v.to_le_bytes()).collect();
            rel.csr_col_idx_bytes = col_bytes.len() as u64;
            payload.extend_from_slice(&col_bytes);

            // CSC Arrays (if enabled)
            if rel.include_csc {
                Self::align_buffer(&mut payload, 128);
                rel.csc_row_off_offset = rel_blocks_base_offset + payload.len() as u64;
                rel.csc_row_off_bytes = row_bytes.len() as u64;
                payload.extend_from_slice(&row_bytes);

                Self::align_buffer(&mut payload, 128);
                rel.csc_col_idx_offset = rel_blocks_base_offset + payload.len() as u64;
                rel.csc_col_idx_bytes = col_bytes.len() as u64;
                payload.extend_from_slice(&col_bytes);
            } else {
                rel.csc_row_off_offset = 0;
                rel.csc_row_off_bytes = 0;
                rel.csc_col_idx_offset = 0;
                rel.csc_col_idx_bytes = 0;
            }

            // Serialize Edge Attributes
            for attr in &mut rel.attributes {
                Self::align_buffer(&mut payload, 128);
                attr.data_offset = rel_blocks_base_offset + payload.len() as u64;
                attr.data_bytes = attr.data.len() as u64;
                payload.extend_from_slice(&attr.data);

                if let Some(ref offs) = attr.offsets {
                    Self::align_buffer(&mut payload, 128);
                    attr.offsets_offset = rel_blocks_base_offset + payload.len() as u64;
                    let off_bytes: Vec<u8> = offs.iter().flat_map(|v| v.to_le_bytes()).collect();
                    attr.offsets_bytes = off_bytes.len() as u64;
                    payload.extend_from_slice(&off_bytes);
                } else {
                    attr.offsets_offset = 0;
                    attr.offsets_bytes = 0;
                }
            }

        }
        // Serialize Index Blocks
        for idx in &mut self.indexes {
            Self::align_buffer(&mut payload, 128);
            idx.data_offset = rel_blocks_base_offset + payload.len() as u64;
            idx.data_bytes = idx.data.len() as u64;
            payload.extend_from_slice(&idx.data);
        }

        // Now serialize Relation Directory Table entries into dir_table_bytes
        for (rel_idx, rel) in self.relations.iter().enumerate() {
            let rel_name_off = rel_name_offsets[rel_idx];
            let rel_entry = RelationDirectoryEntry {
                relation_id: rel.relation_id,
                src_domain_id: rel.src_domain_id,
                tgt_domain_id: rel.tgt_domain_id,
                encoding_id: rel.encoding_id,
                node_id_width: rel.node_id_width,
                edge_index_width: rel.edge_index_width,
                reserved1: [0u8; 3],
                name_offset: rel_name_off,
                node_count: rel.node_count,
                edge_count: rel.edge_count,
                section_features: 0,
                csr_row_off_offset: rel.csr_row_off_offset,
                csr_row_off_bytes: rel.csr_row_off_bytes,
                csr_col_idx_offset: rel.csr_col_idx_offset,
                csr_col_idx_bytes: rel.csr_col_idx_bytes,
                csc_row_off_offset: rel.csc_row_off_offset,
                csc_row_off_bytes: rel.csc_row_off_bytes,
                csc_col_idx_offset: rel.csc_col_idx_offset,
                csc_col_idx_bytes: rel.csc_col_idx_bytes,
                attr_count: rel.attributes.len() as u16,
                reserved2: [0u8; 22],
            };
            let rel_bytes = unsafe {
                std::slice::from_raw_parts(
                    &rel_entry as *const RelationDirectoryEntry as *const u8,
                    std::mem::size_of::<RelationDirectoryEntry>(),
                )
            };
            dir_table_bytes.extend_from_slice(rel_bytes);

            for (attr_idx, attr) in rel.attributes.iter().enumerate() {
                let attr_name_off = attr_name_offsets[rel_idx][attr_idx];
                let attr_desc = AttributeDescriptor {
                    name_offset: attr_name_off,
                    type_code: attr.type_code,
                    reserved1: 0,
                    reserved2: 0,
                    dimension: attr.dimension,
                    data_offset: attr.data_offset,
                    data_bytes: attr.data_bytes,
                    offsets_offset: attr.offsets_offset,
                    offsets_bytes: attr.offsets_bytes,
                };
                let desc_bytes = unsafe {
                    std::slice::from_raw_parts(
                        &attr_desc as *const AttributeDescriptor as *const u8,
                        std::mem::size_of::<AttributeDescriptor>(),
                    )
                };
                dir_table_bytes.extend_from_slice(desc_bytes);
            }
        }

        Self::align_buffer(&mut dir_table_bytes, 128);

        // Serialize Index Directory Table entries
        for (idx_idx, idx) in self.indexes.iter().enumerate() {
            let idx_name_off = index_name_offsets[idx_idx];
            let idx_entry = IndexDirectoryEntry {
                index_id: idx_idx as u32,
                domain_id: idx.domain_id,
                relation_id: idx.relation_id,
                attribute_index: idx.attribute_index,
                index_type: idx.index_type,
                reserved1: 0,
                name_offset: idx_name_off,
                data_offset: idx.data_offset,
                data_bytes: idx.data_bytes,
                payload_feature_mask: 0,
                reserved_padding: [0; 24],
            };
            let entry_bytes = unsafe {
                std::slice::from_raw_parts(
                    &idx_entry as *const IndexDirectoryEntry as *const u8,
                    std::mem::size_of::<IndexDirectoryEntry>(),
                )
            };
            dir_table_bytes.extend_from_slice(entry_bytes);
        }

        Self::align_buffer(&mut dir_table_bytes, aligned_dir_table_len);

        // Combine Directory Table and Relation Payload
        let mut final_payload = Vec::new();
        final_payload.extend_from_slice(&dir_table_bytes);
        final_payload.extend_from_slice(&payload);

        // 3. Serialize Footer Block at EOF
        Self::align_buffer(&mut final_payload, 4096);
        let footer_start = final_payload.len();

        // Metadata Stream
        let meta_count = self.metadata.len() as u32;
        final_payload.extend_from_slice(&meta_count.to_le_bytes());

        for (k, v) in &self.metadata {
            let klen = k.len() as u16;
            final_payload.extend_from_slice(&klen.to_le_bytes());
            final_payload.extend_from_slice(k.as_bytes());

            let vlen = v.len() as u32;
            final_payload.extend_from_slice(&vlen.to_le_bytes());
            final_payload.extend_from_slice(v.as_bytes());
        }

        // 16-Byte Footer Trailer
        let footer_len = (final_payload.len() + 16 - footer_start) as u64;
        let trailer = FooterTrailer {
            footer_length: footer_len,
            spec_version: IMPULSE_VERSION_PACKED as u32,
            footer_magic: IMPULSE_MAGIC,
        };
        let trailer_bytes = unsafe {
            std::slice::from_raw_parts(
                &trailer as *const FooterTrailer as *const u8,
                std::mem::size_of::<FooterTrailer>(),
            )
        };
        final_payload.extend_from_slice(trailer_bytes);

        // 4. Build Header Page 0 (4096 Bytes)
        let mut header = SnapshotHeader {
            magic: IMPULSE_MAGIC,
            version: IMPULSE_VERSION_PACKED,
            data_offset: IMPULSE_DEFAULT_DATA_OFFSET,
            domain_count: self.domains.len() as u16,
            relation_count: self.relations.len() as u16,
            timestamp_ms: 1700000000000,
            required_features: self.global_required_features,
            footer_directory_offset: 0,
            footer_directory_bytes: 0,
            snapshot_uuid: [0u8; 16],
            header_checksum: 0,
            index_count: self.indexes.len() as u16,
            header_padding: [0u8; 4030],
        };

        let header_raw = unsafe {
            std::slice::from_raw_parts(
                &header as *const SnapshotHeader as *const u8,
                0x3E,
            )
        };
        header.header_checksum = compute_crc16(header_raw);

        let header_full_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const SnapshotHeader as *const u8,
                std::mem::size_of::<SnapshotHeader>(),
            )
        };

        writer.write_all(header_full_bytes).map_err(|_| ImpulseError::IoFailure)?;
        writer.write_all(&final_payload).map_err(|_| ImpulseError::IoFailure)?;
        writer.flush().map_err(|_| ImpulseError::IoFailure)?;

        Ok(())
    }

    pub fn finalize(&mut self) -> Result<(), ImpulseError> {
        let mut file = File::create(&self.output_path).map_err(|_| ImpulseError::IoFailure)?;
        self.finalize_to_writer(&mut file)
    }
}

