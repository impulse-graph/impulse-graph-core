import os
import tempfile
import pytest
from impulse_graph import Snapshot, Writer


@pytest.fixture
def ecommerce_snapshot():
    """
    Creates a synthetic multi-domain e-commerce graph:
    Domain 0: User (node 0="alice", node 1="bob")
    Domain 1: Product (node 0="laptop", node 1="mouse", node 2="keyboard")
    Domain 2: Brand (node 0="apple", node 1="logitech")

    Relation 0: (User) -[:PURCHASED]-> (Product)
      - user 0 (alice) -> [0 (laptop), 1 (mouse)]
      - user 1 (bob) -> [1 (mouse), 2 (keyboard)]
    Relation 1: (Product) -[:MANUFACTURED_BY]-> (Brand)
      - product 0 (laptop) -> [0 (apple)]
      - product 1 (mouse) -> [1 (logitech)]
      - product 2 (keyboard) -> [1 (logitech)]
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "ecommerce.imps")
        with Writer(snap_path) as w:
            w.add_domain(domain_id=0, key_type=1, name="User")
            w.add_domain(domain_id=1, key_type=1, name="Product")
            w.add_domain(domain_id=2, key_type=1, name="Brand")

            # Rel 0: User -> Product (2 users, 4 edges)
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=1,
                encoding_type=0,
                node_count=2,
                edge_count=4,
                section_features=0,
                row_offsets=[0, 2, 4],
                col_indices=[0, 1, 1, 2],
            )

            # Rel 1: Product -> Brand (3 products, 3 edges)
            w.add_relation(
                src_domain_id=1,
                tgt_domain_id=2,
                encoding_type=0,
                node_count=3,
                edge_count=3,
                section_features=0,
                row_offsets=[0, 1, 2, 3],
                col_indices=[0, 1, 1],
            )
            w.finalize()

        yield snap_path


def test_domain_anchor_properties(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        user_dom = snap.domain("User")
        assert user_dom.domain_name == "User"
        assert user_dom.domain_id == 0

        prod_dom = snap.domain("Product")
        assert prod_dom.domain_name == "Product"
        assert prod_dom.domain_id == 1

        brand_dom = snap.domain("Brand")
        assert brand_dom.domain_name == "Brand"
        assert brand_dom.domain_id == 2

        # Access by integer index
        assert snap.domain(0).domain_name == "User"
        assert snap.domain(1).domain_name == "Product"
        assert snap.domain(2).domain_name == "Brand"


def test_single_seed_traversal(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # alice (user 0) -> PURCHASED -> laptop (0), mouse (1)
        products = snap.domain("User").from_node(0).out("PURCHASED").to_list()
        assert sorted(products) == [0, 1]

        # bob (user 1) -> PURCHASED -> mouse (1), keyboard (2)
        products_bob = snap.domain("User").from_node(1).out("PURCHASED").to_list()
        assert sorted(products_bob) == [1, 2]


def test_batch_seeds_traversal(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # alice (0) and bob (1) -> PURCHASED -> laptop (0), mouse (1), keyboard (2)
        products = snap.domain("User").from_nodes([0, 1]).out("PURCHASED").to_list()
        assert sorted(products) == [0, 1, 2]
        assert snap.domain("User").from_nodes([0, 1]).out("PURCHASED").count() == 3


def test_all_nodes_frontier_traversal(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # All users -> PURCHASED -> all products
        products = snap.domain("User").all().out("PURCHASED").to_set()
        assert products == {0, 1, 2}


def test_multi_hop_cross_domain_traversal(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # alice (user 0) -> PURCHASED (rel 0) -> MANUFACTURED_BY (rel 1) -> Brands
        # alice bought laptop (apple: 0) and mouse (logitech: 1)
        brands_alice = (
            snap.domain("User")
            .from_node(0)
            .out("PURCHASED")
            .out("MANUFACTURED_BY")
            .to_list()
        )
        assert sorted(brands_alice) == [0, 1]

        # bob (user 1) bought mouse (logitech: 1) and keyboard (logitech: 1) -> [1]
        brands_bob = (
            snap.domain("User")
            .from_node(1)
            .out("PURCHASED")
            .out("MANUFACTURED_BY")
            .to_list()
        )
        assert brands_bob == [1]


def test_domain_validation_errors(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # Attempting to traverse MANUFACTURED_BY (Product -> Brand) directly from User domain
        with pytest.raises(ValueError, match="Domain mismatch|Cannot traverse"):
            snap.domain("User").from_node(0).out("MANUFACTURED_BY").to_list()

        # Unknown domain
        with pytest.raises(KeyError, match="Domain 'Warehouse' not found"):
            snap.domain("Warehouse")


def test_domain_key_resolution(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        # Resolve keys in Product domain
        prod_dom = snap.domain("Product")
        key_0 = prod_dom.to_key(0)
        assert isinstance(key_0, str)

        key_list = snap.domain("User").from_node(0).out("PURCHASED").to_key_list()
        assert len(key_list) == 2
        assert isinstance(key_list[0], str)

        key_set = snap.domain("User").from_node(0).out("PURCHASED").to_key_set()
        assert len(key_set) == 2


def test_domain_contains_and_count(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        t = snap.domain("User").from_node(0).out("PURCHASED")
        assert t.count() == 2
        assert t.contains(0) is True
        assert t.contains(1) is True
        assert t.contains(2) is False


def test_domain_disassembly_impas(ecommerce_snapshot):
    with Snapshot(ecommerce_snapshot) as snap:
        t = snap.domain("User").from_node(0).out("PURCHASED").out("MANUFACTURED_BY")
        impas = t.to_impas()
        assert ".version 0.9.0" in impas
        assert "OP_INIT_INPUT_NODE" in impas
        assert "OP_CSR_WALK" in impas
        assert "OP_COLLECT_BITSET" in impas
        assert "OP_HALT" in impas
