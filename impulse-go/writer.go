package impulse

/*
#include "impulse_graph.h"
#include <stdlib.h>
*/
import "C"

import "unsafe"

// Writer builds and serializes immutable Impulse binary graph snapshot (.imps) files.
type Writer struct {
	ptr *C.impulse_writer_t
}

// NewWriter initializes a streaming snapshot writer direct to disk.
func NewWriter(outputPath string, globalFeatures uint64) (*Writer, error) {
	cPath := C.CString(outputPath)
	defer C.free(unsafe.Pointer(cPath))

	ptr := C.impulse_writer_create(cPath, C.uint64_t(globalFeatures))
	if ptr == nil {
		errStr := C.GoString(C.impulse_get_last_error())
		if errStr != "" {
			return nil, fmtError(ErrIOFailure, errStr)
		}
		return nil, ErrIOFailure
	}

	return &Writer{ptr: ptr}, nil
}

// Close destroys the writer and frees underlying resources if not finalized.
func (w *Writer) Close() {
	if w.ptr != nil {
		C.impulse_writer_destroy(w.ptr)
		w.ptr = nil
	}
}

// Ptr returns the raw C handle pointer.
func (w *Writer) Ptr() *C.impulse_writer_t {
	return w.ptr
}

// AddDomain registers a domain definition entry in the snapshot domain catalog.
func (w *Writer) AddDomain(domainID uint16, keyType KeyType, name string) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	status := C.impulse_writer_add_domain(
		w.ptr,
		C.uint16_t(domainID),
		C.uint8_t(keyType),
		cName,
	)

	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// SetDomainNodeCount sets the explicit node count for a domain catalog entry.
func (w *Writer) SetDomainNodeCount(domainIndex uint16, nodeCount uint64) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}
	status := C.impulse_writer_set_domain_node_count(w.ptr, C.uint16_t(domainIndex), C.uint64_t(nodeCount))
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddRelation registers a graph relation topology direct to the snapshot writer.
func (w *Writer) AddRelation(
	srcDomainID, tgtDomainID uint16,
	encodingID EncodingType,
	nodeCount, edgeCount, sectionFeatures uint64,
	rowOffsets []uint32,
	colIndices []uint32,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}

	var rowOffsetsPtr unsafe.Pointer
	rowOffsetsBytes := uint64(len(rowOffsets) * 4)
	if len(rowOffsets) > 0 {
		rowOffsetsPtr = unsafe.Pointer(&rowOffsets[0])
	}

	var colIndicesPtr unsafe.Pointer
	colIndicesBytes := uint64(len(colIndices) * 4)
	if len(colIndices) > 0 {
		colIndicesPtr = unsafe.Pointer(&colIndices[0])
	}

	status := C.impulse_writer_add_relation(
		w.ptr,
		C.uint16_t(srcDomainID),
		C.uint16_t(tgtDomainID),
		C.uint8_t(encodingID),
		C.uint64_t(nodeCount),
		C.uint64_t(edgeCount),
		C.uint64_t(sectionFeatures),
		rowOffsetsPtr,
		C.uint64_t(rowOffsetsBytes),
		colIndicesPtr,
		C.uint64_t(colIndicesBytes),
	)

	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddAttribute adds node/edge attribute vector data payload to a target relation entry.
