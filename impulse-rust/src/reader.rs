//! Spec v2.4 High-Performance Zero-Copy Reader Engine

use crate::mmap::{MemoryMap, SharedMemoryMap};
use crate::spec::*;
use std::path::Path;

#[derive(Clone, Debug)]
pub struct DomainInfo {
    pub domain_id: u16,
    pub key_type: KeyType,
    pub node_count: u64,
    pub name: String,
    pub aux_sections_pos: u64,
    pub aux_sections_size: u64,
}

#[derive(Clone, Debug)]
pub struct RelationInfo {
    pub src_domain_id: u16,
    pub tgt_domain_id: u16,
    pub encoding_type: EncodingType,
    pub node_count: u64,
    pub edge_count: u64,
    pub csr_offsets_pos: u64,
    pub csr_offsets_size: u64,
    pub csr_targets_pos: u64,
    pub csr_targets_size: u64,
    pub aux_sections_pos: u64,
    pub aux_sections_size: u64,
    pub name: String,
}

pub struct SnapshotReader {
    mmap: SharedMemoryMap,
    header: SnapshotHeader,
    domains: Vec<DomainInfo>,
    relations: Vec<RelationInfo>,
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

        // Check version
        let version_major = header.version() >> 8;
        if version_major != IMPULSE_VERSION_MAJOR {
            return Err(ImpulseError::UnsupportedVersion);
        }

        // Verify Header CRC-32C
        let computed_crc = compute_header_crc32(slice);
        if computed_crc != header.header_crc32() {
            return Err(ImpulseError::CorruptChecksum);
        }

        // Verify Payload SHA-256
        let data_off = header.data_offset() as usize;
        if data_off < slice.len() {
            let payload = &slice[data_off..];
            let computed_sha = compute_sha256(payload);
            if computed_sha != header.payload_checksum() {
                return Err(ImpulseError::CorruptChecksum);
            }
        }

        // Check required feature flags (fail-closed)
        if (header.required_features() & IMPULSE_FEAT_SIGNED_ENFORCED) != 0 {
            return Err(ImpulseError::SignatureMismatch);
        }

        // Domain Catalog Table (O(1) fixed-size 64-byte entries)
        let domain_count = header.domain_count() as usize;
        let mut domains = Vec::with_capacity(domain_count);
        let mut cur = data_off;

