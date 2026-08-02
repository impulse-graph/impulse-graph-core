//! Spec v2.4 Constants, Binary Layout Structs, Enums, and Hashing Primitives

use std::fmt;

// Helper macro for static assertion (must be defined before structs)
#[macro_export]
macro_rules! const_assert_eq {
    ($left:expr, $right:expr) => {
        const _: () = assert!($left == $right);
    };
}

pub const IMPULSE_MAGIC: u32 = 0x494D5053; // "IMPS" Little-Endian
pub const IMPULSE_VERSION_MAJOR: u16 = 2;
pub const IMPULSE_VERSION_MINOR: u16 = 4;
pub const IMPULSE_VERSION_PACKED: u16 = (IMPULSE_VERSION_MAJOR << 8) | IMPULSE_VERSION_MINOR;
pub const IMPULSE_DEFAULT_DATA_OFFSET: u32 = 4096;

// Domain Catalog Key Type Enums
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyType {
    Int16 = 0x00,
    Int32 = 0x01,
    Int64 = 0x02,
    Uuid = 0x03,
    String = 0x04,
}

impl KeyType {
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            0x00 => Some(KeyType::Int16),
            0x01 => Some(KeyType::Int32),
            0x02 => Some(KeyType::Int64),
            0x03 => Some(KeyType::Uuid),
            0x04 => Some(KeyType::String),
            _ => None,
        }
    }
}

// Topology Encodings
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EncodingType {
    RawUint32 = 0x00,
    DeltaVbyte = 0x01,
    RawUint16 = 0x02,
    Hybrid1632 = 0x03,
    SimdComp = 0x04,
    SlicedEllpack = 0x05,
    TpuBcoo = 0x06,
    RawUint64 = 0x07,
    RoaringBitmap = 0x08,
}

impl EncodingType {
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            0x00 => Some(EncodingType::RawUint32),
            0x01 => Some(EncodingType::DeltaVbyte),
            0x02 => Some(EncodingType::RawUint16),
            0x03 => Some(EncodingType::Hybrid1632),
            0x04 => Some(EncodingType::SimdComp),
            0x05 => Some(EncodingType::SlicedEllpack),
            0x06 => Some(EncodingType::TpuBcoo),
            0x07 => Some(EncodingType::RawUint64),
            0x08 => Some(EncodingType::RoaringBitmap),
            _ => None,
        }
    }
}

// Data Types
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DataType {
    Int8 = 0x00,
    Int16 = 0x01,
    Int32 = 0x02,
    Int64 = 0x03,
    Uint8 = 0x04,
    Uint16 = 0x05,
    Uint32 = 0x06,
    Uint64 = 0x07,
    Float16 = 0x08,
    Float32 = 0x09,
    Float64 = 0x0A,
    Bool8 = 0x0B,
    Uuid128 = 0x0C,
    FixedBytes = 0x0D,
}

impl DataType {
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            0x00 => Some(DataType::Int8),
            0x01 => Some(DataType::Int16),
            0x02 => Some(DataType::Int32),
            0x03 => Some(DataType::Int64),
            0x04 => Some(DataType::Uint8),
            0x05 => Some(DataType::Uint16),
            0x06 => Some(DataType::Uint32),
            0x07 => Some(DataType::Uint64),
            0x08 => Some(DataType::Float16),
            0x09 => Some(DataType::Float32),
            0x0A => Some(DataType::Float64),
            0x0B => Some(DataType::Bool8),
            0x0C => Some(DataType::Uuid128),
            0x0D => Some(DataType::FixedBytes),
            _ => None,
        }
    }

    pub fn default_size(&self) -> usize {
        match self {
            DataType::Int8 | DataType::Uint8 | DataType::Bool8 => 1,
            DataType::Int16 | DataType::Uint16 | DataType::Float16 => 2,
            DataType::Int32 | DataType::Uint32 | DataType::Float32 => 4,
            DataType::Int64 | DataType::Uint64 | DataType::Float64 => 8,
            DataType::Uuid128 => 16,
            DataType::FixedBytes => 1,
        }
    }
}

// Auxiliary Section Types
#[repr(u16)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AuxSectionType {
    CscOffsets = 0x0001,
    CscTargets = 0x0002,
    CscEdgeMap = 0x0003,
    EdgeWeights = 0x0004,
    EdgeTimestamps = 0x0005,
    EdgePropsFixed = 0x0006,
    EdgePropsVar = 0x0007,
    NodePropsFixed = 0x0008,
    NodePropsVar = 0x0009,
    IdMap = 0x000A,
    ZstdDict = 0x000C,
}

impl AuxSectionType {
    pub fn from_u16(val: u16) -> Option<Self> {
        match val {
            0x0001 => Some(AuxSectionType::CscOffsets),
            0x0002 => Some(AuxSectionType::CscTargets),
            0x0003 => Some(AuxSectionType::CscEdgeMap),
            0x0004 => Some(AuxSectionType::EdgeWeights),
            0x0005 => Some(AuxSectionType::EdgeTimestamps),
            0x0006 => Some(AuxSectionType::EdgePropsFixed),
            0x0007 => Some(AuxSectionType::EdgePropsVar),
            0x0008 => Some(AuxSectionType::NodePropsFixed),
            0x0009 => Some(AuxSectionType::NodePropsVar),
            0x000A => Some(AuxSectionType::IdMap),
            0x000C => Some(AuxSectionType::ZstdDict),
            _ => None,
        }
    }
}

