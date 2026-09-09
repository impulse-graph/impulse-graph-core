import os
import tempfile

import pytest

from impulse_graph import Snapshot, Writer


@pytest.fixture
def cypher_snapshot():
    """
    Synthetic multi-domain biomedical graph for Cypher testing:
    Domain 0: Disease (node 0)
    Domain 1: Gene (node 0, 1)
    Domain 2: Compound (node 0)

    Rel 0: Disease -> Gene (DaG)
      - disease 0 -> [gene 0, gene 1]
    Rel 1: Gene -> Compound (GbC)
      - gene 0 -> [compound 0]
      - gene 1 -> []
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "cypher_test.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 1, "Disease")
            w.add_domain(1, 1, "Gene")
            w.add_domain(2, 1, "Compound")

            # Rel 0: DaG (Disease -> Gene)
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=1,
                encoding_type=0,
                node_count=1,
                edge_count=2,
                section_features=0,
                row_offsets=[0, 2],
                col_indices=[0, 1],
            )

            # Rel 1: GbC (Gene -> Compound)
            w.add_relation(
                src_domain_id=1,
                tgt_domain_id=2,
                encoding_type=0,
                node_count=2,
                edge_count=1,
                section_features=0,
                row_offsets=[0, 1, 1],
                col_indices=[0],
            )
            w.finalize()
        yield snap_path


def test_cypher_point_query(cypher_snapshot):
    with Snapshot(cypher_snapshot) as snap:
        catalog = {"DaG": 0, "GbC": 1}
        # (Disease) -[:DaG]-> (Gene) -[:GbC]-> (Compound)
        query = (
            "MATCH (d:Disease)-[:DaG]->(g:Gene)-[:GbC]->(c:Compound) "
            "WHERE d.id = $diseaseId RETURN c"
        )
        results = snap.cypher(query, params={"diseaseId": 0}, catalog=catalog)
        assert isinstance(results, list)
        assert results == [0]


def test_cypher_count_query(cypher_snapshot):
    with Snapshot(cypher_snapshot) as snap:
        catalog = {"DaG": 0, "GbC": 1}
        query = (
            "MATCH (d:Disease)-[:DaG]->(g:Gene)-[:GbC]->(c:Compound) WHERE d.id = 0 RETURN count(c)"
        )
        count_res = snap.cypher(query, catalog=catalog)
        assert isinstance(count_res, int)
        assert count_res == 1


def test_compile_cypher(cypher_snapshot):
    with Snapshot(cypher_snapshot) as snap:
        catalog = {"DaG": 0, "GbC": 1}
        query = "MATCH (d:Disease)-[:DaG]->(g:Gene) WHERE d.id = 0 RETURN g"
        traversal = snap.compile_cypher(query, catalog=catalog)
        compiled = traversal.compile()
        assert compiled.instruction_count() > 0
