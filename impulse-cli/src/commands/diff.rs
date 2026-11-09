use comfy_table::Table;
use impulse_graph::SnapshotReader;
use std::error::Error;
use std::path::Path;

pub fn run(base_path: &Path, target_path: &Path) -> Result<(), Box<dyn Error>> {
    println!(
        "Comparing snapshot diff: BASE ({}) vs TARGET ({})...",
        base_path.display(),
        target_path.display()
    );

    let base = SnapshotReader::open(base_path)?;
    let target = SnapshotReader::open(target_path)?;

    let base_header = base.header();
    let target_header = target.header();

    println!();
    println!("--- HEADER COMPARISON ---");
    let mut header_table = Table::new();
    header_table.set_header(vec!["Property", "BASE", "TARGET", "Match"]);

    header_table.add_row(vec![
        "Magic".to_string(),
        format!("0x{:08X}", base_header.magic()),
        format!("0x{:08X}", target_header.magic()),
        if base_header.magic() == target_header.magic() {
            "YES".to_string()
        } else {
            "NO".to_string()
        },
    ]);
    header_table.add_row(vec![
        "Version".to_string(),
        format!("0x{:04X}", base_header.version()),
        format!("0x{:04X}", target_header.version()),
        if base_header.version() == target_header.version() {
            "YES".to_string()
        } else {
            "NO".to_string()
        },
    ]);
    header_table.add_row(vec![
        "Domain Count".to_string(),
        base.domain_count().to_string(),
        target.domain_count().to_string(),
        if base.domain_count() == target.domain_count() {
            "YES".to_string()
        } else {
            "NO".to_string()
        },
    ]);
    header_table.add_row(vec![
        "Relation Count".to_string(),
        base.relation_count().to_string(),
        target.relation_count().to_string(),
        if base.relation_count() == target.relation_count() {
            "YES".to_string()
        } else {
            "NO".to_string()
        },
    ]);
    println!("{}", header_table);

    println!();
    println!("--- RELATION TOPOLOGY DIFFERENCES ---");
    let mut rel_table = Table::new();
    rel_table.set_header(vec![
        "Rel ID",
        "BASE Nodes",
        "TARGET Nodes",
        "BASE Edges",
        "TARGET Edges",
        "Delta Edges",
    ]);

    let max_rel = std::cmp::max(base.relation_count(), target.relation_count());
    for i in 0..max_rel as usize {
        let base_rel = base.relations().get(i);
        let target_rel = target.relations().get(i);

        let base_nodes = base_rel
            .map(|r| r.node_count.to_string())
            .unwrap_or_else(|| "-".into());
        let target_nodes = target_rel
            .map(|r| r.node_count.to_string())
            .unwrap_or_else(|| "-".into());

        let base_edges = base_rel.map(|r| r.edge_count).unwrap_or(0);
        let target_edges = target_rel.map(|r| r.edge_count).unwrap_or(0);
        let delta = target_edges as i64 - base_edges as i64;

        rel_table.add_row(vec![
            i.to_string(),
            base_nodes,
            target_nodes,
            base_edges.to_string(),
            target_edges.to_string(),
            format!("{:+}", delta),
        ]);
    }
    println!("{}", rel_table);

    Ok(())
}
