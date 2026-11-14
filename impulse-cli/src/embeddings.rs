//! Multi-Format Embedding Ingestion & Vector Index Specification Parser
//!
//! Supports loading dense `Float32` embedding matrices `[N, D]` from:
//! - NumPy `.npy` (`<f4`, `<f2`, `<f8`)
//! - Standard ANN Benchmark `.fvecs`, `.bvecs`, `.ivecs` (SIFT1M / GIST / Deep1B binary format)
//! - HuggingFace `.safetensors` (`F32`, `F16`, `BF16`, `F64`)
//! - Apache Parquet `.parquet` (`FixedSizeList`, `List`, `FixedSizeBinary`/`Binary`, `Utf8`, or wide float columns)
//! - JSON Lines `.jsonl` / `.ndjson`
//! - Delimited `.csv` / `.tsv` (inline JSON array `"[...]"`, `;`-delimited, space-delimited, or wide columns)

use arrow_array::cast::AsArray;
use arrow_array::types::{Float32Type, Float64Type};
use arrow_array::Array;
use arrow_schema::DataType;
use impulse_graph::vamana::{f32_from_fp16, VamanaBuildConfig};
use parquet::arrow::arrow_reader::ParquetRecordBatchReaderBuilder;
use safetensors::{Dtype, SafeTensors};
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct LoadedEmbeddings {
    pub num_vectors: usize,
    pub dimension: usize,
    pub vectors: Vec<f32>,
    pub keys: Option<Vec<String>>,
}

impl LoadedEmbeddings {
    /// Normalizes each vector in-place to unit L2 norm.
    pub fn normalize_l2(&mut self) {
        if self.dimension == 0 {
            return;
        }
        for u in 0..self.num_vectors {
            let slice = &mut self.vectors[u * self.dimension..(u + 1) * self.dimension];
            let norm = slice.iter().map(|&x| x * x).sum::<f32>().sqrt();
            if norm > 1e-12 {
                for x in slice.iter_mut() {
                    *x /= norm;
                }
            }
        }
    }

    /// Serializes `vectors` into raw little-endian `f32` bytes (`num_vectors * dimension * 4`).
    pub fn to_le_bytes(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(self.vectors.len() * 4);
        for &v in &self.vectors {
            out.extend_from_slice(&v.to_le_bytes());
        }
        out
    }

    /// Reorders or aligns vectors to match `domain_node_count` using an optional `key_to_dense_id` map.
    pub fn align_to_domain(
        &self,
        domain_node_count: usize,
        key_to_dense_id: Option<&HashMap<String, u32>>,
    ) -> Result<Vec<f32>, String> {
        let d = self.dimension;
        if d == 0 {
            return Err("Embedding dimension cannot be 0".to_string());
        }
        if let (Some(keys), Some(kmap)) = (&self.keys, key_to_dense_id) {
            let mut aligned = vec![0.0f32; domain_node_count * d];
            for (row_idx, key) in keys.iter().enumerate() {
                let dense_id = if let Some(&id) = kmap.get(key) {
                    id as usize
                } else if let Ok(parsed_id) = key.parse::<usize>() {
                    parsed_id
                } else {
                    continue;
                };
                if dense_id < domain_node_count {
                    aligned[dense_id * d..(dense_id + 1) * d]
                        .copy_from_slice(&self.vectors[row_idx * d..(row_idx + 1) * d]);
                }
            }
            return Ok(aligned);
        }

        if self.num_vectors == domain_node_count {
            Ok(self.vectors.clone())
        } else if self.num_vectors > domain_node_count {
            Ok(self.vectors[0..domain_node_count * d].to_vec())
        } else {
            let mut padded = vec![0.0f32; domain_node_count * d];
            padded[0..self.vectors.len()].copy_from_slice(&self.vectors);
            Ok(padded)
        }
    }
}

