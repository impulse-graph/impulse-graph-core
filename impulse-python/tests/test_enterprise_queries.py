"""
Unit and integration test battery for enterprise graph queries, CEL lowering, and type conversions.
"""

import pytest
import math
from impulse_graph import vm


def test_enterprise_cel_temporal_decay_lowering():
    """Test lowering of temporal exponential risk decay formula."""
    expr = "amount * exp(-0.05 * (now() - timestamp))"
    ir = vm.parse_cel(expr)
    assert "exp" in ir
    assert "now" in ir
    assert "amount" in ir
    assert "timestamp" in ir


def test_enterprise_cel_nullable_discount_coalesce():
    """Test lowering of nullable attribute coalescing."""
    expr = "coalesce(discount, 0.0) > 0.15 ? unit_price * (1.0 - discount) : unit_price"
    ir = vm.parse_cel(expr)
    assert "vec-blend" in ir
    assert "coalesce" in ir


def test_enterprise_type_conversions_and_constant_folding():
    """Test integer promotion and arithmetic constant folding."""
    expr = "10 + 20 * 3"
    val = vm.optimize_cel(expr)
    assert val == "70"


def test_enterprise_fluent_multi_hop_query_builder():
    """Test constructing multi-hop multi-domain query with CEL filters."""
    qb = vm.QueryBuilder()
    qb.input_node(0)
    qb.walk_edge(relation_id=0)
    qb.filter_cel("amount > 5000.0 && dest.status == 1")
    qb.walk_edge(relation_id=1)
    qb.collect_array()

    compiled = qb.compile()
    assert compiled.instruction_count() >= 4
    bytecode = compiled.bytecode()
    assert len(bytecode) == compiled.instruction_count()
