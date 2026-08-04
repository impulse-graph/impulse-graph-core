//! Spec v0.9.0 High-Performance Zero-Copy Reader Engine

use crate::mmap::{MemoryMap, SharedMemoryMap};
use crate::spec::*;
use std::path::Path;

#[derive(Clone, Debug)]
pub struct DomainInfo {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub name: String,
    pub node_count: u64,
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
    pub csr_row_off_offset: u64,
    pub csr_row_off_bytes: u64,
    pub csr_col_idx_offset: u64,
    pub csr_col_idx_bytes: u64,
    pub csc_row_off_offset: u64,
    pub csc_row_off_bytes: u64,
    pub csc_col_idx_offset: u64,
    pub csc_col_idx_bytes: u64,
    pub attributes: Vec<AttributeInfo>,
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

pub struct SnapshotReader {
    mmap: SharedMemoryMap,
    header: SnapshotHeader,
    domains: Vec<DomainInfo>,
    relations: Vec<RelationInfo>,
    metadata_offset: u64,
    metadata_bytes: u64,
}

impl SnapshotReader {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, ImpulseError> {
        let mmap = MemoryMap::open(path).map_err(|_| ImpulseError::IoFailure)?;
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
            return Err(ImpulseError::InvalidMagic);
        }

        if header.version() != IMPULSE_VERSION_PACKED && (header.version() >> 8) != IMPULSE_VERSION_MAJOR {
            return Err(ImpulseError::UnsupportedVersion);
        }

        let computed_crc = compute_crc16(&slice[0..0x3E]);
        if computed_crc != header.header_checksum() {
            return Err(ImpulseError::CorruptChecksum);
        }

        let dir_offset = if header.footer_directory_offset() > 0 {
            header.footer_directory_offset() as usize
        } else {
            header.data_offset() as usize
        };

        if dir_offset >= slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }

        let mut cur = dir_offset;

        // Parse Shared String Table
        if cur + 4 > slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }
        let string_table_bytes = u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
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

        // Parse Domain Catalog Entries Array
        let domain_count = header.domain_count() as usize;
        let mut domains = Vec::with_capacity(domain_count);

        for _d_idx in 0..domain_count {
            if cur + std::mem::size_of::<DomainCatalogEntry>() > slice.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let dom_entry = unsafe {
                std::ptr::read_unaligned(slice[cur..].as_ptr() as *const DomainCatalogEntry)
            };
            cur += std::mem::size_of::<DomainCatalogEntry>();

            let key_type = KeyType::from_u8(dom_entry.key_type()).ok_or(ImpulseError::InvalidArgument)?;
            let name = validate_and_get_string(dom_entry.name_offset() as usize)?;

            domains.push(DomainInfo {
                domain_id: dom_entry.domain_id(),
                key_type,
                name,
                node_count: dom_entry.node_count(),
            });
        }

        let rem = cur % 128;
        if rem != 0 {
            cur += 128 - rem;
        }

        // Parse Relation Directory Table
        let relation_count = header.relation_count() as usize;
        let mut relations = Vec::with_capacity(relation_count);

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
                csr_row_off_offset: rel_entry.csr_row_off_offset(),
                csr_row_off_bytes: rel_entry.csr_row_off_bytes(),
                csr_col_idx_offset: rel_entry.csr_col_idx_offset(),
                csr_col_idx_bytes: rel_entry.csr_col_idx_bytes(),
                csc_row_off_offset: rel_entry.csc_row_off_offset,
                csc_row_off_bytes: rel_entry.csc_row_off_bytes,
                csc_col_idx_offset: rel_entry.csc_col_idx_offset,
                csc_col_idx_bytes: rel_entry.csc_col_idx_bytes,
                attributes,
            });
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
                if footer_len <= slice.len() {
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
            metadata_offset,
            metadata_bytes,
        })
    }

    pub fn header(&self) -> &SnapshotHeader {
        &self.header
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

    pub fn get_relation_entry(&self, relation_index: u16) -> Option<&RelationInfo> {
        self.relations.get(relation_index as usize)
    }

    pub fn is_reachable(&self, relation_index: u16, src_id: u64, tgt_id: u64) -> bool {
        let rel = match self.relations.get(relation_index as usize) {
            Some(r) => r,
            None => return false,
        };

        if src_id >= rel.node_count {
            return false;
        }

        let slice = self.mmap.as_slice();
        let row_start = rel.csr_row_off_offset as usize;
        let col_start = rel.csr_col_idx_offset as usize;

        if row_start + rel.csr_row_off_bytes as usize > slice.len()
            || col_start + rel.csr_col_idx_bytes as usize > slice.len()
        {
            return false;
        }

        let row_offsets = unsafe {
            std::slice::from_raw_parts(
                slice[row_start..].as_ptr() as *const u32,
                rel.csr_row_off_bytes as usize / 4,
            )
        };

        let col_indices = unsafe {
            std::slice::from_raw_parts(
                slice[col_start..].as_ptr() as *const u32,
                rel.csr_col_idx_bytes as usize / 4,
            )
        };

        let u = src_id as usize;
        if u + 1 >= row_offsets.len() {
            return false;
        }

        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;

        if start > end || end > col_indices.len() {
            return false;
        }

        col_indices[start..end].iter().any(|&v| v as u64 == tgt_id)
    }

    pub fn is_adjacent(&self, relation_index: u16, src_id: u64, tgt_id: u64) -> Result<bool, ImpulseError> {
        Ok(self.is_reachable(relation_index, src_id, tgt_id))
    }

    pub fn get_neighbors(&self, relation_index: u16, src_id: u64) -> Result<Vec<u64>, ImpulseError> {
        let rel = self.relations.get(relation_index as usize).ok_or(ImpulseError::NotFound)?;
        if src_id >= rel.node_count {
            return Ok(Vec::new());
        }

        let slice = self.mmap.as_slice();
        let row_start = rel.csr_row_off_offset as usize;
        let col_start = rel.csr_col_idx_offset as usize;

        let row_offsets = unsafe {
            std::slice::from_raw_parts(
                slice[row_start..].as_ptr() as *const u32,
                rel.csr_row_off_bytes as usize / 4,
            )
        };

        let col_indices = unsafe {
            std::slice::from_raw_parts(
                slice[col_start..].as_ptr() as *const u32,
                rel.csr_col_idx_bytes as usize / 4,
            )
        };

        let u = src_id as usize;
        if u + 1 >= row_offsets.len() {
            return Ok(Vec::new());
        }

        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;

        if start > end || end > col_indices.len() {
            return Ok(Vec::new());
        }

        Ok(col_indices[start..end].iter().map(|&v| v as u64).collect())
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
            if cur + 2 > start + total { break; }
            let klen = u16::from_le_bytes(slice[cur..cur + 2].try_into().unwrap()) as usize;
            cur += 2;
            if cur + klen > start + total { break; }
            let key = std::str::from_utf8(&slice[cur..cur + klen]).unwrap_or("").to_string();
            cur += klen;

            if cur + 4 > start + total { break; }
            let vlen = u32::from_le_bytes(slice[cur..cur + 4].try_into().unwrap()) as usize;
            cur += 4;
            if cur + vlen > start + total { break; }
            let val = std::str::from_utf8(&slice[cur..cur + vlen]).unwrap_or("").to_string();
            cur += vlen;

            map.insert(key, val);
        }

        Ok(map)
    }
}
