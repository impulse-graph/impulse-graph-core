use impulse_graph::ffi::{
    impulse_instruction_t, impulse_snapshot_close, impulse_snapshot_max_node_count,
    impulse_snapshot_open, impulse_snapshot_t, impulse_test_control,
    impulse_test_set_never_corrupt, impulse_test_set_opt_disable_mask,
    impulse_vm_branch_coverage_reset, impulse_vm_branch_coverage_set_enabled,
    impulse_vm_branch_coverage_summary, impulse_vm_context_bitset_get_word,
    impulse_vm_context_create, impulse_vm_context_destroy, impulse_vm_context_get_double_vector,
    impulse_vm_context_get_float_vector, impulse_vm_context_get_node_vector,
    impulse_vm_context_lane_frontier_active_mask, impulse_vm_context_lane_frontier_data,
    impulse_vm_context_lane_frontier_node_count, impulse_vm_context_lane_frontier_num_lanes,
    impulse_vm_context_lane_frontier_width, impulse_vm_context_node_vector_size,
    impulse_vm_context_t, impulse_vm_execute, impulse_vm_state_t, impulse_vm_validate,
    IMPULSE_TESTCTRL_FAULT_CONFIG, IMPULSE_TESTCTRL_FAULT_INSTALL, IMPULSE_TESTCTRL_NEVER_CORRUPT,
    IMPULSE_TESTCTRL_OPTIMIZATIONS,
};
use std::ffi::CString;
use std::fs::File;
use std::io::Read;
use std::path::Path;

#[derive(Debug, Clone, Default)]
pub struct RunVerifyOptions {
    /// Run static bytecode verifier (`impulse_vm_validate`) before execution.
    pub verify: bool,
    /// Enable SQLite-style `NEVER_CORRUPT` well-formedness assertions.
    pub never_corrupt: bool,
    /// Optimization disable bitmask (`IMPULSE_TESTCTRL_OPTIMIZATIONS`).
    pub opt_disable_mask: u64,
    /// Differential execution check: run once with `opt_disable_mask=0` and once with `0xFFFF_FFFF_FFFF_FFFF` and compare `R0..R63`, `flags`, `pc`, and status.
    pub diff_opt: bool,
    /// Fault simulation (`(delay, repeat)` for `IMPULSE_TESTCTRL_FAULT_CONFIG`).
    pub fault_sim: Option<(i32, i32)>,
    /// Reset and report `IMP_VM_BRANCH_TAG` coverage count & bitmap after execution.
    pub branch_cov: bool,
}

pub fn run(
    snapshot_path: &Path,
    bytecode_path: &Path,
    input_val: u64,
) -> Result<(), Box<dyn std::error::Error>> {
    run_with_options(
        snapshot_path,
        bytecode_path,
        input_val,
        &RunVerifyOptions::default(),
    )
}