/// CLI / Manifest specification for attaching an external embedding file to a domain:
/// Format: `"Domain:attr=path/to/file.{npy,fvecs,safetensors,parquet,tsv,jsonl}[:selector][:normalize]"`
#[derive(Debug, Clone)]
pub struct EmbeddingAttachmentSpec {
    pub domain_name: String,
    pub attr_name: String,
    pub file_path: PathBuf,
    pub selector: Option<String>,
    pub normalize: bool,
}

impl EmbeddingAttachmentSpec {
    pub fn parse(spec: &str) -> Result<Self, String> {
        let (lhs, rhs) = spec.split_once('=').ok_or_else(|| {
            format!(
                "Invalid --embeddings spec '{}' (expected Domain:attr=path[:selector][:normalize])",
                spec
            )
        })?;
        let (domain_name, attr_name) = lhs
            .split_once(':')
            .or_else(|| lhs.split_once('.'))
            .ok_or_else(|| {
                format!(
                    "Invalid --embeddings target '{}' (expected Domain:attr or Domain.attr)",
                    lhs
                )
            })?;

        let parts: Vec<&str> = rhs.split(':').collect();
        let file_path = PathBuf::from(parts[0].trim());
        let mut selector = None;
        let mut normalize = false;

        for &part in &parts[1..] {
            let p = part.trim();
            if p.eq_ignore_ascii_case("normalize") || p.eq_ignore_ascii_case("norm") {
                normalize = true;
            } else if !p.is_empty() {
                selector = Some(p.to_string());
            }
        }

        Ok(Self {
            domain_name: domain_name.trim().to_string(),
            attr_name: attr_name.trim().to_string(),
            file_path,
            selector,
            normalize,
        })
    }
}

/// CLI / Manifest specification for building a Vamana (`IMP_INDEX_VAMANA_ANN = 8`) index:
/// Format: `"Domain:attr[:r=32,l=100,alpha=1.2,metric=cosine,quant=sq8]"`
#[derive(Debug, Clone)]
pub struct VectorIndexSpec {
    pub domain_name: String,
    pub attr_name: String,
    pub index_name: String,
    pub config: VamanaBuildConfig,
}

impl VectorIndexSpec {
    pub fn parse(spec: &str) -> Result<Self, String> {
        let (target_part, params_opt) = if let Some((first, rest)) = spec.split_once(':') {
            if first.contains('.') {
                (first, Some(rest))
            } else if let Some((attr, params)) = rest.split_once(':') {
                return Self::from_parts(first, attr, Some(params));
            } else {
                return Self::from_parts(first, rest, None);
            }
        } else {
            (spec, None)
        };
        let (domain_name, attr_name) = target_part.split_once('.').ok_or_else(|| {
            format!(
                "Invalid --vector-index spec '{}' (expected Domain:attr[:params] or Domain.attr[:params])",
                spec
            )
        })?;
        Self::from_parts(domain_name, attr_name, params_opt)
    }

    fn from_parts(
        domain_name: &str,
        attr_name: &str,
        params_opt: Option<&str>,
    ) -> Result<Self, String> {
        let domain_name = domain_name.trim().to_string();
        let attr_name = attr_name.trim().to_string();
        let config = if let Some(params) = params_opt {
            VamanaBuildConfig::parse_params(params)?
        } else {
            VamanaBuildConfig::default()
        };
        let index_name = format!("{}_{}_vamana", domain_name, attr_name);
        Ok(Self {
            domain_name,
            attr_name,
            index_name,
            config,
        })
    }
}

