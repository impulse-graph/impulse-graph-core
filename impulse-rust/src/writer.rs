//! Spec v2.4 Binary Snapshot Generator / Writer

use crate::spec::*;
use std::fs::File;
use std::io::Write;

pub struct PropertyField {
    pub name: String,
    pub data_type: DataType,
    pub data: Vec<u8>,
}

pub struct PropertyBlock {
    pub is_soa: bool,
    pub fields: Vec<PropertyField>,
}

pub struct WriterDomain {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub node_count: u64,
    pub name: String,
    pub fixed_props: Option<PropertyBlock>,
}

pub struct WriterRelation {
    pub src_domain_id: u16,
    pub tgt_domain_id: u16,
    pub encoding_type: EncodingType,
    pub node_count: u64,
    pub edge_count: u64,
    pub row_offsets: Vec<u32>,
    pub col_indices: Vec<u32>,
    pub fixed_props: Option<PropertyBlock>,
}

pub struct SnapshotWriter {
    output_path: String,
    global_required_features: u64,
    global_compat_features: u64,
    domains: Vec<WriterDomain>,
    relations: Vec<WriterRelation>,
}

impl SnapshotWriter {
    pub fn new(output_path: &str) -> Self {
        Self {
            output_path: output_path.to_string(),
            global_required_features: 0,
            global_compat_features: IMPULSE_COMPAT_PAGE_ALIGNED,
            domains: Vec::new(),
            relations: Vec::new(),
        }
    }

    pub fn add_domain(&mut self, domain_id: u16, key_type: KeyType, name: &str, node_count: u64) {
        self.domains.push(WriterDomain {
            domain_id,
            key_type,
            node_count,
            name: name.to_string(),
            fixed_props: None,
        });
    }

    pub fn add_domain_fixed_props(
        &mut self,
        domain_id: u16,
        is_soa: bool,
        fields: Vec<PropertyField>,
    ) {
        if let Some(dom) = self.domains.iter_mut().find(|d| d.domain_id == domain_id) {
            dom.fixed_props = Some(PropertyBlock { is_soa, fields });
        }
    }

    pub fn add_relation(
        &mut self,
        src_domain_id: u16,
        tgt_domain_id: u16,
        encoding_type: EncodingType,
        node_count: u64,
        edge_count: u64,
        row_offsets: Vec<u32>,
        col_indices: Vec<u32>,
    ) {
        self.relations.push(WriterRelation {
            src_domain_id,
            tgt_domain_id,
            encoding_type,
            node_count,
            edge_count,
            row_offsets,
            col_indices,
            fixed_props: None,
        });
    }