pub fn run_with_options(
    snapshot_path: &Path,
    bytecode_path: &Path,
    input_val: u64,
    opts: &RunVerifyOptions,
) -> Result<(), Box<dyn std::error::Error>> {
    // Apply test-control knobs
    unsafe {
        impulse_test_set_never_corrupt(if opts.never_corrupt { 1 } else { 0 });
        let _ = impulse_test_control(
            IMPULSE_TESTCTRL_NEVER_CORRUPT,
            if opts.never_corrupt { 1i32 } else { 0i32 },
        );
        impulse_test_set_opt_disable_mask(opts.opt_disable_mask as u32);
        let _ = impulse_test_control(IMPULSE_TESTCTRL_OPTIMIZATIONS, opts.opt_disable_mask as u32);
        if let Some((delay, repeat)) = opts.fault_sim {
            let _ = impulse_test_control(IMPULSE_TESTCTRL_FAULT_INSTALL, 1i32);
            let _ = impulse_test_control(IMPULSE_TESTCTRL_FAULT_CONFIG, delay, repeat);
        }
        if opts.branch_cov {
            impulse_vm_branch_coverage_reset();
            impulse_vm_branch_coverage_set_enabled(1);
        }
    }

    // 1. Open snapshot file via FFI
    let c_path = CString::new(snapshot_path.to_str().ok_or("Invalid snapshot path")?)?;
    let mut status = 0i32;
    let snap: *mut impulse_snapshot_t =
        unsafe { impulse_snapshot_open(c_path.as_ptr(), &mut status) };

    if snap.is_null() {
        return Err(format!("Failed to open snapshot: status code {}", status).into());
    }
    println!("Loaded snapshot: {:?}", snapshot_path);

    // 2. Create VM query context
    let ctx: *mut impulse_vm_context_t = unsafe { impulse_vm_context_create(snap) };
    if ctx.is_null() {
        unsafe {
            impulse_snapshot_close(snap);
        }
        return Err("Failed to create VM context".into());
    }

    // 3. Read bytecode program file
    let mut byte_file = File::open(bytecode_path)?;
    let mut buffer = Vec::new();
    byte_file.read_to_end(&mut buffer)?;

    if buffer.len() % 8 != 0 {
        unsafe {
            impulse_vm_context_destroy(ctx);
            impulse_snapshot_close(snap);
        }
        return Err("Bytecode file size must be a multiple of 8 bytes".into());
    }

    let instruction_count = buffer.len() / 8;
    let instructions = unsafe {
        std::slice::from_raw_parts(
            buffer.as_ptr() as *const impulse_instruction_t,
            instruction_count,
        )
    };

    if opts.verify {
        let val_status = unsafe { impulse_vm_validate(instructions.as_ptr(), instruction_count) };
        if val_status != 0 {
            unsafe {
                impulse_vm_context_destroy(ctx);
                impulse_snapshot_close(snap);
            }
            return Err(format!(
                "Static bytecode verification failed with status {}",
                val_status
            )
            .into());
        }
        println!(
            "  [OK] Static bytecode verification passed ({} instructions)",
            instruction_count
        );
    }

    // 4. Initialize VM execution state
    let mut state = impulse_vm_state_t {
        pc: 0,
        reserved: 0,
        flags: 0,
        registers: [0; 64],
        register_types: [0; 64], // starts as TYPE_NULL (0x00)
        query_context: ctx,
        call_stack: [0; 8],
        call_stack_depth: 0,
        reserved_padding2: 0,
    };

    // Initialize R0 as the primary input parameter (e.g. Root Node ID)
    state.registers[0] = input_val;
    state.register_types[0] = 0x01; // TYPE_INT64

    println!(
        "Executing bytecode ({} instructions) with input R0 = {}...",
        instruction_count, input_val
    );
    let start_time = std::time::Instant::now();

    // 5. Run the VM execute loop
    let exec_status = unsafe {
        impulse_vm_execute(
            instructions.as_ptr(),
            instruction_count,
            &mut state,
            input_val,
        )
    };

    let elapsed = start_time.elapsed();
    println!(
        "Execution completed in {:.3?} with code {}",
        elapsed, exec_status
    );

    if opts.diff_opt {
        let ctx_unopt: *mut impulse_vm_context_t = unsafe { impulse_vm_context_create(snap) };
        if !ctx_unopt.is_null() {
            unsafe {
                impulse_test_set_opt_disable_mask(u32::MAX);
                let _ = impulse_test_control(IMPULSE_TESTCTRL_OPTIMIZATIONS, u32::MAX);
            }
            let mut state_unopt = impulse_vm_state_t {
                pc: 0,
                reserved: 0,
                flags: 0,
                registers: [0; 64],
                register_types: [0; 64],
                query_context: ctx_unopt,
                call_stack: [0; 8],
                call_stack_depth: 0,
                reserved_padding2: 0,
            };
            state_unopt.registers[0] = input_val;
            state_unopt.register_types[0] = 0x01;
            let status_unopt = unsafe {
                impulse_vm_execute(
                    instructions.as_ptr(),
                    instruction_count,
                    &mut state_unopt,
                    input_val,
                )
            };
            unsafe {
                impulse_test_set_opt_disable_mask(opts.opt_disable_mask as u32);
                let _ = impulse_test_control(
                    IMPULSE_TESTCTRL_OPTIMIZATIONS,
                    opts.opt_disable_mask as u32,
                );
                impulse_vm_context_destroy(ctx_unopt);
            }
            if status_unopt != exec_status
                || state_unopt.pc != state.pc
                || state_unopt.flags != state.flags
                || state_unopt.register_types != state.register_types
            {
                unsafe {
                    impulse_vm_context_destroy(ctx);
                    impulse_snapshot_close(snap);
                }
                return Err(format!(
                    "Differential optimization mismatch: opt(status={}, pc={}, flags=0x{:X}) vs unopt(status={}, pc={}, flags=0x{:X})",
                    exec_status, state.pc, state.flags, status_unopt, state_unopt.pc, state_unopt.flags
                ).into());
            }
            println!("  [OK] Differential optimization check passed (opt == unopt)");
        }
    }

    if opts.branch_cov {
        let mut total_sites: u32 = 0;
        let mut covered_sites: u32 = 0;
        let mut missing_branches: u32 = 0;
        unsafe {
            impulse_vm_branch_coverage_summary(
                &mut total_sites,
                &mut covered_sites,
                &mut missing_branches,
            );
            impulse_vm_branch_coverage_set_enabled(0);
        }
        println!(
            "  [Branch Coverage] Observed {} VM branch sites ({} fully covered, {} missing branch directions)",
            total_sites, covered_sites, missing_branches
        );
    }

    if exec_status != 0 {
        unsafe {
            impulse_vm_context_destroy(ctx);
            impulse_snapshot_close(snap);
        }
        return Err(format!("VM returned error status: {}", exec_status).into());
    }

    // 6. Format and display the output register R63
    let return_reg = 63usize;
    let ret_type = state.register_types[return_reg];
    let ret_val = state.registers[return_reg];

    println!("\n# VM Execution Results:");
    println!("Return Register R63 Type: 0x{:02X}", ret_type);

    match ret_type {
        0x04 => {
            // TYPE_BITSET_HANDLE
            let handle = ret_val as usize;
            let num_nodes = unsafe { impulse_snapshot_max_node_count(snap) } as usize;
            let word_count = num_nodes.div_ceil(64);

            let mut active_nodes = Vec::new();
            for word_idx in 0..word_count {
                let word = unsafe { impulse_vm_context_bitset_get_word(ctx, handle, word_idx) };
                if word != 0 {
                    for bit in 0..64 {
                        if word & (1 << bit) != 0 {
                            let node_id = word_idx * 64 + bit;
                            if node_id < num_nodes {
                                active_nodes.push(node_id);
                            }
                        }
                    }
                }
            }
            println!("Output BitSet Handle: {}", handle);
            println!("Cardinality: {} / {}", active_nodes.len(), num_nodes);
            if !active_nodes.is_empty() {
                let print_limit = 20usize;
                let print_len = std::cmp::min(active_nodes.len(), print_limit);
                println!(
                    "Active Nodes (first {}): {:?}",
                    print_len,
                    &active_nodes[..print_len]
                );
            }
        }
        0x05 | 0x0E => {
            // TYPE_NODE_VECTOR / TYPE_UINT64_VECTOR
            let handle = ret_val as usize;
            let len = unsafe { impulse_vm_context_node_vector_size(ctx, handle) };
            let ptr = unsafe { impulse_vm_context_get_node_vector(ctx, handle) };
            println!(
                "Output Node/UInt64 Vector Handle: {} (length={})",
                handle, len
            );
            if !ptr.is_null() && len > 0 {
                let slice = unsafe { std::slice::from_raw_parts(ptr, len) };
                let print_len = len.min(20);
                println!("Elements (first {}): {:?}", print_len, &slice[..print_len]);
            }
        }
        0x08 => {
            // TYPE_FLOAT
            let fval = f32::from_bits(ret_val as u32);
            println!(
                "Scalar Float Return Value: {:.6} (0x{:08X})",
                fval, ret_val as u32
            );
        }
        0x09 => {
            // TYPE_DOUBLE
            let dval = f64::from_bits(ret_val);
            println!(
                "Scalar Double Return Value: {:.6} (0x{:016X})",
                dval, ret_val
            );
        }
        0x0C => {
            // TYPE_FLOAT_VECTOR
            let handle = ret_val as usize;
            let num_nodes = unsafe { impulse_snapshot_max_node_count(snap) } as usize;
            let float_ptr = unsafe { impulse_vm_context_get_float_vector(ctx, handle) };

            if !float_ptr.is_null() {
                let slice = unsafe { std::slice::from_raw_parts(float_ptr, num_nodes) };
                let mut non_zero_count = 0;
                let mut first_values = Vec::new();

                for (i, &val) in slice.iter().enumerate() {
                    if val != 0.0 {
                        non_zero_count += 1;
                        if first_values.len() < 10 {
                            first_values.push((i, val));
                        }
                    }
                }
                println!("Output Float Vector Handle: {}", handle);
                println!("Total Vector Size: {}", num_nodes);
                println!("Non-Zero Elements Count: {}", non_zero_count);
                println!("First 10 non-zero elements: {:?}", first_values);
            } else {
                println!("Float vector pointer is NULL");
            }
        }
        0x0D => {
            // TYPE_DOUBLE_VECTOR
            let handle = ret_val as usize;
            let num_nodes = unsafe { impulse_snapshot_max_node_count(snap) } as usize;
            let dbl_ptr = unsafe { impulse_vm_context_get_double_vector(ctx, handle) };
            if !dbl_ptr.is_null() {
                let slice = unsafe { std::slice::from_raw_parts(dbl_ptr, num_nodes) };
                let mut first_values = Vec::new();
                for (i, &val) in slice.iter().enumerate() {
                    if val != 0.0 && first_values.len() < 10 {
                        first_values.push((i, val));
                    }
                }
                println!(
                    "Output Double Vector Handle: {} (first non-zero: {:?})",
                    handle, first_values
                );
            }
        }
        0x10 => {
            // TYPE_LANE_FRONTIER
            let handle = ret_val as usize;
            let num_lanes = unsafe { impulse_vm_context_lane_frontier_num_lanes(ctx, handle) };
            let width = unsafe { impulse_vm_context_lane_frontier_width(ctx, handle) };
            let active_mask = unsafe { impulse_vm_context_lane_frontier_active_mask(ctx, handle) };
            let node_count = unsafe { impulse_vm_context_lane_frontier_node_count(ctx, handle) };
            let data_ptr = unsafe { impulse_vm_context_lane_frontier_data(ctx, handle) };
            let mut active_nodes = Vec::new();
            if !data_ptr.is_null() && node_count > 0 {
                let slice = unsafe { std::slice::from_raw_parts(data_ptr, node_count) };
                for (v, &w) in slice.iter().enumerate() {
                    if (w & active_mask) != 0 && active_nodes.len() < 16 {
                        active_nodes.push((v, format!("0x{:X}", w & active_mask)));
                    }
                }
            }
            println!(
                "Output Lane Frontier Handle: {} (num_lanes={}, width={}, active_mask=0x{:X}, node_count={})",
                handle, num_lanes, width, active_mask, node_count
            );
            println!("Sample Active Lane Words: {:?}", active_nodes);
        }
        _ => {
            // Scalar or fallback
            println!("Scalar Return Value: {}", ret_val);
        }
    }

    // 7. Cleanup VM and snapshot resources
    unsafe {
        impulse_vm_context_destroy(ctx);
        impulse_snapshot_close(snap);
    }

    Ok(())
}
