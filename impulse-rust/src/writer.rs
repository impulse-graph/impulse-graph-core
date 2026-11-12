//! Spec v0.10.0 Binary Snapshot Generator / Writer

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
    pub payload_feature_mask: u64,
    pub data_offset: u64,
    pub data_bytes: u64,
}

pub struct WriterDomain {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub name: String,
    pub node_count: u64,
    pub key_attr_index: u16,
    pub attributes: Vec<AttributeField>,
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
    pub section_features: u64,
    pub row_offsets: Vec<u32>,
    pub col_indices: Vec<u32>,
    pub coo_src_indices: Option<Vec<u32>>,
    pub csc_override: Option<(Vec<u32>, Vec<u32>)>,
    pub virtual_component_ids: Vec<u16>,
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
                key_attr_index: 0xFFFF,
                attributes: Vec::new(),
            });
        }
    }

    pub fn set_domain_key_attr(&mut self, domain_id: u16, key_attr_index: u16) {
        if let Some(dom) = self.domains.iter_mut().find(|d| d.domain_id == domain_id) {
            dom.key_attr_index = key_attr_index;
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
            section_features: 0,
            row_offsets,
            col_indices,
            coo_src_indices: None,
            csc_override: None,
            virtual_component_ids: Vec::new(),
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

    pub fn add_virtual_relation(
        &mut self,
        src_domain_id: u16,
        tgt_domain_id: u16,
        node_count: u64,
        name: &str,
        component_rel_ids: Vec<u16>,
    ) -> usize {
        let idx = self.relations.len();
        self.add_relation(src_domain_id, tgt_domain_id, node_count, 0, Vec::new(), Vec::new());
        self.relations[idx].name = name.to_string();
        self.relations[idx].section_features |= IMP_REL_FEATURE_VIRTUAL;
        self.relations[idx].virtual_component_ids = component_rel_ids;
        idx
    }

    pub fn set_domain_node_count(&mut self, domain_id: u16, node_count: u64) {
        if let Some(dom) = self.domains.iter_mut().find(|d| d.domain_id == domain_id) {
            dom.node_count = node_count;
        }
    }

    pub fn set_relation_name(&mut self, relation_index: usize, name: &str) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].name = name.to_string();
        }
    }

    pub fn set_relation_include_csc(&mut self, relation_index: usize, include_csc: bool) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].include_csc = include_csc;
        }
    }

    pub fn set_relation_section_features(&mut self, relation_index: usize, features: u64) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].section_features = features;
        }
    }

    pub fn set_relation_coo_sources(&mut self, relation_index: usize, src_indices: Vec<u32>) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].coo_src_indices = Some(src_indices);
        }
    }

    pub fn set_relation_csc_override(
        &mut self,
        relation_index: usize,
        csc_row_offsets: Vec<u32>,
        csc_col_indices: Vec<u32>,
    ) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].csc_override = Some((csc_row_offsets, csc_col_indices));
            self.relations[relation_index].include_csc = true;
        }
    }

    pub fn set_relation_virtual_components(
        &mut self,
        relation_index: usize,
        component_rel_ids: Vec<u16>,
    ) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].section_features |= IMP_REL_FEATURE_VIRTUAL;
            self.relations[relation_index].virtual_component_ids = component_rel_ids;
        }
    }

    pub fn set_relation_widths(
        &mut self,
        relation_index: usize,
        node_id_width: u8,
        edge_index_width: u8,
    ) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].node_id_width = node_id_width;
            self.relations[relation_index].edge_index_width = edge_index_width;
        }
    }

    pub fn set_relation_encoding(&mut self, relation_index: usize, encoding_id: u8) {
        if relation_index < self.relations.len() {
            self.relations[relation_index].encoding_id = encoding_id;
        }
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
            self.relations[relation_index]
                .attributes
                .push(AttributeField {
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

    pub fn add_attribute_to_domain(
        &mut self,
        domain_id: u16,
        name: &str,
        type_code: u8,
        dimension: u32,
        data: Vec<u8>,
        offsets: Option<Vec<u32>>,
    ) {
        if let Some(dom) = self.domains.iter_mut().find(|d| d.domain_id == domain_id) {
            dom.attributes.push(AttributeField {
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
        self.add_index_with_mask(domain_id, relation_id, attribute_index, index_type, name, data, 0);
    }

    #[allow(clippy::too_many_arguments)]
    pub fn add_index_with_mask(
        &mut self,
        domain_id: u16,
        relation_id: u16,
        attribute_index: u16,
        index_type: u8,
        name: &str,
        data: Vec<u8>,
        payload_feature_mask: u64,
    ) {
        self.indexes.push(WriterIndex {
            domain_id,
            relation_id,
            attribute_index,
            index_type,
            name: name.to_string(),
            data,
            payload_feature_mask,
            data_offset: 0,
            data_bytes: 0,
        });
    }

    /// Registers a pre-built `BuiltVamanaIndex` (`IMP_INDEX_VAMANA_ANN = 8`) in Section 2.6.
    pub fn add_built_vamana_index(&mut self, name: &str, built: crate::vamana::BuiltVamanaIndex) {
        self.add_built_vamana_index_with_relation(name, 0xFFFF, built);
    }

    /// Registers a pre-built `BuiltVamanaIndex` (`IMP_INDEX_VAMANA_ANN = 8`) bound to a relation in Section 2.6.
    /// Layout inside `idx.data` (which starts at a 128-byte aligned `idx.data_offset`):
    /// - `0..64`: `VamanaHeader`
    /// - `64..128`: zero padding
    /// - `128..128 + adj_slab.len()`: `adj_slab` (128-byte aligned!)
    /// - `quant_rel_off..quant_rel_off + quant_slab.len()`: `quant_slab` (128-byte aligned!)
    pub fn add_built_vamana_index_with_relation(
        &mut self,
        name: &str,
        relation_id: u16,
        mut built: crate::vamana::BuiltVamanaIndex,
    ) {
        // Reserve 128 bytes for header + 64B padding
        let mut blob = vec![0u8; 128];
        let adj_rel_off = 128u64;
        built.header.adj_slab_offset = adj_rel_off; // Relative offset; converted to absolute in finalize_to_writer
        built.header.adj_slab_bytes = built.adj_slab.len() as u64;
        blob.extend_from_slice(&built.adj_slab);

        if !built.quant_slab.is_empty() {
            Self::align_buffer(&mut blob, 128);
            let quant_rel_off = blob.len() as u64;
            built.header.quant_code_offset = quant_rel_off; // Relative offset; converted to absolute in finalize_to_writer
            blob.extend_from_slice(&built.quant_slab);
        } else {
            built.header.quant_code_offset = 0;
        }

        let hdr_bytes = unsafe {
            std::slice::from_raw_parts(
                &built.header as *const VamanaHeader as *const u8,
                std::mem::size_of::<VamanaHeader>(),
            )
        };
        blob[0..64].copy_from_slice(hdr_bytes);

        self.add_index(
            built.header.domain_id,
            relation_id,
            built.header.vec_attr_id,
            IMP_INDEX_VAMANA_ANN,
            name,
            blob,
        );
    }

    /// Builds and registers an `IMP_INDEX_VAMANA_ANN` (`8`) index from an existing domain Float32 vector attribute.
    pub fn add_vamana_index(
        &mut self,
        domain_id: u16,
        attr_name: &str,
        index_name: &str,
        cfg: &crate::vamana::VamanaBuildConfig,
    ) -> Result<(), String> {
        let mut found_attr: Option<(u16, u16, u32, Vec<f32>)> = None;

        // Pass 0: Search Spec v0.10.0 inline domain attributes first (relation_id = 0xFFFF)
        if let Some(dom) = self.domains.iter().find(|d| d.domain_id == domain_id) {
            for (a_idx, attr) in dom.attributes.iter().enumerate() {
                if attr.name == attr_name {
                    let base_t = attr.type_code & IMPULSE_TYPE_MASK;
                    if base_t != 6 {
                        return Err(format!(
                            "Attribute '{}' on domain {} is not Float32 (type_code=0x{:02X})",
                            attr_name, domain_id, attr.type_code
                        ));
                    }
                    let mut raw_slice: &[u8] = &attr.data;
                    if (attr.type_code & IMPULSE_NULLABLE_FLAG) != 0 {
                        let bm = align_128(dom.node_count.div_ceil(64) * 8) as usize;
                        if raw_slice.len() < bm {
                            return Err(format!(
                                "Nullable attribute '{}' byte length {} smaller than bitmap {}",
                                attr_name,
                                raw_slice.len(),
                                bm
                            ));
                        }
                        raw_slice = &raw_slice[bm..];
                    }
                    if !raw_slice.len().is_multiple_of(4) {
                        return Err(format!(
                            "Attribute '{}' byte length {} is not a multiple of 4",
                            attr_name,
                            raw_slice.len()
                        ));
                    }
                    let mut vecs = Vec::with_capacity(raw_slice.len() / 4);
                    for &chunk in raw_slice.as_chunks::<4>().0 {
                        vecs.push(f32::from_le_bytes(chunk));
                    }
                    found_attr = Some((0xFFFF, a_idx as u16, attr.dimension.max(1), vecs));
                    break;
                }
            }
        }

        if found_attr.is_none() {
            for rel in &self.relations {
                if rel.src_domain_id == domain_id {
                    for (a_idx, attr) in rel.attributes.iter().enumerate() {
                        if attr.name == attr_name {
                            let base_t = attr.type_code & IMPULSE_TYPE_MASK;
                            if base_t != 6 {
                                return Err(format!(
                                    "Attribute '{}' on domain {} is not Float32 (type_code=0x{:02X})",
                                    attr_name, domain_id, attr.type_code
                                ));
                            }
                            if !attr.data.len().is_multiple_of(4) {
                                return Err(format!(
                                    "Attribute '{}' byte length {} is not a multiple of 4",
                                    attr_name,
                                    attr.data.len()
                                ));
                            }
                            let mut vecs = Vec::with_capacity(attr.data.len() / 4);
                            for &chunk in attr.data.as_chunks::<4>().0 {
                                vecs.push(f32::from_le_bytes(chunk));
                            }
                            found_attr =
                                Some((rel.relation_id, a_idx as u16, attr.dimension.max(1), vecs));
                            break;
                        }
                    }
                }
                if found_attr.is_some() {
                    break;
                }
            }
        }

        let (found_rel_id, vec_attr_id, dim, vectors) = found_attr.ok_or_else(|| {
            format!(
                "Vector attribute '{}' not found on domain {}",
                attr_name, domain_id
            )
        })?;
        let d = dim as usize;
        let n = vectors.len().checked_div(d).unwrap_or(0);
        let built = crate::vamana::build_vamana_index(domain_id, vec_attr_id, &vectors, n, d, cfg)?;
        self.add_built_vamana_index_with_relation(index_name, found_rel_id, built);
        Ok(())
    }

    pub fn finalize_to_writer(&mut self, writer: &mut impl Write) -> Result<(), ImpulseError> {
        // Sort relations primary by src_domain_id, secondary by tgt_domain_id
        self.relations.sort_by(|a, b| {
            a.src_domain_id
                .cmp(&b.src_domain_id)
                .then_with(|| a.tgt_domain_id.cmp(&b.tgt_domain_id))
        });
        let mut old_to_new_rel_id = std::collections::HashMap::new();
        for (idx, rel) in self.relations.iter_mut().enumerate() {
            let old_id = rel.relation_id;
            let new_id = idx as u16;
            rel.relation_id = new_id;
            old_to_new_rel_id.insert(old_id, new_id);
        }

        // Remap relation_id on registered secondary/Vamana indices and virtual relation components
        for idx in &mut self.indexes {
            if idx.relation_id != 0xFFFF {
                if let Some(&new_id) = old_to_new_rel_id.get(&idx.relation_id) {
                    idx.relation_id = new_id;
                }
            }
        }
        for rel in &mut self.relations {
            for cid in &mut rel.virtual_component_ids {
                if let Some(&new_id) = old_to_new_rel_id.get(cid) {
                    *cid = new_id;
                }
            }
        }

        // Infer domain node_count when 0 from outgoing/incoming relations or domain attributes
        for dom in &mut self.domains {
            if dom.node_count == 0 {
                let mut inferred = 0u64;
                for rel in &self.relations {
                    if (rel.section_features & IMP_REL_FEATURE_VIRTUAL) != 0 {
                        continue;
                    }
                    if rel.src_domain_id == dom.domain_id {
                        let out_count = if rel.coo_src_indices.is_none() && rel.row_offsets.len() > 1 {
                            (rel.row_offsets.len() - 1) as u64
                        } else {
                            rel.node_count
                        };
                        inferred = inferred.max(out_count);
                    }
                    if rel.tgt_domain_id == dom.domain_id {
                        let sentinel = if rel.node_id_width == 2 {
                            0xFFFFu32
                        } else {
                            u32::MAX
                        };
                        if let Some(max_tgt) =
                            rel.col_indices.iter().copied().filter(|&v| v != sentinel).max()
                        {
                            inferred = inferred.max((max_tgt as u64) + 1);
                        }
                    }
                }
                if inferred == 0 {
                    for attr in &dom.attributes {
                        if let Some(ref offs) = attr.offsets {
                            if !offs.is_empty() {
                                inferred = inferred.max((offs.len() - 1) as u64);
                            }
                        } else if (attr.type_code & IMPULSE_NULLABLE_FLAG) == 0 {
                            if let Some(bt) = BaseDataType::from_u8(attr.type_code) {
                                let stride = bt.element_byte_size() * (attr.dimension.max(1) as usize);
                                if stride > 0 {
                                    inferred = inferred.max((attr.data.len() / stride) as u64);
                                }
                            }
                        }
                    }
                }
                dom.node_count = inferred;
            }
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

        let domain_name_offsets: Vec<u32> = self
            .domains
            .iter()
            .map(|d| get_or_add_string(&d.name))
            .collect();
        let mut dom_attr_name_offsets: Vec<Vec<u32>> = Vec::new();
        for dom in &self.domains {
            let mut a_offs = Vec::with_capacity(dom.attributes.len());
            for attr in &dom.attributes {
                a_offs.push(get_or_add_string(&attr.name));
            }
            dom_attr_name_offsets.push(a_offs);
        }

        let rel_name_offsets: Vec<u32> = self
            .relations
            .iter()
            .map(|r| get_or_add_string(&r.name))
            .collect();

        let mut attr_name_offsets: Vec<Vec<u32>> = Vec::new();
        for rel in &self.relations {
            let mut a_offs = Vec::with_capacity(rel.attributes.len());
            for attr in &rel.attributes {
                a_offs.push(get_or_add_string(&attr.name));
            }
            attr_name_offsets.push(a_offs);
        }

        let index_name_offsets: Vec<u32> = self
            .indexes
            .iter()
            .map(|i| get_or_add_string(&i.name))
            .collect();

        // Compute exact Section 2 Directory Table byte length prior to 4KB page padding
        let mut dir_len = align_128((4 + string_pool.len()) as u64) as usize;
        for dom in &self.domains {
            dir_len += std::mem::size_of::<DomainCatalogEntry>()
                + dom.attributes.len() * std::mem::size_of::<AttributeDescriptor>();
        }
        dir_len = align_128(dir_len as u64) as usize;
        for rel in &self.relations {
            dir_len += std::mem::size_of::<RelationDirectoryEntry>()
                + rel.attributes.len() * std::mem::size_of::<AttributeDescriptor>();
        }
        dir_len = align_128(dir_len as u64) as usize;
        dir_len += self.indexes.len() * std::mem::size_of::<IndexDirectoryEntry>();

        let aligned_dir_table_len = align_4k(dir_len as u64) as usize;
        let payload_base_offset =
            IMPULSE_DEFAULT_DATA_OFFSET as u64 + aligned_dir_table_len as u64;

        // 2. Serialize Payload Blocks (Node Domain Attribute Blocks -> Relation Blocks -> Index Blocks)
        let mut payload = Vec::new();

        // 2a. Node Domain Attribute Blocks (Spec v0.10.0 §3.2 / §4.3)
        for dom in &mut self.domains {
            if dom.attributes.is_empty() {
                continue;
            }
            Self::align_buffer(&mut payload, 4096);
            for attr in &mut dom.attributes {
                Self::align_buffer(&mut payload, 128);
                attr.data_offset = payload_base_offset + payload.len() as u64;
                attr.data_bytes = attr.data.len() as u64;
                payload.extend_from_slice(&attr.data);

                if let Some(ref offs) = attr.offsets {
                    Self::align_buffer(&mut payload, 128);
                    attr.offsets_offset = payload_base_offset + payload.len() as u64;
                    let off_bytes: Vec<u8> = offs.iter().flat_map(|v| v.to_le_bytes()).collect();
                    attr.offsets_bytes = off_bytes.len() as u64;
                    payload.extend_from_slice(&off_bytes);
                } else {
                    attr.offsets_offset = 0;
                    attr.offsets_bytes = 0;
                }
            }
        }

        // Helper closure for domain counts when building CSC
        let dom_counts: std::collections::HashMap<u16, u64> = self
            .domains
            .iter()
            .map(|d| (d.domain_id, d.node_count))
            .collect();

        // 2b. Relation Blocks
        for rel in &mut self.relations {
            let is_virtual = (rel.section_features & IMP_REL_FEATURE_VIRTUAL) != 0;
            if is_virtual {
                rel.csr_row_off_offset = 0;
                rel.csr_row_off_bytes = 0;
                rel.csc_row_off_offset = 0;
                rel.csc_row_off_bytes = 0;
                rel.csc_col_idx_offset = 0;
                rel.csc_col_idx_bytes = 0;
                if rel.virtual_component_ids.len() > 10 {
                    Self::align_buffer(&mut payload, 128);
                    rel.csr_col_idx_offset = payload_base_offset + payload.len() as u64;
                    let comp_bytes: Vec<u8> = rel
                        .virtual_component_ids
                        .iter()
                        .flat_map(|&cid| cid.to_le_bytes())
                        .collect();
                    rel.csr_col_idx_bytes = comp_bytes.len() as u64;
                    payload.extend_from_slice(&comp_bytes);
                } else {
                    rel.csr_col_idx_offset = 0;
                    rel.csr_col_idx_bytes = 0;
                }
                continue;
            }

            Self::align_buffer(&mut payload, 4096);

            let card = rel.section_features & IMP_REL_CARDINALITY_MASK;
            let want_flat_fwd = rel.encoding_id != 3
                && (rel.row_offsets.is_empty()
                    || card == IMP_REL_CARDINALITY_M_1
                    || card == IMP_REL_CARDINALITY_1_1);
            let want_flat_rev = rel.encoding_id != 3
                && (card == IMP_REL_CARDINALITY_1_M || card == IMP_REL_CARDINALITY_1_1);

            if let Some(ref coo_srcs) = rel.coo_src_indices {
                // Explicit raw COO [E, 2] override: csr_row_off = source u32[E], csr_col_idx = target u16/u32[E]
                Self::align_buffer(&mut payload, 128);
                rel.csr_row_off_offset = payload_base_offset + payload.len() as u64;
                let row_bytes: Vec<u8> = coo_srcs.iter().flat_map(|v| v.to_le_bytes()).collect();
                rel.csr_row_off_bytes = row_bytes.len() as u64;
                payload.extend_from_slice(&row_bytes);

                Self::align_buffer(&mut payload, 128);
                rel.csr_col_idx_offset = payload_base_offset + payload.len() as u64;
                let col_bytes: Vec<u8> = if rel.node_id_width == 2 {
                    rel.col_indices
                        .iter()
                        .flat_map(|&v| (v as u16).to_le_bytes())
                        .collect()
                } else {
                    rel.col_indices
                        .iter()
                        .flat_map(|v| v.to_le_bytes())
                        .collect()
                };
                rel.csr_col_idx_bytes = col_bytes.len() as u64;
                payload.extend_from_slice(&col_bytes);
            } else if want_flat_fwd {
                rel.csr_row_off_offset = 0;
                rel.csr_row_off_bytes = 0;
                let sentinel = if rel.node_id_width == 2 {
                    0xFFFFu32
                } else {
                    u32::MAX
                };
                let src_n = rel.node_count as usize;
                let flat_targets: Vec<u32> = if rel.row_offsets.is_empty() {
                    let mut v = rel.col_indices.clone();
                    if v.len() < src_n {
                        v.resize(src_n, sentinel);
                    }
                    v
                } else {
                    let mut flat = vec![sentinel; src_n];
                    let max_u = src_n.min(rel.row_offsets.len().saturating_sub(1));
                    for u in 0..max_u {
                        let s = rel.row_offsets[u] as usize;
                        let e = (rel.row_offsets[u + 1] as usize).min(rel.col_indices.len());
                        if e > s {
                            flat[u] = rel.col_indices[s];
                        }
                    }
                    flat
                };

                Self::align_buffer(&mut payload, 128);
                rel.csr_col_idx_offset = payload_base_offset + payload.len() as u64;
                let col_bytes: Vec<u8> = if rel.node_id_width == 2 {
                    flat_targets
                        .iter()
                        .flat_map(|&v| (v as u16).to_le_bytes())
                        .collect()
                } else {
                    flat_targets.iter().flat_map(|v| v.to_le_bytes()).collect()
                };
                rel.csr_col_idx_bytes = col_bytes.len() as u64;
                payload.extend_from_slice(&col_bytes);
            } else {
                // Standard CSR
                Self::align_buffer(&mut payload, 128);
                rel.csr_row_off_offset = payload_base_offset + payload.len() as u64;
                let row_bytes: Vec<u8> = rel
                    .row_offsets
                    .iter()
                    .flat_map(|v| v.to_le_bytes())
                    .collect();
                rel.csr_row_off_bytes = row_bytes.len() as u64;
                payload.extend_from_slice(&row_bytes);

                Self::align_buffer(&mut payload, 128);
                rel.csr_col_idx_offset = payload_base_offset + payload.len() as u64;
                let col_bytes: Vec<u8> = if rel.node_id_width == 2 {
                    rel.col_indices
                        .iter()
                        .flat_map(|&v| (v as u16).to_le_bytes())
                        .collect()
                } else {
                    rel.col_indices
                        .iter()
                        .flat_map(|v| v.to_le_bytes())
                        .collect()
                };
                rel.csr_col_idx_bytes = col_bytes.len() as u64;
                payload.extend_from_slice(&col_bytes);
            }

            // CSC Arrays (if enabled)
            if rel.include_csc {
                let sentinel = if rel.node_id_width == 2 {
                    0xFFFFu32
                } else {
                    u32::MAX
                };
                let tgt_node_count = dom_counts
                    .get(&rel.tgt_domain_id)
                    .copied()
                    .filter(|&c| c > 0)
                    .unwrap_or_else(|| {
                        rel.col_indices
                            .iter()
                            .copied()
                            .filter(|&m| m != sentinel)
                            .max()
                            .map(|m| (m as u64) + 1)
                            .unwrap_or(rel.node_count)
                    }) as usize;

                let (csc_row_offsets, csc_col_indices) =
                    if let Some((ref c_rows, ref c_cols)) = rel.csc_override {
                        (c_rows.clone(), c_cols.clone())
                    } else {
                        // Collect directed pairs (u, v) from forward representation
                        let mut pairs: Vec<(u32, u32)> = Vec::with_capacity(rel.edge_count as usize);
                        if let Some(ref srcs) = rel.coo_src_indices {
                            for (&u, &v) in srcs.iter().zip(rel.col_indices.iter()) {
                                pairs.push((u, v));
                            }
                        } else if rel.row_offsets.is_empty() {
                            for (u, &v) in rel.col_indices.iter().enumerate() {
                                if v != sentinel {
                                    pairs.push((u as u32, v));
                                }
                            }
                        } else {
                            let src_n = if rel.node_count > 0 {
                                rel.node_count as usize
                            } else {
                                rel.row_offsets.len().saturating_sub(1)
                            };
                            let max_u = src_n.min(rel.row_offsets.len().saturating_sub(1));
                            let num_edges = rel.col_indices.len();
                            for u in 0..max_u {
                                let start = rel.row_offsets[u] as usize;
                                let end = (rel.row_offsets[u + 1] as usize).min(num_edges);
                                for &v in &rel.col_indices[start..end] {
                                    pairs.push((u as u32, v));
                                }
                            }
                        }

                        if want_flat_rev {
                            let mut flat_rev = vec![sentinel; tgt_node_count];
                            for &(u, v) in &pairs {
                                let v_idx = v as usize;
                                if v_idx < tgt_node_count && flat_rev[v_idx] == sentinel {
                                    flat_rev[v_idx] = u;
                                }
                            }
                            (Vec::new(), flat_rev)
                        } else {
                            let mut c_row = vec![0u32; tgt_node_count + 1];
                            for &(_u, v) in &pairs {
                                let v_idx = v as usize;
                                if v_idx < tgt_node_count {
                                    c_row[v_idx + 1] += 1;
                                }
                            }
                            for v in 0..tgt_node_count {
                                c_row[v + 1] += c_row[v];
                            }
                            let mut write_heads = c_row.clone();
                            let mut c_col = vec![0u32; pairs.len()];
                            for &(u, v) in &pairs {
                                let v_idx = v as usize;
                                if v_idx < tgt_node_count {
                                    let pos = write_heads[v_idx] as usize;
                                    if pos < c_col.len() {
                                        c_col[pos] = u;
                                    }
                                    write_heads[v_idx] += 1;
                                }
                            }
                            (c_row, c_col)
                        }
                    };

                if csc_row_offsets.is_empty() {
                    rel.csc_row_off_offset = 0;
                    rel.csc_row_off_bytes = 0;
                } else {
                    let csc_row_bytes: Vec<u8> = csc_row_offsets
                        .iter()
                        .flat_map(|v| v.to_le_bytes())
                        .collect();
                    Self::align_buffer(&mut payload, 128);
                    rel.csc_row_off_offset = payload_base_offset + payload.len() as u64;
                    rel.csc_row_off_bytes = csc_row_bytes.len() as u64;
                    payload.extend_from_slice(&csc_row_bytes);
                }

                let csc_col_bytes: Vec<u8> = if rel.node_id_width == 2 {
                    csc_col_indices
                        .iter()
                        .flat_map(|&v| (v as u16).to_le_bytes())
                        .collect()
                } else {
                    csc_col_indices
                        .iter()
                        .flat_map(|v| v.to_le_bytes())
                        .collect()
                };
                Self::align_buffer(&mut payload, 128);
                rel.csc_col_idx_offset = payload_base_offset + payload.len() as u64;
                rel.csc_col_idx_bytes = csc_col_bytes.len() as u64;
                payload.extend_from_slice(&csc_col_bytes);
            } else {
                rel.csc_row_off_offset = 0;
                rel.csc_row_off_bytes = 0;
                rel.csc_col_idx_offset = 0;
                rel.csc_col_idx_bytes = 0;
            }

            // Serialize Edge Attributes
            for attr in &mut rel.attributes {
                Self::align_buffer(&mut payload, 128);
                attr.data_offset = payload_base_offset + payload.len() as u64;
                attr.data_bytes = attr.data.len() as u64;
                payload.extend_from_slice(&attr.data);

                if let Some(ref offs) = attr.offsets {
                    Self::align_buffer(&mut payload, 128);
                    attr.offsets_offset = payload_base_offset + payload.len() as u64;
                    let off_bytes: Vec<u8> = offs.iter().flat_map(|v| v.to_le_bytes()).collect();
                    attr.offsets_bytes = off_bytes.len() as u64;
                    payload.extend_from_slice(&off_bytes);
                } else {
                    attr.offsets_offset = 0;
                    attr.offsets_bytes = 0;
                }
            }
        }

        // 2c. Serialize Index Blocks
        for idx in &mut self.indexes {
            Self::align_buffer(&mut payload, 128);
            idx.data_offset = payload_base_offset + payload.len() as u64;
            if idx.index_type == IMP_INDEX_VAMANA_ANN && idx.data.len() >= 64 {
                let rel_adj = u64::from_le_bytes(idx.data[40..48].try_into().unwrap());
                let abs_adj = if rel_adj == 0 {
                    idx.data_offset + 128
                } else if rel_adj < idx.data_offset {
                    idx.data_offset + rel_adj
                } else {
                    rel_adj
                };
                idx.data[40..48].copy_from_slice(&abs_adj.to_le_bytes());

                let rel_quant = u64::from_le_bytes(idx.data[56..64].try_into().unwrap());
                if rel_quant > 0 {
                    let abs_quant = if rel_quant < idx.data_offset {
                        idx.data_offset + rel_quant
                    } else {
                        rel_quant
                    };
                    idx.data[56..64].copy_from_slice(&abs_quant.to_le_bytes());
                }
            }
            idx.data_bytes = idx.data.len() as u64;
            payload.extend_from_slice(&idx.data);
        }

        // 3. Serialize Section 2 Directory Table with finalized payload offsets
        let mut dir_table_bytes = Vec::with_capacity(aligned_dir_table_len);

        let str_pool_len = string_pool.len() as u32;
        dir_table_bytes.extend_from_slice(&str_pool_len.to_le_bytes());
        dir_table_bytes.extend_from_slice(&string_pool);
        Self::align_buffer(&mut dir_table_bytes, 128);

        // 3a. Domain Catalog Entries (32B) + Inline Node Domain AttributeDescriptors (44B)
        for (d_idx, dom) in self.domains.iter().enumerate() {
            let dom_entry = DomainCatalogEntry {
                domain_id: dom.domain_id,
                key_type: dom.key_type as u8,
                reserved: 0,
                name_offset: domain_name_offsets[d_idx],
                node_count: dom.node_count,
                attr_count: dom.attributes.len() as u16,
                key_attr_index: dom.key_attr_index,
                reserved2: [0u8; 12],
            };
            let entry_bytes = unsafe {
                std::slice::from_raw_parts(
                    &dom_entry as *const DomainCatalogEntry as *const u8,
                    std::mem::size_of::<DomainCatalogEntry>(),
                )
            };
            dir_table_bytes.extend_from_slice(entry_bytes);

            for (a_idx, attr) in dom.attributes.iter().enumerate() {
                let attr_desc = AttributeDescriptor {
                    name_offset: dom_attr_name_offsets[d_idx][a_idx],
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

        // 3b. Relation Directory Table entries (128B) + Inline Edge AttributeDescriptors (44B)
        for (rel_idx, rel) in self.relations.iter().enumerate() {
            let rel_name_off = rel_name_offsets[rel_idx];
            let mut sec_features: u64 = rel.section_features;
            let is_virtual = (sec_features & IMP_REL_FEATURE_VIRTUAL) != 0;
            if !is_virtual
                && (rel.csc_row_off_offset > 0 || rel.csc_col_idx_offset > 0 || rel.include_csc)
            {
                sec_features |= IMPULSE_SECTION_FEAT_INCLUDE_CSC;
            }
            if !is_virtual && rel.attributes.iter().any(|a| a.name == "weight") {
                sec_features |= IMP_REL_FEATURE_WEIGHTED;
            }

            let mut reserved2 = [0u8; 22];
            if is_virtual {
                let comp_count = rel.virtual_component_ids.len() as u16;
                reserved2[0..2].copy_from_slice(&comp_count.to_le_bytes());
                if rel.virtual_component_ids.len() > 10 {
                    reserved2[2..4].copy_from_slice(&0xFFFFu16.to_le_bytes());
                } else {
                    for (c_i, &cid) in rel.virtual_component_ids.iter().enumerate() {
                        reserved2[2 + c_i * 2..4 + c_i * 2].copy_from_slice(&cid.to_le_bytes());
                    }
                }
            }

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
                section_features: sec_features,
                csr_row_off_offset: rel.csr_row_off_offset,
                csr_row_off_bytes: rel.csr_row_off_bytes,
                csr_col_idx_offset: rel.csr_col_idx_offset,
                csr_col_idx_bytes: rel.csr_col_idx_bytes,
                csc_row_off_offset: rel.csc_row_off_offset,
                csc_row_off_bytes: rel.csc_row_off_bytes,
                csc_col_idx_offset: rel.csc_col_idx_offset,
                csc_col_idx_bytes: rel.csc_col_idx_bytes,
                attr_count: rel.attributes.len() as u16,
                reserved2,
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

        // 3c. Index Directory Table entries (64B)
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
                payload_feature_mask: idx.payload_feature_mask,
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

        debug_assert_eq!(dir_table_bytes.len(), dir_len);
        if dir_table_bytes.len() < aligned_dir_table_len {
            dir_table_bytes.resize(aligned_dir_table_len, 0x00);
        }

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
            std::slice::from_raw_parts(&header as *const SnapshotHeader as *const u8, 0x3E)
        };
        header.header_checksum = compute_crc16(header_raw);

        let header_full_bytes = unsafe {
            std::slice::from_raw_parts(
                &header as *const SnapshotHeader as *const u8,
                std::mem::size_of::<SnapshotHeader>(),
            )
        };

        writer
            .write_all(header_full_bytes)
            .map_err(|_| ImpulseError::IoFailure)?;
        writer
            .write_all(&final_payload)
            .map_err(|_| ImpulseError::IoFailure)?;
        writer.flush().map_err(|_| ImpulseError::IoFailure)?;

        Ok(())
    }

    pub fn finalize(&mut self) -> Result<(), ImpulseError> {
        let mut file = File::create(&self.output_path).map_err(|_| ImpulseError::IoFailure)?;
        self.finalize_to_writer(&mut file)
    }

    pub fn finalize_to_bytes(&mut self) -> Result<Vec<u8>, ImpulseError> {
        let mut buf = Vec::new();
        self.finalize_to_writer(&mut buf)?;
        Ok(buf)
    }
}