/// Parses an inline vector string such as `"[0.1, -0.2, 0.3]"`, `"0.1;0.2;0.3"`, or `"0.1 0.2 0.3"`.
pub fn parse_inline_vector_str(s: &str) -> Result<Vec<f32>, String> {
    let trimmed = s
        .trim()
        .trim_start_matches('[')
        .trim_end_matches(']')
        .trim();
    if trimmed.is_empty() {
        return Ok(Vec::new());
    }
    let sep = if trimmed.contains(';') {
        ';'
    } else if trimmed.contains(',') {
        ','
    } else {
        ' '
    };
    let mut out = Vec::new();
    if sep == ' ' {
        for tok in trimmed.split_whitespace() {
            let v = tok
                .parse::<f32>()
                .map_err(|_| format!("Invalid float '{}' in inline vector", tok))?;
            out.push(v);
        }
    } else {
        for tok in trimmed.split(sep) {
            let t = tok.trim();
            if t.is_empty() {
                continue;
            }
            let v = t
                .parse::<f32>()
                .map_err(|_| format!("Invalid float '{}' in inline vector", t))?;
            out.push(v);
        }
    }
    Ok(out)
}

/// Auto-detects format by file extension and loads embeddings into `LoadedEmbeddings`.
pub fn load_embeddings_from_path(
    path: &Path,
    selector: Option<&str>,
    normalize: bool,
) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();

    let mut loaded = match ext.as_str() {
        "npy" => load_npy(path)?,
        "fvecs" => load_fvecs(path)?,
        "bvecs" => load_bvecs(path)?,
        "ivecs" => load_ivecs(path)?,
        "safetensors" => load_safetensors(path, selector)?,
        "parquet" => load_parquet_embeddings(path, selector)?,
        "jsonl" | "ndjson" => load_jsonl_embeddings(path, selector)?,
        "tsv" => load_delimited_embeddings(path, b'\t', selector)?,
        "csv" | "txt" => load_delimited_embeddings(path, b',', selector)?,
        other => {
            return Err(format!(
                "Unsupported embedding file extension '.{}' for {}",
                other,
                path.display()
            )
            .into())
        }
    };

    if normalize {
        loaded.normalize_l2();
    }
    Ok(loaded)
}

/// Loads a NumPy `.npy` v1.0 / v2.0 2D array (`<f4`, `<f2`, `<f8`).
pub fn load_npy(path: &Path) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let mut f = File::open(path)?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;

    if buf.len() < 10 || &buf[0..6] != b"\x93NUMPY" {
        return Err(format!(
            "File {} does not have a valid .npy magic header",
            path.display()
        )
        .into());
    }
    let major = buf[6];
    let (hdr_len, data_start) = if major == 1 {
        let len = u16::from_le_bytes([buf[8], buf[9]]) as usize;
        (len, 10 + len)
    } else if major == 2 || major == 3 {
        if buf.len() < 12 {
            return Err("Truncated .npy v2 header".into());
        }
        let len = u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]) as usize;
        (len, 12 + len)
    } else {
        return Err(format!("Unsupported .npy version {}", major).into());
    };

    if data_start > buf.len() {
        return Err("Corrupt .npy header length".into());
    }
    let hdr_str = std::str::from_utf8(&buf[data_start - hdr_len..data_start])?;

    if hdr_str.contains("'fortran_order': True") || hdr_str.contains("\"fortran_order\": True") {
        return Err(
            "Fortran-ordered .npy arrays are not supported; save in C-contiguous order".into(),
        );
    }

    // Extract descr
    let descr = extract_npy_field(hdr_str, "descr")
        .ok_or("Missing 'descr' in .npy header")?
        .trim_matches(|c| c == '\'' || c == '"')
        .to_string();

    // Extract shape tuple
    let shape_pos = hdr_str
        .find("'shape'")
        .or_else(|| hdr_str.find("\"shape\""))
        .ok_or("Missing 'shape' in .npy header")?;
    let after_shape = &hdr_str[shape_pos..];
    let lparen = after_shape.find('(').ok_or("Missing '(' in .npy shape")?;
    let rparen = after_shape[lparen..]
        .find(')')
        .ok_or("Missing ')' in .npy shape")?
        + lparen;
    let dims: Vec<usize> = after_shape[lparen + 1..rparen]
        .split(',')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .map(|s| s.parse::<usize>())
        .collect::<Result<Vec<_>, _>>()?;

    let (num_vectors, dimension) = match dims.as_slice() {
        [n, d] => (*n, *d),
        [n] => (*n, 1),
        _ => return Err(format!("Expected 1D or 2D .npy array, got shape {:?}", dims).into()),
    };

    let payload = &buf[data_start..];
    let total_elems = num_vectors * dimension;
    let mut vectors = Vec::with_capacity(total_elems);

    match descr.as_str() {
        "<f4" | "|f4" | "f4" => {
            if payload.len() < total_elems * 4 {
                return Err("Truncated .npy <f4 payload".into());
            }
            for &chunk in payload[..total_elems * 4].as_chunks::<4>().0 {
                vectors.push(f32::from_le_bytes(chunk));
            }
        }
        "<f2" | "|f2" | "f2" => {
            if payload.len() < total_elems * 2 {
                return Err("Truncated .npy <f2 payload".into());
            }
            for &chunk in payload[..total_elems * 2].as_chunks::<2>().0 {
                let h = u16::from_le_bytes(chunk);
                vectors.push(f32_from_fp16(h));
            }
        }
        "<f8" | "|f8" | "f8" => {
            if payload.len() < total_elems * 8 {
                return Err("Truncated .npy <f8 payload".into());
            }
            for &chunk in payload[..total_elems * 8].as_chunks::<8>().0 {
                vectors.push(f64::from_le_bytes(chunk) as f32);
            }
        }
        other => return Err(format!("Unsupported .npy dtype '{}'", other).into()),
    }

    Ok(LoadedEmbeddings {
        num_vectors,
        dimension,
        vectors,
        keys: None,
    })
}

