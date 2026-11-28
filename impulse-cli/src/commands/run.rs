//! Canonical v0.11 `.impb` execution command using the 2-buffer caller-owned lifecycle.

use impulse_graph::ffi::{
    impulse_snapshot_close, impulse_snapshot_open, impulse_snapshot_t, impulse_test_control,
    impulse_test_set_never_corrupt, impulse_test_set_opt_disable_mask,
    impulse_vm_branch_coverage_reset, impulse_vm_branch_coverage_set_enabled, impulse_vm_execute,
    impulse_vm_get_output, impulse_vm_get_output_count, impulse_vm_memory_plan_t,
    impulse_vm_output_view_t, impulse_vm_param_binding_t, impulse_vm_params_t,
    impulse_vm_plan_memory, impulse_vm_prepare_snapshot, impulse_vm_validate,
    impulse_vm_workspace_t, impulse_workspace_init, IMPULSE_TESTCTRL_FAULT_CONFIG,
    IMPULSE_TESTCTRL_FAULT_INSTALL, IMPULSE_TESTCTRL_NEVER_CORRUPT, IMPULSE_TESTCTRL_OPTIMIZATIONS,
    IMPULSE_VM_PARAM_FRONTIER_IDS,
};
use std::alloc::{alloc_zeroed, dealloc, Layout};
use std::ffi::CString;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::ptr;

#[derive(Debug, Clone, Default)]
pub struct RunVerifyOptions {
    pub verify: bool,
    pub never_corrupt: bool,
    pub opt_disable_mask: u64,
    pub diff_opt: bool,
    pub fault_sim: Option<(i32, i32)>,
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

struct SnapshotGuard(*mut impulse_snapshot_t);

impl Drop for SnapshotGuard {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe { impulse_snapshot_close(self.0) };
        }
    }
}

pub fn run_with_options(
    snapshot_path: &Path,
    bytecode_path: &Path,
    input_val: u64,
    opts: &RunVerifyOptions,
) -> Result<(), Box<dyn std::error::Error>> {
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

    let c_path = CString::new(snapshot_path.to_str().ok_or("Invalid snapshot path")?)?;
    let mut status = 0i32;
    let snap: *mut impulse_snapshot_t =
        unsafe { impulse_snapshot_open(c_path.as_ptr(), &mut status) };
    if snap.is_null() {
        return Err(format!("Failed to open snapshot: status code {}", status).into());
    }
    let _snap_guard = SnapshotGuard(snap);

    let mut prog_bytes = Vec::new();
    File::open(bytecode_path)?.read_to_end(&mut prog_bytes)?;
    if prog_bytes.len() < 32 {
        return Err("Bytecode file too small for IMPB v11 header".into());
    }

    unsafe {
        if opts.verify {
            let vr = impulse_vm_validate(prog_bytes.as_ptr().cast(), prog_bytes.len());
            if vr.status != 0 {
                return Err(format!(
                    "Validation failed: status={} at pc={}",
                    vr.status, vr.bad_pc
                )
                .into());
            }
        }

        let _ = impulse_vm_prepare_snapshot(snap);

        let mut plan = impulse_vm_memory_plan_t::default();
        let prc = impulse_vm_plan_memory(
            prog_bytes.as_ptr().cast(),
            prog_bytes.len(),
            snap,
            1,
            &mut plan,
        );
        if prc != 0 {
            return Err(format!("impulse_vm_plan_memory failed: {}", prc).into());
        }

        let scratch_len = plan.guaranteed_scratch_bytes.max(1024 * 1024);
        let layout = Layout::from_size_align(scratch_len, 128)?;
        let raw_buf = alloc_zeroed(layout);
        if raw_buf.is_null() {
            return Err("Failed to allocate aligned workspace buffer".into());
        }

        let mut ws: *mut impulse_vm_workspace_t = ptr::null_mut();
        let wrc = impulse_workspace_init(raw_buf.cast(), scratch_len, &mut ws);
        if wrc != 0 || ws.is_null() {
            dealloc(raw_buf, layout);
            return Err(format!("impulse_workspace_init failed: {}", wrc).into());
        }

        // Infer seed domain from first WALK/SPMV instruction's relation src_domain_id (fallback 0)
        let mut seed_domain: u16 = 0;
        if let Ok(reader) = impulse_graph::SnapshotReader::open(snapshot_path) {
            let word_count =
                u32::from_le_bytes(prog_bytes[8..12].try_into().unwrap_or([0; 4])) as usize;
            let code_end = (32 + word_count * 8).min(prog_bytes.len());
            for &chunk in prog_bytes[32..code_end].as_chunks::<8>().0 {
                let w = u64::from_le_bytes(chunk);
                let op = (w & 0xFF) as u8;
                if op == 0x30 || op == 0x31 || op == 0x34 || op == 0x35 {
                    let rel_id = ((w >> 32) & 0xFFFF) as usize;
                    if let Some(rel) = reader.relations().get(rel_id) {
                        seed_domain = rel.src_domain_id;
                    }
                    break;
                }
            }
        }

        let seed = input_val;
        let binding = impulse_vm_param_binding_t {
            param_id: 0,
            kind: IMPULSE_VM_PARAM_FRONTIER_IDS,
            domain_id: seed_domain,
            reserved: 0,
            count: 1,
            scalar_bits: 0,
            data_ptr: (&seed as *const u64).cast(),
            valid_bits: ptr::null(),
        };
        let params = impulse_vm_params_t {
            gas_limit: u64::MAX,
            geometry_override: ptr::null(),
            bindings: &binding,
            binding_count: 1,
        };

        let mut out_bytes = 0usize;
        let rc = impulse_vm_execute(
            prog_bytes.as_ptr().cast(),
            prog_bytes.len(),
            snap,
            &params,
            ws,
            ptr::null_mut(),
            0,
            &mut out_bytes,
        );

        if rc == 0 {
            let out_cnt = impulse_vm_get_output_count(ws);
            println!("Execution succeeded (IMPULSE_VM_OK, outputs={})", out_cnt);
            if out_cnt > 0 {
                let mut ov = std::mem::zeroed::<impulse_vm_output_view_t>();
                if impulse_vm_get_output(ws, 0u32, &mut ov) == 1 {
                    println!("  slot[0]: elem_tag={}, length={}", ov.elem_tag, ov.length);
                }
            }
        }

        dealloc(raw_buf, layout);

        if rc != 0 {
            return Err(format!("ImpulseVM execution failed with status {}", rc).into());
        }
    }
    Ok(())
}
