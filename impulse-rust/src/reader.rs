//! Spec v0.9.0 High-Performance Zero-Copy Reader Engine

use crate::mmap::{MemoryMap, SharedMemoryMap};
use crate::spec::*;
use std::path::Path;

#[derive(Clone, Debug)]
pub struct DomainInfo {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub name: String,
}

#[derive(Clone, Debug)]
pub struct RelationInfo {
    pub relation_id: u16,
    pub src_domain_id: u16,
    pub tgt_domain_id: u16,
    pub encoding_id: u8,
    pub node_id_width: u8,
    pub edge_index_width: u8,
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

        // Parse header struct
        let header = unsafe { std::ptr::read_unaligned(slice.as_ptr() as *const SnapshotHeader) };

        if header.magic() != IMPULSE_MAGIC {
            return Err(ImpulseError::InvalidMagic);
        }

        // Check version (0.9.0 packed is 9)
        if header.version() != IMPULSE_VERSION_PACKED && (header.version() >> 8) != IMPULSE_VERSION_MAJOR {
            return Err(ImpulseError::UnsupportedVersion);
        }

        // Verify Header CRC-16
        let computed_crc = compute_crc16(&slice[0..0x3E]);
        if computed_crc != header.header_checksum() {
            return Err(ImpulseError::CorruptChecksum);
        }

        // Determine Catalog Directory offset (Page 1 by default, or Footer Directory)
        let dir_offset = if header.footer_directory_offset() > 0 {
            header.footer_directory_offset() as usize
        } else {
            header.data_offset() as usize
        };

        if dir_offset >= slice.len() {
            return Err(ImpulseError::BufferOverflow);
        }

        // Parse Domain Catalog
        let domain_count = header.domain_count() as usize;
        let mut domains = Vec::with_capacity(domain_count);
        let mut cur = dir_offset;

        for _d_idx in 0..domain_count {
            if cur + std::mem::size_of::<DomainCatalogEntryHeader>() > slice.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let dom_hdr = unsafe {
                std::ptr::read_unaligned(slice[cur..].as_ptr() as *const DomainCatalogEntryHeader)
            };
            cur += std::mem::size_of::<DomainCatalogEntryHeader>();

            let key_type = KeyType::from_u8(dom_hdr.key_type).ok_or(ImpulseError::InvalidArgument)?;
            let name_len = dom_hdr.name_len as usize;

            if cur + name_len > slice.len() {
                return Err(ImpulseError::BufferOverflow);
            }
            let name = std::str::from_utf8(&slice[cur..cur + name_len])
                .map_err(|_| ImpulseError::InvalidArgument)?
                .to_string();
            cur += name_len;

            domains.push(DomainInfo {
                domain_id: dom_hdr.domain_id,
                key_type,
                name,
            });
        }

        // 128-byte align to Relation Directory Table
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

                let name_len = attr_desc.name_len as usize;
                if cur + name_len > slice.len() {
                    return Err(ImpulseError::BufferOverflow);
                }
                let attr_name = std::str::from_utf8(&slice[cur..cur + name_len])
                    .map_err(|_| ImpulseError::InvalidArgument)?
                    .to_string();
                cur += name_len;

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

