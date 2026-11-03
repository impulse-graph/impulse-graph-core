MATCH (d:Disease)-[:DaG]->(g:Gene) WHERE d.id = $id RETURN g
