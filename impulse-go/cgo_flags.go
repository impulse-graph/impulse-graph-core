package impulse

/*
#cgo CFLAGS: -I${SRCDIR}/../impulse-cpp/include
#cgo LDFLAGS: -L${SRCDIR}/../impulse-cpp/build -limpulse_graph_static
#cgo darwin LDFLAGS: -lc++
#cgo linux LDFLAGS: -lstdc++
#include <stdalign.h>
#include "impulse_graph.h"
#include "impulse_vm.h"
#include <stdlib.h>
*/
import "C"
