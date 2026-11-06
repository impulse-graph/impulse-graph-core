use impulse_graph::SnapshotReader;
use std::error::Error;
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::Path;

pub fn run(file: &Path, out_dir: &Path, format: &str) -> Result<(), Box<dyn Error>> {
    println!(
        "Exporting snapshot {} to {}...",
        file.display(),
        out_dir.display()
    );
    fs::create_dir_all(out_dir)?;

    let reader = SnapshotReader::open(file)?;
    let sep = match format {
        "csv" => ",",
        _ => "\t",
    };

    for (idx, rel) in reader.relations().iter().enumerate() {
        let out_filename = format!(
            "relation_{}_{}.{}",
            rel.src_domain_id, rel.tgt_domain_id, format
        );
        let out_path = out_dir.join(&out_filename);
        println!("  Writing relation #{} -> {}...", idx, out_path.display());

        let out_file = File::create(&out_path)?;
        let mut writer = BufWriter::new(out_file);

        let row_offsets = reader.get_row_offsets(idx)?;
        let col_indices = reader.get_col_indices(idx)?;

        for u in 0..rel.node_count as usize {
            let start = row_offsets[u] as usize;
            let end = row_offsets[u + 1] as usize;
            for &v in &col_indices[start..end] {
                if format == "jsonl" {
                    writeln!(writer, "{{\"src\":{},\"tgt\":{}}}", u, v)?;
                } else {
                    writeln!(writer, "{}{}{}", u, sep, v)?;
                }
            }
        }
        writer.flush()?;
    }

    println!("SUCCESS: Exported all relations to {}", out_dir.display());
    Ok(())
}
