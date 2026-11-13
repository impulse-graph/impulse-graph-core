//! Spec v0.10.0 (with v0.9.0 dual-version compat) High-Performance Zero-Copy Reader Engine

use crate::mmap::{MemoryMap, SharedMemoryMap};
use crate::spec::*;
use std::path::Path;

#[derive(Clone, Debug)]
pub struct DomainInfo {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub name: String,
    pub node_count: u64,
    pub key_attr_index: u16,
    pub attributes: Vec<AttributeInfo>,
}

#[derive(Clone, Debug)]
pub struct RelationInfo {
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
    pub csr_row_off_offset: u64,
    pub csr_row_off_bytes: u64,
    pub csr_col_idx_offset: u64,
    pub csr_col_idx_bytes: u64,
    pub csc_row_off_offset: u64,
    pub csc_row_off_bytes: u64,
    pub csc_col_idx_offset: u64,
    pub csc_col_idx_bytes: u64,
    pub attributes: Vec<AttributeInfo>,
    pub virtual_component_ids: Vec<u16>,
}

impl RelationInfo {
    pub fn has_csc(&self) -> bool {
        (self.section_features & crate::spec::IMPULSE_SECTION_FEAT_INCLUDE_CSC) != 0
            || self.csc_row_off_bytes > 0
            || self.csc_col_idx_bytes > 0
    }

    pub fn is_virtual(&self) -> bool {
        (self.section_features & crate::spec::IMP_REL_FEATURE_VIRTUAL) != 0
    }

    pub fn cardinality_bits(&self) -> u64 {
        self.section_features & crate::spec::IMP_REL_CARDINALITY_MASK
    }

    pub fn is_flat_forward(&self) -> bool {
        !self.is_virtual()
            && self.encoding_id != 3
            && self.csr_row_off_bytes == 0
            && self.csr_col_idx_bytes > 0
    }

    pub fn is_flat_reverse(&self) -> bool {
        !self.is_virtual()
            && self.encoding_id != 3
            && self.csc_row_off_bytes == 0
            && self.csc_col_idx_bytes > 0
    }
}

#[derive(Clone, Debug)]
pub struct AttributeInfo {
    pub name: String,
    pub type_code: u8,
    pub dimension: u32,
    pub data_offset: u64,
    pub data_bytes: u64,
    pub offsets_offset: u64,
    pub offsets_bytes: u64,
}

impl AttributeInfo {
    pub fn base_type(&self) -> Option<BaseDataType> {
        BaseDataType::from_u8(self.type_code)
    }

    pub fn is_nullable(&self) -> bool {
        (self.type_code & IMPULSE_NULLABLE_FLAG) != 0
    }
}

#[derive(Clone, Debug)]
pub struct IndexEntryInfo {
    pub index_id: u32,
    pub domain_id: u16,
    pub relation_id: u16,
    pub attribute_index: u16,
    pub index_type: u8,
    pub name: String,
    pub data_offset: u64,
    pub data_bytes: u64,
    pub payload_feature_mask: u64,
}

pub struct SnapshotReader {
    mmap: SharedMemoryMap,
    header: SnapshotHeader,
    domains: Vec<DomainInfo>,
    relations: Vec<RelationInfo>,
    index_entries: Vec<IndexEntryInfo>,
    metadata_offset: u64,
    metadata_bytes: u64,
}

impl SnapshotReader {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, ImpulseError> {
        let p = path.as_ref();
        if p.exists() {
            let mmap = MemoryMap::open(p).map_err(|_| ImpulseError::IoFailure)?;
            return Self::from_mmap(mmap);
        }

        let env_val = std::env::var("IMPULSE_DATASETS_DIR")
            .or_else(|_| std::env::var("IMPULSEGRAPH_DATA_DIR"))
            .or_else(|_| std::env::var("IMPULSE_DATA_DIR"));

        if let Ok(base_dir) = env_val {
            let candidate1 = Path::new(&base_dir).join(p);
            if candidate1.exists() {
                let mmap = MemoryMap::open(candidate1).map_err(|_| ImpulseError::IoFailure)?;
                return Self::from_mmap(mmap);
            }

            if let Some(file_str) = p.to_str() {
                if let Some(dot_pos) = file_str.find('.') {
                    let dataset = &file_str[..dot_pos];
                    let candidate2 = Path::new(&base_dir).join(dataset).join(p);
                    if candidate2.exists() {
                        let mmap =
                            MemoryMap::open(candidate2).map_err(|_| ImpulseError::IoFailure)?;
                        return Self::from_mmap(mmap);
                    }
                }
            }
        }

        // Check local search paths
        let local_prefixes = [
            "datasets/",
            "../datasets/",
            "../../datasets/",
            "../../../datasets/",
        ];
        for prefix in &local_prefixes {
            let candidate1 = Path::new(prefix).join(p);
            if candidate1.exists() {
                let mmap = MemoryMap::open(candidate1).map_err(|_| ImpulseError::IoFailure)?;
                return Self::from_mmap(mmap);
            }
            if let Some(file_str) = p.to_str() {
                if let Some(dot_pos) = file_str.find('.') {
                    let dataset = &file_str[..dot_pos];
                    let candidate2 = Path::new(prefix).join(dataset).join(p);
                    if candidate2.exists() {
                        let mmap =
                            MemoryMap::open(candidate2).map_err(|_| ImpulseError::IoFailure)?;
                        return Self::from_mmap(mmap);
                    }
                }
            }
        }

        let mmap = MemoryMap::open(p).map_err(|_| ImpulseError::IoFailure)?;
        Self::from_mmap(mmap)
    }

    pub fn from_bytes(bytes: Vec<u8>) -> Result<Self, ImpulseError> {
        let mmap = MemoryMap::from_vec(bytes);
        Self::from_mmap(mmap)
    }

