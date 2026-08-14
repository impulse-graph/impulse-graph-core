//! @file stratification.rs
//! @brief Stratified Negation & Dependency Graph Validator for ImpLog Datalog rules.

use std::collections::{HashMap, HashSet};

/// @brief Represents a dependency edge in the Datalog Predicate Dependency Graph (PDG).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DependencyEdge {
    pub from: String,
    pub to: String,
    pub is_negated: bool,
}

/// @brief Validates that Datalog rules obey Stratified Negation (no negative cycles).
/// @param edges List of dependency edges extracted from Datalog rules.
/// @return `Result<(), String>`
pub fn validate_stratification(edges: &[DependencyEdge]) -> Result<(), String> {
    let mut predicates = HashSet::new();
    let mut adj: HashMap<String, Vec<(String, bool)>> = HashMap::new();

    for edge in edges {
        predicates.insert(edge.from.clone());
        predicates.insert(edge.to.clone());
        adj.entry(edge.from.clone())
            .or_default()
            .push((edge.to.clone(), edge.is_negated));
    }

    let mut in_stack = HashSet::new();
    let mut path = Vec::new();

    // Check for negative cycles using DFS recursion stack
    for pred in &predicates {
        if let Err(err) = dfs_check(pred, &adj, &mut in_stack, &mut path) {
            return Err(err);
        }
    }

    Ok(())
}

fn dfs_check(
    current: &str,
    adj: &HashMap<String, Vec<(String, bool)>>,
    in_stack: &mut HashSet<String>,
    path: &mut Vec<(String, bool)>,
) -> Result<(), String> {
    in_stack.insert(current.to_string());

    if let Some(neighbors) = adj.get(current) {
        for (next, is_negated) in neighbors {
            path.push((next.clone(), *is_negated));

            if in_stack.contains(next) {
                // Cycle detected in recursion stack!
                if let Some(pos) = path.iter().position(|(node, _)| node == next) {
                    let has_negation = path[pos..].iter().any(|(_, neg)| *neg);
                    if has_negation {
                        let cycle_nodes: Vec<String> = path[pos..].iter().map(|(n, _)| n.clone()).collect();
                        return Err(format!(
                            "Unstratified Negation Error: Circular dependency involving negation detected: {}",
                            cycle_nodes.join(" -> ")
                        ));
                    }
                }
            } else {
                dfs_check(next, adj, in_stack, path)?;
            }

            path.pop();
        }
    }

    in_stack.remove(current);
    Ok(())
}
