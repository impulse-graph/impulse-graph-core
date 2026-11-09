use crate::embeddings::VectorIndexSpec;
use impulse_graph::spec::{BaseDataType, KeyType};
use impulse_graph::vamana::f32_from_fp16;
use impulse_graph::SnapshotWriter;
use memmap2::MmapOptions;
use safetensors::{Dtype, SafeTensors};
use std::error::Error;
use std::fs::{self, File};
use std::path::{Path, PathBuf};

pub fn run(
    safetensors_path: &Path,
    output_path: &Path,
    domain_name: &str,
) -> Result<(), Box<dyn Error>> {
    run_with_options(safetensors_path, output_path, domain_name, &[])
}

pub fn run_with_options(
    safetensors_path: &Path,
    output_path: &Path,
    domain_name: &str,
    vector_indices: &[String],
) -> Result<(), Box<dyn Error>> {
    println!(
        "Ingesting .safetensors binary weight shards: {} -> {}...",
        safetensors_path.display(),
        output_path.display()
    );

    let mut shard_files: Vec<PathBuf> = Vec::new();

    if safetensors_path.is_dir() {
        for entry_res in fs::read_dir(safetensors_path)? {
            let entry = entry_res?;
            let p = entry.path();
            if p.is_file() && p.extension().is_some_and(|ext| ext == "safetensors") {
                shard_files.push(p);
            }
        }
        shard_files.sort();
    } else {
        shard_files.push(safetensors_path.to_path_buf());
    }

    if shard_files.is_empty() {
        return Err(format!(
            "No .safetensors files found in {}",
            safetensors_path.display()
        )
        .into());
    }

    println!(
        "  Found {} .safetensors shard files to ingest",
        shard_files.len()
    );

    let mut total_tensors = 0usize;
    let mut total_bytes: u64 = 0;
    // Collect (tensor_name, num_rows, dim, f32_le_bytes)
    let mut extracted_attrs: Vec<(String, usize, u32, Vec<u8>)> = Vec::new();

    for shard in &shard_files {
        println!("  - Ingesting shard file: {}...", shard.display());
        let file = File::open(shard)?;
        let mmap = unsafe { MmapOptions::new().map(&file)? };

        let tensors = SafeTensors::deserialize(&mmap)?;
        let mut names: Vec<String> = tensors.names().into_iter().cloned().collect();
        names.sort();

        for name in &names {
            if let Ok(tensor) = tensors.tensor(name) {
                let raw_data = tensor.data();
                let bytes_len = raw_data.len() as u64;
                total_bytes += bytes_len;
                total_tensors += 1;

                let shape = tensor.shape();
                println!(
                    "      tensor '{}': shape={:?}, dtype={:?}, bytes={:.2} MB",
                    name,
                    shape,
                    tensor.dtype(),
                    bytes_len as f64 / (1024.0 * 1024.0)
                );

                let (n_rows, dim) = match shape {
                    [n, d] => (*n, *d as u32),
                    [n] => (*n, 1u32),
                    _ => continue,
                };

                let mut f32_bytes = Vec::with_capacity(n_rows * (dim as usize) * 4);
                match tensor.dtype() {
                    Dtype::F32 => {
                        f32_bytes.extend_from_slice(raw_data);
                    }
                    Dtype::F16 => {
                        for &chunk in raw_data.as_chunks::<2>().0 {
                            let h = u16::from_le_bytes(chunk);
                            let f = f32_from_fp16(h);
                            f32_bytes.extend_from_slice(&f.to_le_bytes());
                        }
                    }
                    Dtype::BF16 => {
                        for &chunk in raw_data.as_chunks::<2>().0 {
                            let b16 = u16::from_le_bytes(chunk) as u32;
                            let f = f32::from_bits(b16 << 16);
                            f32_bytes.extend_from_slice(&f.to_le_bytes());
                        }
                    }
                    Dtype::F64 => {
                        for &chunk in raw_data.as_chunks::<8>().0 {
                            let f = f64::from_le_bytes(chunk) as f32;
                            f32_bytes.extend_from_slice(&f.to_le_bytes());
                        }
                    }
                    _ => continue,
                }
                extracted_attrs.push((name.clone(), n_rows, dim, f32_bytes));
            }
        }
    }

    let primary_node_count = extracted_attrs
        .first()
        .map(|(_, n, _, _)| *n)
        .unwrap_or(total_tensors);

    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap());
    writer.add_domain(0, KeyType::String, domain_name);
    writer.set_domain_node_count(0, primary_node_count as u64);

    let row_offsets = vec![0u32; primary_node_count + 1];
    let col_indices = vec![];
    writer.add_relation(0, 0, primary_node_count as u64, 0, row_offsets, col_indices);
    writer.set_relation_name(0, &format!("{}_nodes", domain_name));

    for (attr_name, n_rows, dim, f32_bytes) in extracted_attrs {
        if n_rows == primary_node_count {
            writer.add_attribute_to_domain(
                0,
                &attr_name,
                BaseDataType::Float32 as u8,
                dim,
                f32_bytes,
                None,
            );
        }
    }

    for spec_str in vector_indices {
        let vspec = VectorIndexSpec::parse(spec_str)?;
        if vspec.domain_name == domain_name || vspec.domain_name == "0" {
            writer.add_vamana_index(0, &vspec.attr_name, &vspec.index_name, &vspec.config)?;
        }
    }

    writer.finalize()?;

    let final_mb = total_bytes as f64 / (1024.0 * 1024.0);
    let final_gb = final_mb / 1024.0;
    println!(
        "SUCCESS: Ingested {} weight tensors ({:.2} MB / {:.2} GB) into Impulse Graph snapshot: {}",
        total_tensors,
        final_mb,
        final_gb,
        output_path.display()
    );

    Ok(())
}