fn extract_npy_field<'a>(hdr: &'a str, key: &str) -> Option<&'a str> {
    let k1 = format!("'{}'", key);
    let k2 = format!("\"{}\"", key);
    let pos = hdr.find(&k1).or_else(|| hdr.find(&k2))?;
    let rest = &hdr[pos..];
    let colon = rest.find(':')?;
    let after_colon = rest[colon + 1..].trim_start();
    let end = after_colon.find([',', '}'])?;
    Some(after_colon[..end].trim())
}

/// Writes a 2D `<f4` NumPy `.npy` v1.0 file.
pub fn write_npy(
    path: &Path,
    vectors: &[f32],
    num_vectors: usize,
    dimension: usize,
) -> Result<(), Box<dyn Error>> {
    let expected = num_vectors.saturating_mul(dimension);
    if vectors.len() < expected {
        return Err(format!(
            "write_fvecs: vectors slice too short ({} < {} * {})",
            vectors.len(),
            num_vectors,
            dimension
        )
        .into());
    }
    let mut f = File::create(path)?;
    let dict = format!(
        "{{'descr': '<f4', 'fortran_order': False, 'shape': ({}, {}), }}",
        num_vectors, dimension
    );
    let total_unaligned = 10 + dict.len() + 1;
    let pad = (64 - (total_unaligned % 64)) % 64;
    let hdr_len = dict.len() + pad + 1;

    f.write_all(b"\x93NUMPY\x01\x00")?;
    f.write_all(&(hdr_len as u16).to_le_bytes())?;
    f.write_all(dict.as_bytes())?;
    if pad > 0 {
        f.write_all(&vec![b' '; pad])?;
    }
    let expected = num_vectors.saturating_mul(dimension);
    if vectors.len() < expected {
        return Err(format!(
            "write_npy: vectors slice too short ({} < {} * {})",
            vectors.len(),
            num_vectors,
            dimension
        )
        .into());
    }
    f.write_all(b"\n")?;
    for &v in &vectors[..expected] {
        f.write_all(&v.to_le_bytes())?;
    }
    f.flush()?;
    Ok(())
}

