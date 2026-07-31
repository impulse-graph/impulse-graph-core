package impulse

/*
#cgo CFLAGS: -I${SRCDIR}/../impulse-cpp/include
#cgo LDFLAGS: -L${SRCDIR}/../impulse-cpp -limpulse_graph
#include "impulse_graph.h"
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"unsafe"
)

type Snapshot struct {
	ptr *C.impulse_snapshot_t
}

func OpenSnapshot(path string) (*Snapshot, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	var status C.impulse_status_t
	ptr := C.impulse_snapshot_open(cPath, &status)
	if ptr == nil || status != C.IMPULSE_OK {
		errStr := C.GoString(C.impulse_get_last_error())
		return nil, errors.New("failed to open snapshot: " + errStr)
	}

	return &Snapshot{ptr: ptr}, nil
}

func (s *Snapshot) Close() {
	if s.ptr != nil {
		C.impulse_snapshot_close(s.ptr)
		s.ptr = nil
	}
}

func (s *Snapshot) IsReachable(srcDomain uint16, srcID uint32, tgtDomain uint16, tgtID uint32) bool {
	if s.ptr == nil {
		return false
	}
	return bool(C.impulse_snapshot_is_reachable(s.ptr, C.uint16_t(srcDomain), C.uint32_t(srcID), C.uint16_t(tgtDomain), C.uint32_t(tgtID)))
}