// Global Feature Flags
pub const IMPULSE_FEAT_WIDE_NODE_IDS: u64 = 1 << 0;
pub const IMPULSE_FEAT_SECTION_DIRECTORY: u64 = 1 << 1;
pub const IMPULSE_FEAT_SIGNED_ENFORCED: u64 = 1 << 2;
pub const IMPULSE_FEAT_4KB_PAGE_ALIGNED: u64 = 1 << 3;

pub const IMPULSE_COMPAT_PAGE_ALIGNED: u64 = 1 << 0;
pub const IMPULSE_COMPAT_SIGNED: u64 = 1 << 1;
pub const IMPULSE_COMPAT_ZSTD_DICT: u64 = 1 << 2;

// Error Codes
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImpulseError {
    Ok = 0,
    InvalidMagic = 1,
    UnsupportedVersion = 2,
    UnsupportedGlobalFeature = 3,
    UnsupportedSectionFeature = 4,
    CorruptChecksum = 5,
    IoFailure = 6,
    InvalidArgument = 7,
    SignatureMismatch = 8,
    BufferOverflow = 9,
    NotFound = 10,
}

impl fmt::Display for ImpulseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl std::error::Error for ImpulseError {}

// ---------------------------------------------------------------------------
// Binary Layout Structs (#repr(C, packed))
// ---------------------------------------------------------------------------

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct SnapshotHeader {
    pub magic: u32,                  // 0x00..0x03
    pub version: u16,                // 0x04..0x05
    pub data_offset: u32,            // 0x06..0x09
    pub domain_count: u16,           // 0x0A..0x0B
    pub relation_count: u16,         // 0x0C..0x0D
    pub kafka_offset: u64,           // 0x0E..0x15
    pub timestamp_ms: u64,           // 0x16..0x1D
    pub payload_checksum: [u8; 32],  // 0x1E..0x3D
    pub reserved: u16,               // 0x3E..0x3F
    pub required_features: u64,      // 0x40..0x47
    pub sig_block: [u8; 1024],       // 0x48..0x447
    pub compat_features: u64,        // 0x448..0x44F
    pub total_file_size: u64,        // 0x450..0x457
    pub header_crc32: u32,           // 0x458..0x45B
    pub section_dir_count: u16,      // 0x45C..0x45D
    pub string_table_encoding: u16,  // 0x45E..0x45F
    pub section_dir_offset: u64,     // 0x460..0x467
    pub relation_dir_entry_size: u16,// 0x468..0x469 (128)
    pub domain_dir_entry_size: u16,  // 0x46A..0x46B (64)
    pub reserved2: u32,              // 0x46C..0x46F
    pub header_padding: [u8; 2960],  // 0x470..0xFFF (Pads to 4096)
}

impl SnapshotHeader {
    pub fn magic(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.magic)) }
    }
    pub fn version(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.version)) }
    }
    pub fn data_offset(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.data_offset)) }
    }
    pub fn domain_count(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.domain_count)) }
    }
    pub fn relation_count(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.relation_count)) }
    }
    pub fn required_features(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.required_features)) }
    }
    pub fn header_crc32(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.header_crc32)) }
    }
    pub fn payload_checksum(&self) -> [u8; 32] {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.payload_checksum)) }
    }
    pub fn relation_dir_entry_size(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.relation_dir_entry_size)) }
    }
}

const_assert_eq!(std::mem::size_of::<SnapshotHeader>(), 4096);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct DomainCatalogEntry {
    pub domain_id: u16,              // 0x00
    pub key_type: u8,                // 0x02
    pub reserved1: u8,               // 0x03
    pub node_count: u64,             // 0x04
    pub required_features: u64,      // 0x0C
    pub compat_features: u64,        // 0x14
    pub aux_sections_pos: u64,       // 0x1C
    pub aux_sections_size: u64,      // 0x24
    pub name_offset: u32,            // 0x2C
    pub name_length: u16,            // 0x30
    pub reserved2: [u8; 14],         // 0x32
}

const_assert_eq!(std::mem::size_of::<DomainCatalogEntry>(), 64);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct RelationDirectoryEntry {
    pub src_domain_id: u16,          // 0x00
    pub tgt_domain_id: u16,          // 0x02
    pub encoding_type: u8,          // 0x04
    pub node_count: u64,             // 0x05
    pub edge_count: u64,             // 0x0D
    pub required_features: u64,      // 0x15
    pub compat_features: u64,        // 0x1D
    pub csr_offsets_pos: u64,        // 0x25
    pub csr_offsets_size: u64,       // 0x2D
    pub csr_targets_pos: u64,        // 0x35
    pub csr_targets_size: u64,       // 0x3D
    pub aux_sections_pos: u64,       // 0x45
    pub aux_sections_size: u64,      // 0x4D
    pub name_offset: u32,            // 0x55
    pub name_length: u16,            // 0x59
    pub tgt_node_count_lo16: u16,    // 0x5B
    pub reserved: [u8; 35],          // 0x5D..0x7F (93 + 35 = 128 bytes)
}

