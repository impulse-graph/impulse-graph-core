use impulse_graph_tooling::commands;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let man_dir = PathBuf::from("man");
    let website_md = PathBuf::from("../impulse-website/docs/reference/cli.md");

    println!("[Build Tool] Generating documentation from Rust source AST...");
    commands::docgen::run(&man_dir, Some(&website_md))?;

    Ok(())
}