        for _ in 0..domain_count {
            if cur + std::mem::size_of::<DomainCatalogEntry>() > slice.len() {
                break;
            }
            let entry = unsafe {
                std::ptr::read_unaligned(slice[cur..].as_ptr() as *const DomainCatalogEntry)
            };
            cur += std::mem::size_of::<DomainCatalogEntry>();

            let key_type = KeyType::from_u8(entry.key_type).ok_or(ImpulseError::InvalidArgument)?;
            let name_len = entry.name_length as usize;
            let name_off = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.name_offset)) } as usize;

            let name = if name_len > 0 && name_off > 0 && name_off + name_len <= slice.len() {
                std::str::from_utf8(&slice[name_off..name_off + name_len])
                    .unwrap_or("")
                    .to_string()
            } else {
                String::new()
            };

            let aux_pos = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.aux_sections_pos)) };
            let aux_sz = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.aux_sections_size)) };

            domains.push(DomainInfo {
                domain_id: entry.domain_id,
                key_type,
                node_count: entry.node_count,
                name,
                aux_sections_pos: aux_pos,
                aux_sections_size: aux_sz,
            });
        }

        // 64-byte align to Relation Directory Table
        let domain_table_bytes = domain_count * std::mem::size_of::<DomainCatalogEntry>();
        cur = data_off + domain_table_bytes;
        let rem = cur % 64;
        if rem != 0 {
            cur += 64 - rem;
        }

        // Relation Directory Table (O(1) fixed-size 128-byte entries)
        let relation_count = header.relation_count() as usize;
        let mut relations = Vec::with_capacity(relation_count);
        let rel_entry_size = if header.relation_dir_entry_size() > 0 {
            header.relation_dir_entry_size() as usize
        } else {
            128
        };

        for _ in 0..relation_count {
            if cur + std::mem::size_of::<RelationDirectoryEntry>() > slice.len() {
                break;
            }
            let entry = unsafe {
                std::ptr::read_unaligned(slice[cur..].as_ptr() as *const RelationDirectoryEntry)
            };
            cur += rel_entry_size;

            let encoding_type =
                EncodingType::from_u8(entry.encoding_type).ok_or(ImpulseError::InvalidArgument)?;

            let aux_pos = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.aux_sections_pos)) };
            let aux_sz = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.aux_sections_size)) };

            let name_len = entry.name_length as usize;
            let name_off = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(entry.name_offset)) } as usize;

            let name = if name_len > 0 && name_off > 0 && name_off + name_len <= slice.len() {
                std::str::from_utf8(&slice[name_off..name_off + name_len])
                    .unwrap_or("")
                    .to_string()
            } else {
                String::new()
            };

            relations.push(RelationInfo {
                src_domain_id: entry.src_domain_id,
                tgt_domain_id: entry.tgt_domain_id,
                encoding_type,
                node_count: entry.node_count,
                edge_count: entry.edge_count,
                csr_offsets_pos: entry.csr_offsets_pos,
                csr_offsets_size: entry.csr_offsets_size,
                csr_targets_pos: entry.csr_targets_pos,
                csr_targets_size: entry.csr_targets_size,
                aux_sections_pos: aux_pos,
                aux_sections_size: aux_sz,
                name,
            });
        }

        Ok(Self {
            mmap: shared,
            header,
            domains,
            relations,
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

        let offsets_buf = self.get_buffer(rel.csr_offsets_pos, rel.csr_offsets_size)?;
        let targets_buf = self.get_buffer(rel.csr_targets_pos, rel.csr_targets_size)?;

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

        let offsets_buf = self.get_buffer(rel.csr_offsets_pos, rel.csr_offsets_size)?;
        let targets_buf = self.get_buffer(rel.csr_targets_pos, rel.csr_targets_size)?;

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

    /// Read Fixed Node Property (AoS or SoA math)
    pub fn get_node_property(
        &self,
        domain_index: usize,
        node_id: u64,
        field_name: &str,
    ) -> Result<Option<&[u8]>, ImpulseError> {
        if domain_index >= self.domains.len() {
            return Err(ImpulseError::InvalidArgument);
        }
        let dom = &self.domains[domain_index];
        if node_id >= dom.node_count || dom.aux_sections_pos == 0 {
            return Ok(None);
        }

        self.get_property_from_aux(dom.aux_sections_pos, dom.aux_sections_size, node_id, field_name)
    }

    /// Read Fixed Edge Property (AoS or SoA math)
    pub fn get_edge_property(
        &self,
        relation_index: usize,
        edge_id: u64,
        field_name: &str,
    ) -> Result<Option<&[u8]>, ImpulseError> {
        if relation_index >= self.relations.len() {
            return Err(ImpulseError::InvalidArgument);
        }
        let rel = &self.relations[relation_index];
        if edge_id >= rel.edge_count || rel.aux_sections_pos == 0 {
            return Ok(None);
        }

        self.get_property_from_aux(rel.aux_sections_pos, rel.aux_sections_size, edge_id, field_name)
    }

    fn get_property_from_aux(
        &self,
        aux_pos: u64,
        aux_size: u64,
        element_id: u64,
        field_name: &str,
    ) -> Result<Option<&[u8]>, ImpulseError> {
        let aux_buf = self.get_buffer(aux_pos, aux_size)?;
        let entry_count = aux_buf.len() / std::mem::size_of::<AuxSectionEntry>();

        for i in 0..entry_count {
            let aux_entry = unsafe {
                std::ptr::read_unaligned(
                    aux_buf[i * std::mem::size_of::<AuxSectionEntry>()..].as_ptr()
                        as *const AuxSectionEntry,
                )
            };

            let sec_type = AuxSectionType::from_u16(aux_entry.section_type);
            if sec_type == Some(AuxSectionType::NodePropsFixed)
                || sec_type == Some(AuxSectionType::EdgePropsFixed)
            {
                let prop_buf = self.get_buffer(aux_entry.offset, aux_entry.size)?;
                if prop_buf.len() < std::mem::size_of::<PropBlockHeader>() {
                    continue;
                }

                let hdr = unsafe {
                    std::ptr::read_unaligned(prop_buf.as_ptr() as *const PropBlockHeader)
                };

                let target_hash = fnv1a_hash(field_name);
                let desc_start = std::mem::size_of::<PropBlockHeader>();
                let field_count = hdr.field_count as usize;

                for f in 0..field_count {
                    let desc_pos = desc_start + f * std::mem::size_of::<FieldDescriptor>();
                    if desc_pos + std::mem::size_of::<FieldDescriptor>() > prop_buf.len() {
                        break;
                    }

                    let desc = unsafe {
                        std::ptr::read_unaligned(
                            prop_buf[desc_pos..].as_ptr() as *const FieldDescriptor
                        )
                    };

                    if desc.name_hash == target_hash {
                        let data_start = desc_start + field_count * std::mem::size_of::<FieldDescriptor>();
                        let fsize = desc.field_size as usize;

                        if hdr.layout == 0 {
                            // AoS Layout: DataStart + (idx * record_size) + offset_in_record
                            let offset = data_start
                                + (element_id as usize) * (hdr.record_size as usize)
                                + (desc.offset_in_record as usize);

                            if offset + fsize <= prop_buf.len() {
                                return Ok(Some(&prop_buf[offset..offset + fsize]));
                            }
                        } else {
                            // SoA Layout: DataStart + column_offset + (idx * field_size)
                            let offset = data_start
                                + (desc.column_offset as usize)
                                + (element_id as usize) * fsize;

                            if offset + fsize <= prop_buf.len() {
                                return Ok(Some(&prop_buf[offset..offset + fsize]));
                            }
                        }
                    }
                }
            }
        }

        Ok(None)
    }
}
