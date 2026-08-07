# Impulse Graph Engine Python SDK & C++ Bytecode VM Binding

The official Python SDK for **Impulse Graph Engine** (`impulse-graph`), an ultra-high-performance zero-copy graph analytics engine and C++ bytecode Virtual Machine.

## Features

- **Zero-Copy Memory-Mapped Snapshots**: Query multi-terabyte binary graph snapshots (`.imps`) directly off-heap.
- **Bytecode Virtual Machine (`QueryBuilder`)**: Fluent C++20 bytecode compiler and execution pipeline.
- **Graph Analytics Opcodes**: PageRank, Brandes Betweenness Centrality, Connected Components (Afforest), GraphBLAS Semirings, SIMD Neighborhood Sampling.
- **NumPy Integration**: Direct off-heap zero-copy memoryview and NumPy array extractions.

## Installation

```bash
pip install impulse-graph
```

## Quickstart

```python
from impulse_graph import Snapshot, vm

# 1. Build a VM query using QueryBuilder
builder = vm.QueryBuilder()
query = (
    builder
    .input_node(0)
    .walk_edge(relation_id=1)
    .collect_array()
    .compile()
)

# 2. Execute query against snapshot
with Snapshot("graph.imps") as snap:
    result = snap.execute_query(query, input_param=42)
    print("Result ok:", result.is_ok(), "Result register:", result.result_register)
```

## Documentation

For full documentation and API reference, visit [docs.impulsegraph.io](https://docs.impulsegraph.io).
