"""
Impulse Graph Engine - openCypher Query Parser & Virtual Machine Executor
Translates declarative openCypher statements directly into optimized ImpulseVM pipelines.
"""

import re
from typing import Dict, Any, Optional, Union, List, Set
import numpy as np
from .traversal import Traversal


class CypherQuery:
    """
    Represents a parsed openCypher query lowered into an ImpulseVM execution plan.
    """

    def __init__(self, query: str, catalog: Optional[Union[str, Dict[str, int]]] = None):
        self.raw_query = query.strip()
        self.catalog = catalog
        self.steps: List[tuple] = []  # (direction: str, relation: str)
        self.seed_var: Optional[str] = None
        self.seed_param_name: Optional[str] = None
        self.seed_literal: Optional[Union[int, str]] = None
        self.return_var: Optional[str] = None
        self.is_count: bool = False
        self._parse()

    def _parse(self):
        # 1. Parse MATCH clause
        match_m = re.search(r"MATCH\s+(.+?)(?:\s+WHERE|\s+RETURN|$)", self.raw_query, re.IGNORECASE | re.DOTALL)
        if not match_m:
            raise ValueError(f"Invalid Cypher: missing MATCH clause in '{self.raw_query}'")
        pattern = match_m.group(1).strip()

        # Find start node
        start_node_m = re.match(r"\((\w+)(?::\w+)?\)", pattern)
        if not start_node_m:
            raise ValueError(f"Invalid Cypher path start node: '{pattern}'")
        self.seed_var = start_node_m.group(1)

        # Check for untyped edges like --> or <-- or -[]->
        if re.search(r"-(?:\[\s*\])?->|<-(?:\[\s*\])?-", pattern):
            raise ValueError("Impulse Graph requires typed relationship patterns (e.g. -[:Rel]->)")

        # Parse edge steps: (<-|-)->[:Rel]->(node)
        step_regex = re.compile(
            r"(\<-|-\>|-)\s*\[(?::(?:`([^`]+)`|([\w:]+)))?(?:\*(\d+))?\]\s*(-\>|\<-|-)\s*\((?:(\w+)(?::\w+)?)?\)"
        )

        for m in step_regex.finditer(pattern):
            left_arrow, rel_quoted, rel_unquoted, hops, right_arrow, target_var = m.groups()
            rel_name = rel_quoted or rel_unquoted
            if not rel_name:
                raise ValueError("Impulse Graph requires typed relationship patterns (e.g. -[:Rel]->)")

            if left_arrow == "<-" and right_arrow == "-":
                direction = "in"
            elif left_arrow == "-" and right_arrow == "->":
                direction = "out"
            else:
                direction = "out"

            hop_count = int(hops) if hops else 1
            for _ in range(hop_count):
                self.steps.append((direction, rel_name))

        # 2. Parse WHERE clause
        where_m = re.search(r"WHERE\s+(.+?)(?:\s+RETURN|$)", self.raw_query, re.IGNORECASE | re.DOTALL)
        if where_m:
            where_str = where_m.group(1).strip()
            pred_m = re.search(
                r"(\w+)\.(?:id|name|dense_id)\s*(?:=|==)\s*(?:\$(\w+)|(\d+)|\'([^\']+)\'|\"([^\"]+)\")",
                where_str,
            )
            if pred_m:
                v, param_name, num_val, s1, s2 = pred_m.groups()
                str_val = s1 or s2
                if v == self.seed_var:
                    if param_name:
                        self.seed_param_name = param_name
                    elif num_val:
                        self.seed_literal = int(num_val)
                    elif str_val:
                        self.seed_literal = str_val

        # 3. Parse RETURN clause
        ret_m = re.search(r"RETURN\s+(.+)$", self.raw_query, re.IGNORECASE | re.DOTALL)
        if ret_m:
            ret_str = ret_m.group(1).strip()
            count_m = re.match(r"count\s*\(\s*(\w+)\s*\)", ret_str, re.IGNORECASE)
            if count_m:
                self.is_count = True
                self.return_var = count_m.group(1)
            else:
                self.return_var = ret_str.split()[0]

    def build_traversal(
        self, snapshot: "Snapshot", params: Optional[Dict[str, Any]] = None
    ) -> Traversal:
        params = params or {}
        seed_node = 0

        if self.seed_param_name and self.seed_param_name in params:
            seed_node = int(params[self.seed_param_name])
        elif self.seed_literal is not None:
            seed_node = int(self.seed_literal)
        elif params:
            # Fallback to first parameter value
            seed_node = int(next(iter(params.values())))

        t = Traversal(snapshot, start_node=seed_node, catalog=self.catalog)
        for direction, rel in self.steps:
            if direction == "out":
                t.out(rel)
            else:
                t.in_(rel)
        return t

    def execute(
        self, snapshot: "Snapshot", params: Optional[Dict[str, Any]] = None
    ) -> Union[List[int], int]:
        t = self.build_traversal(snapshot, params)
        if self.is_count:
            return t.count()
        return t.to_list()