    pub fn from_mmap(mmap: MemoryMap) -> Result<Self, ImpulseError> {
        let shared = SharedMemoryMap::new(mmap);
        let slice = shared.as_slice();

        if slice.len() < 4096 {
            return Err(ImpulseError::IoFailure);
        }

        let header = unsafe { std::ptr::read_unaligned(slice.as_ptr() as *const SnapshotHeader) };

        if header.magic() != IMPULSE_MAGIC {
            crate::assert::check_well_formed(false, "Snapshot header magic mismatch");
            return Err(ImpulseError::InvalidMagic);
        }

        let ver = header.version();
        let is_modern = ver == IMPULSE_VERSION_PACKED || ver == IMPULSE_VERSION_V090;
        if !is_modern && ver != 2 && ver != 0x0204 {
            crate::assert::check_well_formed(false, "Unsupported snapshot format version");
            return Err(ImpulseError::UnsupportedVersion);
        }

        if is_modern {
            let computed_crc = compute_crc16(&slice[0..0x3E]);
            if computed_crc != header.header_checksum() {
                crate::assert::check_well_formed(false, "Snapshot header CRC-16 checksum mismatch");
                return Err(ImpulseError::CorruptChecksum);
            }
        }

        if is_modern && header.footer_directory_offset() > 0 {
            let f_off = header.footer_directory_offset() as usize;
            let f_bytes = header.footer_directory_bytes() as usize;
            if f_off >= slice.len() || f_off.saturating_add(f_bytes) > slice.len() {
                crate::assert::check_well_formed(
                    false,
                    "Snapshot header footer_directory_offset out of bounds",
                );
                return Err(ImpulseError::BufferOverflow);
            }
        }

        let dir_offset = if is_modern && header.footer_directory_offset() > 0 {
            header.footer_directory_offset() as usize
        } else {
            header.data_offset() as usize
        };

        if dir_offset >= slice.len() {
            crate::assert::check_well_formed(false, "Snapshot header data_offset out of bounds");
            return Err(ImpulseError::BufferOverflow);
        }

        let mut cur = dir_offset;

        let domain_count = header.domain_count() as usize;
        let relation_count = header.relation_count() as usize;
        let index_count = if is_modern {
            unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(header.index_count)) as usize }
        } else {
            0
        };
        let mut domains = Vec::with_capacity(domain_count);
        let mut relations = Vec::with_capacity(relation_count);
        let mut index_entries = Vec::with_capacity(index_count);

        if is_modern {
            // Parse Shared String Table
            if cur + 4 > slice.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let string_table_bytes =
                u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
            cur += 4;

            if cur + string_table_bytes > slice.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let string_pool_slice = &slice[cur..cur + string_table_bytes];
            cur += string_table_bytes;

            if string_table_bytes == 0 || string_pool_slice[0] != 0 {
                return Err(ImpulseError::InvalidArgument);
            }

            let validate_and_get_string = |name_off: usize| -> Result<String, ImpulseError> {
                if name_off >= string_pool_slice.len() {
                    return Err(ImpulseError::BufferOverflow);
                }
                if let Some(null_pos) = string_pool_slice[name_off..].iter().position(|&b| b == 0) {
                    std::str::from_utf8(&string_pool_slice[name_off..name_off + null_pos])
                        .map(|s| s.to_string())
                        .map_err(|_| ImpulseError::InvalidArgument)
                } else {
                    Err(ImpulseError::BufferOverflow)
                }
            };

            let rem = cur % 128;
            if rem != 0 {
                cur += 128 - rem;
            }

            // Parse Domain Catalog Entries Array (32B + inline AttributeDescriptors in v0.10.0; 16B in v0.9.0)
            for _d_idx in 0..domain_count {
                if ver == IMPULSE_VERSION_PACKED {
                    if cur + std::mem::size_of::<DomainCatalogEntry>() > slice.len() {
                        return Err(ImpulseError::BufferOverflow);
                    }
                    let dom_entry = unsafe {
                        std::ptr::read_unaligned(slice[cur..].as_ptr() as *const DomainCatalogEntry)
                    };
                    cur += std::mem::size_of::<DomainCatalogEntry>();

                    let key_type = KeyType::from_u8_for_version(dom_entry.key_type(), ver)
                        .ok_or(ImpulseError::InvalidArgument)?;
                    let name = validate_and_get_string(dom_entry.name_offset() as usize)?;
                    let attr_count = dom_entry.attr_count() as usize;
                    let key_attr_index = dom_entry.key_attr_index();
                    if key_attr_index != 0xFFFF && (key_attr_index as usize) >= attr_count {
                        return Err(ImpulseError::InvalidArgument);
                    }

                    let mut attributes = Vec::with_capacity(attr_count);
                    for _ in 0..attr_count {
                        if cur + std::mem::size_of::<AttributeDescriptor>() > slice.len() {
                            return Err(ImpulseError::BufferOverflow);
                        }
                        let attr_desc = unsafe {
                            std::ptr::read_unaligned(
                                slice[cur..].as_ptr() as *const AttributeDescriptor
                            )
                        };
                        cur += std::mem::size_of::<AttributeDescriptor>();

                        let attr_name = validate_and_get_string(attr_desc.name_offset() as usize)?;
                        let data_off = unsafe {
                            std::ptr::read_unaligned(std::ptr::addr_of!(attr_desc.data_offset))
                        };
                        let data_bytes = unsafe {
                            std::ptr::read_unaligned(std::ptr::addr_of!(attr_desc.data_bytes))
                        };
                        let offs_off = unsafe {
                            std::ptr::read_unaligned(std::ptr::addr_of!(attr_desc.offsets_offset))
                        };
                        let offs_bytes = unsafe {
                            std::ptr::read_unaligned(std::ptr::addr_of!(attr_desc.offsets_bytes))
                        };
                        let dim = unsafe {
                            std::ptr::read_unaligned(std::ptr::addr_of!(attr_desc.dimension))
                        };

                        if data_bytes > 0
                            && (data_off as usize).saturating_add(data_bytes as usize) > slice.len()
                        {
                            return Err(ImpulseError::BufferOverflow);
                        }
                        if offs_bytes > 0
                            && (offs_off as usize).saturating_add(offs_bytes as usize) > slice.len()
                        {
                            return Err(ImpulseError::BufferOverflow);
                        }

                        attributes.push(AttributeInfo {
                            name: attr_name,
                            type_code: attr_desc.type_code,
                            dimension: dim,
                            data_offset: data_off,
                            data_bytes,
                            offsets_offset: offs_off,
                            offsets_bytes: offs_bytes,
                        });
                    }

                    domains.push(DomainInfo {
                        domain_id: dom_entry.domain_id(),
                        key_type,
                        name,
                        node_count: dom_entry.node_count(),
                        key_attr_index,
                        attributes,
                    });
                } else {
                    if cur + std::mem::size_of::<DomainCatalogEntryV09>() > slice.len() {
                        return Err(ImpulseError::BufferOverflow);
                    }
                    let dom_entry = unsafe {
                        std::ptr::read_unaligned(
                            slice[cur..].as_ptr() as *const DomainCatalogEntryV09
                        )
                    };
                    cur += std::mem::size_of::<DomainCatalogEntryV09>();

                    let key_type = KeyType::from_u8_for_version(dom_entry.key_type(), ver)
                        .ok_or(ImpulseError::InvalidArgument)?;
                    let name = validate_and_get_string(dom_entry.name_offset() as usize)?;

                    domains.push(DomainInfo {
                        domain_id: dom_entry.domain_id(),
                        key_type,
                        name,
                        node_count: dom_entry.node_count(),
                        key_attr_index: 0xFFFF,
                        attributes: Vec::new(),
                    });
                }
            }

            let rem = cur % 128;
            if rem != 0 {
                cur += 128 - rem;
            }

            // Parse Relation Directory Table
            for _ in 0..relation_count {
                if cur + std::mem::size_of::<RelationDirectoryEntry>() > slice.len() {
                    return Err(ImpulseError::BufferOverflow);
                }
                let rel_entry = unsafe {
                    std::ptr::read_unaligned(slice[cur..].as_ptr() as *const RelationDirectoryEntry)
                };
                cur += std::mem::size_of::<RelationDirectoryEntry>();

                let attr_count = rel_entry.attr_count as usize;
                let mut attributes = Vec::with_capacity(attr_count);

                for _ in 0..attr_count {
                    if cur + std::mem::size_of::<AttributeDescriptor>() > slice.len() {
                        return Err(ImpulseError::BufferOverflow);
                    }
                    let attr_desc = unsafe {
                        std::ptr::read_unaligned(slice[cur..].as_ptr() as *const AttributeDescriptor)
                    };
                    cur += std::mem::size_of::<AttributeDescriptor>();

                    let attr_name = validate_and_get_string(attr_desc.name_offset() as usize)?;

                    attributes.push(AttributeInfo {
                        name: attr_name,
                        type_code: attr_desc.type_code,
                        dimension: attr_desc.dimension,
                        data_offset: attr_desc.data_offset,
                        data_bytes: attr_desc.data_bytes,
                        offsets_offset: attr_desc.offsets_offset,
                        offsets_bytes: attr_desc.offsets_bytes,
                    });
                }

                let rel_name = validate_and_get_string(rel_entry.name_offset() as usize)?;
                let sec_feats = rel_entry.section_features();
                let mut virtual_component_ids = Vec::new();
                if (sec_feats & IMP_REL_FEATURE_VIRTUAL) != 0 {
                    let comp_count =
                        u16::from_le_bytes([rel_entry.reserved2[0], rel_entry.reserved2[1]])
                            as usize;
                    let first_id =
                        u16::from_le_bytes([rel_entry.reserved2[2], rel_entry.reserved2[3]]);
                    if comp_count > 10 || (comp_count > 0 && first_id == 0xFFFF) {
                        let off = rel_entry.csr_col_idx_offset() as usize;
                        let bytes = comp_count * 2;
                        if off.saturating_add(bytes) <= slice.len() {
                            for c in 0..comp_count {
                                let p = off + c * 2;
                                virtual_component_ids
                                    .push(u16::from_le_bytes([slice[p], slice[p + 1]]));
                            }
                        }
                    } else {
                        for c in 0..comp_count.min(10) {
                            let p = 2 + c * 2;
                            virtual_component_ids.push(u16::from_le_bytes([
                                rel_entry.reserved2[p],
                                rel_entry.reserved2[p + 1],
                            ]));
                        }
                    }
                }

                relations.push(RelationInfo {
                    relation_id: rel_entry.relation_id(),
                    src_domain_id: rel_entry.src_domain_id(),
                    tgt_domain_id: rel_entry.tgt_domain_id(),
                    encoding_id: rel_entry.encoding_id(),
                    node_id_width: rel_entry.node_id_width(),
                    edge_index_width: rel_entry.edge_index_width(),
                    name: rel_name,
                    node_count: rel_entry.node_count(),
                    edge_count: rel_entry.edge_count(),
                    section_features: sec_feats,
                    csr_row_off_offset: rel_entry.csr_row_off_offset(),
                    csr_row_off_bytes: rel_entry.csr_row_off_bytes(),
                    csr_col_idx_offset: rel_entry.csr_col_idx_offset(),
                    csr_col_idx_bytes: rel_entry.csr_col_idx_bytes(),
                    csc_row_off_offset: rel_entry.csc_row_off_offset,
                    csc_row_off_bytes: rel_entry.csc_row_off_bytes,
                    csc_col_idx_offset: rel_entry.csc_col_idx_offset,
                    csc_col_idx_bytes: rel_entry.csc_col_idx_bytes,
                    attributes,
                    virtual_component_ids,
                });
            }

            // Align to 128 bytes before Section 2.6 Index Directory Table
            let rem = cur % 128;
            if rem != 0 {
                cur += 128 - rem;
            }

            for _ in 0..index_count {
                if cur + std::mem::size_of::<IndexDirectoryEntry>() > slice.len() {
                    crate::assert::check_well_formed(
                        false,
                        "IndexDirectoryEntry exceeds snapshot buffer",
                    );
                    return Err(ImpulseError::BufferOverflow);
                }
                let idx_entry = unsafe {
                    std::ptr::read_unaligned(slice[cur..].as_ptr() as *const IndexDirectoryEntry)
                };
                cur += std::mem::size_of::<IndexDirectoryEntry>();

                let idx_name = validate_and_get_string(idx_entry.name_offset as usize)?;

                if idx_entry.data_bytes > 0
                    && (idx_entry.data_offset as usize)
                        .saturating_add(idx_entry.data_bytes as usize)
                        > slice.len()
                {
                    crate::assert::check_well_formed(
                        false,
                        "IndexDirectoryEntry payload exceeds snapshot buffer",
                    );
                    return Err(ImpulseError::BufferOverflow);
                }

                index_entries.push(IndexEntryInfo {
                    index_id: idx_entry.index_id,
                    domain_id: idx_entry.domain_id,
                    relation_id: idx_entry.relation_id,
                    attribute_index: idx_entry.attribute_index,
                    index_type: idx_entry.index_type,
                    name: idx_name,
                    data_offset: idx_entry.data_offset,
                    data_bytes: idx_entry.data_bytes,
                    payload_feature_mask: idx_entry.payload_feature_mask,
                });
            }
        } else {
            // Legacy v2.4 parsing
            for d_idx in 0..domain_count {
                if cur + 64 > slice.len() {
                    eprintln!(
                        "Overflow at domain index d_idx={}, cur={}, slice.len()={}",
                        d_idx,
                        cur,
                        slice.len()
                    );
                    return Err(ImpulseError::BufferOverflow);
                }
                let dom_id = u16::from_le_bytes(slice[cur..cur + 2].try_into().unwrap());
                let key_t = KeyType::from_u8_for_version(slice[cur + 2], 9)
                    .ok_or(ImpulseError::InvalidArgument)?;
                let name_off =
                    u32::from_le_bytes(slice[cur + 44..cur + 48].try_into().unwrap()) as usize;
                let name_len =
                    u16::from_le_bytes(slice[cur + 48..cur + 50].try_into().unwrap()) as usize;
                cur += 64;

                let name = if name_len > 0 && name_off > 0 && name_off + name_len <= slice.len() {
                    std::str::from_utf8(&slice[name_off..name_off + name_len])
                        .unwrap_or("")
                        .to_string()
                } else {
                    String::new()
                };

                domains.push(DomainInfo {
                    domain_id: dom_id,
                    key_type: key_t,
                    name,
                    node_count: 0,
                    key_attr_index: 0xFFFF,
                    attributes: Vec::new(),
                });
            }

            let rem = cur % 128;
            if rem != 0 {
                cur += 128 - rem;
            }

            for r_idx in 0..relation_count {
                if cur + 128 > slice.len() {
                    eprintln!(
                        "Overflow at relation index r_idx={}, cur={}, slice.len()={}",
                        r_idx,
                        cur,
                        slice.len()
                    );
                    return Err(ImpulseError::BufferOverflow);
                }
                let src_dom = u16::from_le_bytes(slice[cur..cur + 2].try_into().unwrap());
                let tgt_dom = u16::from_le_bytes(slice[cur + 2..cur + 4].try_into().unwrap());
                let enc_id = slice[cur + 4];
                let node_cnt = u64::from_le_bytes(slice[cur + 16..cur + 24].try_into().unwrap());
                let edge_cnt = u64::from_le_bytes(slice[cur + 24..cur + 32].try_into().unwrap());
                let csr_row_off = u64::from_le_bytes(slice[cur + 32..cur + 40].try_into().unwrap());
                let csr_row_bytes =
                    u64::from_le_bytes(slice[cur + 40..cur + 48].try_into().unwrap());
                let csr_col_idx = u64::from_le_bytes(slice[cur + 48..cur + 56].try_into().unwrap());
                let csr_col_bytes =
                    u64::from_le_bytes(slice[cur + 56..cur + 64].try_into().unwrap());
                cur += 128;

                relations.push(RelationInfo {
                    relation_id: r_idx as u16,
                    src_domain_id: src_dom,
                    tgt_domain_id: tgt_dom,
                    encoding_id: enc_id,
                    node_id_width: 4,
                    edge_index_width: 4,
                    name: String::new(),
                    node_count: node_cnt,
                    edge_count: edge_cnt,
                    section_features: 0,
                    csr_row_off_offset: csr_row_off,
                    csr_row_off_bytes: csr_row_bytes,
                    csr_col_idx_offset: csr_col_idx,
                    csr_col_idx_bytes: csr_col_bytes,
                    csc_row_off_offset: 0,
                    csc_row_off_bytes: 0,
                    csc_col_idx_offset: 0,
                    csc_col_idx_bytes: 0,
                    attributes: Vec::new(),
                    virtual_component_ids: Vec::new(),
                });
            }
        }

        let mut metadata_offset = 0;
        let mut metadata_bytes = 0;
        if slice.len() >= 16 {
            let trailer_pos = slice.len() - 16;
            let trailer = unsafe {
                std::ptr::read_unaligned(slice[trailer_pos..].as_ptr() as *const FooterTrailer)
            };
            if trailer.footer_magic() == IMPULSE_MAGIC {
                let footer_len = trailer.footer_length() as usize;
                if footer_len >= 16 && footer_len <= slice.len() {
                    metadata_offset = (slice.len() - footer_len) as u64;
                    metadata_bytes = (footer_len - 16) as u64;
                }
            }
        }

        Ok(Self {
            mmap: shared,
            header,
            domains,
            relations,
            index_entries,
            metadata_offset,
            metadata_bytes,
        })
    }

    pub fn as_bytes(&self) -> &[u8] {
        self.mmap.as_slice()
    }

    pub fn header(&self) -> &SnapshotHeader {
        &self.header
    }

    pub fn index_entries(&self) -> &[IndexEntryInfo] {
        &self.index_entries
    }

    pub fn domain_count(&self) -> u16 {
        self.domains.len() as u16
    }

    pub fn relation_count(&self) -> u16 {
        self.relations.len() as u16
    }

    pub fn domains(&self) -> &[DomainInfo] {
        &self.domains
    }

    pub fn relations(&self) -> &[RelationInfo] {
        &self.relations
    }

    pub fn metadata_offset(&self) -> u64 {
        self.metadata_offset
    }

    pub fn metadata_bytes(&self) -> u64 {
        self.metadata_bytes
    }

    pub fn get_relation_entry(&self, relation_index: u16) -> Option<&RelationInfo> {
        self.relations.get(relation_index as usize)
    }

    pub fn get_row_offsets(&self, relation_index: usize) -> Result<&[u32], ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        let slice = self.mmap.as_slice();
        let start = rel.csr_row_off_offset as usize;
        let bytes = rel.csr_row_off_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let row_offsets =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(row_offsets)
    }

    pub fn get_col_indices(&self, relation_index: usize) -> Result<&[u32], ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        let slice = self.mmap.as_slice();
        let start = rel.csr_col_idx_offset as usize;
        let bytes = rel.csr_col_idx_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let col_indices =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(col_indices)
    }

    pub fn get_col_indices_u16(&self, relation_index: usize) -> Result<&[u16], ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        let slice = self.mmap.as_slice();
        let start = rel.csr_col_idx_offset as usize;
        let bytes = rel.csr_col_idx_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u16>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let col_indices =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u16, bytes / 2) };
        Ok(col_indices)
    }

    pub fn get_col_indices_as_u32_vec(
        &self,
        relation_index: usize,
    ) -> Result<Vec<u32>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        if rel.node_id_width == 2 {
            let u16_slice = self.get_col_indices_u16(relation_index)?;
            Ok(u16_slice.iter().map(|&x| x as u32).collect())
        } else {
            let u32_slice = self.get_col_indices(relation_index)?;
            Ok(u32_slice.to_vec())
        }
    }

    pub fn get_csc_row_offsets(
        &self,
        relation_index: usize,
    ) -> Result<Option<&[u32]>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        if rel.csc_row_off_bytes == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = rel.csc_row_off_offset as usize;
        let bytes = rel.csc_row_off_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let row_offsets =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(Some(row_offsets))
    }

    pub fn get_csc_col_indices(
        &self,
        relation_index: usize,
    ) -> Result<Option<&[u32]>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        if rel.csc_col_idx_bytes == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = rel.csc_col_idx_offset as usize;
        let bytes = rel.csc_col_idx_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let col_indices =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(Some(col_indices))
    }

    pub fn get_csc_col_indices_u16(
        &self,
        relation_index: usize,
    ) -> Result<Option<&[u16]>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        if rel.csc_col_idx_bytes == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = rel.csc_col_idx_offset as usize;
        let bytes = rel.csc_col_idx_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u16>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let col_indices =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u16, bytes / 2) };
        Ok(Some(col_indices))
    }

    pub fn get_csc_col_indices_as_u32_vec(
        &self,
        relation_index: usize,
    ) -> Result<Option<Vec<u32>>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        if rel.csc_col_idx_bytes == 0 {
            return Ok(None);
        }
        if rel.node_id_width == 2 {
            Ok(self
                .get_csc_col_indices_u16(relation_index)?
                .map(|s| s.iter().map(|&x| x as u32).collect()))
        } else {
            Ok(self
                .get_csc_col_indices(relation_index)?
                .map(|s| s.to_vec()))
        }
    }

    /// Parses and validates a 64-byte `VamanaHeader` (`IMP_INDEX_VAMANA_ANN = 8`) at `entry.data_offset`.
    pub fn parse_vamana_header(
        &self,
        entry: &IndexEntryInfo,
    ) -> Result<VamanaHeader, ImpulseError> {
        if entry.index_type != IMP_INDEX_VAMANA_ANN {
            return Err(ImpulseError::InvalidArgument);
        }
        let slice = self.mmap.as_slice();
        let hdr_off = entry.data_offset as usize;
        if hdr_off + 64 > slice.len() {
            crate::assert::check_well_formed(false, "Vamana header exceeds snapshot buffer");
            return Err(ImpulseError::BufferOverflow);
        }
        if !hdr_off.is_multiple_of(128) {
            crate::assert::check_well_formed(false, "Vamana header offset is not 128-byte aligned");
            return Err(ImpulseError::InvalidArgument);
        }
        let hdr =
            unsafe { std::ptr::read_unaligned(slice[hdr_off..].as_ptr() as *const VamanaHeader) };
        if hdr.magic != VAMANA_MAGIC {
            crate::assert::check_well_formed(false, "Vamana header magic mismatch");
            return Err(ImpulseError::InvalidMagic);
        }
        if hdr.version != VAMANA_VERSION {
            crate::assert::check_well_formed(false, "Unsupported Vamana header version");
            return Err(ImpulseError::UnsupportedVersion);
        }
        let Some(_metric) = VamanaMetric::from_u8(hdr.metric) else {
            crate::assert::check_well_formed(false, "Unsupported Vamana metric code");
            return Err(ImpulseError::InvalidArgument);
        };
        let Some(quant_type) = VamanaQuantType::from_u8(hdr.quant_type) else {
            crate::assert::check_well_formed(false, "Unsupported Vamana quant_type code");
            return Err(ImpulseError::InvalidArgument);
        };
        if hdr.dimension == 0 || hdr.max_degree_r == 0 {
            crate::assert::check_well_formed(
                false,
                "Vamana dimension and max_degree_r must be > 0",
            );
            return Err(ImpulseError::InvalidArgument);
        }
        let n = hdr.node_count as usize;
        if n > 0 && (hdr.medoid_node_id as usize) >= n {
            crate::assert::check_well_formed(false, "Vamana medoid_node_id out of bounds");
            return Err(ImpulseError::InvalidArgument);
        }
        let degree_plus_1 = (hdr.max_degree_r as u64)
            .checked_add(1)
            .ok_or(ImpulseError::BufferOverflow)?;
        let expected_adj_bytes = (n as u64)
            .checked_mul(degree_plus_1)
            .and_then(|v| v.checked_mul(4))
            .ok_or(ImpulseError::BufferOverflow)?;
        if !hdr.adj_slab_offset.is_multiple_of(128) {
            crate::assert::check_well_formed(false, "Vamana adj_slab_offset not 128B aligned");
            return Err(ImpulseError::InvalidArgument);
        }
        if hdr.adj_slab_bytes < expected_adj_bytes
            || (hdr.adj_slab_offset as usize).saturating_add(hdr.adj_slab_bytes as usize)
                > slice.len()
        {
            crate::assert::check_well_formed(false, "Vamana adjacency slab out of bounds");
            return Err(ImpulseError::BufferOverflow);
        }
        if hdr.quant_code_offset > 0 {
            if !hdr.quant_code_offset.is_multiple_of(128) {
                crate::assert::check_well_formed(
                    false,
                    "Vamana quant_code_offset not 128B aligned",
                );
                return Err(ImpulseError::InvalidArgument);
            }
            let row_bytes = quant_type.row_bytes(hdr.dimension as usize);
            let q_bytes = n
                .checked_mul(row_bytes)
                .ok_or(ImpulseError::BufferOverflow)?;
            if (hdr.quant_code_offset as usize).saturating_add(q_bytes) > slice.len() {
                crate::assert::check_well_formed(false, "Vamana quantized slab out of bounds");
                return Err(ImpulseError::BufferOverflow);
            }
        }

        // Optional $O(N \cdot R)$ audit of neighbor IDs when AUDIT_ENABLED
        let adj_base = hdr.adj_slab_offset as usize;
        let row_stride = ((hdr.max_degree_r as usize) + 1) * 4;
        let r_max = hdr.max_degree_r as usize;
        crate::assert::audit(
            || {
                for u in 0..n {
                    let row_off = adj_base + u * row_stride;
                    let deg = u32::from_le_bytes(slice[row_off..row_off + 4].try_into().unwrap())
                        as usize;
                    if deg > r_max {
                        return false;
                    }
                    for j in 0..deg {
                        let nb_off = row_off + (j + 1) * 4;
                        let nb = u32::from_le_bytes(slice[nb_off..nb_off + 4].try_into().unwrap())
                            as usize;
                        if nb >= n {
                            return false;
                        }
                    }
                }
                true
            },
            "Vamana adjacency slab degree or neighbor node_id bounds audit failed",
        );

        Ok(hdr)
    }

    pub fn get_vamana_adj_slab(&self, hdr: &VamanaHeader) -> Result<&[u8], ImpulseError> {
        let slice = self.mmap.as_slice();
        let start = hdr.adj_slab_offset as usize;
        let bytes = hdr.adj_slab_bytes as usize;
        if start.saturating_add(bytes) > slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(&slice[start..start + bytes])
    }

    pub fn get_vamana_quant_slab(&self, hdr: &VamanaHeader) -> Result<Option<&[u8]>, ImpulseError> {
        let quant_type =
            VamanaQuantType::from_u8(hdr.quant_type).ok_or(ImpulseError::InvalidArgument)?;
        if quant_type == VamanaQuantType::None || hdr.quant_code_offset == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = hdr.quant_code_offset as usize;
        let bytes = (hdr.node_count as usize) * quant_type.row_bytes(hdr.dimension as usize);
        if start.saturating_add(bytes) > slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(Some(&slice[start..start + bytes]))
    }

    /// Locates a domain's Float32 vector attribute (`[node_count, dimension]`), prioritizing
    /// Spec v0.10.0 inline `DomainInfo.attributes` before legacy v0.9.0 carrier relations.
    fn resolve_domain_float32_raw_slice(
        &self,
        domain_id: u16,
        relation_hint: Option<u16>,
        attr_index: Option<u16>,
        attr_name: Option<&str>,
    ) -> Result<(&AttributeInfo, u64, &[u8]), ImpulseError> {
        // Pass 0: Spec v0.10.0 inline DomainInfo.attributes (when relation_hint is None or 0xFFFF)
        if relation_hint.is_none() || relation_hint == Some(0xFFFF) {
            if let Some((d_idx, dom)) = self
                .domains
                .iter()
                .enumerate()
                .find(|(_, d)| d.domain_id == domain_id)
            {
                for (a_idx, a) in dom.attributes.iter().enumerate() {
                    if (a.type_code & IMPULSE_TYPE_MASK) != 6 {
                        continue;
                    }
                    let matched = if let Some(name) = attr_name {
                        a.name == name
                    } else if let Some(target_idx) = attr_index {
                        a_idx == target_idx as usize
                    } else {
                        a.dimension > 1
                    };
                    if matched {
                        let raw = self.get_domain_attribute_by_index(d_idx, a_idx)?;
                        return Ok((a, dom.node_count, raw));
                    }
                }
            }
        }

        let dom_node_count = self
            .domains
            .iter()
            .find(|d| d.domain_id == domain_id)
            .map(|d| d.node_count);

        let match_attr_in_rel = |r_idx: usize| -> Option<usize> {
            let r = &self.relations[r_idx];
            for (a_idx, a) in r.attributes.iter().enumerate() {
                let base_t = a.type_code & IMPULSE_TYPE_MASK;
                if base_t != 6 {
                    continue;
                }
                let matched = if let Some(name) = attr_name {
                    a.name == name
                } else if let Some(target_idx) = attr_index {
                    a_idx == target_idx as usize
                } else {
                    a.dimension > 1
                };
                if matched {
                    return Some(a_idx);
                }
            }
            None
        };

        let mut found_rel_attr: Option<(usize, usize)> = None;

        // Pass 1: Explicit relation_hint matches
        if let Some(rel_id) = relation_hint {
            if rel_id != 0xFFFF {
                if let Some(r_idx) = self.relations.iter().position(|r| r.relation_id == rel_id) {
                    if let Some(a_idx) = match_attr_in_rel(r_idx) {
                        found_rel_attr = Some((r_idx, a_idx));
                    }
                }
            }
        }

        // Pass 2: Dedicated node carrier relation
        if found_rel_attr.is_none() {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id
                    && r.tgt_domain_id == domain_id
                    && (r.edge_count == 0 || r.name.ends_with("_nodes"))
                {
                    if let Some(a_idx) = match_attr_in_rel(r_idx) {
                        found_rel_attr = Some((r_idx, a_idx));
                        break;
                    }
                }
            }
        }

        // Pass 3: Any node carrier relation with src_domain_id == domain_id
        if found_rel_attr.is_none() {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id && (r.edge_count == 0 || r.name.ends_with("_nodes"))
                {
                    if let Some(a_idx) = match_attr_in_rel(r_idx) {
                        found_rel_attr = Some((r_idx, a_idx));
                        break;
                    }
                }
            }
        }

        // Pass 4: Any relation where data_bytes == node_count * dim * 4
        if found_rel_attr.is_none() {
            if let Some(nc) = dom_node_count {
                for (r_idx, r) in self.relations.iter().enumerate() {
                    if r.src_domain_id == domain_id {
                        if let Some(a_idx) = match_attr_in_rel(r_idx) {
                            let a = &r.attributes[a_idx];
                            let expected_bytes = nc * (a.dimension.max(1) as u64) * 4;
                            if a.data_bytes == expected_bytes {
                                found_rel_attr = Some((r_idx, a_idx));
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Pass 5: Any self-relation
        if found_rel_attr.is_none() {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id && r.tgt_domain_id == domain_id {
                    if let Some(a_idx) = match_attr_in_rel(r_idx) {
                        found_rel_attr = Some((r_idx, a_idx));
                        break;
                    }
                }
            }
        }

        // Pass 6: Fallback to any relation with src_domain_id == domain_id
        if found_rel_attr.is_none() {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id {
                    if let Some(a_idx) = match_attr_in_rel(r_idx) {
                        found_rel_attr = Some((r_idx, a_idx));
                        break;
                    }
                }
            }
        }

        let (r_idx, a_idx) = found_rel_attr.ok_or(ImpulseError::NotFound)?;
        let a = &self.relations[r_idx].attributes[a_idx];
        let raw = self.get_attribute_data(r_idx, a_idx)?;
        let nc = dom_node_count.unwrap_or(self.relations[r_idx].node_count);
        Ok((a, nc, raw))
    }

    /// Reads a domain's Float32 vector attribute (`[node_count, dimension]`) by `attr_index` or `attr_name`.
    pub fn get_domain_float32_vectors(
        &self,
        domain_id: u16,
        attr_index: Option<u16>,
        attr_name: Option<&str>,
    ) -> Result<(usize, usize, Vec<f32>), ImpulseError> {
        self.get_domain_float32_vectors_with_relation(domain_id, None, attr_index, attr_name)
    }

    /// Reads a domain's Float32 vector attribute with an optional relation binding hint.
    pub fn get_domain_float32_vectors_with_relation(
        &self,
        domain_id: u16,
        relation_hint: Option<u16>,
        attr_index: Option<u16>,
        attr_name: Option<&str>,
    ) -> Result<(usize, usize, Vec<f32>), ImpulseError> {
        let (a, entity_count, mut raw) =
            self.resolve_domain_float32_raw_slice(domain_id, relation_hint, attr_index, attr_name)?;
        if a.is_nullable() {
            let bm_bytes = align_128(entity_count.div_ceil(64) * 8) as usize;
            if raw.len() < bm_bytes {
                return Err(ImpulseError::BufferOverflow);
            }
            raw = &raw[bm_bytes..];
        }
        let dim = a.dimension.max(1) as usize;
        if !raw.len().is_multiple_of(4) {
            return Err(ImpulseError::InvalidArgument);
        }
        let total_floats = raw.len() / 4;
        let n_nodes = total_floats / dim;
        let mut vecs = Vec::with_capacity(total_floats);
        for &chunk in raw.as_chunks::<4>().0 {
            vecs.push(f32::from_le_bytes(chunk));
        }
        Ok((n_nodes, dim, vecs))
    }

    /// Zero-copy slice view over a domain's Float32 vector attribute (`[node_count, dimension]`) when 4-byte aligned.
    pub fn get_domain_float32_slice(
        &self,
        domain_id: u16,
        attr_index: Option<u16>,
        attr_name: Option<&str>,
    ) -> Result<(usize, usize, &[f32]), ImpulseError> {
        self.get_domain_float32_slice_with_relation(domain_id, None, attr_index, attr_name)
    }

    /// Zero-copy slice view over a domain's Float32 vector attribute with an optional relation binding hint.
    pub fn get_domain_float32_slice_with_relation(
        &self,
        domain_id: u16,
        relation_hint: Option<u16>,
        attr_index: Option<u16>,
        attr_name: Option<&str>,
    ) -> Result<(usize, usize, &[f32]), ImpulseError> {
        let (a, entity_count, mut raw) =
            self.resolve_domain_float32_raw_slice(domain_id, relation_hint, attr_index, attr_name)?;
        if a.is_nullable() {
            let bm_bytes = align_128(entity_count.div_ceil(64) * 8) as usize;
            if raw.len() < bm_bytes {
                return Err(ImpulseError::BufferOverflow);
            }
            raw = &raw[bm_bytes..];
        }
        let dim = a.dimension.max(1) as usize;
        if !raw.len().is_multiple_of(4) || !(raw.as_ptr() as usize).is_multiple_of(4) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let total_floats = raw.len() / 4;
        let n_nodes = total_floats / dim;
        let slice = unsafe { std::slice::from_raw_parts(raw.as_ptr() as *const f32, total_floats) };
        Ok((n_nodes, dim, slice))
    }

    /// Executes two-stage quantized Vamana ANN beam search + full-precision Float32 reranking on `entry`.
    pub fn vamana_ann_search(
        &self,
        entry: &IndexEntryInfo,
        query_vec: &[f32],
        top_k: usize,
        ef_search: usize,
    ) -> Result<Vec<(u32, f32, f32)>, ImpulseError> {
        let hdr = self.parse_vamana_header(entry)?;
        let adj_slab = self.get_vamana_adj_slab(&hdr)?;
        let quant_slab = self.get_vamana_quant_slab(&hdr)?;
        let rel_hint = if entry.relation_id != 0xFFFF {
            Some(entry.relation_id)
        } else {
            None
        };
        if let Ok((_n, _d, vec_slice)) = self.get_domain_float32_slice_with_relation(
            hdr.domain_id,
            rel_hint,
            Some(hdr.vec_attr_id),
            None,
        ) {
            return crate::vamana::vamana_ann_search(
                &hdr, adj_slab, quant_slab, vec_slice, query_vec, top_k, ef_search,
            )
            .map_err(|_| ImpulseError::InvalidArgument);
        }
        let (_n, _d, vectors) = self.get_domain_float32_vectors_with_relation(
            hdr.domain_id,
            rel_hint,
            Some(hdr.vec_attr_id),
            None,
        )?;
        crate::vamana::vamana_ann_search(
            &hdr, adj_slab, quant_slab, &vectors, query_vec, top_k, ef_search,
        )
        .map_err(|_| ImpulseError::InvalidArgument)
    }

    pub fn is_reachable(&self, relation_index: u16, src_id: u64, tgt_id: u64) -> bool {
        match self.get_neighbors(relation_index, src_id) {
            Ok(nbrs) => nbrs.contains(&tgt_id),
            Err(_) => false,
        }
    }

    pub fn is_adjacent(
        &self,
        relation_index: u16,
        src_id: u64,
        tgt_id: u64,
    ) -> Result<bool, ImpulseError> {
        Ok(self.is_reachable(relation_index, src_id, tgt_id))
    }

    pub fn get_neighbors(
        &self,
        relation_index: u16,
        src_id: u64,
    ) -> Result<Vec<u64>, ImpulseError> {
        let r_idx = relation_index as usize;
        let rel = self.relations.get(r_idx).ok_or(ImpulseError::NotFound)?;

        if rel.is_virtual() {
            let mut merged = Vec::new();
            for &comp_rel_id in &rel.virtual_component_ids {
                if let Some(comp_idx) = self
                    .relations
                    .iter()
                    .position(|r| r.relation_id == comp_rel_id)
                {
                    if comp_idx != r_idx {
                        if let Ok(sub) = self.get_neighbors(comp_idx as u16, src_id) {
                            for v in sub {
                                if !merged.contains(&v) {
                                    merged.push(v);
                                }
                            }
                        }
                    }
                }
            }
            return Ok(merged);
        }

        if src_id >= rel.node_count {
            return Ok(Vec::new());
        }

        if rel.is_flat_forward() {
            let u = src_id as usize;
            if rel.node_id_width == 2 {
                let cols = self.get_col_indices_u16(r_idx)?;
                if u >= cols.len() || cols[u] == 0xFFFF {
                    return Ok(Vec::new());
                }
                return Ok(vec![cols[u] as u64]);
            } else {
                let cols = self.get_col_indices(r_idx)?;
                if u >= cols.len() || cols[u] == u32::MAX {
                    return Ok(Vec::new());
                }
                return Ok(vec![cols[u] as u64]);
            }
        }

        let row_offsets = self.get_row_offsets(r_idx)?;
        let u = src_id as usize;
        if u + 1 >= row_offsets.len() {
            return Ok(Vec::new());
        }
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        if start > end {
            return Ok(Vec::new());
        }

        if rel.node_id_width == 2 {
            let col_indices = self.get_col_indices_u16(r_idx)?;
            if end > col_indices.len() {
                return Ok(Vec::new());
            }
            Ok(col_indices[start..end].iter().map(|&v| v as u64).collect())
        } else {
            let col_indices = self.get_col_indices(r_idx)?;
            if end > col_indices.len() {
                return Ok(Vec::new());
            }
            Ok(col_indices[start..end].iter().map(|&v| v as u64).collect())
        }
    }

    pub fn get_metadata(&self) -> Result<std::collections::HashMap<String, String>, ImpulseError> {
        let mut map = std::collections::HashMap::new();
        if self.metadata_bytes < 4 {
            return Ok(map);
        }

        let slice = self.mmap.as_slice();
        let start = self.metadata_offset as usize;
        let total = self.metadata_bytes as usize;

        if start + total > slice.len() {
            return Ok(map);
        }

        let mut cur = start;
        let count = u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
        cur += 4;

        for _ in 0..count {
            if cur + 2 > start + total {
                break;
            }
            let klen = u16::from_le_bytes(slice[cur..cur + 2].try_into().unwrap()) as usize;
            cur += 2;
            if cur + klen > start + total {
                break;
            }
            let key = std::str::from_utf8(&slice[cur..cur + klen])
                .unwrap_or("")
                .to_string();
            cur += klen;

            if cur + 4 > start + total {
                break;
            }
            let vlen = u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
            cur += 4;
            if cur + vlen > start + total {
                break;
            }
            let val = std::str::from_utf8(&slice[cur..cur + vlen])
                .unwrap_or("")
                .to_string();
            cur += vlen;

            map.insert(key, val);
        }

        Ok(map)
    }

    pub fn get_attribute_data(
        &self,
        relation_index: usize,
        attribute_index: usize,
    ) -> Result<&[u8], ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        let attr = rel
            .attributes
            .get(attribute_index)
            .ok_or(ImpulseError::NotFound)?;
        let slice = self.mmap.as_slice();
        let start = attr.data_offset as usize;
        let bytes = attr.data_bytes as usize;
        if start + bytes > slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(&slice[start..start + bytes])
    }

    pub fn get_attribute_offsets(
        &self,
        relation_index: usize,
        attribute_index: usize,
    ) -> Result<Option<&[u32]>, ImpulseError> {
        let rel = self
            .relations
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;
        let attr = rel
            .attributes
            .get(attribute_index)
            .ok_or(ImpulseError::NotFound)?;
        if attr.offsets_bytes == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = attr.offsets_offset as usize;
        let bytes = attr.offsets_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let offsets =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(Some(offsets))
    }

    pub fn get_domain_attribute_by_index(
        &self,
        domain_index: usize,
        attribute_index: usize,
    ) -> Result<&[u8], ImpulseError> {
        let dom = self
            .domains
            .get(domain_index)
            .ok_or(ImpulseError::NotFound)?;
        let attr = dom
            .attributes
            .get(attribute_index)
            .ok_or(ImpulseError::NotFound)?;
        let slice = self.mmap.as_slice();
        let start = attr.data_offset as usize;
        let bytes = attr.data_bytes as usize;
        if start.saturating_add(bytes) > slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(&slice[start..start + bytes])
    }

    pub fn get_domain_attribute_offsets_by_index(
        &self,
        domain_index: usize,
        attribute_index: usize,
    ) -> Result<Option<&[u32]>, ImpulseError> {
        let dom = self
            .domains
            .get(domain_index)
            .ok_or(ImpulseError::NotFound)?;
        let attr = dom
            .attributes
            .get(attribute_index)
            .ok_or(ImpulseError::NotFound)?;
        if attr.offsets_bytes == 0 {
            return Ok(None);
        }
        let slice = self.mmap.as_slice();
        let start = attr.offsets_offset as usize;
        let bytes = attr.offsets_bytes as usize;
        if start.checked_add(bytes).is_none_or(|end| end > slice.len()) {
            return Err(ImpulseError::BufferOverflow);
        }
        if !(slice[start..].as_ptr() as usize).is_multiple_of(std::mem::align_of::<u32>()) {
            return Err(ImpulseError::InvalidAlignment);
        }
        let offsets =
            unsafe { std::slice::from_raw_parts(slice[start..].as_ptr() as *const u32, bytes / 4) };
        Ok(Some(offsets))
    }

    /// Resolves a dense node ID (`0..node_count-1`) to its primary external key bytes in $O(1)$ time
    /// via `DomainInfo.key_attr_index` (Spec v0.10.0 §5.1). Returns `Ok(None)` if no `key_attr_index`
    /// is configured (`0xFFFF`) or if the key entry is marked NULL.
    pub fn resolve_dense_id(
        &self,
        domain_id: u16,
        dense_id: u32,
    ) -> Result<Option<&[u8]>, ImpulseError> {
        let (d_idx, dom) = self
            .domains
            .iter()
            .enumerate()
            .find(|(_, d)| d.domain_id == domain_id)
            .ok_or(ImpulseError::NotFound)?;
        if (dense_id as u64) >= dom.node_count {
            return Err(ImpulseError::NotFound);
        }
        if dom.key_attr_index == 0xFFFF {
            return Ok(None);
        }
        let a_idx = dom.key_attr_index as usize;
        let attr = dom
            .attributes
            .get(a_idx)
            .ok_or(ImpulseError::InvalidArgument)?;
        let mut raw = self.get_domain_attribute_by_index(d_idx, a_idx)?;
        let idx = dense_id as usize;

        if attr.is_nullable() {
            let raw_bm = dom.node_count.div_ceil(64) * 8;
            let padded_bm = align_128(raw_bm) as usize;
            if raw.len() < padded_bm || (idx >> 3) >= raw_bm as usize {
                return Err(ImpulseError::BufferOverflow);
            }
            if (raw[idx >> 3] & (1u8 << (idx & 7))) == 0 {
                return Ok(None);
            }
            raw = &raw[padded_bm..];
        }

        let base = attr.base_type().ok_or(ImpulseError::InvalidArgument)?;
        if base.is_variable() {
            let offs = self
                .get_domain_attribute_offsets_by_index(d_idx, a_idx)?
                .ok_or(ImpulseError::InvalidArgument)?;
            if idx + 1 >= offs.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let s = offs[idx] as usize;
            let mut e = offs[idx + 1] as usize;
            if s > e || e > raw.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            if base == BaseDataType::VarString && e > s && raw[e - 1] == 0 {
                e -= 1;
            }
            return Ok(Some(&raw[s..e]));
        }

        let stride = base.element_byte_size() * (attr.dimension.max(1) as usize);
        let start = idx
            .checked_mul(stride)
            .ok_or(ImpulseError::BufferOverflow)?;
        let end = start
            .checked_add(stride)
            .ok_or(ImpulseError::BufferOverflow)?;
        if end > raw.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(Some(&raw[start..end]))
    }

    pub fn get_domain_attribute_data(
        &self,
        domain_id: u16,
        name: &str,
    ) -> Result<&[u8], ImpulseError> {
        // Pass 0: Spec v0.10.0 inline DomainInfo.attributes
        if let Some((d_idx, dom)) = self
            .domains
            .iter()
            .enumerate()
            .find(|(_, d)| d.domain_id == domain_id)
        {
            for (a_idx, a) in dom.attributes.iter().enumerate() {
                if a.name == name {
                    return self.get_domain_attribute_by_index(d_idx, a_idx);
                }
            }
        }

        let dom_node_count = self
            .domains
            .iter()
            .find(|d| d.domain_id == domain_id)
            .map(|d| d.node_count);

        // Pass 1: Dedicated node carrier relation (src == tgt == domain_id && (edge_count == 0 || name.ends_with("_nodes")))
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id
                && r.tgt_domain_id == domain_id
                && (r.edge_count == 0 || r.name.ends_with("_nodes"))
            {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_data(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 2: Any carrier relation with src == domain_id && (edge_count == 0 || name.ends_with("_nodes"))
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id && (r.edge_count == 0 || r.name.ends_with("_nodes")) {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_data(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 3: Any relation where data_bytes == node_count * dim * elem_size
        if let Some(nc) = dom_node_count {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id {
                    for (a_idx, a) in r.attributes.iter().enumerate() {
                        if a.name == name {
                            let elem_size = (a.dimension.max(1) as u64)
                                * (a.base_type()
                                    .map(|t| t.element_byte_size() as u64)
                                    .unwrap_or(1));
                            if elem_size > 0 && a.data_bytes == nc * elem_size {
                                return self.get_attribute_data(r_idx, a_idx);
                            }
                        }
                    }
                }
            }
        }

        // Pass 4: Any self-relation (src == tgt == domain_id)
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id && r.tgt_domain_id == domain_id {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_data(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 5: Fallback to any relation with src_domain_id == domain_id
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_data(r_idx, a_idx);
                    }
                }
            }
        }
        Err(ImpulseError::NotFound)
    }

    pub fn get_domain_attribute_offsets(
        &self,
        domain_id: u16,
        name: &str,
    ) -> Result<Option<&[u32]>, ImpulseError> {
        // Pass 0: Spec v0.10.0 inline DomainInfo.attributes
        if let Some((d_idx, dom)) = self
            .domains
            .iter()
            .enumerate()
            .find(|(_, d)| d.domain_id == domain_id)
        {
            for (a_idx, a) in dom.attributes.iter().enumerate() {
                if a.name == name {
                    return self.get_domain_attribute_offsets_by_index(d_idx, a_idx);
                }
            }
        }

        let dom_node_count = self
            .domains
            .iter()
            .find(|d| d.domain_id == domain_id)
            .map(|d| d.node_count);

        // Pass 1: Dedicated node carrier relation (src == tgt == domain_id && (edge_count == 0 || name.ends_with("_nodes")))
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id
                && r.tgt_domain_id == domain_id
                && (r.edge_count == 0 || r.name.ends_with("_nodes"))
            {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_offsets(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 2: Any carrier relation with src == domain_id && (edge_count == 0 || name.ends_with("_nodes"))
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id && (r.edge_count == 0 || r.name.ends_with("_nodes")) {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_offsets(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 3: Any relation where data_bytes == node_count * dim * elem_size
        if let Some(nc) = dom_node_count {
            for (r_idx, r) in self.relations.iter().enumerate() {
                if r.src_domain_id == domain_id {
                    for (a_idx, a) in r.attributes.iter().enumerate() {
                        if a.name == name {
                            let elem_size = (a.dimension.max(1) as u64)
                                * (a.base_type()
                                    .map(|t| t.element_byte_size() as u64)
                                    .unwrap_or(1));
                            if elem_size > 0 && a.data_bytes == nc * elem_size {
                                return self.get_attribute_offsets(r_idx, a_idx);
                            }
                        }
                    }
                }
            }
        }

        // Pass 4: Any self-relation (src == tgt == domain_id)
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id && r.tgt_domain_id == domain_id {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_offsets(r_idx, a_idx);
                    }
                }
            }
        }

        // Pass 5: Fallback to any relation with src_domain_id == domain_id
        for (r_idx, r) in self.relations.iter().enumerate() {
            if r.src_domain_id == domain_id {
                for (a_idx, a) in r.attributes.iter().enumerate() {
                    if a.name == name {
                        return self.get_attribute_offsets(r_idx, a_idx);
                    }
                }
            }
        }
        Err(ImpulseError::NotFound)
    }

    pub fn get_relation_statistics(
        &self,
        relation_index: usize,
    ) -> Result<crate::stats::RelationStatistics, ImpulseError> {
        crate::stats::RelationStatisticsCalculator::calculate(
            self,
            relation_index,
            crate::stats::RelationStatisticsCalculator::DEFAULT_SUPERNODE_ZSCORE_THRESHOLD,
        )
    }

    pub fn get_graph_statistics(&self) -> Result<crate::stats::GraphStatistics, ImpulseError> {
        let mut rel_map = std::collections::HashMap::new();
        let mut attr_map = std::collections::HashMap::new();

        for (d_idx, dom) in self.domains.iter().enumerate() {
            let dom_name = if dom.name.is_empty() {
                format!("dom_{}", dom.domain_id)
            } else {
                dom.name.clone()
            };
            for (a_idx, a) in dom.attributes.iter().enumerate() {
                if let Ok(mut data) = self.get_domain_attribute_by_index(d_idx, a_idx) {
                    if a.is_nullable() {
                        let bm = align_128(dom.node_count.div_ceil(64) * 8) as usize;
                        data = if data.len() >= bm { &data[bm..] } else { &[] };
                    }
                    let base_type = a.type_code & crate::spec::IMPULSE_TYPE_MASK;
                    let attr_stats = match base_type {
                        3 => crate::stats::AttributeStatisticsCalculator::calculate_int32(
                            &a.name, data,
                        ),
                        4 | 8 | 9 => crate::stats::AttributeStatisticsCalculator::calculate_int64(
                            &a.name, data,
                        ),
                        6 => crate::stats::AttributeStatisticsCalculator::calculate_float32(
                            &a.name, data,
                        ),
                        7 => crate::stats::AttributeStatisticsCalculator::calculate_float64(
                            &a.name, data,
                        ),
                        _ => crate::stats::AttributeStatistics::empty(&a.name),
                    };
                    attr_map.insert(format!("{}.{}", dom_name, a.name), attr_stats);
                }
            }
        }

        for (r_idx, r) in self.relations.iter().enumerate() {
            let stats = self.get_relation_statistics(r_idx)?;
            let rel_name = if r.name.is_empty() {
                format!("rel_{}", r.relation_id)
            } else {
                r.name.clone()
            };
            rel_map.insert(rel_name.clone(), stats);

            for (a_idx, a) in r.attributes.iter().enumerate() {
                if let Ok(mut data) = self.get_attribute_data(r_idx, a_idx) {
                    if a.is_nullable() {
                        let bm = align_128(r.edge_count.div_ceil(64) * 8) as usize;
                        data = if data.len() >= bm { &data[bm..] } else { &[] };
                    }
                    let base_type = a.type_code & crate::spec::IMPULSE_TYPE_MASK;
                    let attr_stats = match base_type {
                        3 => crate::stats::AttributeStatisticsCalculator::calculate_int32(
                            &a.name, data,
                        ),
                        4 | 8 | 9 => crate::stats::AttributeStatisticsCalculator::calculate_int64(
                            &a.name, data,
                        ),
                        6 => crate::stats::AttributeStatisticsCalculator::calculate_float32(
                            &a.name, data,
                        ),
                        7 => crate::stats::AttributeStatisticsCalculator::calculate_float64(
                            &a.name, data,
                        ),
                        _ => crate::stats::AttributeStatistics::empty(&a.name),
                    };
                    attr_map.insert(format!("{}.{}", rel_name, a.name), attr_stats);
                }
            }
        }

        let metadata = self.get_metadata().unwrap_or_default();

        Ok(crate::stats::GraphStatistics {
            relation_stats: rel_map,
            attribute_stats: attr_map,
            metadata_sketches: metadata,
        })
    }
}
