import pytest
from impulse_graph import Snapshot, Writer, vm


def test_cel_parser_ir_generation():
    """Verify that C++ Google CEL Pratt parser compiles expressions to ImpScheme IR."""
    ir1 = vm.parse_cel("2 + 3 * 4")
    assert ir1 == "(+ 2 (* 3 4))"

    ir2 = vm.parse_cel("a > 10 && b <= 20")
    assert ir2 == "(mask-and (vec-cmp-gt a 10) (<= b 20))"

    ir3 = vm.parse_cel("!is_active || status == 200")
    assert ir3 == "(mask-or (mask-not is_active) (vec-cmp-eq status 200))"


def test_cel_vector_math_functions():
    """Verify that the 42-function analytical math catalog resolves via CEL."""
    ir_sqrt = vm.parse_cel("sqrt(edge.weight * 100.0)")
    assert "sqrt" in ir_sqrt

    ir_clamp = vm.parse_cel("clamp(safeDiv(sqrt(edge.weight), 1.0, 0.0), 0.0, 100.0)")
    assert "clamp" in ir_clamp
    assert "safeDiv" in ir_clamp


def test_cel_constant_folding():
    """Verify that C++ CEL AST optimizer folds constant expressions."""
    # 2 + 3 * 4 folds to 14 in optimizer
    folded = vm.optimize_cel("2 + 3 * 4")
    assert folded == "14"

    # true && false folds to false (#f)
    folded_bool = vm.optimize_cel("true && false")
    assert folded_bool == "#f"


def test_query_builder_filter_cel():
    """Verify QueryBuilder.filter_cel emits valid bytecode."""
    qb = vm.QueryBuilder()
    qb.input_node(0).walk_edge(0).filter_cel("age >= 21").collect_bitset()
    compiled = qb.compile()
    assert compiled.instruction_count() == 5


def test_query_builder_filter_cel_prefix():
    """Verify QueryBuilder.filter_cel handles string prefix calls."""
    qb = vm.QueryBuilder()
    qb.input_node(0).filter_cel("startsWith(node, 'USER_')").collect_bitset()
    compiled = qb.compile()
    assert compiled.instruction_count() == 4


def test_traversal_fluent_cel_filter():
    """Verify Traversal.filter() seamlessly delegates CEL strings to C++."""
    import tempfile, os

    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "cel_test.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 1, "Node")
            w.add_relation(0, 0, 0, 3, 2, 0, [0, 1, 2, 2], [1, 2])
            w.finalize()

        with Snapshot(snap_path) as snap:
            # String passed directly as first arg
            t1 = snap.traverse(0).out(0).filter("status == 200")
            assert t1.compile().instruction_count() > 0

            # Explicit cel keyword arg
            t2 = snap.traverse(0).out(0).filter(cel="user.age >= 21 && user.score > 0.5")
            assert t2.compile().instruction_count() > 0


def test_cel_invalid_syntax_error():
    """Verify that invalid CEL syntax raises an error."""
    with pytest.raises(Exception, match="Failed to parse CEL|Unexpected token"):
        vm.parse_cel("a > && 10")