            relations.push(RelationInfo {
                relation_id: rel_entry.relation_id(),
                src_domain_id: rel_entry.src_domain_id(),
                tgt_domain_id: rel_entry.tgt_domain_id(),
                encoding_id: rel_entry.encoding_id(),
                node_id_width: rel_entry.node_id_width(),
                edge_index_width: rel_entry.edge_index_width(),
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

        // Determine metadata location (Footer Block)
        let mut metadata_offset = 0;
        let mut metadata_bytes = 0;

        if slice.len() >= 16 {
            let trailer_pos = slice.len() - 16;
            let trailer = unsafe {
                std::ptr::read_unaligned(slice[trailer_pos..].as_ptr() as *const FooterTrailer)
            };
            if trailer.footer_magic() == IMPULSE_MAGIC {
                let footer_len = trailer.footer_length() as usize;
                if footer_len > 16 && footer_len <= slice.len() {
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

    pub fn domain_count(&self) -> usize {
        self.domains.len()
    }

    pub fn relation_count(&self) -> usize {
        self.relations.len()
    }

    pub fn domains(&self) -> &[DomainInfo] {
        &self.domains
    }

    pub fn relations(&self) -> &[RelationInfo] {
        &self.relations
    }

    pub fn get_buffer(&self, offset: u64, size: u64) -> Result<&[u8], ImpulseError> {
        let slice = self.mmap.as_slice();
        let off = offset as usize;
        let sz = size as usize;

        if off > slice.len() || sz > slice.len() - off {
            return Err(ImpulseError::BufferOverflow);
        }
        Ok(&slice[off..off + sz])
    }

    /// Direct single-hop adjacency check
    pub fn is_adjacent(
        &self,
        relation_index: usize,
        src_id: u64,
        tgt_id: u64,
    ) -> Result<bool, ImpulseError> {
        if relation_index >= self.relations.len() {
            return Err(ImpulseError::InvalidArgument);
        }

        let rel = &self.relations[relation_index];
        if src_id >= rel.node_count {
            return Ok(false);
        }

        let offsets_buf = self.get_buffer(rel.csr_row_off_offset, rel.csr_row_off_bytes)?;
        let targets_buf = self.get_buffer(rel.csr_col_idx_offset, rel.csr_col_idx_bytes)?;

        let row_offsets: &[u32] = unsafe {
            std::slice::from_raw_parts(
                offsets_buf.as_ptr() as *const u32,
                offsets_buf.len() / 4,
            )
        };
        let col_indices: &[u32] = unsafe {
            std::slice::from_raw_parts(
                targets_buf.as_ptr() as *const u32,
                targets_buf.len() / 4,
            )
        };

        let u = src_id as usize;
        if u + 1 >= row_offsets.len() {
            return Ok(false);
        }

        let start_idx = row_offsets[u] as usize;
        let end_idx = row_offsets[u + 1] as usize;

        if start_idx > end_idx || end_idx > col_indices.len() {
            return Err(ImpulseError::CorruptChecksum);
        }

        for &tgt in &col_indices[start_idx..end_idx] {
            if tgt as u64 == tgt_id {
                return Ok(true);
            }
        }

        Ok(false)
    }

    /// Zero-copy neighbor slice accessor
    pub fn get_neighbors(
        &self,
        relation_index: usize,
        node_id: u64,
    ) -> Result<&[u32], ImpulseError> {
        if relation_index >= self.relations.len() {
            return Err(ImpulseError::InvalidArgument);
        }

        let rel = &self.relations[relation_index];
        if node_id >= rel.node_count {
            return Ok(&[]);
        }

        let offsets_buf = self.get_buffer(rel.csr_row_off_offset, rel.csr_row_off_bytes)?;
        let targets_buf = self.get_buffer(rel.csr_col_idx_offset, rel.csr_col_idx_bytes)?;

        let row_offsets: &[u32] = unsafe {
            std::slice::from_raw_parts(
                offsets_buf.as_ptr() as *const u32,
                offsets_buf.len() / 4,
            )
        };
        let col_indices: &[u32] = unsafe {
            std::slice::from_raw_parts(
                targets_buf.as_ptr() as *const u32,
                targets_buf.len() / 4,
            )
        };

        let u = node_id as usize;
        if u + 1 >= row_offsets.len() {
            return Ok(&[]);
        }

        let start_idx = row_offsets[u] as usize;
        let end_idx = row_offsets[u + 1] as usize;

        if start_idx > end_idx || end_idx > col_indices.len() {
            return Err(ImpulseError::CorruptChecksum);
        }

        Ok(&col_indices[start_idx..end_idx])
    }

    /// Access zero-copy CSR row offsets array slice for a relation
    pub fn get_row_offsets(&self, relation_index: usize) -> Result<&[u32], ImpulseError> {
        if relation_index >= self.relations.len() {
            return Err(ImpulseError::InvalidArgument);
        }
        let rel = &self.relations[relation_index];
        let offsets_buf = self.get_buffer(rel.csr_row_off_offset, rel.csr_row_off_bytes)?;
        let row_offsets: &[u32] = unsafe {
            std::slice::from_raw_parts(
                offsets_buf.as_ptr() as *const u32,
                offsets_buf.len() / 4,
            )
        };
        Ok(row_offsets)
    }

    /// Access zero-copy CSR column targets array slice for a relation
    pub fn get_col_indices(&self, relation_index: usize) -> Result<&[u32], ImpulseError> {
        if relation_index >= self.relations.len() {
            return Err(ImpulseError::InvalidArgument);
        }
        let rel = &self.relations[relation_index];
        let targets_buf = self.get_buffer(rel.csr_col_idx_offset, rel.csr_col_idx_bytes)?;
        let elem_size = match rel.node_id_width {
            2 => 2,
            8 => 8,
            _ => 4,
        };
        let col_indices: &[u32] = unsafe {
            std::slice::from_raw_parts(
                targets_buf.as_ptr() as *const u32,
                targets_buf.len() / elem_size,
            )
        };
        Ok(col_indices)
    }

    /// Retrieve Metadata map from Footer Block
    pub fn get_metadata(&self) -> Result<std::collections::HashMap<String, String>, ImpulseError> {
        if self.metadata_offset == 0 || self.metadata_bytes == 0 {
            return Ok(std::collections::HashMap::new());
        }
        let buf = self.get_buffer(self.metadata_offset, self.metadata_bytes)?;
        decode_metadata_map(buf)
    }
}