/// Loads a standard SIFT/ANN `.fvecs` binary file (`[u32 dim, f32 * dim]` per record).
pub fn load_fvecs(path: &Path) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let mut f = File::open(path)?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;
    if buf.is_empty() {
        return Ok(LoadedEmbeddings {
            num_vectors: 0,
            dimension: 0,
            vectors: Vec::new(),
            keys: None,
        });
    }
    if buf.len() < 4 {
        return Err("Truncated .fvecs file".into());
    }
    let dim = u32::from_le_bytes(buf[0..4].try_into().unwrap()) as usize;
    if dim == 0 {
        return Err("Invalid 0 dimension in .fvecs file".into());
    }
    let stride = 4 + dim * 4;
    if !buf.len().is_multiple_of(stride) {
        return Err(format!(
            "Corrupt .fvecs file size {} (not a multiple of record stride {} for dim={})",
            buf.len(),
            stride,
            dim
        )
        .into());
    }
    let num_vectors = buf.len() / stride;
    let mut vectors = Vec::with_capacity(num_vectors * dim);
    for u in 0..num_vectors {
        let rec = &buf[u * stride..(u + 1) * stride];
        let rec_dim = u32::from_le_bytes(rec[0..4].try_into().unwrap()) as usize;
        if rec_dim != dim {
            return Err(format!(
                "Inconsistent dimension at vector {}: expected {}, got {}",
                u, dim, rec_dim
            )
            .into());
        }
        for &chunk in rec[4..].as_chunks::<4>().0 {
            vectors.push(f32::from_le_bytes(chunk));
        }
    }
    Ok(LoadedEmbeddings {
        num_vectors,
        dimension: dim,
        vectors,
        keys: None,
    })
}

/// Writes a standard SIFT/ANN `.fvecs` binary file.
pub fn write_fvecs(
    path: &Path,
    vectors: &[f32],
    num_vectors: usize,
    dimension: usize,
) -> Result<(), Box<dyn Error>> {
    let expected = num_vectors.saturating_mul(dimension);
    if vectors.len() < expected {
        return Err(format!(
            "write_fvecs: vectors slice too short ({} < {} * {})",
            vectors.len(),
            num_vectors,
            dimension
        )
        .into());
    }
    let mut f = File::create(path)?;
    let dim_u32 = dimension as u32;
    for u in 0..num_vectors {
        f.write_all(&dim_u32.to_le_bytes())?;
        for &v in &vectors[u * dimension..(u + 1) * dimension] {
            f.write_all(&v.to_le_bytes())?;
        }
    }
    f.flush()?;
    Ok(())
}

/// Loads a `.bvecs` binary file (`[u32 dim, u8 * dim]` per record).
pub fn load_bvecs(path: &Path) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let mut f = File::open(path)?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;
    if buf.len() < 4 {
        return Err("Truncated .bvecs file".into());
    }
    let dim = u32::from_le_bytes(buf[0..4].try_into().unwrap()) as usize;
    let stride = 4 + dim;
    if dim == 0 || !buf.len().is_multiple_of(stride) {
        return Err("Invalid .bvecs layout".into());
    }
    let num_vectors = buf.len() / stride;
    let mut vectors = Vec::with_capacity(num_vectors * dim);
    for u in 0..num_vectors {
        let rec = &buf[u * stride..(u + 1) * stride];
        for &b in &rec[4..] {
            vectors.push(b as f32);
        }
    }
    Ok(LoadedEmbeddings {
        num_vectors,
        dimension: dim,
        vectors,
        keys: None,
    })
}

/// Loads an `.ivecs` binary file (`[u32 dim, i32 * dim]` per record).
pub fn load_ivecs(path: &Path) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let mut f = File::open(path)?;
    let mut buf = Vec::new();
    f.read_to_end(&mut buf)?;
    if buf.len() < 4 {
        return Err("Truncated .ivecs file".into());
    }
    let dim = u32::from_le_bytes(buf[0..4].try_into().unwrap()) as usize;
    let stride = 4 + dim * 4;
    if dim == 0 || !buf.len().is_multiple_of(stride) {
        return Err("Invalid .ivecs layout".into());
    }
    let num_vectors = buf.len() / stride;
    let mut vectors = Vec::with_capacity(num_vectors * dim);
    for u in 0..num_vectors {
        let rec = &buf[u * stride..(u + 1) * stride];
        for &chunk in rec[4..].as_chunks::<4>().0 {
            vectors.push(i32::from_le_bytes(chunk) as f32);
        }
    }
    Ok(LoadedEmbeddings {
        num_vectors,
        dimension: dim,
        vectors,
        keys: None,
    })
}

