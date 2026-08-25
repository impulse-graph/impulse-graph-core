"""
Impulse Graph Engine - Pythonic Graph Traversal Builder & DSL
"""

from typing import Optional, List, Set, Union, Dict, Any
import numpy as np
from . import vm


# Built-in Domain Catalog Presets (Extracted directly from Snapshot Headers)
BUILTIN_CATALOGS: Dict[str, Dict[str, int]] = {
    "hetionet": {
        "AdG": 0, "AeG": 1, "AuG": 2, "CbG": 3, "CcSE": 4,
        "CdG": 5, "CpD": 6, "CrC": 7, "CtD": 8, "CuG": 9,
        "DaG": 10, "DdG": 11, "DlA": 12, "DpS": 13, "DrD": 14,
        "DuG": 15, "GcG": 16, "GiG": 17, "GpBP": 18, "GpCC": 19,
        "GpMF": 20, "GpPW": 21, "Gr>G": 22, "PCiC": 23,
    },
    "drkg": {
        "DISGENET::da": 0, "STRING::interacts_with": 0,
        "DRUGBANK::target": 101, "DRUGBANK::ddi_interactor_in": 0,
        "GNBR::C": 0,
    },
    "northwind": {
        "PURCHASED": 0, "CONTAINS": 1, "PRODUCED_BY": 2, "REPORTS_TO": 3,
    },
    "ecommerce": {
        "PURCHASED": 0, "MANUFACTURED_BY": 1, "PRODUCED_BY": 1, "REVIEWS": 2, "CONTAINS": 3,
    },
}


