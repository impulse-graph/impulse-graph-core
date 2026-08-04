//! Spec v0.9.0 Constants, Binary Layout Structs, Enums, and Hashing Primitives

use std::fmt;

// Helper macro for static assertion
#[macro_export]
macro_rules! const_assert_eq {
    ($left:expr, $right:expr) => {
        const _: () = assert!($left == $right);
    };
}

pub const IMPULSE_MAGIC: u32 = 0x494D5053; // "IMPS" Little-Endian
pub const IMPULSE_VERSION_MAJOR: u16 = 0;
pub const IMPULSE_VERSION_MINOR: u16 = 9;
pub const IMPULSE_VERSION_PACKED: u16 = 9; // v0.9.0 packed as 9
pub const IMPULSE_DEFAULT_DATA_OFFSET: u32 = 4096;

// Alignment Constants & Helpers
pub const IMPULSE_ALIGN_SIMD: u64 = 128;
pub const IMPULSE_ALIGN_PAGE: u64 = 4096;

#[inline]
pub fn align_128(val: u64) -> u64 {
    (val + 127) & !127
}

#[inline]
pub fn align_4k(val: u64) -> u64 {
    (val + 4095) & !4095
}

// Domain Catalog Key Type Enums
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyType {
    Int8 = 0x01,
    Int16 = 0x02,
    Int32 = 0x03,
    Int64 = 0x04,
    Uuid = 0x0A,
    String = 0x0B,
}

impl KeyType {
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            0x01 => Some(KeyType::Int8),
            0x02 => Some(KeyType::Int16),
            0x03 => Some(KeyType::Int32),
            0x04 => Some(KeyType::Int64),
            0x0A => Some(KeyType::Uuid),
            0x0B => Some(KeyType::String),
            _ => None,
        }
    }
}

// Topology Encodings (v0.9.0: Standard RAW zero-copy is 0x00)
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EncodingType {
    Raw = 0x00,
    ZstdFrame = 0x01,
    CustomVendor(u8),
}

impl EncodingType {
    pub fn from_u8(val: u8) -> Self {
        match val {
            0x00 => EncodingType::Raw,
            0x01 => EncodingType::ZstdFrame,
            v => EncodingType::CustomVendor(v),
        }
    }

    pub fn to_u8(&self) -> u8 {
        match self {
            EncodingType::Raw => 0x00,
            EncodingType::ZstdFrame => 0x01,
            EncodingType::CustomVendor(v) => *v,
        }
    }
}

// Attribute Data Types & Nullability Flags
pub const IMPULSE_TYPE_MASK: u8 = 0x7F;
pub const IMPULSE_NULLABLE_FLAG: u8 = 0x80;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BaseDataType {
    Int8 = 0x01,
    Int16 = 0x02,
    Int32 = 0x03,
    Int64 = 0x04,
    Float16 = 0x05,
    Float32 = 0x06,
    Float64 = 0x07,
    TimestampMs = 0x08,
    TimestampNs = 0x09,
    FixedBytes = 0x0A,
    VarString = 0x0B,
    VarBytes = 0x0C,
}

impl BaseDataType {
    pub fn from_u8(val: u8) -> Option<Self> {
        let base = val & IMPULSE_TYPE_MASK;
        match base {
            0x01 => Some(BaseDataType::Int8),
            0x02 => Some(BaseDataType::Int16),
            0x03 => Some(BaseDataType::Int32),
            0x04 => Some(BaseDataType::Int64),
            0x05 => Some(BaseDataType::Float16),
            0x06 => Some(BaseDataType::Float32),
            0x07 => Some(BaseDataType::Float64),
            0x08 => Some(BaseDataType::TimestampMs),
            0x09 => Some(BaseDataType::TimestampNs),
            0x0A => Some(BaseDataType::FixedBytes),
            0x0B => Some(BaseDataType::VarString),
            0x0C => Some(BaseDataType::VarBytes),
            _ => None,
        }
    }

    pub fn element_byte_size(&self) -> usize {
        match self {
            BaseDataType::Int8 => 1,
            BaseDataType::Int16 | BaseDataType::Float16 => 2,
            BaseDataType::Int32 | BaseDataType::Float32 => 4,
            BaseDataType::Int64 | BaseDataType::Float64 | BaseDataType::TimestampMs | BaseDataType::TimestampNs => 8,
            BaseDataType::FixedBytes => 1,
            BaseDataType::VarString | BaseDataType::VarBytes => 0,
        }
    }

