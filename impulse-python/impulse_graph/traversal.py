"""
Impulse Graph Engine - Pythonic Graph Traversal Builder & DSL
"""

from typing import Optional, List, Set, Union, Dict, Any
import numpy as np
from . import vm


class Traversal:
    """
    High-level, Pythonic graph traversal pipeline over Impulse Graph Snapshots.
    Automatically manages VM bytecode generation, off-heap memory contexts,
    and returns Pythonic native collections (lists, sets, numpy arrays, count).
    """

    def __init__(
        self,
        snapshot: "Snapshot",
        start_node: int = 0,
        catalog: Optional[Dict[str, int]] = None,
    ):
        self._snapshot = snapshot
        self._start_node = start_node
        self._catalog = catalog or {}
        self._steps = []

    def _resolve_rel(self, rel: Union[str, int]) -> int:
        if isinstance(rel, int):
            return rel
        if rel in self._catalog:
            return self._catalog[rel]
        raise ValueError(
            f"Unknown relation name '{rel}'. Provide relation ID integer or pass catalog={{'{rel}': id}}."
        )

    def out(self, relation: Union[str, int]) -> "Traversal":
        """Follow outgoing edge (forward CSR walk): (a)-[:rel]->(b)"""
        self._steps.append(("out", self._resolve_rel(relation)))
        return self

    def in_(self, relation: Union[str, int]) -> "Traversal":
        """Follow incoming edge (reverse CSC walk): (a)<-[:rel]-(b)"""
        self._steps.append(("in_", self._resolve_rel(relation)))
        return self

    def degree(self, relation: Union[str, int]) -> "Traversal":
        """Compute degree on relation."""
        self._steps.append(("degree", self._resolve_rel(relation)))
        return self

    def compile(self) -> vm.CompiledQuery:
        """Compile traversal steps into an optimized ImpulseVM bytecode program."""
        qb = vm.QueryBuilder()
        qb.input_node(0)
        for op, rel_id in self._steps:
            if op == "out":
                qb.walk_edge(rel_id)
            elif op == "in_":
                qb.walk_csc(rel_id)
            elif op == "degree":
                qb.walk_degree(rel_id)
        qb.collect_bitset()
        return qb.compile()

    def execute(self, start_node: Optional[int] = None) -> List[int]:
        """Execute traversal and return list of matching target node IDs."""
        return self.to_list(start_node=start_node)

    def to_list(self, start_node: Optional[int] = None) -> List[int]:
        """Execute traversal and collect matching target node IDs as a list."""
        seed = self._start_node if start_node is None else start_node
        compiled = self.compile()

        with vm.VmContext(self._snapshot) as ctx:
            state = vm.VmState()
            res = compiled.execute_with_context(ctx, state, seed)
            if not res.is_ok():
                return []

            handle = res.raw_value
            # Extract active bits efficiently
            nodes = []
            total_nodes = 64
            for i in range(self._snapshot.relation_count()):
                rel = self._snapshot.get_relation(i)
                total_nodes = max(total_nodes, rel["node_count"])
            num_words = (total_nodes + 63) // 64
            for w in range(num_words):
                word = ctx.bitset_get_word(handle, w)
                if word != 0:
                    base = w * 64
                    for b in range(64):
                        if (word & (1 << b)) != 0:
                            nodes.append(base + b)
            ctx.release_bitset(handle)
            return nodes

    def to_set(self, start_node: Optional[int] = None) -> Set[int]:
        """Execute traversal and return set of matching target node IDs."""
        return set(self.to_list(start_node=start_node))

    def count(self, start_node: Optional[int] = None) -> int:
        """Execute traversal and return count of matching target node IDs."""
        return len(self.to_list(start_node=start_node))

    def contains(self, target_node: int, start_node: Optional[int] = None) -> bool:
        """Test if target_node is reachable via this traversal."""
        seed = self._start_node if start_node is None else start_node
        compiled = self.compile()

        with vm.VmContext(self._snapshot) as ctx:
            state = vm.VmState()
            res = compiled.execute_with_context(ctx, state, seed)
            if not res.is_ok():
                return False
            found = res.test_bitset(ctx, target_node)
            ctx.release_bitset(res.raw_value)
            return found
