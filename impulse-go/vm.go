package impulse

/*
#include "impulse_vm.h"
#include <stdlib.h>
*/
import "C"

import (
	"unsafe"
)

// Instruction defines a fixed 64-bit opcode instruction layout for ImpulseVM.
type Instruction struct {
	Opcode  uint8
	Flags   uint8
	DstReg  uint16
	Payload uint32
}

// VMContext manages off-heap execution handles (bitsets, node/float/double vectors, string vectors, value maps).
type VMContext struct {
	ptr      *C.impulse_vm_context_t
	snapshot *Snapshot
}

// NewVMContext creates a new execution context bound to an immutable graph snapshot.
func NewVMContext(snapshot *Snapshot) (*VMContext, error) {
	var cSnap *C.impulse_snapshot_t
	if snapshot != nil {
		cSnap = snapshot.Ptr()
	}

	ptr := C.impulse_vm_context_create(cSnap)
	if ptr == nil {
		return nil, VMErrNullSnapshot
	}

	return &VMContext{ptr: ptr, snapshot: snapshot}, nil
}

// Close releases the VM context and all associated off-heap buffers.
func (ctx *VMContext) Close() {
	if ctx.ptr != nil {
		C.impulse_vm_context_destroy(ctx.ptr)
		ctx.ptr = nil
	}
}

// Ptr returns raw pointer to C handle.
func (ctx *VMContext) Ptr() *C.impulse_vm_context_t {
	return ctx.ptr
}

// VectorSize returns maximum capacity size for vectors in context.
func (ctx *VMContext) VectorSize() int {
	if ctx.ptr == nil {
		return 0
	}
	return int(C.impulse_vm_context_get_vector_size(ctx.ptr))
}

func (ctx *VMContext) defaultVectorLen() int {
	sz := ctx.VectorSize()
	if sz > 0 {
		return sz
	}
	if ctx.snapshot != nil {
		m := ctx.snapshot.MaxNodeCount()
		if m > 0 {
			return int(m)
		}
	}
	return 1024 * 1024
}

// Bitset operations
func (ctx *VMContext) AcquireBitset() int {
	return int(C.impulse_vm_context_acquire_bitset(ctx.ptr))
}