    pub fn is_variable(&self) -> bool {
        matches!(self, BaseDataType::VarString | BaseDataType::VarBytes)
    }
}

// Global Feature Flags
pub const IMPULSE_FEAT_4KB_PAGE_ALIGNED: u64 = 1 << 0;
pub const IMPULSE_FEAT_CRYPTO_SIGNED: u64 = 1 << 1;

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
    InvalidAlignment = 11,
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
    pub magic: u32,                     // 0x00..0x03 ("IMPS" = 0x494D5053)
    pub version: u16,                   // 0x04..0x05 (0x0009)
    pub data_offset: u32,               // 0x06..0x09 (4096)
    pub domain_count: u16,              // 0x0A..0x0B
    pub relation_count: u16,            // 0x0C..0x0D
    pub timestamp_ms: u64,              // 0x0E..0x15
    pub required_features: u64,         // 0x16..0x1D
    pub footer_directory_offset: u64,   // 0x1E..0x25
    pub footer_directory_bytes: u64,    // 0x26..0x2D
    pub snapshot_uuid: [u8; 16],        // 0x2E..0x3D
    pub header_checksum: u16,           // 0x3E..0x3F (CRC-16-CCITT)
    pub header_padding: [u8; 4032],     // 0x40..0xFFF (Pads to 4096)
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
    pub fn footer_directory_offset(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.footer_directory_offset)) }
    }
    pub fn footer_directory_bytes(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.footer_directory_bytes)) }
    }
    pub fn header_checksum(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.header_checksum)) }
    }
}

const_assert_eq!(std::mem::size_of::<SnapshotHeader>(), 4096);

#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct FooterTrailer {
    pub footer_length: u64, // 0x00..0x07 (Byte size of Footer Block)
    pub spec_version: u32,  // 0x08..0x0B (0x0009)
    pub footer_magic: u32,  // 0x0C..0x0F ("IMPS" = 0x494D5053)
}

impl FooterTrailer {
    pub fn footer_length(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.footer_length)) }
    }
    pub fn spec_version(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.spec_version)) }
    }
    pub fn footer_magic(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.footer_magic)) }
    }
}

const_assert_eq!(std::mem::size_of::<FooterTrailer>(), 16);

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct DomainCatalogEntry {
    pub domain_id: u16,     // 0x00..0x01
    pub key_type: u8,       // 0x02
    pub reserved: u8,       // 0x03
    pub name_offset: u32,   // 0x04..0x07 (Offset into Shared String Table)
    pub node_count: u64,    // 0x08..0x0F
}

impl DomainCatalogEntry {
    pub fn domain_id(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.domain_id)) }
    }
    pub fn key_type(&self) -> u8 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.key_type)) }
    }
    pub fn name_offset(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.name_offset)) }
    }
    pub fn node_count(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.node_count)) }
    }
}

const_assert_eq!(std::mem::size_of::<DomainCatalogEntry>(), 16);

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct RelationDirectoryEntry {
    pub relation_id: u16,       // 0x00..0x01
    pub src_domain_id: u16,     // 0x02..0x03
    pub tgt_domain_id: u16,     // 0x04..0x05
    pub encoding_id: u8,        // 0x06
    pub node_id_width: u8,      // 0x07 (2 = u16, 4 = u32, 8 = u64)
    pub edge_index_width: u8,   // 0x08 (4 = u32, 8 = u64)
    pub reserved1: [u8; 3],     // 0x09..0x0B
    pub name_offset: u32,       // 0x0C..0x0F (Offset into Shared String Table)
    pub node_count: u64,        // 0x10..0x17
    pub edge_count: u64,        // 0x18..0x1F
    pub section_features: u64,  // 0x20..0x27
    pub csr_row_off_offset: u64,// 0x28..0x2F
    pub csr_row_off_bytes: u64, // 0x30..0x37
    pub csr_col_idx_offset: u64,// 0x38..0x3F
    pub csr_col_idx_bytes: u64, // 0x40..0x47
    pub csc_row_off_offset: u64,// 0x48..0x4F
    pub csc_row_off_bytes: u64, // 0x50..0x57
    pub csc_col_idx_offset: u64,// 0x58..0x5F
    pub csc_col_idx_bytes: u64, // 0x60..0x67
    pub attr_count: u16,        // 0x68..0x69
    pub reserved2: [u8; 22],    // 0x6A..0x7F (Pads struct to exactly 128 Bytes)
}

