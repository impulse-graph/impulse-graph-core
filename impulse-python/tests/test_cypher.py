
import pytest
from impulse_graph import Snapshot

IMPS_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"

def test_cypher_point_query():
    with Snapshot(IMPS_PATH) as snap:
        query = (
            "MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) "
            "WHERE d.id = $diseaseId RETURN c"
        )
        # Execute query
        results = snap.cypher(query, params={"diseaseId": 14738}, catalog="hetionet")
        assert isinstance(results, list)

def test_cypher_count_query():
    with Snapshot(IMPS_PATH) as snap:
        query = (
            "MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound) "
            "WHERE d.id = $diseaseId RETURN count(c)"
        )
        count_res = snap.cypher(query, params={"diseaseId": 14738}, catalog="hetionet")
        assert isinstance(count_res, int)

def test_compile_cypher():
    with Snapshot(IMPS_PATH) as snap:
        query = "MATCH (d:Disease)-[:DlA]->(a:Anatomy)-[:AeG]->(g:Gene)<-[:CbG]-(c:Compound) WHERE d.id = 14738 RETURN c"
        traversal = snap.compile_cypher(query, catalog="hetionet")
        compiled = traversal.compile()
        assert compiled.instruction_count() > 0
