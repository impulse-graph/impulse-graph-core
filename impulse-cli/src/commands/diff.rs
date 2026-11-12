use comfy_table::Table;
use impulse_graph::SnapshotReader;
use serde_json::json;
use std::collections::HashSet;
use std::error::Error;
use std::path::Path;

pub fn run(base_path: &Path, target_path: &Path) -> Result<(), Box<dyn Error>> {
    run_with_format(base_path, target_path, "text")
}

pub fn run_with_format(base_path: &Path, target_path: &Path, format: &str) -> Result<(), Box<dyn Error>> {
    let base = SnapshotReader::open(base_path)?;
    let target = SnapshotReader::open(target_path)?;

    let base_header = base.header();
    let target_header = target.header();

    let is_json = format.eq_ignore_ascii_case("json");

    if is_json {
        let mut diff_obj = serde_json::Map::new();

        diff_obj.insert("header".into(), json!({
            "magic_match": base_header.magic() == target_header.magic(),
            "version_match": base_header.version() == target_header.version(),
            "base_domains": base.domain_count(),
            "target_domains": target.domain_count(),
            "base_relations": base.relation_count(),
            "target_relations": target.relation_count(),
            "base_features": format!("0x{:016X}", base_header.required_features()),
            "target_features": format!("0x{:016X}", target_header.required_features()),
        }));

        let mut domain_diffs = Vec::new();
        let mut dom_names: HashSet<String> = HashSet::new();
        for d in base.domains() { dom_names.insert(d.name.clone()); }
        for d in target.domains() { dom_names.insert(d.name.clone()); }

        for name in dom_names {
            let b_d = base.domains().iter().find(|d| d.name == name);
            let t_d = target.domains().iter().find(|d| d.name == name);
            domain_diffs.push(json!({
                "domain": name,
                "in_base": b_d.is_some(),
                "in_target": t_d.is_some(),
                "base_nodes": b_d.map(|d| d.node_count),
                "target_nodes": t_d.map(|d| d.node_count),
            }));
        }
        diff_obj.insert("domains".into(), json!(domain_diffs));

        let mut rel_diffs = Vec::new();
        let mut rel_names: HashSet<String> = HashSet::new();
        for r in base.relations() { rel_names.insert(r.name.clone()); }
        for r in target.relations() { rel_names.insert(r.name.clone()); }

        for name in rel_names {
            let b_r = base.relations().iter().find(|r| r.name == name);
            let t_r = target.relations().iter().find(|r| r.name == name);
            rel_diffs.push(json!({
                "relation": name,
                "in_base": b_r.is_some(),
                "in_target": t_r.is_some(),
                "base_edges": b_r.map(|r| r.edge_count),
                "target_edges": t_r.map(|r| r.edge_count),
                "base_csc": b_r.map(|r| r.has_csc()),
                "target_csc": t_r.map(|r| r.has_csc()),
            }));
        }
        diff_obj.insert("relations".into(), json!(rel_diffs));

        // Index Diffs
        let mut idx_diffs = Vec::new();
        let mut idx_names: HashSet<String> = HashSet::new();
        for i in base.index_entries() { idx_names.insert(i.name.clone()); }
        for i in target.index_entries() { idx_names.insert(i.name.clone()); }

        for name in idx_names {
            let b_i = base.index_entries().iter().find(|i| i.name == name);
            let t_i = target.index_entries().iter().find(|i| i.name == name);
            idx_diffs.push(json!({
                "index": name,
                "in_base": b_i.is_some(),
                "in_target": t_i.is_some(),
                "base_type": b_i.map(|i| i.index_type),
                "target_type": t_i.map(|i| i.index_type),
                "base_bytes": b_i.map(|i| i.data_bytes),
                "target_bytes": t_i.map(|i| i.data_bytes),
            }));
        }
        diff_obj.insert("indexes".into(), json!(idx_diffs));

        println!("{}", serde_json::Value::Object(diff_obj));
        return Ok(());
    }

    println!(
        "Comparing snapshot diff: BASE ({}) vs TARGET ({})...",
        base_path.display(),
        target_path.display()
    );

    println!();
    println!("--- HEADER COMPARISON ---");
    let mut header_table = Table::new();
    header_table.set_header(vec!["Property", "BASE", "TARGET", "Match"]);

    header_table.add_row(vec![
        "Magic".to_string(),
        format!("0x{:08X}", base_header.magic()),
        format!("0x{:08X}", target_header.magic()),
        if base_header.magic() == target_header.magic() { "YES".to_string() } else { "NO".to_string() },
    ]);
    header_table.add_row(vec![
        "Version".to_string(),
        format!("0x{:04X}", base_header.version()),
        format!("0x{:04X}", target_header.version()),
        if base_header.version() == target_header.version() { "YES".to_string() } else { "NO".to_string() },
    ]);
    header_table.add_row(vec![
        "Features".to_string(),
        format!("0x{:016X}", base_header.required_features()),
        format!("0x{:016X}", target_header.required_features()),
        if base_header.required_features() == target_header.required_features() { "YES".to_string() } else { "NO".to_string() },
    ]);
    header_table.add_row(vec![
        "Domain Count".to_string(),
        base.domain_count().to_string(),
        target.domain_count().to_string(),
        if base.domain_count() == target.domain_count() { "YES".to_string() } else { "NO".to_string() },
    ]);
    header_table.add_row(vec![
        "Relation Count".to_string(),
        base.relation_count().to_string(),
        target.relation_count().to_string(),
        if base.relation_count() == target.relation_count() { "YES".to_string() } else { "NO".to_string() },
    ]);
    println!("{}", header_table);

    println!();
    println!("--- DOMAIN CATALOG DIFFERENCES ---");
    let mut dom_table = Table::new();
    dom_table.set_header(vec!["Domain Name", "BASE Nodes", "TARGET Nodes", "KeyType Match", "Status"]);

    let mut dom_names: Vec<String> = base.domains().iter().map(|d| d.name.clone()).collect();
    for d in target.domains() {
        if !dom_names.contains(&d.name) {
            dom_names.push(d.name.clone());
        }
    }
    for name in &dom_names {
        let b_d = base.domains().iter().find(|d| d.name == *name);
        let t_d = target.domains().iter().find(|d| d.name == *name);
        let b_cnt = b_d.map(|d| d.node_count.to_string()).unwrap_or_else(|| "-".into());
        let t_cnt = t_d.map(|d| d.node_count.to_string()).unwrap_or_else(|| "-".into());
        let key_match = if let (Some(b), Some(t)) = (b_d, t_d) {
            if b.key_type == t.key_type { "YES".into() } else { "NO".into() }
        } else {
            "-".into()
        };
        let status = match (b_d, t_d) {
            (Some(_), Some(_)) => "MATCH/CHANGED",
            (Some(_), None) => "REMOVED_IN_TARGET",
            (None, Some(_)) => "ADDED_IN_TARGET",
            (None, None) => "-",
        };
        dom_table.add_row(vec![name.clone(), b_cnt, t_cnt, key_match, status.into()]);
    }
    println!("{}", dom_table);

    println!();
    println!("--- RELATION TOPOLOGY DIFFERENCES ---");
    let mut rel_table = Table::new();
    rel_table.set_header(vec![
        "Relation",
        "BASE Edges",
        "TARGET Edges",
        "Delta Edges",
        "BASE CSC",
        "TARGET CSC",
        "Attr Count",
    ]);

    let mut rel_names: Vec<String> = base.relations().iter().map(|r| r.name.clone()).collect();
    for r in target.relations() {
        if !rel_names.contains(&r.name) {
            rel_names.push(r.name.clone());
        }
    }
    for name in &rel_names {
        let b_r = base.relations().iter().find(|r| r.name == *name);
        let t_r = target.relations().iter().find(|r| r.name == *name);

        let b_edges = b_r.map(|r| r.edge_count).unwrap_or(0);
        let t_edges = t_r.map(|r| r.edge_count).unwrap_or(0);
        let delta = t_edges as i64 - b_edges as i64;

        let b_csc = b_r.map(|r| if r.has_csc() { "YES" } else { "NO" }).unwrap_or("-");
        let t_csc = t_r.map(|r| if r.has_csc() { "YES" } else { "NO" }).unwrap_or("-");

        let attr_info = format!("{}/{}", b_r.map(|r| r.attributes.len()).unwrap_or(0), t_r.map(|r| r.attributes.len()).unwrap_or(0));

        rel_table.add_row(vec![
            name.clone(),
            b_edges.to_string(),
            t_edges.to_string(),
            format!("{:+}", delta),
            b_csc.into(),
            t_csc.into(),
            attr_info,
        ]);
    }
    println!("{}", rel_table);

    println!();
    println!("--- SECONDARY INDEX DIRECTORY DIFFERENCES ---");
    let mut idx_table = Table::new();
    idx_table.set_header(vec!["Index Name", "Type", "BASE Bytes", "TARGET Bytes", "Status"]);

    let mut idx_names: Vec<String> = base.index_entries().iter().map(|i| i.name.clone()).collect();
    for i in target.index_entries() {
        if !idx_names.contains(&i.name) {
            idx_names.push(i.name.clone());
        }
    }

    if idx_names.is_empty() {
        println!("  (No secondary indexes found in either snapshot)");
    } else {
        for name in &idx_names {
            let b_i = base.index_entries().iter().find(|i| i.name == *name);
            let t_i = target.index_entries().iter().find(|i| i.name == *name);
            let type_str = b_i.or(t_i).map(|i| format!("0x{:02X}", i.index_type)).unwrap_or("-".into());
            let b_bytes = b_i.map(|i| i.data_bytes.to_string()).unwrap_or("-".into());
            let t_bytes = t_i.map(|i| i.data_bytes.to_string()).unwrap_or("-".into());
            let status = match (b_i, t_i) {
                (Some(_), Some(_)) => "MATCH",
                (Some(_), None) => "REMOVED_IN_TARGET",
                (None, Some(_)) => "ADDED_IN_TARGET",
                (None, None) => "-",
            };
            idx_table.add_row(vec![name.clone(), type_str, b_bytes, t_bytes, status.into()]);
        }
        println!("{}", idx_table);
    }

    Ok(())
}
