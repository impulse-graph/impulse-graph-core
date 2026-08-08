package impulse

/*
#include "impulse_graph.h"
#include <stdlib.h>
*/
import "C"

import "unsafe"

// DeltaLayer manages thread-safe concurrent in-memory edge mutations and tombstones for live overlay queries.
type DeltaLayer struct {
	ptr *C.impulse_delta_layer_t
}

// NewDeltaLayer creates a new live mutation overlay delta layer for a target relation.
func NewDeltaLayer(srcDomainID, tgtDomainID uint16, relationName string) (*DeltaLayer, error) {
	cName := C.CString(relationName)
	defer C.free(unsafe.Pointer(cName))

	ptr := C.impulse_delta_layer_create(
		C.uint16_t(srcDomainID),
		C.uint16_t(tgtDomainID),
		cName,
	)

	if ptr == nil {
		return nil, ErrIOFailure
	}

	return &DeltaLayer{ptr: ptr}, nil
}

// Close destroys the delta layer handle and frees memory.
func (d *DeltaLayer) Close() {
	if d.ptr != nil {
		C.impulse_delta_layer_destroy(d.ptr)
		d.ptr = nil
	}
}

// Ptr returns the raw C pointer handle.
func (d *DeltaLayer) Ptr() *C.impulse_delta_layer_t {
	return d.ptr
}

// AddEdge inserts a directed edge mutation into the live delta layer.
func (d *DeltaLayer) AddEdge(srcNode, tgtNode uint64) error {
	if d.ptr == nil {
		return ErrInvalidArgument
	}

	status := C.impulse_delta_layer_add_edge(d.ptr, C.uint64_t(srcNode), C.uint64_t(tgtNode))
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// TombstoneEdge records a deleted edge tombstone in the live delta layer.
func (d *DeltaLayer) TombstoneEdge(srcNode, tgtNode uint64) error {
	if d.ptr == nil {
		return ErrInvalidArgument
	}

	status := C.impulse_delta_layer_tombstone_edge(d.ptr, C.uint64_t(srcNode), C.uint64_t(tgtNode))
	if status != C.IMPULSE_OK {
		return Status(status)
	}
	return nil
}

// IsTombstoned checks if an edge between srcNode and tgtNode is tombstoned in the delta layer.
func (d *DeltaLayer) IsTombstoned(srcNode, tgtNode uint64) bool {
	if d.ptr == nil {
		return false
	}
	return bool(C.impulse_delta_layer_is_tombstoned(d.ptr, C.uint64_t(srcNode), C.uint64_t(tgtNode)))
}
