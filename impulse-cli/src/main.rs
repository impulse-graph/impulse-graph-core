use clap::Parser;
use impulse_graph_tooling::commands;
use impulse_graph_tooling::{
    Cli, Commands, CompilerCommands, CryptoCommands, IndexCommands, SnapshotCommands,
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    match cli.command {
        // Top-Level Ergonomic & Shorthand Commands
        Commands::Run {
            snapshot,
            bytecode,
            input_val,
            verify,
            never_corrupt,
            opt_disable_mask,
            diff_opt,
            fault_delay,
            fault_repeat,
            branch_cov,
        } => {
            commands::run::run_with_options(
                &snapshot,
                &bytecode,
                input_val,
                &commands::run::RunVerifyOptions {
                    verify,
                    never_corrupt,
                    opt_disable_mask,
                    diff_opt,
                    fault_sim: fault_delay.map(|d| (d, fault_repeat)),
                    branch_cov,
                },
            )?;
        }
        Commands::Compile {
            input,
            output,
            emit_ir,
        } => {
            commands::script_compile::run(&input, output.as_deref(), emit_ir)?;
        }
        Commands::Build {
            manifest,
            output,
            embeddings,
            vector_index,
        } => {
            commands::compile::run_with_options(&manifest, &output, &embeddings, &vector_index)?;
        }
        Commands::BenchVector(args) => {
            commands::bench_vector::run(&args)?;
        }
        Commands::Inspect {
            file,
            format,
            verbose,
            anonymize,
            domain,
            relation,
            page_map,
        } => {
            commands::inspect::run_with_options(
                &file,
                &commands::inspect::InspectOptions {
                    format: &format,
                    verbose,
                    anonymize,
                    domain_filter: domain.as_deref(),
                    relation_filter: relation.as_deref(),
                    page_map,
                },
            )?;
        }
        Commands::Assemble { input, output } => {
            commands::assemble::run(&input, &output)?;
        }
        Commands::Disassemble { input } => {
            commands::disassemble::run(&input)?;
        }
        Commands::Generate(args) => {
            commands::generate::run(&args)?;
        }
        Commands::Stats {
            file,
            format,
            verbose,
            supernode_threshold,
        } => {
            commands::stats::run(&file, &format, verbose, supernode_threshold)?;
        }

        // Compiler Namespace (Code Only)
        Commands::Compiler { command } => match command {
            CompilerCommands::Compile {
                input,
                output,
                emit_ir,
            }
            | CompilerCommands::Build {
                input,
                output,
                emit_ir,
            } => {
                commands::script_compile::run(&input, output.as_deref(), emit_ir)?;
            }
            CompilerCommands::Assemble { input, output } => {
                commands::assemble::run(&input, &output)?;
            }
            CompilerCommands::Disassemble { input } => {
                commands::disassemble::run(&input)?;
            }
            CompilerCommands::Inspect { input } => {
                commands::disassemble::run(&input)?;
            }
        },

        // Snapshot Namespace (Snapshots / Datasets Only)
        Commands::Snapshot { command } => match command {
            SnapshotCommands::Build {
                manifest,
                output,
                embeddings,
                vector_index,
            } => {
                commands::compile::run_with_options(
                    &manifest,
                    &output,
                    &embeddings,
                    &vector_index,
                )?;
            }
            SnapshotCommands::AttachEmbeddings {
                input,
                output,
                embeddings,
                vector_index,
            } => {
                commands::optimize::run_with_options(
                    &input,
                    &output,
                    &commands::optimize::OptimizeOptions {
                        embeddings,
                        vector_indices: vector_index,
                        ..Default::default()
                    },
                )?;
            }
            SnapshotCommands::BenchVector(args) => {
                commands::bench_vector::run(&args)?;
            }
            SnapshotCommands::Merge {
                base,
                deltas,
                output,
            } => {
                commands::merge::run(&base, &deltas, &output)?;
            }
            SnapshotCommands::Inspect {
                file,
                format,
                verbose,
                anonymize,
                domain,
                relation,
                page_map,
            } => {
                commands::inspect::run_with_options(
                    &file,
                    &commands::inspect::InspectOptions {
                        format: &format,
                        verbose,
                        anonymize,
                        domain_filter: domain.as_deref(),
                        relation_filter: relation.as_deref(),
                        page_map,
                    },
                )?;
            }
            SnapshotCommands::Validate {
                file,
                strict_alignment,
                public_key,
                quick_check,
                deep_audit,
            } => {
                commands::validate::run_with_options(
                    &file,
                    &commands::validate::ValidateOptions {
                        strict_alignment,
                        public_key: public_key.as_deref(),
                        quick_check,
                        deep_audit,
                    },
                )?;
            }
            SnapshotCommands::Optimize {
                input,
                output,
                rcm,
                degree_sort,
                vamana_locality,
                rebuild_vector_index,
                embeddings,
                vector_index,
                csc,
                encoding,
                strip_mappings,
                strip_properties,
            } => {
                commands::optimize::run_with_options(
                    &input,
                    &output,
                    &commands::optimize::OptimizeOptions {
                        rcm,
                        degree_sort,
                        vamana_locality,
                        csc,
                        encoding,
                        strip_mappings,
                        strip_properties,
                        rebuild_vector_index,
                        embeddings,
                        vector_indices: vector_index,
                    },
                )?;
            }
            SnapshotCommands::Diff { base, target } => {
                commands::diff::run(&base, &target)?;
            }
            SnapshotCommands::Export {
                file,
                out_dir,
                format,
            } => {
                commands::export::run(&file, &out_dir, &format)?;
            }
            SnapshotCommands::ConvertTensors {
                input,
                output,
                domain,
                vector_index,
            } => {
                let vspecs: Vec<String> = vector_index.into_iter().collect();
                commands::safetensors::run_with_options(&input, &output, &domain, &vspecs)?;
            }
            SnapshotCommands::Generate(args) => {
                commands::generate::run(&args)?;
            }
            SnapshotCommands::Stats {
                file,
                format,
                verbose,
                supernode_threshold,
            } => {
                commands::stats::run(&file, &format, verbose, supernode_threshold)?;
            }
            SnapshotCommands::Index { command } => match command {
                IndexCommands::List { file, format } => {
                    commands::index::list(&file, &format)?;
                }
                IndexCommands::Add {
                    input,
                    output,
                    specs,
                } => {
                    commands::index::add(&input, &output, &specs)?;
                }
                IndexCommands::Drop {
                    input,
                    output,
                    name,
                    index_type,
                    all,
                } => {
                    commands::index::drop(
                        &input,
                        &output,
                        name.as_deref(),
                        index_type.as_deref(),
                        all,
                    )?;
                }
            },
            SnapshotCommands::Sample {
                file,
                limit,
                domain,
                relation,
                format,
            } => {
                commands::sample::run(
                    &file,
                    domain.as_deref(),
                    relation.as_deref(),
                    limit,
                    0,
                    &format,
                )?;
            }
            SnapshotCommands::Transmute(args) => {
                commands::transmute::run(&args)?;
            }
        },

        // Crypto Namespace
        Commands::Crypto { command } => match command {
            CryptoCommands::Keygen { out } => {
                commands::crypto::keygen(&out)?;
            }
            CryptoCommands::Sign { file, key } => {
                commands::crypto::sign(&file, &key)?;
            }
            CryptoCommands::Verify { file, key } => {
                commands::crypto::verify(&file, &key)?;
            }
        },
    }

    Ok(())
}