/// Loads a 2D `[N, D]` tensor (`F32`, `F16`, `BF16`, `F64`) from a HuggingFace `.safetensors` file.
pub fn load_safetensors(
    path: &Path,
    tensor_name: Option<&str>,
) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let file = File::open(path)?;
    let mmap = unsafe { memmap2::MmapOptions::new().map(&file)? };
    let st = SafeTensors::deserialize(&mmap)?;

    let chosen_name = if let Some(name) = tensor_name {
        name.to_string()
    } else {
        let mut names: Vec<String> = st.names().into_iter().cloned().collect();
        names.sort();
        names
            .into_iter()
            .find(|n| st.tensor(n).map(|t| t.shape().len() == 2).unwrap_or(false))
            .ok_or_else(|| format!("No 2D tensor found in {}", path.display()))?
    };

    let view = st.tensor(&chosen_name)?;
    let shape = view.shape();
    if shape.len() != 2 {
        return Err(format!(
            "Tensor '{}' in {} is not 2D (shape={:?})",
            chosen_name,
            path.display(),
            shape
        )
        .into());
    }
    let num_vectors = shape[0];
    let dimension = shape[1];
    let data = view.data();
    let total = num_vectors * dimension;
    let mut vectors = Vec::with_capacity(total);

    match view.dtype() {
        Dtype::F32 => {
            for &chunk in data.as_chunks::<4>().0 {
                vectors.push(f32::from_le_bytes(chunk));
            }
        }
        Dtype::F16 => {
            for &chunk in data.as_chunks::<2>().0 {
                let h = u16::from_le_bytes(chunk);
                vectors.push(f32_from_fp16(h));
            }
        }
        Dtype::BF16 => {
            for &chunk in data.as_chunks::<2>().0 {
                let b16 = u16::from_le_bytes(chunk) as u32;
                vectors.push(f32::from_bits(b16 << 16));
            }
        }
        Dtype::F64 => {
            for &chunk in data.as_chunks::<8>().0 {
                vectors.push(f64::from_le_bytes(chunk) as f32);
            }
        }
        other => {
            return Err(format!(
                "Unsupported safetensors dtype {:?} for tensor '{}'",
                other, chosen_name
            )
            .into())
        }
    }

    Ok(LoadedEmbeddings {
        num_vectors,
        dimension,
        vectors,
        keys: None,
    })
}

