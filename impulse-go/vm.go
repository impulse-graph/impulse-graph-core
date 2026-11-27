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

static impulse_vm_status_v11_t exec_v11_with_seeds(
    const void* impb, size_t impb_len,
    const impulse_snapshot_t* snap,
    impulse_vm_workspace_v11_t* ws,
    uint64_t gas_limit,
    uint16_t dom_id,
    const uint64_t* seeds, uint32_t seed_cnt
) {
    impulse_vm_params_v11_t params;
    memset(&params, 0, sizeof(params));
    params.gas_limit = gas_limit;
    impulse_vm_param_binding_v11_t binding;
    memset(&binding, 0, sizeof(binding));
    if (seeds != NULL && seed_cnt > 0) {
        binding.param_id = 0;
        binding.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
        binding.domain_id = dom_id;
        binding.count = seed_cnt;
        binding.data_ptr = seeds;
        params.bindings = &binding;
        params.binding_count = 1;
    }
    size_t out_bytes = 0;
    return impulse_vm_execute_v11(impb, impb_len, snap, &params, ws, NULL, 0, &out_bytes);
}
*/
import "C"

import (
	"encoding/binary"
	"errors"
	"fmt"
	"runtime"
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
	VmErrGasExhausted          VmStatus = 13
	VmErrTypeMismatch          VmStatus = 14
	VmErrValidationFailed      VmStatus = 15
	VmErrArithmeticOverflow    VmStatus = 16
	VmErrDivByZero             VmStatus = 17
	VmStatusArithmeticOverflow VmStatus = 16
	VmStatusDivByZero          VmStatus = 17
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
	w := &VmWorkspace{rawBuf: buf, ws: ws, cap: C.size_t(aligned)}
	runtime.SetFinalizer(w, (*VmWorkspace).Close)
	return w, nil
}

// Close releases the underlying aligned scratch buffer.
func (w *VmWorkspace) Close() {
	if w.rawBuf != nil {
		C.free(w.rawBuf)
		w.rawBuf = nil
		w.ws = nil
		runtime.SetFinalizer(w, nil)
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
	var seedPtr *C.uint64_t
	if len(seedIDs) > 0 {
		seedPtr = (*C.uint64_t)(unsafe.Pointer(&seedIDs[0]))
	}
	rc := C.exec_v11_with_seeds(
		unsafe.Pointer(&impb[0]),
		C.size_t(len(impb)),
		cSnap,
		w.ws,
		C.uint64_t(gasLimit),
		C.uint16_t(domainID),
		seedPtr,
		C.uint32_t(len(seedIDs)),
	)
	runtime.KeepAlive(w)
	runtime.KeepAlive(snapshot)
	runtime.KeepAlive(impb)
	runtime.KeepAlive(seedIDs)
	return VmStatus(rc)
}

// OutputCount returns the number of terminal OP_COLLECT outputs produced by the last Execute call.
func (w *VmWorkspace) OutputCount() uint32 {
	if w.ws == nil {
		return 0
	}
	n := uint32(C.impulse_vm_v11_get_output_count(w.ws))
	runtime.KeepAlive(w)
	return n
}

// OutputU64Slice copies a terminal OP_COLLECT frontier/u64/i64 output slot into a Go []uint64 slice.
func (w *VmWorkspace) OutputU64Slice(slot uint32) ([]uint64, error) {
	if w.ws == nil {
		return nil, errors.New("workspace closed")
	}
	var view C.impulse_vm_output_view_v11_t
	if C.impulse_vm_v11_get_output(w.ws, C.uint32_t(slot), &view) == 0 {
		runtime.KeepAlive(w)
		return nil, fmt.Errorf("output slot %d not available", slot)
	}
	n := int(view.length)
	out := make([]uint64, n)
	if n > 0 && view.i64_data != nil {
		raw := unsafe.Slice((*uint64)(unsafe.Pointer(view.i64_data)), n)
		copy(out, raw)
	}
	runtime.KeepAlive(w)
	return out, nil
}

// OutputScalarU64 reads the first u64/i64 scalar from an OP_COLLECT.scalar output slot.
func (w *VmWorkspace) OutputScalarU64(slot uint32) (uint64, bool) {
	if w.ws == nil {
		return 0, false
	}
	var view C.impulse_vm_output_view_v11_t
	if C.impulse_vm_v11_get_output(w.ws, C.uint32_t(slot), &view) == 0 || view.length == 0 || view.i64_data == nil {
		runtime.KeepAlive(w)
		return 0, false
	}
	v := uint64(*view.i64_data)
	runtime.KeepAlive(w)
	return v, true
}

// EncodeV11Word packs a single 64-bit ImpulseVM v0.11 instruction word.
func EncodeV11Word(op, subop, flags, dst, s1, s2, s3 uint8, imm32 uint32) uint64 {
	return uint64(op) |
		(uint64(subop&0xF) << 8) |
		(uint64(flags&0xF) << 12) |
		(uint64(s1&0xF) << 16) |
		(uint64(dst&0xF) << 20) |
		(uint64(s3&0xF) << 24) |
		(uint64(s2&0xF) << 28) |
		(uint64(imm32) << 32)
}

// BuildImpbV11 wraps raw 64-bit v0.11 instruction words in a canonical 32-byte .impb v11 container.
func BuildImpbV11(words []uint64, domainCountReq uint16) []byte {
	buf := make([]byte, 32+len(words)*8)
	binary.LittleEndian.PutUint32(buf[0:4], 0x494D5042)       // kImpbMagic
	binary.LittleEndian.PutUint16(buf[4:6], 11)               // v0.11
	binary.LittleEndian.PutUint16(buf[6:8], 0)  // flags
	binary.LittleEndian.PutUint32(buf[8:12], uint32(len(words)))
	binary.LittleEndian.PutUint32(buf[12:16], 0)              // const_pool_bytes
	binary.LittleEndian.PutUint32(buf[16:20], 0)              // entry_pc
	binary.LittleEndian.PutUint16(buf[20:22], domainCountReq) // required_domains
	for i, w := range words {
		binary.LittleEndian.PutUint64(buf[32+i*8:40+i*8], w)
	}
	return buf
}

// ComputeIctxV2 computes the canonical Dual ICTX v2 digest for the workspace.
func (w *VmWorkspace) ComputeIctxV2() DualIctxV2 {
	if w.ws == nil {
		return DualIctxV2{}
	}
	d := C.impulse_vm_v11_compute_ictx_v2(w.ws)
	runtime.KeepAlive(w)
	return DualIctxV2{
		LogicalHash:  uint64(d.logical_hash),
		PhysicalHash: uint64(d.physical_hash),
	}
}