func (w *Writer) AddAttribute(
	relationIndex uint16,
	name string,
	typeCode uint8,
	dimension uint32,
	data []byte,
	offsets []uint32,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var dataPtr unsafe.Pointer
	dataBytes := uint64(len(data))
	if len(data) > 0 {
		dataPtr = unsafe.Pointer(&data[0])
	}

	var offsetsPtr unsafe.Pointer
	offsetsBytes := uint64(len(offsets) * 4)
	if len(offsets) > 0 {
		offsetsPtr = unsafe.Pointer(&offsets[0])
	}

	status := C.impulse_writer_add_attribute(
		w.ptr,
		C.uint16_t(relationIndex),
		cName,
		C.uint8_t(typeCode),
		C.uint32_t(dimension),
		dataPtr,
		C.uint64_t(dataBytes),
		offsetsPtr,
		C.uint64_t(offsetsBytes),
	)

	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddDomainAttributeDecimal64 adds a DECIMAL64 attribute column to a domain entry.
func (w *Writer) AddDomainAttributeDecimal64(
	domainIndex uint16,
	name string,
	typeCode uint8,
	dimension uint32,
	precision uint16,
	scale uint8,
	data []int64,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}
	if typeCode == 0 {
		typeCode = AttrTypeDecimal64
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var dataPtr unsafe.Pointer
	dataBytes := uint64(len(data) * 8)
	if len(data) > 0 {
		dataPtr = unsafe.Pointer(&data[0])
	}

	status := C.impulse_writer_add_domain_attribute_decimal64(
		w.ptr,
		C.uint16_t(domainIndex),
		cName,
		C.uint8_t(typeCode),
		C.uint32_t(dimension),
		C.uint16_t(precision),
		C.uint8_t(scale),
		dataPtr,
		C.uint64_t(dataBytes),
	)

	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddAttributeDecimal64 adds a DECIMAL64 attribute column to a target relation entry.
func (w *Writer) AddAttributeDecimal64(
	relationIndex uint16,
	name string,
	typeCode uint8,
	dimension uint32,
	precision uint16,
	scale uint8,
	data []int64,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}
	if typeCode == 0 {
		typeCode = AttrTypeDecimal64
	}

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	var dataPtr unsafe.Pointer
	dataBytes := uint64(len(data) * 8)
	if len(data) > 0 {
		dataPtr = unsafe.Pointer(&data[0])
	}

	status := C.impulse_writer_add_attribute_decimal64(
		w.ptr,
		C.uint16_t(relationIndex),
		cName,
		C.uint8_t(typeCode),
		C.uint32_t(dimension),
		C.uint16_t(precision),
		C.uint8_t(scale),
		dataPtr,
		C.uint64_t(dataBytes),
	)

	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddDomainAttributeDecimal64Nullable adds a nullable DECIMAL64 (0x95) attribute column to a domain entry.
func (w *Writer) AddDomainAttributeDecimal64Nullable(
	domainIndex uint16,
	name string,
	dimension uint32,
	precision uint16,
	scale uint8,
	values []NullableDecimal64,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	packed := packNullableDecimal64Bytes(values)
	var dataPtr unsafe.Pointer
	if len(packed) > 0 {
		dataPtr = unsafe.Pointer(&packed[0])
	}
	status := C.impulse_writer_add_domain_attribute_decimal64(
		w.ptr,
		C.uint16_t(domainIndex),
		cName,
		C.uint8_t(AttrTypeDecimal64Nullable),
		C.uint32_t(dimension),
		C.uint16_t(precision),
		C.uint8_t(scale),
		dataPtr,
		C.uint64_t(len(packed)),
	)
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// AddAttributeDecimal64Nullable adds a nullable DECIMAL64 (0x95) attribute column to a relation entry.
func (w *Writer) AddAttributeDecimal64Nullable(
	relationIndex uint16,
	name string,
	dimension uint32,
	precision uint16,
	scale uint8,
	values []NullableDecimal64,
) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	packed := packNullableDecimal64Bytes(values)
	var dataPtr unsafe.Pointer
	if len(packed) > 0 {
		dataPtr = unsafe.Pointer(&packed[0])
	}
	status := C.impulse_writer_add_attribute_decimal64(
		w.ptr,
		C.uint16_t(relationIndex),
		cName,
		C.uint8_t(AttrTypeDecimal64Nullable),
		C.uint32_t(dimension),
		C.uint16_t(precision),
		C.uint8_t(scale),
		dataPtr,
		C.uint64_t(len(packed)),
	)
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// SetMetadata adds a custom key-value metadata entry to the snapshot metadata header block.
func (w *Writer) SetMetadata(key, value string) error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}

	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))
	cValue := C.CString(value)
	defer C.free(unsafe.Pointer(cValue))

	status := C.impulse_writer_set_metadata(w.ptr, cKey, cValue)
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// Finalize completes writing and closes the snapshot binary file.
func (w *Writer) Finalize() error {
	if w.ptr == nil {
		return ErrInvalidArgument
	}

	status := C.impulse_writer_finalize(w.ptr)
	C.impulse_writer_destroy(w.ptr)
	w.ptr = nil
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}
