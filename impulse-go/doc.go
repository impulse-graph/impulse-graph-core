// Package impulse provides high-performance, zero-copy Golang bindings for the Impulse Graph Engine.
//
// Impulse Graph is an ultra-high-performance, zero-copy, C-ABI binary snapshot graph engine
// designed for sub-millisecond cold start, sub-microsecond vector traversals, and multi-terabyte (TB+) scale graph analytics.
//
// Key components provided by package impulse:
//   - Snapshot: Memory-mapped read-only graph snapshot reader (.imps).
//   - Writer: Direct-to-disk binary snapshot serialization engine.
//   - DeltaLayer: Concurrent in-memory live edge mutation and tombstone overlay.
//   - ImpulseVM: Bytecode execution Virtual Machine with off-heap context handle management.
//   - SIMD: High-speed hardware-accelerated dot product, vector sum, and sorted array set intersection routines.
package impulse
