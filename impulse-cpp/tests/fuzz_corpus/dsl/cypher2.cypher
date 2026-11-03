MATCH (a:User)-[:KNOWS*1..3]->(b:User) WHERE a.age > 21 RETURN b.name LIMIT 10