func (ctx *VMContext) ReleaseBitset(handle int) {
	C.impulse_vm_context_release_bitset(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) BitsetAdd(handle int, nodeID uint64) {
	C.impulse_vm_context_bitset_add(ctx.ptr, C.size_t(handle), C.uint64_t(nodeID))
}

func (ctx *VMContext) BitsetTest(handle int, nodeID uint64) bool {
	return bool(C.impulse_vm_context_bitset_test(ctx.ptr, C.size_t(handle), C.uint64_t(nodeID)))
}

func (ctx *VMContext) BitsetFill(handle int, count uint64) {
	C.impulse_vm_context_bitset_fill(ctx.ptr, C.size_t(handle), C.uint64_t(count))
}

func (ctx *VMContext) BitsetGetWord(handle int, wordIdx int) uint64 {
	return uint64(C.impulse_vm_context_bitset_get_word(ctx.ptr, C.size_t(handle), C.size_t(wordIdx)))
}

// Float Vector operations
func (ctx *VMContext) AcquireFloatVector() int {
	return int(C.impulse_vm_context_acquire_float_vector(ctx.ptr))
}

func (ctx *VMContext) ReleaseFloatVector(handle int) {
	C.impulse_vm_context_release_float_vector(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) FloatVectorSet(handle int, index int, val float32) {
	C.impulse_vm_context_float_vector_set(ctx.ptr, C.size_t(handle), C.size_t(index), C.float(val))
}

func (ctx *VMContext) GetFloatVector(handle int, count ...int) []float32 {
	if ctx.ptr == nil {
		return nil
	}
	cPtr := C.impulse_vm_context_get_float_vector(ctx.ptr, C.size_t(handle))
	if cPtr == nil {
		return nil
	}
	sz := ctx.defaultVectorLen()
	if len(count) > 0 && count[0] > 0 {
		sz = count[0]
	}
	return unsafe.Slice((*float32)(unsafe.Pointer(cPtr)), sz)
}

// Double Vector operations
func (ctx *VMContext) AcquireDoubleVector() int {
	return int(C.impulse_vm_context_acquire_double_vector(ctx.ptr))
}

func (ctx *VMContext) ReleaseDoubleVector(handle int) {
	C.impulse_vm_context_release_double_vector(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) DoubleVectorSet(handle int, index int, val float64) {
	C.impulse_vm_context_double_vector_set(ctx.ptr, C.size_t(handle), C.size_t(index), C.double(val))
}

func (ctx *VMContext) GetDoubleVector(handle int, count ...int) []float64 {
	if ctx.ptr == nil {
		return nil
	}
	cPtr := C.impulse_vm_context_get_double_vector(ctx.ptr, C.size_t(handle))
	if cPtr == nil {
		return nil
	}
	sz := ctx.defaultVectorLen()
	if len(count) > 0 && count[0] > 0 {
		sz = count[0]
	}
	return unsafe.Slice((*float64)(unsafe.Pointer(cPtr)), sz)
}

// Node Vector operations
func (ctx *VMContext) AcquireNodeVector() int {
	return int(C.impulse_vm_context_acquire_node_vector(ctx.ptr))
}

func (ctx *VMContext) ReleaseNodeVector(handle int) {
	C.impulse_vm_context_release_node_vector(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) GetNodeVector(handle int, count ...int) []uint64 {
	if ctx.ptr == nil {
		return nil
	}
	cPtr := C.impulse_vm_context_get_node_vector(ctx.ptr, C.size_t(handle))
	if cPtr == nil {
		return nil
	}
	sz := ctx.defaultVectorLen()
	if len(count) > 0 && count[0] > 0 {
		sz = count[0]
	}
	return unsafe.Slice((*uint64)(unsafe.Pointer(cPtr)), sz)
}

// String Vector operations
func (ctx *VMContext) AcquireStringVector() int {
	return int(C.impulse_vm_context_acquire_string_vector(ctx.ptr))
}

func (ctx *VMContext) ReleaseStringVector(handle int) {
	C.impulse_vm_context_release_string_vector(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) StringVectorAdd(handle int, str string) {
	cStr := C.CString(str)
	defer C.free(unsafe.Pointer(cStr))
	C.impulse_vm_context_string_vector_add(ctx.ptr, C.size_t(handle), cStr)
}

func (ctx *VMContext) StringVectorSize(handle int) int {
	return int(C.impulse_vm_context_string_vector_size(ctx.ptr, C.size_t(handle)))
}

func (ctx *VMContext) StringVectorGet(handle int, index int) string {
	cStr := C.impulse_vm_context_string_vector_get(ctx.ptr, C.size_t(handle), C.size_t(index))
	if cStr == nil {
		return ""
	}
	return C.GoString(cStr)
}

// Value Map operations
func (ctx *VMContext) AcquireValueMap() int {
	return int(C.impulse_vm_context_acquire_value_map(ctx.ptr))
}

func (ctx *VMContext) ReleaseValueMap(handle int) {
	C.impulse_vm_context_release_value_map(ctx.ptr, C.size_t(handle))
}

func (ctx *VMContext) ValueMapSize(handle int) int {
	return int(C.impulse_vm_context_value_map_size(ctx.ptr, C.size_t(handle)))
}

func (ctx *VMContext) ValueMapGetKey(handle int, index int) string {
	cStr := C.impulse_vm_context_value_map_get_key(ctx.ptr, C.size_t(handle), C.size_t(index))
	if cStr == nil {
		return ""
	}
	return C.GoString(cStr)
}

func (ctx *VMContext) ValueMapGetValue(handle int, index int) float32 {
	return float32(C.impulse_vm_context_value_map_get_value(ctx.ptr, C.size_t(handle), C.size_t(index)))
}

// VMState wraps the 640-byte aligned C VM state execution frame.
type VMState struct {
	state C.impulse_vm_state_t
}

// NewVMState creates a zeroed VMState frame ready for execution.
func NewVMState() *VMState {
	return &VMState{}
}

// SetQueryContext links a VMContext off-heap context handle pool to the state frame.
func (s *VMState) SetQueryContext(ctx *VMContext) {
	if ctx != nil {
		s.state.query_context = ctx.Ptr()
	} else {
		s.state.query_context = nil
	}
}

// GetRegister reads the value of hardware register R0..R63.
func (s *VMState) GetRegister(regIdx int) uint64 {
	if regIdx < 0 || regIdx >= 64 {
		return 0
	}
	return uint64(s.state.registers[regIdx])
}

// SetRegister sets the value of hardware register R0..R63.
func (s *VMState) SetRegister(regIdx int, val uint64) {
	if regIdx >= 0 && regIdx < 64 {
		s.state.registers[regIdx] = C.uint64_t(val)
	}
}

// GetRegisterType returns the type tag for hardware register R0..R63.
func (s *VMState) GetRegisterType(regIdx int) RegisterType {
	if regIdx < 0 || regIdx >= 64 {
		return TypeNull
	}
	return RegisterType(s.state.register_types[regIdx])
}

// SetRegisterType sets the type tag for hardware register R0..R63.
func (s *VMState) SetRegisterType(regIdx int, typ RegisterType) {
	if regIdx >= 0 && regIdx < 64 {
		s.state.register_types[regIdx] = C.uint8_t(typ)
	}
}

// Flags returns the status flags register (ZF, LT, GT, EQ, ST).
func (s *VMState) Flags() uint64 {
	return uint64(s.state.flags)
}

// PC returns the current program counter offset.
func (s *VMState) PC() uint32 {
	return uint32(s.state.pc)
}

// ExecuteVM executes a sequence of ImpulseVM bytecode instructions against a state frame.
func ExecuteVM(bytecode []Instruction, state *VMState, inputParam uint64) error {
	if state == nil {
		return VMErrNullSnapshot
	}

	n := len(bytecode)
	if n == 0 {
		return nil
	}

	cInsts := make([]C.impulse_instruction_t, n)
	for i, inst := range bytecode {
		cInsts[i].opcode = C.uint8_t(inst.Opcode)
		cInsts[i].flags = C.uint8_t(inst.Flags)
		cInsts[i].dst_reg = C.uint16_t(inst.DstReg)
		cInsts[i].payload = C.uint32_t(inst.Payload)
	}

	status := C.impulse_vm_execute(
		&cInsts[0],
		C.size_t(n),
		&state.state,
		C.uint64_t(inputParam),
	)

	if status != C.IMPULSE_VM_OK {
		return VMStatus(status)
	}

	return nil
}

// QueryBuilder provides a fluent Go API for constructing impOps VM bytecode programs.
type QueryBuilder struct {
	instructions []Instruction
}

// NewQueryBuilder initializes a new fluent QueryBuilder.
func NewQueryBuilder() *QueryBuilder {
	return &QueryBuilder{
		instructions: make([]Instruction, 0, 8),
	}
}

func (b *QueryBuilder) InputNode(dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x01, Flags: 0, DstReg: dstReg, Payload: 0})
	return b
}

func (b *QueryBuilder) InputSet(dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x02, Flags: 0, DstReg: dstReg, Payload: 0})
	return b
}

func (b *QueryBuilder) LoadConstInt(val uint32, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x03, Flags: 0, DstReg: dstReg, Payload: val})
	return b
}