const_assert_eq!(std::mem::size_of::<RelationDirectoryEntry>(), 128);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct AuxSectionEntry {
    pub section_type: u16,           // 0x00
    pub flags: u16,                  // 0x02
    pub reserved: u32,               // 0x04
    pub offset: u64,                 // 0x08
    pub size: u64,                   // 0x10
}

const_assert_eq!(std::mem::size_of::<AuxSectionEntry>(), 24);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct FieldDescriptor {
    pub name: [u8; 32],              // 0x00
    pub data_type: u8,               // 0x20
    pub field_size: u8,              // 0x21
    pub offset_in_record: u16,       // 0x22
    pub column_index: u16,           // 0x24
    pub flags: u16,                  // 0x26
    pub name_hash: u32,              // 0x28
    pub column_offset: u64,          // 0x2C
}

const_assert_eq!(std::mem::size_of::<FieldDescriptor>(), 52);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct PropBlockHeader {
    pub field_count: u16,            // 0x00
    pub record_size: u32,            // 0x02
    pub layout: u8,                  // 0x06 (0 = AoS, 1 = SoA)
    pub reserved1: u8,               // 0x07
    pub element_count: u64,          // 0x08
}

const_assert_eq!(std::mem::size_of::<PropBlockHeader>(), 16);

// ---------------------------------------------------------------------------
// Pure Rust Hashing Utilities (SHA-256 and CRC-32C)
// ---------------------------------------------------------------------------

pub fn fnv1a_hash(name: &str) -> u32 {
    let mut hash: u32 = 0x811c9dc5;
    for byte in name.as_bytes() {
        hash ^= *byte as u32;
        hash = hash.wrapping_mul(0x01000193);
    }
    hash
}

pub fn compute_sha256(data: &[u8]) -> [u8; 32] {
    let mut state: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];
    let k: [u32; 64] = [
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    ];

    let bit_len = (data.len() as u64) * 8;
    let mut padded = data.to_vec();
    padded.push(0x80);
    while (padded.len() % 64) != 56 {
        padded.push(0x00);
    }
    padded.extend_from_slice(&bit_len.to_be_bytes());

    for chunk in padded.chunks_exact(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([chunk[i*4], chunk[i*4+1], chunk[i*4+2], chunk[i*4+3]]);
        }
        for i in 16..64 {
            let s0 = w[i-15].rotate_right(7) ^ w[i-15].rotate_right(18) ^ (w[i-15] >> 3);
            let s1 = w[i-2].rotate_right(17) ^ w[i-2].rotate_right(19) ^ (w[i-2] >> 10);
            w[i] = w[i-16].wrapping_add(s0).wrapping_add(w[i-7]).wrapping_add(s1);
        }

        let mut a = state[0]; let mut b = state[1]; let mut c = state[2]; let mut d = state[3];
        let mut e = state[4]; let mut f = state[5]; let mut g = state[6]; let mut h = state[7];

        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let temp1 = h.wrapping_add(s1).wrapping_add(ch).wrapping_add(k[i]).wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0.wrapping_add(maj);

            h = g; g = f; f = e; e = d.wrapping_add(temp1);
            d = c; c = b; b = a; a = temp1.wrapping_add(temp2);
        }

        state[0] = state[0].wrapping_add(a);
        state[1] = state[1].wrapping_add(b);
        state[2] = state[2].wrapping_add(c);
        state[3] = state[3].wrapping_add(d);
        state[4] = state[4].wrapping_add(e);
        state[5] = state[5].wrapping_add(f);
        state[6] = state[6].wrapping_add(g);
        state[7] = state[7].wrapping_add(h);
    }

    let mut out = [0u8; 32];
    for i in 0..8 {
        out[i*4..(i+1)*4].copy_from_slice(&state[i].to_be_bytes());
    }
    out
}

pub fn compute_crc32c(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFFFFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            if (crc & 1) != 0 {
                crc = (crc >> 1) ^ 0x82F63B78;
            } else {
                crc >>= 1;
            }
        }
    }
    !crc
}

/// Compute Header CRC-32C over structural fields excluding sig_block and header_crc32 itself
pub fn compute_header_crc32(header_bytes: &[u8]) -> u32 {
    assert!(header_bytes.len() >= 4096);
    // Part 1: bytes 0x00..0x47 (72 bytes)
    // Part 2: bytes 0x448..0x457 (16 bytes)
    let mut buffer = Vec::with_capacity(88);
    buffer.extend_from_slice(&header_bytes[0x00..0x48]);
    buffer.extend_from_slice(&header_bytes[0x448..0x458]);
    compute_crc32c(&buffer)
}