/// Loads embeddings from a `.parquet` file (`FixedSizeList`, `List`, `Binary`, `Utf8`, or wide float columns).
pub fn load_parquet_embeddings(
    path: &Path,
    col_name: Option<&str>,
) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let file = File::open(path)?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)?;
    let reader = builder.build()?;

    let mut vectors = Vec::new();
    let mut dimension = 0usize;
    let mut num_vectors = 0usize;

    for batch_res in reader {
        let batch = batch_res?;
        let schema = batch.schema();
        let col_idx = if let Some(name) = col_name {
            schema
                .index_of(name)
                .map_err(|_| format!("Column '{}' not found in {}", name, path.display()))?
        } else {
            // Find first list or string/binary vector column, else fallback to column 0
            schema
                .fields()
                .iter()
                .position(|f| {
                    matches!(
                        f.data_type(),
                        DataType::FixedSizeList(_, _)
                            | DataType::List(_)
                            | DataType::FixedSizeBinary(_)
                            | DataType::Binary
                    )
                })
                .unwrap_or(0)
        };

        let col = batch.column(col_idx);
        match col.data_type() {
            DataType::FixedSizeList(_field, size) => {
                let dim = *size as usize;
                if dimension == 0 {
                    dimension = dim;
                }
                let fsl = col.as_fixed_size_list();
                let values = fsl.values();
                match values.data_type() {
                    DataType::Float32 => {
                        let f32_arr = values.as_primitive::<Float32Type>();
                        vectors.extend_from_slice(f32_arr.values());
                        num_vectors += batch.num_rows();
                    }
                    DataType::Float64 => {
                        let f64_arr = values.as_primitive::<Float64Type>();
                        for &v in f64_arr.values() {
                            vectors.push(v as f32);
                        }
                        num_vectors += batch.num_rows();
                    }
                    other => {
                        return Err(
                            format!("Unsupported FixedSizeList element type {:?}", other).into(),
                        )
                    }
                }
            }
            DataType::List(_) => {
                let list_arr = col.as_list::<i32>();
                for row in 0..batch.num_rows() {
                    let slice_arr = list_arr.value(row);
                    let row_len = slice_arr.len();
                    if dimension == 0 {
                        dimension = row_len;
                    }
                    match slice_arr.data_type() {
                        DataType::Float32 => {
                            let f32_arr = slice_arr.as_primitive::<Float32Type>();
                            vectors.extend_from_slice(f32_arr.values());
                        }
                        DataType::Float64 => {
                            let f64_arr = slice_arr.as_primitive::<Float64Type>();
                            for &v in f64_arr.values() {
                                vectors.push(v as f32);
                            }
                        }
                        other => {
                            return Err(format!("Unsupported List element type {:?}", other).into())
                        }
                    }
                    num_vectors += 1;
                }
            }
            DataType::Utf8 => {
                let str_arr = col.as_string::<i32>();
                for row in 0..batch.num_rows() {
                    let parsed = parse_inline_vector_str(str_arr.value(row))?;
                    if dimension == 0 {
                        dimension = parsed.len();
                    }
                    vectors.extend_from_slice(&parsed);
                    num_vectors += 1;
                }
            }
            _ => {
                return Err(format!(
                    "Unsupported Parquet embedding column type {:?}",
                    col.data_type()
                )
                .into())
            }
        }
    }

    Ok(LoadedEmbeddings {
        num_vectors,
        dimension,
        vectors,
        keys: None,
    })
}

/// Loads embeddings from a `.jsonl` / `.ndjson` file.
/// Supports lines like `{"id": "doc_0", "embedding": [0.1, 0.2, ...]}` or `[0.1, 0.2, ...]`.
pub fn load_jsonl_embeddings(
    path: &Path,
    field_name: Option<&str>,
) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let f = File::open(path)?;
    let reader = BufReader::new(f);
    let target_field = field_name.unwrap_or("embedding");

    let mut vectors = Vec::new();
    let mut keys = Vec::new();
    let mut has_keys = false;
    let mut dimension = 0usize;
    let mut num_vectors = 0usize;

    for (line_no, line_res) in reader.lines().enumerate() {
        let line = line_res?;
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        let val: serde_json::Value = serde_json::from_str(trimmed)
            .map_err(|e| format!("Invalid JSON on line {}: {}", line_no + 1, e))?;

        let arr = if let Some(a) = val.as_array() {
            a
        } else if let Some(obj) = val.as_object() {
            if let Some(id_val) = obj
                .get("id")
                .or_else(|| obj.get("key"))
                .or_else(|| obj.get("node_id"))
            {
                has_keys = true;
                keys.push(match id_val {
                    serde_json::Value::String(s) => s.clone(),
                    other => other.to_string(),
                });
            }
            obj.get(target_field)
                .or_else(|| obj.get("vector"))
                .or_else(|| obj.get("values"))
                .and_then(|v| v.as_array())
                .ok_or_else(|| {
                    format!(
                        "Missing array field '{}' on line {}",
                        target_field,
                        line_no + 1
                    )
                })?
        } else {
            return Err(format!("Unsupported JSON value on line {}", line_no + 1).into());
        };

        if dimension == 0 {
            dimension = arr.len();
        } else if arr.len() != dimension {
            return Err(format!(
                "Dimension mismatch on line {}: expected {}, got {}",
                line_no + 1,
                dimension,
                arr.len()
            )
            .into());
        }

        for item in arr {
            let fval = item
                .as_f64()
                .ok_or_else(|| format!("Non-numeric vector element on line {}", line_no + 1))?
                as f32;
            vectors.push(fval);
        }
        num_vectors += 1;
    }

    Ok(LoadedEmbeddings {
        num_vectors,
        dimension,
        vectors,
        keys: if has_keys && keys.len() == num_vectors {
            Some(keys)
        } else {
            None
        },
    })
}

