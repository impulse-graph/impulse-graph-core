use impulse_graph::SnapshotReader;
use impulse_graph_tooling::commands;
use std::fs;
use std::path::PathBuf;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_index_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

#[test]
fn test_index_list_add_drop_lifecycle() {
    let test_dir = setup_test_workspace();
    let manifest_path = test_dir.join("manifest.json");
    let edges_path = test_dir.join("edges.tsv");
    let initial_snap = test_dir.join("initial.imps");
    let indexed_snap = test_dir.join("indexed.imps");
    let dropped_one_snap = test_dir.join("dropped_one.imps");
    let dropped_all_snap = test_dir.join("dropped_all.imps");

    fs::write(&edges_path, "1\t2\t100\n2\t3\t200\n3\t4\t150\n4\t1\t300\n").unwrap();

    let manifest_json = r#"{
        "version": "0.9.0",
        "domains": [
            { "id": 0, "name": "Account", "key_type": "uint64" }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "edges.tsv",
                "name": "TRANSFERS",
                "attributes": [
                    { "name": "amount", "column": "amount", "type": "int32" }
                ]
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    let build_res = commands::compile::run(&manifest_path, &initial_snap);
    assert!(build_res.is_ok(), "Initial build failed: {:?}", build_res);

    // 1. Initial snapshot has the default domain key index (MPHF)
    let list_res = commands::index::list(&initial_snap, "json");
    assert!(list_res.is_ok());

    let reader_init = SnapshotReader::open(&initial_snap).unwrap();
    let initial_count = reader_init.index_entries().len();
    assert_eq!(initial_count, 1);

    // 2. Add indexes: permutation and zonemap on TRANSFERS.amount
    let specs = vec![
        "TRANSFERS.amount:permutation".to_string(),
        "TRANSFERS.amount:zonemap:block_size=2".to_string(),
    ];
    let add_res = commands::index::add(&initial_snap, &indexed_snap, &specs);
    assert!(add_res.is_ok(), "Index add failed: {:?}", add_res);
    assert!(indexed_snap.exists());

    // Verify indexes present
    let reader_indexed = SnapshotReader::open(&indexed_snap).unwrap();
    let entries = reader_indexed.index_entries();
    assert_eq!(entries.len(), initial_count + 2);
    // Find the newly added entries
    let perm_entry = entries.iter().find(|e| e.index_type == 0x01).expect("Permutation index not found");
    let zone_entry = entries.iter().find(|e| e.index_type == 0x02).expect("ZoneMap index not found");
    assert_eq!(perm_entry.index_type, 0x01);
    assert_eq!(zone_entry.index_type, 0x02);

    // List in text & JSON format
    assert!(commands::index::list(&indexed_snap, "text").is_ok());
    assert!(commands::index::list(&indexed_snap, "json").is_ok());

    // 3. Drop one by name (drop the permutation index)
    let drop_one_res = commands::index::drop(
        &indexed_snap,
        &dropped_one_snap,
        Some(&perm_entry.name),
        None,
        false,
    );
    assert!(drop_one_res.is_ok(), "Drop one failed: {:?}", drop_one_res);
    let reader_drop_one = SnapshotReader::open(&dropped_one_snap).unwrap();
    assert_eq!(reader_drop_one.index_entries().len(), initial_count + 1);
    assert!(reader_drop_one.index_entries().iter().all(|e| e.name != perm_entry.name));

    // 3b. Drop by type: drop zonemap
    let dropped_type_snap = test_dir.join("dropped_type.imps");
    let drop_type_res = commands::index::drop(
        &dropped_one_snap,
        &dropped_type_snap,
        None,
        Some("zonemap"),
        false,
    );
    assert!(drop_type_res.is_ok(), "Drop type failed: {:?}", drop_type_res);
    let reader_drop_type = SnapshotReader::open(&dropped_type_snap).unwrap();
    assert_eq!(reader_drop_type.index_entries().len(), initial_count);
    assert!(reader_drop_type.index_entries().iter().all(|e| e.index_type != 0x02));

    // 4. Drop all
    let drop_all_res = commands::index::drop(
        &dropped_type_snap,
        &dropped_all_snap,
        None,
        None,
        true,
    );
    assert!(drop_all_res.is_ok(), "Drop all failed: {:?}", drop_all_res);
    let reader_drop_all = SnapshotReader::open(&dropped_all_snap).unwrap();
    assert_eq!(reader_drop_all.index_entries().len(), 0);

    let _ = fs::remove_dir_all(&test_dir);
}