func (b *QueryBuilder) WalkEdge(relationID uint16, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x10, Flags: 0, DstReg: dstReg, Payload: uint32(relationID)})
	return b
}

func (b *QueryBuilder) WalkEdgeFiltered(relationID uint16, filterReg uint16, dstReg uint16) *QueryBuilder {
	payload := (uint32(filterReg) << 16) | uint32(relationID)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x11, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) Degree(relationID uint16, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x12, Flags: 0, DstReg: dstReg, Payload: uint32(relationID)})
	return b
}

func (b *QueryBuilder) CscWalk(relationID uint16, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x18, Flags: 0, DstReg: dstReg, Payload: uint32(relationID)})
	return b
}

func (b *QueryBuilder) SetUnion(srcReg1, srcReg2, dstReg uint16) *QueryBuilder {
	payload := (uint32(srcReg1) << 16) | uint32(srcReg2)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x30, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) SetIntersect(srcReg1, srcReg2, dstReg uint16) *QueryBuilder {
	payload := (uint32(srcReg1) << 16) | uint32(srcReg2)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x31, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) SetDifference(srcReg1, srcReg2, dstReg uint16) *QueryBuilder {
	payload := (uint32(srcReg1) << 16) | uint32(srcReg2)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x32, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) SetCardinality(srcReg, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x33, Flags: 0, DstReg: dstReg, Payload: uint32(srcReg)})
	return b
}

func (b *QueryBuilder) AfforestCC(relationID uint16, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x40, Flags: 0, DstReg: dstReg, Payload: uint32(relationID)})
	return b
}

func (b *QueryBuilder) MxV(relationID uint16, vectorReg, dstReg uint16) *QueryBuilder {
	payload := (uint32(vectorReg) << 16) | uint32(relationID)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x41, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) BrandesForward(relationID uint16, dstReg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x48, Flags: 0, DstReg: dstReg, Payload: uint32(relationID)})
	return b
}

func (b *QueryBuilder) SampleNeighbors(relationID uint16, kSamples uint16, dstReg uint16) *QueryBuilder {
	payload := (uint32(relationID) << 16) | uint32(kSamples)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x60, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) RebacCheck(relationID uint16, targetNodeReg, dstReg uint16) *QueryBuilder {
	payload := (uint32(targetNodeReg) << 16) | uint32(relationID)
	b.instructions = append(b.instructions, Instruction{Opcode: 0x63, Flags: 0, DstReg: dstReg, Payload: payload})
	return b
}

func (b *QueryBuilder) CollectBitset(reg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x90, Flags: 0, DstReg: reg, Payload: 0})
	return b
}

func (b *QueryBuilder) CollectArray(reg uint16) *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0x91, Flags: 0, DstReg: reg, Payload: 0})
	return b
}

func (b *QueryBuilder) Halt() *QueryBuilder {
	b.instructions = append(b.instructions, Instruction{Opcode: 0xFF, Flags: 0, DstReg: 0, Payload: 0})
	return b
}

func (b *QueryBuilder) Compile() []Instruction {
	if len(b.instructions) == 0 || b.instructions[len(b.instructions)-1].Opcode != 0xFF {
		b.Halt()
	}
	bytecode := make([]Instruction, len(b.instructions))
	copy(bytecode, b.instructions)
	return bytecode
}

