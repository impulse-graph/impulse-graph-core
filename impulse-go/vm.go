package impulse

/*
#include "impulse_vm_v11.h"
#include <stdlib.h>
#include <string.h>

static void* alloc_aligned_128(size_t n) {
    size_t aligned = ((n + 127) / 128) * 128;
    if (aligned < 65536) aligned = 65536;
    void* p = NULL;
    if (posix_memalign(&p, 128, aligned) != 0) return NULL;
    memset(p, 0, aligned);
    return p;
}
*/
import "C"

import (
	"errors"
	"fmt"
	"unsafe"
)

// VmStatus mirrors impulse_vm_status_v11_t in impulse_vm_v11.h.
type VmStatus uint32

const (
	VmOk                  VmStatus = 0
	VmErrInvalidOpcode    VmStatus = 1
	VmErrOutOfBounds      VmStatus = 2
	VmErrNullSnapshot     VmStatus = 3
	VmErrStackOverflow    VmStatus = 4
	VmErrStackUnderflow   VmStatus = 5
	VmErrInvalidOperand   VmStatus = 6
	VmErrUserThrow        VmStatus = 7
	VmErrAssertionFailed  VmStatus = 8
	VmErrTrap             VmStatus = 9
	VmErrDomainMismatch   VmStatus = 10
	VmErrBufferOverflow   VmStatus = 11
	VmErrFloatingPoint    VmStatus = 12
	VmErrGasExhausted     VmStatus = 13
	VmErrTypeMismatch     VmStatus = 14
	VmErrValidationFailed VmStatus = 15
)

// MemoryPlan holds static AOT scratch and result buffer bounds from impulse_vm_plan_memory.
type MemoryPlan struct {
	MinScratchBytes        uint64
	GuaranteedScratchBytes uint64
	MaxResultBytes         uint64
}

// DualIctxV2 holds the canonical logical & physical ICTX v2 digest.
type DualIctxV2 struct {
	LogicalHash  uint64
	PhysicalHash uint64
}

// ValidateBytecode runs the static load-time verifier over a .impb v0.11 container.
func ValidateBytecode(impb []byte) (VmStatus, uint32) {
	if len(impb) == 0 {
		return VmErrValidationFailed, 0
	}
	res := C.impulse_vm_validate_v11(unsafe.Pointer(&impb[0]), C.size_t(len(impb)))
	return VmStatus(res.status), uint32(res.bad_pc)
}

// PlanMemory computes deterministic scratch and result buffer bounds for a .impb v0.11 program.
func PlanMemory(impb []byte, snapshot *Snapshot, maxDop uint16) (MemoryPlan, error) {
	if len(impb) == 0 {
		return MemoryPlan{}, errors.New("empty bytecode")
	}
	var cSnap *C.impulse_snapshot_t
	if snapshot != nil {
		cSnap = snapshot.Ptr()
	}
	var cPlan C.impulse_vm_memory_plan_t
	rc := C.impulse_vm_plan_memory(
		unsafe.Pointer(&impb[0]),
		C.size_t(len(impb)),
		cSnap,
		C.uint16_t(maxDop),
		&cPlan,
	)
	if rc != 0 {
		return MemoryPlan{}, fmt.Errorf("impulse_vm_plan_memory failed: status=%d", uint32(rc))
	}
	return MemoryPlan{
		MinScratchBytes:        uint64(cPlan.min_scratch_bytes),
		GuaranteedScratchBytes: uint64(cPlan.guaranteed_scratch_bytes),
		MaxResultBytes:         uint64(cPlan.max_result_bytes),
	}, nil
}

// VmWorkspace owns a 128-byte-aligned scratch arena initialized with impulse_workspace_init.
type VmWorkspace struct {
	rawBuf unsafe.Pointer
	ws     *C.impulse_vm_workspace_v11_t
	cap    C.size_t
}

// NewVmWorkspace allocates and placement-initializes a reusable v0.11 workspace.
func NewVmWorkspace(scratchBytes uint64) (*VmWorkspace, error) {
	if scratchBytes < 65536 {
		scratchBytes = 65536
	}
	aligned := ((scratchBytes + 127) / 128) * 128
	buf := C.alloc_aligned_128(C.size_t(aligned))
	if buf == nil {
		return nil, errors.New("failed to allocate 128-byte-aligned workspace")
	}
	var ws *C.impulse_vm_workspace_v11_t
	rc := C.impulse_workspace_init(buf, C.size_t(aligned), &ws)
	if rc != 0 || ws == nil {
		C.free(buf)
		return nil, fmt.Errorf("impulse_workspace_init failed: status=%d", uint32(rc))
	}
	return &VmWorkspace{rawBuf: buf, ws: ws, cap: C.size_t(aligned)}, nil
}

// Close releases the underlying aligned scratch buffer.
func (w *VmWorkspace) Close() {
	if w.rawBuf != nil {
		C.free(w.rawBuf)
		w.rawBuf = nil
		w.ws = nil
	}
}

// Execute runs a .impb v0.11 program with zero heap allocations inside the VM.
func (w *VmWorkspace) Execute(impb []byte, snapshot *Snapshot, seedIDs []uint64, domainID uint16, gasLimit uint64) VmStatus {
	if w.ws == nil || len(impb) == 0 {
		return VmErrValidationFailed
	}
	var cSnap *C.impulse_snapshot_t
	if snapshot != nil {
		cSnap = snapshot.Ptr()
	}
	var params C.impulse_vm_params_v11_t
	params.gas_limit = C.uint64_t(gasLimit)
	var binding C.impulse_vm_param_binding_v11_t
	if len(seedIDs) > 0 {
		binding.param_id = 0
		binding.kind = C.IMPULSE_VM_V11_PARAM_FRONTIER_IDS
		binding.domain_id = C.uint16_t(domainID)
		binding.count = C.uint32_t(len(seedIDs))
		binding.data_ptr = unsafe.Pointer(&seedIDs[0])
		params.bindings = &binding
		params.binding_count = 1
	}
	var outBytes C.size_t
	rc := C.impulse_vm_execute_v11(
		unsafe.Pointer(&impb[0]),
		C.size_t(len(impb)),
		cSnap,
		&params,
		w.ws,
		nil,
		0,
		&outBytes,
	)
	return VmStatus(rc)
}

// OutputCount returns the number of terminal OP_COLLECT outputs produced by the last Execute call.
func (w *VmWorkspace) OutputCount() uint32 {
	if w.ws == nil {
		return 0
	}
	return uint32(C.impulse_vm_v11_get_output_count(w.ws))
}

// ComputeIctxV2 computes the canonical Dual ICTX v2 digest for the workspace.
func (w *VmWorkspace) ComputeIctxV2() DualIctxV2 {
	if w.ws == nil {
		return DualIctxV2{}
	}
	d := C.impulse_vm_v11_compute_ictx_v2(w.ws)
	return DualIctxV2{
		LogicalHash:  uint64(d.logical_hash),
		PhysicalHash: uint64(d.physical_hash),
	}
}