/// Loads embeddings from a `.csv` or `.tsv` file.
/// Supports:
/// - Inline vector column (`"[0.1, 0.2]"`, `"0.1;0.2"`, `"0.1 0.2"`), optionally with an `id` column
/// - Wide float table (`f0, f1, ..., f_{D-1}`)
pub fn load_delimited_embeddings(
    path: &Path,
    delimiter: u8,
    col_selector: Option<&str>,
) -> Result<LoadedEmbeddings, Box<dyn Error>> {
    let mut rdr = csv::ReaderBuilder::new()
        .delimiter(delimiter)
        .has_headers(true)
        .from_path(path)?;

    let headers = rdr.headers()?.clone();
    let selected_col_idx = if let Some(sel) = col_selector {
        headers.iter().position(|h| h.trim() == sel)
    } else {
        headers.iter().position(|h| {
            let hl = h.trim().to_ascii_lowercase();
            hl == "embedding" || hl == "vector" || hl == "emb" || hl == "vec"
        })
    };

    let id_col_idx = headers.iter().position(|h| {
        let hl = h.trim().to_ascii_lowercase();
        hl == "id" || hl == "node_id" || hl == "key"
    });

    let mut vectors = Vec::new();
    let mut keys = Vec::new();
    let mut dimension = 0usize;
    let mut num_vectors = 0usize;

    for rec_res in rdr.records() {
        let rec = rec_res?;
        if rec.is_empty() {
            continue;
        }
        if let Some(id_i) = id_col_idx {
            if let Some(k) = rec.get(id_i) {
                keys.push(k.trim().to_string());
            }
        }

        let row_vec = if let Some(c_idx) = selected_col_idx {
            let cell = rec.get(c_idx).unwrap_or("");
            parse_inline_vector_str(cell)?
        } else if rec.len() == 1
            || (rec.len() == 2 && id_col_idx.is_some() && {
                let vec_idx = if id_col_idx == Some(1) { 0 } else { 1 };
                rec[vec_idx].contains('[')
                    || rec[vec_idx].contains(';')
                    || rec[vec_idx].contains(' ')
            })
        {
            let vec_cell = if rec.len() == 1 || id_col_idx == Some(1) {
                &rec[0]
            } else {
                &rec[1]
            };
            parse_inline_vector_str(vec_cell)?
        } else {
            // Wide numeric columns (skipping id_col_idx if present)
            let mut v = Vec::new();
            for (i, field) in rec.iter().enumerate() {
                if Some(i) == id_col_idx {
                    continue;
                }
                v.push(field.trim().parse::<f32>()?);
            }
            v
        };

        if dimension == 0 {
            dimension = row_vec.len();
        } else if row_vec.len() != dimension {
            return Err(format!(
                "Dimension mismatch in {}: expected {}, got {}",
                path.display(),
                dimension,
                row_vec.len()
            )
            .into());
        }
        vectors.extend_from_slice(&row_vec);
        num_vectors += 1;
    }

    Ok(LoadedEmbeddings {
        num_vectors,
        dimension,
        vectors,
        keys: if !keys.is_empty() && keys.len() == num_vectors {
            Some(keys)
        } else {
            None
        },
    })
}