    pub fn add_relation_fixed_props(
        &mut self,
        relation_index: usize,
        is_soa: bool,
        fields: Vec<PropertyField>,
    ) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].fixed_props = Some(PropertyBlock { is_soa, fields });
        }
    }

    fn align_buffer(buf: &mut Vec<u8>, align: usize) {
        let rem = buf.len() % align;
        if rem != 0 {
            buf.resize(buf.len() + (align - rem), 0x00);
        }
    }

    pub fn finalize(&mut self) -> Result<(), ImpulseError> {
        let mut payload = Vec::new();
        let base_offset = IMPULSE_DEFAULT_DATA_OFFSET as u64;

        // 1. Reserve Domain Catalog Table
        let domain_table_start = payload.len();
        let domain_table_size = self.domains.len() * std::mem::size_of::<DomainCatalogEntry>();
        payload.resize(domain_table_size, 0x00);

        Self::align_buffer(&mut payload, 64);

        // 2. Reserve Relation Directory Table
        let rel_table_start = payload.len();
        let rel_table_size = self.relations.len() * std::mem::size_of::<RelationDirectoryEntry>();
        payload.resize(payload.len() + rel_table_size, 0x00);

        let mut domain_entries = Vec::with_capacity(self.domains.len());

        // 3. Write Domain Names & Aux Section Tables
        for dom in &self.domains {
            let mut entry = DomainCatalogEntry {
                domain_id: dom.domain_id,
                key_type: dom.key_type as u8,
                reserved1: 0,
                node_count: dom.node_count,
                required_features: 0,
                compat_features: 0,
                aux_sections_pos: 0,
                aux_sections_size: 0,
                name_offset: 0,
                name_length: dom.name.len() as u16,
                reserved2: [0u8; 14],
            };

            // Write name string
            if !dom.name.is_empty() {
                entry.name_offset = (base_offset + payload.len() as u64) as u32;
                payload.extend_from_slice(dom.name.as_bytes());
            }

            // Write Node Property Block + Aux Table
            if let Some(ref props) = dom.fixed_props {
                Self::align_buffer(&mut payload, 64);
                let prop_sec_offset = base_offset + payload.len() as u64;
                let prop_bytes = Self::encode_prop_block(props, dom.node_count);
                payload.extend_from_slice(&prop_bytes);

                Self::align_buffer(&mut payload, 64);
                entry.aux_sections_pos = base_offset + payload.len() as u64;

                let aux_entry = AuxSectionEntry {
                    section_type: AuxSectionType::NodePropsFixed as u16,
                    flags: 0,
                    reserved: 0,
                    offset: prop_sec_offset,
                    size: prop_bytes.len() as u64,
                };
                entry.aux_sections_size = std::mem::size_of::<AuxSectionEntry>() as u64;

                let aux_bytes = unsafe {
                    std::slice::from_raw_parts(
                        &aux_entry as *const AuxSectionEntry as *const u8,
                        std::mem::size_of::<AuxSectionEntry>(),
                    )
                };
                payload.extend_from_slice(aux_bytes);
            }

            domain_entries.push(entry);
        }

        // Copy Domain Entries into Domain Table
        let dom_raw = unsafe {
            std::slice::from_raw_parts(
                domain_entries.as_ptr() as *const u8,
                domain_table_size,
            )
        };
        payload[domain_table_start..domain_table_start + domain_table_size].copy_from_slice(dom_raw);

        // 4. Write Relation Offsets, Targets, Edge Props & Aux Tables
        let mut rel_entries = Vec::with_capacity(self.relations.len());

        for rel in &self.relations {
            let mut entry = RelationDirectoryEntry {
                src_domain_id: rel.src_domain_id,
                tgt_domain_id: rel.tgt_domain_id,
                encoding_type: rel.encoding_type as u8,
                node_count: rel.node_count,
                edge_count: rel.edge_count,
                required_features: 0,
                compat_features: 0,
                csr_offsets_pos: 0,
                csr_offsets_size: 0,
                csr_targets_pos: 0,
                csr_targets_size: 0,
                aux_sections_pos: 0,
                aux_sections_size: 0,
                name_offset: 0,
                name_length: 0,
                tgt_node_count_lo16: 0,
                reserved: [0u8; 35],
            };

            // Write Row Offsets
            Self::align_buffer(&mut payload, 64);
            entry.csr_offsets_pos = base_offset + payload.len() as u64;
            let offsets_raw: &[u8] = unsafe {
                std::slice::from_raw_parts(
                    rel.row_offsets.as_ptr() as *const u8,
                    rel.row_offsets.len() * 4,
                )
            };
            entry.csr_offsets_size = offsets_raw.len() as u64;
            payload.extend_from_slice(offsets_raw);

            // Write Col Indices
            Self::align_buffer(&mut payload, 64);
            entry.csr_targets_pos = base_offset + payload.len() as u64;
            let targets_raw: &[u8] = unsafe {
                std::slice::from_raw_parts(
                    rel.col_indices.as_ptr() as *const u8,
                    rel.col_indices.len() * 4,
                )
            };
            entry.csr_targets_size = targets_raw.len() as u64;
            payload.extend_from_slice(targets_raw);

            // Write Edge Properties if present
            if let Some(ref props) = rel.fixed_props {
                Self::align_buffer(&mut payload, 64);
                let prop_sec_offset = base_offset + payload.len() as u64;
                let prop_bytes = Self::encode_prop_block(props, rel.edge_count);
                payload.extend_from_slice(&prop_bytes);

                Self::align_buffer(&mut payload, 64);
                entry.aux_sections_pos = base_offset + payload.len() as u64;

                let aux_entry = AuxSectionEntry {
                    section_type: AuxSectionType::EdgePropsFixed as u16,
                    flags: 0,
                    reserved: 0,
                    offset: prop_sec_offset,
                    size: prop_bytes.len() as u64,
                };
                entry.aux_sections_size = std::mem::size_of::<AuxSectionEntry>() as u64;

                let aux_bytes = unsafe {
                    std::slice::from_raw_parts(
                        &aux_entry as *const AuxSectionEntry as *const u8,
                        std::mem::size_of::<AuxSectionEntry>(),
                    )
                };
                payload.extend_from_slice(aux_bytes);
            }

            rel_entries.push(entry);
        }

        // Copy Relation Entries into Relation Table
        let rel_raw = unsafe {
            std::slice::from_raw_parts(
                rel_entries.as_ptr() as *const u8,
                rel_table_size,
            )
        };
        payload[rel_table_start..rel_table_start + rel_table_size].copy_from_slice(rel_raw);

        Self::align_buffer(&mut payload, 4096);

        // Calculate Payload SHA-256
        let payload_sha = compute_sha256(&payload);

        // Build Header
        let mut header = SnapshotHeader {
            magic: IMPULSE_MAGIC,
            version: IMPULSE_VERSION_PACKED,
            data_offset: IMPULSE_DEFAULT_DATA_OFFSET,
            domain_count: self.domains.len() as u16,
            relation_count: self.relations.len() as u16,
            kafka_offset: 0,
            timestamp_ms: 0,
            payload_checksum: payload_sha,
            reserved: 0,
            required_features: self.global_required_features,
            sig_block: [0u8; 1024],
            compat_features: self.global_compat_features,
            total_file_size: (IMPULSE_DEFAULT_DATA_OFFSET as usize + payload.len()) as u64,
            header_crc32: 0,
            section_dir_count: 0,
            string_table_encoding: 0,
            section_dir_offset: 0,
            relation_dir_entry_size: 128,
            domain_dir_entry_size: 64,
            reserved2: 0,
            header_padding: [0u8; 2960],
        };

        // Compute Header CRC-32C
        let header_raw: &[u8] = unsafe {
            std::slice::from_raw_parts(
                &header as *const SnapshotHeader as *const u8,
                std::mem::size_of::<SnapshotHeader>(),
            )
        };
        header.header_crc32 = compute_header_crc32(header_raw);

        // Write header + payload to output file
        let mut file = File::create(&self.output_path).map_err(|_| ImpulseError::IoFailure)?;
        let final_header_raw: &[u8] = unsafe {
            std::slice::from_raw_parts(
                &header as *const SnapshotHeader as *const u8,
                std::mem::size_of::<SnapshotHeader>(),
            )
        };

        file.write_all(final_header_raw).map_err(|_| ImpulseError::IoFailure)?;
        file.write_all(&payload).map_err(|_| ImpulseError::IoFailure)?;
        file.flush().map_err(|_| ImpulseError::IoFailure)?;

        Ok(())
    }

    fn encode_prop_block(props: &PropertyBlock, element_count: u64) -> Vec<u8> {
        let mut buf = Vec::new();
        let field_count = props.fields.len() as u16;

        let mut record_size: u32 = 0;
        let mut descriptors = Vec::with_capacity(props.fields.len());

        let mut curr_col_offset: u64 = 0;

        for (idx, f) in props.fields.iter().enumerate() {
            let fsize = f.data_type.default_size() as u8;
            let mut name_buf = [0u8; 32];
            let nbytes = f.name.as_bytes();
            let len = nbytes.len().min(31);
            name_buf[..len].copy_from_slice(&nbytes[..len]);

            let desc = FieldDescriptor {
                name: name_buf,
                data_type: f.data_type as u8,
                field_size: fsize,
                offset_in_record: record_size as u16,
                column_index: idx as u16,
                flags: 0,
                name_hash: fnv1a_hash(&f.name),
                column_offset: curr_col_offset,
            };

            record_size += fsize as u32;
            curr_col_offset += f.data.len() as u64;

            descriptors.push(desc);
        }

        let hdr = PropBlockHeader {
            field_count,
            record_size,
            layout: if props.is_soa { 1 } else { 0 },
            reserved1: 0,
            element_count,
        };

        let hdr_bytes: &[u8] = unsafe {
            std::slice::from_raw_parts(
                &hdr as *const PropBlockHeader as *const u8,
                std::mem::size_of::<PropBlockHeader>(),
            )
        };
        buf.extend_from_slice(hdr_bytes);

        for desc in &descriptors {
            let desc_bytes: &[u8] = unsafe {
                std::slice::from_raw_parts(
                    desc as *const FieldDescriptor as *const u8,
                    std::mem::size_of::<FieldDescriptor>(),
                )
            };
            buf.extend_from_slice(desc_bytes);
        }

        for f in &props.fields {
            buf.extend_from_slice(&f.data);
        }

        buf
    }
}
