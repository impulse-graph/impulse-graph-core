//! Spec v0.9.0 Graph Statistics and Optimizer Sketches Command
//! Parity with Java engine statistics (`org.impulsegraph.core.stats`).

use comfy_table::{Cell, Color, Table};
use impulse_graph::stats::{
    AttributeStatisticsCalculator, DegreeDistributionSketch, EquiDepthHistogramBuilder,
    HyperLogLogSketch, Multiplicity, RelationStatisticsCalculator,
};
use impulse_graph::SnapshotReader;
use serde_json::json;
use std::error::Error;
use std::fs;
use std::path::Path;
use std::time::Instant;

pub fn run(
    file: &Path,
    format: &str,
    verbose: bool,
    supernode_threshold: f64,
) -> Result<(), Box<dyn Error>> {
    let start_time = Instant::now();
    let reader = SnapshotReader::open(file)?;
    let metadata = fs::metadata(file)?;
    let header = reader.header();

    let mut rel_stats_list = Vec::new();
    let mut attr_stats_list = Vec::new();
    let mut sketches_map = std::collections::HashMap::new();

    for (r_idx, rel) in reader.relations().iter().enumerate() {
        let stats = RelationStatisticsCalculator::calculate(&reader, r_idx, supernode_threshold)?;
        let rel_name = if rel.name.is_empty() {
            format!("rel_{}", rel.relation_id)
        } else {
            rel.name.clone()
        };

        // Degree distribution sketch
        let row_offsets = reader.get_row_offsets(r_idx)?;
        let mut degree_sketch = DegreeDistributionSketch::new(10_000);
        let n = rel.node_count as usize;
        if row_offsets.len() > n {
            for i in 0..n {
                let deg = row_offsets[i + 1].saturating_sub(row_offsets[i]);
                degree_sketch.offer(deg);
            }
        }
        sketches_map.insert(
            format!("impulse.stats.relation.{}.out_degree", r_idx),
            degree_sketch.to_json(),
        );

        // Attribute stats & sketches
        let mut current_rel_attrs = Vec::new();
        for (a_idx, attr) in rel.attributes.iter().enumerate() {
            if let Ok(data) = reader.get_attribute_data(r_idx, a_idx) {
                let attr_stat = match attr.type_code {
                    1 => AttributeStatisticsCalculator::calculate_int32(&attr.name, data),
                    2 => AttributeStatisticsCalculator::calculate_int64(&attr.name, data),
                    3 => AttributeStatisticsCalculator::calculate_float32(&attr.name, data),
                    4 => AttributeStatisticsCalculator::calculate_float64(&attr.name, data),
                    _ => impulse_graph::stats::AttributeStatistics::empty(&attr.name),
                };

                // Build CBO Sketch for attribute
                if attr.type_code == 1
                    || attr.type_code == 2
                    || attr.type_code == 3
                    || attr.type_code == 4
                {
                    let mut hll = HyperLogLogSketch::new(10);
                    let mut hist = EquiDepthHistogramBuilder::new(10_000);

                    match attr.type_code {
                        1 => {
                            for &chunk in data.as_chunks::<4>().0 {
                                let v = i32::from_le_bytes(chunk);
                                if v == i32::MIN {
                                    hist.offer_null();
                                } else {
                                    hll.offer_long(v as u64);
                                    hist.offer(v as f64);
                                }
                            }
                        }
                        2 => {
                            for &chunk in data.as_chunks::<8>().0 {
                                let v = i64::from_le_bytes(chunk);
                                if v == i64::MIN {
                                    hist.offer_null();
                                } else {
                                    hll.offer_long(v as u64);
                                    hist.offer(v as f64);
                                }
                            }
                        }
                        3 => {
                            for &chunk in data.as_chunks::<4>().0 {
                                let v = f32::from_le_bytes(chunk);
                                if v.is_nan() {
                                    hist.offer_null();
                                } else {
                                    hll.offer_long((v as f64).to_bits());
                                    hist.offer(v as f64);
                                }
                            }
                        }
                        4 => {
                            for &chunk in data.as_chunks::<8>().0 {
                                let v = f64::from_le_bytes(chunk);
                                if v.is_nan() {
                                    hist.offer_null();
                                } else {
                                    hll.offer_long(v.to_bits());
                                    hist.offer(v);
                                }
                            }
                        }
                        _ => {}
                    }

                    sketches_map.insert(
                        format!("impulse.stats.relation.{}.attr.{}", r_idx, a_idx),
                        hist.to_json(20, hll.estimate()),
                    );
                }

                current_rel_attrs.push(attr_stat);
            }
        }

        rel_stats_list.push((rel_name, stats));
        attr_stats_list.push(current_rel_attrs);
    }

    if format.eq_ignore_ascii_case("json") {
        let mut rels_json = Vec::new();
        for (idx, (name, s)) in rel_stats_list.iter().enumerate() {
            let mult_str = match s.multiplicity {
                Multiplicity::OneToOne => "ONE_TO_ONE",
                Multiplicity::ManyToOne => "MANY_TO_ONE",
                Multiplicity::OneToMany => "ONE_TO_MANY",
                Multiplicity::ManyToMany => "MANY_TO_MANY",
            };

            let mut attrs_json = Vec::new();
            for a in &attr_stats_list[idx] {
                let mono_str = format!("{:?}", a.monotonicity).to_uppercase();
                attrs_json.push(json!({
                    "name": a.name,
                    "min_int": a.min_int_val,
                    "max_int": a.max_int_val,
                    "min_float": a.min_float_val,
                    "max_float": a.max_float_val,
                    "null_count": a.null_count,
                    "distinct_count": a.distinct_count,
                    "monotonicity": mono_str,
                    "has_nulls": a.has_nulls,
                }));
            }

            rels_json.push(json!({
                "relation_id": idx,
                "name": name,
                "node_count": s.node_count,
                "edge_count": s.edge_count,
                "unique_source_nodes": s.unique_source_nodes,
                "max_out_degree": s.max_out_degree,
                "avg_out_degree": s.avg_out_degree,
                "std_dev_degree": s.std_dev_degree,
                "p50_degree": s.p50_degree,
                "p90_degree": s.p90_degree,
                "p99_degree": s.p99_degree,
                "sparsity": s.sparsity,
                "supernode_count": s.supernode_count,
                "multiplicity": mult_str,
                "max_in_degree": s.max_in_degree,
                "avg_in_degree": s.avg_in_degree,
                "is_functional": s.is_functional(),
                "is_injective": s.is_injective(),
                "is_bijective": s.is_bijective(),
                "attributes": attrs_json,
            }));
        }

        let output = json!({
            "file": file.to_string_lossy(),
            "size_bytes": metadata.len(),
            "calculation_time_ms": start_time.elapsed().as_secs_f64() * 1000.0,
            "relations": rels_json,
            "cbo_sketches": sketches_map,
        });

        println!("{}", serde_json::to_string_pretty(&output)?);
        return Ok(());
    }

    println!("=========================================================================");
    println!("             IMPULSE GRAPH ENGINE TOPOLOGY & CBO STATISTICS             ");
    println!("=========================================================================");
    println!("File:              {}", file.display());
    println!("File Size:         {} bytes", metadata.len());
    println!("Domains:           {}", header.domain_count());
    println!("Relations:         {}", header.relation_count());
    println!(
        "Calculation Time:  {:.2} ms",
        start_time.elapsed().as_secs_f64() * 1000.0
    );
    println!();

    // 1. Relation Structural Multiplicity & Cardinality Table
    println!("--- RELATION TOPOLOGY & MULTIPLICITY ---");
    let mut rel_table = Table::new();
    rel_table.set_header(vec![
        "ID",
        "Relation",
        "Multiplicity",
        "Algebraic Traits",
        "Nodes",
        "Edges",
        "Unique Src",
        "Sparsity",
    ]);

    for (idx, (name, s)) in rel_stats_list.iter().enumerate() {
        let (mult_str, mult_color) = match s.multiplicity {
            Multiplicity::OneToOne => ("1:1 (Bijective)", Color::Green),
            Multiplicity::ManyToOne => ("M:1 (Functional)", Color::Cyan),
            Multiplicity::OneToMany => ("1:M (Injective)", Color::Yellow),
            Multiplicity::ManyToMany => ("M:M (General)", Color::White),
        };

        let mut traits = Vec::new();
        if s.is_functional() {
            traits.push("Functional");
        }
        if s.is_injective() {
            traits.push("Injective");
        }
        if s.is_bijective() {
            traits.push("Bijective");
        }
        let trait_str = if traits.is_empty() {
            "None".to_string()
        } else {
            traits.join(", ")
        };

        rel_table.add_row(vec![
            Cell::new(idx.to_string()),
            Cell::new(name.clone()),
            Cell::new(mult_str).fg(mult_color),
            Cell::new(trait_str),
            Cell::new(s.node_count.to_string()),
            Cell::new(s.edge_count.to_string()),
            Cell::new(s.unique_source_nodes.to_string()),
            Cell::new(format!("{:.4}", s.sparsity)),
        ]);
    }
    println!("{}", rel_table);
    println!();

    // 2. Degree Distribution & Percentiles Table
    println!("--- DEGREE DISTRIBUTIONS & PERCENTILES ---");
    let mut deg_table = Table::new();
    deg_table.set_header(vec![
        "ID",
        "Relation",
        "Avg Out",
        "StdDev",
        "Max Out",
        "Max In",
        "P50",
        "P90",
        "P99",
        "Supernodes",
    ]);

    for (idx, (name, s)) in rel_stats_list.iter().enumerate() {
        let super_str = format!(
            "{} ({:.2}%)",
            s.supernode_count,
            (s.supernode_count as f64 / s.node_count.max(1) as f64) * 100.0
        );
        deg_table.add_row(vec![
            Cell::new(idx.to_string()),
            Cell::new(name.clone()),
            Cell::new(format!("{:.2}", s.avg_out_degree)),
            Cell::new(format!("{:.2}", s.std_dev_degree)),
            Cell::new(s.max_out_degree.to_string()),
            Cell::new(s.max_in_degree.to_string()),
            Cell::new(s.p50_degree.to_string()),
            Cell::new(s.p90_degree.to_string()),
            Cell::new(s.p99_degree.to_string()).fg(if s.p99_degree > 100 {
                Color::Yellow
            } else {
                Color::White
            }),
            Cell::new(super_str).fg(if s.supernode_count > 0 {
                Color::Cyan
            } else {
                Color::White
            }),
        ]);
    }
    println!("{}", deg_table);
    println!();

    // 3. Attribute Zone Maps & Monotonicity
    let total_attrs: usize = attr_stats_list.iter().map(|a| a.len()).sum();
    if total_attrs > 0 {
        println!("--- ATTRIBUTE ZONE MAPS & MONOTONICITY ---");
        let mut attr_table = Table::new();
        attr_table.set_header(vec![
            "Relation",
            "Attribute",
            "Min Val",
            "Max Val",
            "Distinct",
            "Nulls",
            "Monotonicity",
        ]);

        for (rel_idx, (rel_name, _)) in rel_stats_list.iter().enumerate() {
            for a in &attr_stats_list[rel_idx] {
                let (min_str, max_str) = if a.min_float_val != 0.0 || a.max_float_val != 0.0 {
                    (
                        format!("{:.4}", a.min_float_val),
                        format!("{:.4}", a.max_float_val),
                    )
                } else {
                    (a.min_int_val.to_string(), a.max_int_val.to_string())
                };

                let mono_str = format!("{:?}", a.monotonicity);

                attr_table.add_row(vec![
                    Cell::new(rel_name.clone()),
                    Cell::new(a.name.clone()),
                    Cell::new(min_str),
                    Cell::new(max_str),
                    Cell::new(a.distinct_count.to_string()),
                    Cell::new(a.null_count.to_string()),
                    Cell::new(mono_str).fg(
                        if a.monotonicity != impulse_graph::stats::Monotonicity::None {
                            Color::Green
                        } else {
                            Color::White
                        },
                    ),
                ]);
            }
        }
        println!("{}", attr_table);
        println!();
    }

    if verbose {
        println!("--- CBO SKETCHES JSON METADATA ---");
        for (k, v) in &sketches_map {
            println!("{}: {}", k, v);
        }
        println!();
    }

    Ok(())
}