class Traversal:
    """
    High-level, Pythonic graph traversal pipeline over Impulse Graph Snapshots.
    Automatically manages VM bytecode generation, off-heap memory contexts,
    and returns Pythonic native collections (lists, sets, numpy arrays, count).
    """

    def __init__(
        self,
        snapshot: "Snapshot",
        start_node: Optional[int] = None,
        start_nodes: Optional[Union[List[int], np.ndarray]] = None,
        all_nodes: bool = False,
        initial_domain: Optional[Any] = None,
        catalog: Optional[Union[str, Dict[str, int]]] = None,
    ):
        self._snapshot = snapshot
        self._start_node = start_node if start_node is not None else 0
        self._start_nodes = start_nodes
        self._all_nodes = all_nodes
        self._initial_domain = initial_domain
        self._current_domain = initial_domain
        self._steps = []

        if isinstance(catalog, str):
            key = catalog.lower().replace("_catalog", "").replace(".imps", "")
            if key in BUILTIN_CATALOGS:
                self._catalog = dict(BUILTIN_CATALOGS[key])
            else:
                raise ValueError(
                    f"Unknown catalog preset '{catalog}'. Available presets: {list(BUILTIN_CATALOGS.keys())}"
                )
        elif isinstance(catalog, dict):
            self._catalog = dict(catalog)
        else:
            self._catalog = {}

    @property
    def current_domain(self) -> Optional[Any]:
        return self._current_domain

    def _resolve_rel(self, rel: Union[str, int]) -> int:
        if isinstance(rel, int):
            return rel
        if rel in self._catalog:
            return self._catalog[rel]
        for cat in BUILTIN_CATALOGS.values():
            if rel in cat:
                return cat[rel]
        raise ValueError(
            f"Unknown relation name '{rel}'. Provide relation ID integer or pass catalog={{'{rel}': id}}."
        )

    def out(
        self,
        relation: Union[str, int],
        filter_id: Optional[int] = None,
        predicate_id: Optional[int] = None,
    ) -> "Traversal":
        """
        Follow outgoing edge (forward CSR walk): (a)-[:rel]->(b)
        Optional edge filters:
          - filter_id: secondary attribute filter index (e.g. edge weight, confidence)
          - predicate_id: fused SIMD boolean predicate filter
        """
        rel = self._resolve_rel(relation)

        # Validate domain transitions if domain context is active
        if self._current_domain is not None:
            rel_info = self._snapshot.get_relation(rel)
            src_dom = rel_info["src_domain_id"]
            tgt_dom = rel_info["tgt_domain_id"]
            cur_dom = self._current_domain.domain_id
            if src_dom != cur_dom:
                raise ValueError(
                    f"Cannot traverse relation '{relation}' (src domain {src_dom}) from current domain '{self._current_domain.domain_name}' (domain {cur_dom})"
                )
            self._current_domain = self._snapshot.domain(tgt_dom)

        if predicate_id is not None:
            self._steps.append(("out_predicate", rel, predicate_id))
        elif filter_id is not None:
            self._steps.append(("out_filtered", rel, filter_id))
        else:
            self._steps.append(("out", rel, 0))
        return self

    def in_(
        self,
        relation: Union[str, int],
    ) -> "Traversal":
        """Follow incoming edge (reverse CSC walk): (a)<-[:rel]-(b)"""
        rel = self._resolve_rel(relation)

        # Validate reverse domain transitions if domain context is active
        if self._current_domain is not None:
            rel_info = self._snapshot.get_relation(rel)
            src_dom = rel_info["src_domain_id"]
            tgt_dom = rel_info["tgt_domain_id"]
            cur_dom = self._current_domain.domain_id
            if tgt_dom != cur_dom:
                raise ValueError(
                    f"Cannot traverse reverse relation '{relation}' (tgt domain {tgt_dom}) from current domain '{self._current_domain.domain_name}' (domain {cur_dom})"
                )
            self._current_domain = self._snapshot.domain(src_dom)

        self._steps.append(("in_", rel, 0))
        return self

    def filter_node(self, filter_id: int) -> "Traversal":
        """Filter current node set using secondary node attribute index (OP_NODE_FILTER)."""
        self._steps.append(("filter_node", filter_id, 0))
        return self

    def filter_prefix(self, prefix: str) -> "Traversal":
        """Filter current node set matching string attribute prefix (OP_NODE_FILTER_STR_PREFIX)."""
        self._steps.append(("filter_prefix", prefix, 0))
        return self

    def filter_cel(self, expression: str) -> "Traversal":
        """Filter active node set by evaluating a Google CEL expression via C++ Pratt compiler."""
        self._steps.append(("filter_cel", expression, 0))
        return self

    def filter(
        self,
        predicate: Optional[Union[str, int]] = None,
        *,
        filter_id: Optional[int] = None,
        prefix: Optional[str] = None,
        cel: Optional[str] = None,
    ) -> "Traversal":
        """
        Filter current node set using secondary indices, prefix match, or Google CEL expression:
          - snap.traverse(...).filter("user.age >= 21 && user.status == 'ACTIVE'")
          - snap.traverse(...).filter(cel="startsWith(node, 'USER_')")
          - snap.traverse(...).filter(filter_id=42)
          - snap.traverse(...).filter(prefix="COVID-19")
        """
        if cel is not None:
            return self.filter_cel(cel)
        if isinstance(predicate, str):
            return self.filter_cel(predicate)
        if isinstance(predicate, int):
            return self.filter_node(predicate)
        if prefix is not None:
            return self.filter_prefix(prefix)
        if filter_id is not None:
            return self.filter_node(filter_id)
        return self

    def degree(self, relation: Union[str, int]) -> "Traversal":
        """Compute degree on relation."""
        self._steps.append(("degree", self._resolve_rel(relation), 0))
        return self

    def compile(self, input_type: str = "node") -> vm.CompiledQuery:
        """Compile traversal steps into an optimized ImpulseVM bytecode program."""
        qb = vm.QueryBuilder(start_register=0)
        if input_type == "node":
            qb.input_node(0)
        elif input_type == "set":
            qb.input_set(0)
        # if input_type == "none", register 0 is pre-seeded in VmState

        for step in self._steps:
            op = step[0]
            if op == "out":
                qb.walk_edge(step[1])
            elif op == "out_filtered":
                qb.walk_edge_filtered(step[1], step[2])
            elif op == "out_predicate":
                qb.walk_edge_predicate(step[1], step[2])
            elif op == "in_":
                qb.walk_csc(step[1])
            elif op == "filter_node":
                qb.filter_node(step[1])
            elif op == "filter_prefix":
                qb.filter_node_str_prefix(step[1])
            elif op == "filter_cel":
                qb.filter_cel(step[1])
            elif op == "degree":
                qb.walk_degree(step[1])
        qb.collect_bitset()
        return qb.compile()

    def execute(self, start_node: Optional[int] = None) -> List[int]:
        """Execute traversal and return list of matching target node IDs."""
        return self.to_list(start_node=start_node)

    def to_list(self, start_node: Optional[int] = None) -> List[int]:
        """Execute traversal and collect matching target node IDs as a list."""
        with vm.VmContext(self._snapshot) as ctx:
            state = vm.VmState()

            if self._all_nodes:
                bs_in = ctx.acquire_bitset()
                node_count = self._initial_domain.node_count if self._initial_domain else 0
                if node_count == 0:
                    for i in range(self._snapshot.relation_count()):
                        rel = self._snapshot.get_relation(i)
                        if self._initial_domain and rel["src_domain_id"] == self._initial_domain.domain_id:
                            node_count = max(node_count, rel["node_count"])
                    if node_count == 0:
                        node_count = 64
                ctx.bitset_fill(bs_in, node_count)
                compiled = self.compile(input_type="none")
                state.set_register(0, bs_in)
                state.set_register_type(0, vm.RegisterType.TYPE_BITSET_HANDLE)
                res = compiled.execute_with_context(ctx, state, 0)
                ctx.release_bitset(bs_in)
            elif self._start_nodes is not None and len(self._start_nodes) > 1:
                bs_in = ctx.acquire_bitset()
                for nid in self._start_nodes:
                    ctx.bitset_add(bs_in, int(nid))
                compiled = self.compile(input_type="none")
                state.set_register(0, bs_in)
                state.set_register_type(0, vm.RegisterType.TYPE_BITSET_HANDLE)
                res = compiled.execute_with_context(ctx, state, 0)
                ctx.release_bitset(bs_in)
            else:
                seed = self._start_node if start_node is None else start_node
                if self._start_nodes is not None and len(self._start_nodes) == 1:
                    seed = self._start_nodes[0]
                compiled = self.compile(input_type="node")
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

    def to_key_list(self, start_node: Optional[int] = None) -> List[str]:
        """Execute traversal and resolve target dense IDs into string keys in active domain."""
        nodes = self.to_list(start_node=start_node)
        if self._current_domain is not None:
            return [self._current_domain.to_key(nid) for nid in nodes]
        return [str(nid) for nid in nodes]

    def to_key_set(self, start_node: Optional[int] = None) -> Set[str]:
        """Execute traversal and resolve target dense IDs into unique string keys in active domain."""
        return set(self.to_key_list(start_node=start_node))

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

    def to_impas(self) -> str:
        """Disassemble traversal steps into human-readable ImpAsm (.impas) text assembly."""
        lines = [
            "; =========================================================================",
            ";                  IMPULSE VM BYTECODE DISASSEMBLY (.impas)               ",
            "; =========================================================================",
            ".version 0.9.0",
            f".instructions {len(self._steps) + 3}",
            "",
            "  0x0000:  OP_INIT_INPUT_NODE    R0              ; Seed input parameter -> R0",
        ]

        curr_reg = 0
        pc = 1
        inv_catalog = {v: k for k, v in self._catalog.items()} if self._catalog else {}

        for step in self._steps:
            op = step[0]
            rel_id = step[1]
            rel_name = inv_catalog.get(rel_id, f"rel_{rel_id}")
            next_reg = curr_reg + 1

            if op == "out":
                lines.append(f"  0x{pc:04x}:  OP_CSR_WALK           R{next_reg}, R{curr_reg}, #{rel_id:<3} ; Forward CSR walk via [\"{rel_name}\"] (rel_id={rel_id})")
            elif op == "in_":
                lines.append(f"  0x{pc:04x}:  OP_CSC_WALK           R{next_reg}, R{curr_reg}, #{rel_id:<3} ; Reverse CSC walk via [\"{rel_name}\"] (rel_id={rel_id})")
            elif op == "filter_node":
                lines.append(f"  0x{pc:04x}:  OP_NODE_FILTER        R{curr_reg}, #{rel_id:<3}     ; Filter R{curr_reg} by attribute filter #{rel_id}")
                next_reg = curr_reg
            elif op == "filter_prefix":
                lines.append(f"  0x{pc:04x}:  OP_FILTER_STR_PREFIX  R{curr_reg}, \"{rel_id}\"     ; Filter R{curr_reg} by prefix \"{rel_id}\"")
                next_reg = curr_reg

            curr_reg = next_reg
            pc += 1

        lines.append(f"  0x{pc:04x}:  OP_COLLECT_BITSET     R{curr_reg}             ; Materialize active target bitset handle")
        lines.append(f"  0x{pc+1:04x}:  OP_HALT                               ; Execution complete")
        return "\n".join(lines)

    def disassemble(self) -> str:
        """Alias for to_impas()."""
        return self.to_impas()
