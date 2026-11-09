use clap::CommandFactory;
use clap_mangen::Man;
use std::error::Error;
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;

use crate::Cli;

pub fn run(man_dir: &Path, website_cli_md: Option<&Path>) -> Result<(), Box<dyn Error>> {
    println!("Generating automated CLI documentation & man pages...");
    fs::create_dir_all(man_dir)?;

    let mut cmd = Cli::command();

    // 1. Generate Roff Man Pages using clap_mangen
    let main_man_path = man_dir.join("impulse-graph.1");
    let mut main_file = File::create(&main_man_path)?;
    Man::new(cmd.clone()).render(&mut main_file)?;
    println!("  [OK] Rendered main man page: {}", main_man_path.display());

    for subcmd in cmd.get_subcommands_mut() {
        let sub_name = subcmd.get_name().to_string();
        let man_filename = format!("impulse-graph-{}.1", sub_name);
        let man_path = man_dir.join(&man_filename);
        let mut sub_file = File::create(&man_path)?;
        Man::new(subcmd.clone()).render(&mut sub_file)?;
        println!(
            "  [OK] Rendered subcommand man page: {}",
            man_path.display()
        );
    }

    // 2. Generate MkDocs Markdown for website if requested
    if let Some(md_path) = website_cli_md {
        if let Some(parent) = md_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut md_file = File::create(md_path)?;

        writeln!(md_file, "# CLI Reference (`impulse` / `impulse-graph`)")?;
        writeln!(
            md_file,
            "\nAuto-generated CLI documentation for the Impulse Graph Engine developer tooling suite.\n"
        )?;
        writeln!(
            md_file,
            "!!! note \"Canonical Executable\"\n    The primary executable name is `impulse-graph`, symlinked as `impulse`.\n"
        )?;

        let root_cmd = Cli::command();
        writeln!(
            md_file,
            "## Global Usage\n```bash\nimpulse <COMMAND> [OPTIONS]\n```\n"
        )?;

        writeln!(md_file, "## Subcommands\n")?;
        for subcmd in root_cmd.get_subcommands() {
            let name = subcmd.get_name();
            let about = subcmd.get_about().unwrap_or_default();
            writeln!(md_file, "### `impulse {}`", name)?;
            writeln!(md_file, "{}\n", about)?;

            writeln!(md_file, "**Usage:**")?;
            writeln!(md_file, "```bash\nimpulse {} [OPTIONS]\n```\n", name)?;

            let opts: Vec<_> = subcmd.get_opts().collect();
            let positionals: Vec<_> = subcmd.get_positionals().collect();

            if !positionals.is_empty() {
                writeln!(md_file, "**Arguments:**\n")?;
                writeln!(md_file, "| Argument | Description |")?;
                writeln!(md_file, "| :--- | :--- |")?;
                for p in positionals {
                    let p_name = p.get_id().as_str();
                    let p_help = p.get_help().unwrap_or_default();
                    writeln!(md_file, "| `<{}>` | {} |", p_name, p_help)?;
                }
                writeln!(md_file)?;
            }

            if !opts.is_empty() {
                writeln!(md_file, "**Flags & Options:**\n")?;
                writeln!(md_file, "| Flag / Option | Description | Default |")?;
                writeln!(md_file, "| :--- | :--- | :--- |")?;
                for opt in opts {
                    let mut flag_str = String::new();
                    if let Some(short) = opt.get_short() {
                        flag_str.push_str(&format!("`-{}`, ", short));
                    }
                    if let Some(long) = opt.get_long() {
                        flag_str.push_str(&format!("`--{}`", long));
                    }
                    let help = opt.get_help().unwrap_or_default();
                    let default_val = opt
                        .get_default_values()
                        .iter()
                        .map(|v| v.to_string_lossy())
                        .collect::<Vec<_>>()
                        .join(", ");

                    writeln!(md_file, "| {} | {} | {} |", flag_str, help, default_val)?;
                }
                writeln!(md_file)?;
            }
            writeln!(md_file, "---")?;
        }

        println!(
            "  [OK] Rendered website CLI markdown: {}",
            md_path.display()
        );
    }

    println!("SUCCESS: Automated documentation generation complete.");
    Ok(())
}