impl RelationDirectoryEntry {
    pub fn relation_id(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.relation_id)) }
    }
    pub fn src_domain_id(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.src_domain_id)) }
    }
    pub fn tgt_domain_id(&self) -> u16 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.tgt_domain_id)) }
    }
    pub fn encoding_id(&self) -> u8 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.encoding_id)) }
    }
    pub fn name_offset(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.name_offset)) }
    }
    pub fn node_id_width(&self) -> u8 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.node_id_width)) }
    }
    pub fn edge_index_width(&self) -> u8 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.edge_index_width)) }
    }
    pub fn node_count(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.node_count)) }
    }
    pub fn edge_count(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.edge_count)) }
    }
    pub fn csr_row_off_offset(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.csr_row_off_offset)) }
    }
    pub fn csr_row_off_bytes(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.csr_row_off_bytes)) }
    }
    pub fn csr_col_idx_offset(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.csr_col_idx_offset)) }
    }
    pub fn csr_col_idx_bytes(&self) -> u64 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.csr_col_idx_bytes)) }
    }
}

const_assert_eq!(std::mem::size_of::<RelationDirectoryEntry>(), 128);

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct AttributeDescriptor {
    pub name_offset: u32,       // 0x00..0x03 (Offset into Shared String Table)
    pub type_code: u8,          // 0x04 (Base type + 0x80 Nullable flag)
    pub reserved1: u8,          // 0x05
    pub reserved2: u16,         // 0x06..0x07
    pub dimension: u32,         // 0x08..0x0B (1 for scalar, D for vector)
    pub data_offset: u64,       // 0x0C..0x13
    pub data_bytes: u64,        // 0x14..0x1B
    pub offsets_offset: u64,    // 0x1C..0x23 (For VAR_STRING / VAR_BYTES)
    pub offsets_bytes: u64,     // 0x24..0x2B (For VAR_STRING / VAR_BYTES)
}

impl AttributeDescriptor {
    pub fn name_offset(&self) -> u32 {
        unsafe { std::ptr::read_unaligned(std::ptr::addr_of!(self.name_offset)) }
    }
    pub fn is_nullable(&self) -> bool {
        (self.type_code & IMPULSE_NULLABLE_FLAG) != 0
    }

    pub fn base_type(&self) -> Option<BaseDataType> {
        BaseDataType::from_u8(self.type_code & IMPULSE_TYPE_MASK)
    }
}

const_assert_eq!(std::mem::size_of::<AttributeDescriptor>(), 44);

// ---------------------------------------------------------------------------
// Hashing & CRC-16 Utilities
// ---------------------------------------------------------------------------

pub fn compute_sha256(data: &[u8]) -> [u8; 32] {
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];
    let k: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];

    let bit_len = (data.len() as u64) * 8;
    let mut padded = data.to_vec();
    padded.push(0x80);
    while (padded.len() + 8) % 64 != 0 {
        padded.push(0x00);
    }
    padded.extend_from_slice(&bit_len.to_be_bytes());

    for chunk in padded.chunks_exact(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes(chunk[i * 4..(i + 1) * 4].try_into().unwrap());
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }

        let mut a = h[0];
        let mut b = h[1];
        let mut c = h[2];
        let mut d = h[3];
        let mut e = h[4];
        let mut f = h[5];
        let mut g = h[6];
        let mut h_var = h[7];

        for i in 0..64 {
            let s1_var = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let temp1 = h_var.wrapping_add(s1_var).wrapping_add(ch).wrapping_add(k[i]).wrapping_add(w[i]);
            let s0_var = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0_var.wrapping_add(maj);

            h_var = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }

        h[0] = h[0].wrapping_add(a);
        h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c);
        h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e);
        h[5] = h[5].wrapping_add(f);
        h[6] = h[6].wrapping_add(g);
        h[7] = h[7].wrapping_add(h_var);
    }

    let mut out = [0u8; 32];
    for i in 0..8 {
        out[i * 4..(i + 1) * 4].copy_from_slice(&h[i].to_be_bytes());
    }
    out
}

pub fn compute_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            if (crc & 0x8000) != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}
