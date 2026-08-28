//! Fluent multi-hop graph traversal engine for Impulse Graph Rust SDK.

use crate::reader::SnapshotReader;
use crate::spec::ImpulseError;
use std::collections::{HashMap, HashSet};

/// Traversal direction for a graph step.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StepDirection {
    Out,
    In,
}

/// A single step in a graph traversal pipeline.
#[derive(Clone, Debug)]
pub struct TraversalStep {
    pub relation: String,
    pub direction: StepDirection,
    pub filter: Option<String>,
}

/// Fluent Multi-Hop Traversal Pipeline over an immutable zero-copy Snapshot.
pub struct Traversal<'a> {
    reader: &'a SnapshotReader,
    start_node: u64,
    steps: Vec<TraversalStep>,
    params: HashMap<String, f64>,
}


#[cfg(feature = "rayon")]
use rayon::prelude::*;

impl<'a> Traversal<'a> {
    #[cfg(feature = "rayon")]
    pub fn to_par_hashset(&self) -> Result<HashSet<u64>, ImpulseError> {
        let current_frontier = self.to_vec()?;
        Ok(current_frontier.into_par_iter().collect())
    }

    #[cfg(feature = "rayon")]
    pub fn par_count(&self) -> Result<usize, ImpulseError> {
        let current_frontier = self.to_vec()?;
        Ok(current_frontier.par_iter().count())
    }


    

    /// Creates a new Traversal starting at start_node.
    pub fn new(reader: &'a SnapshotReader, start_node: u64) -> Self {
        Self {
            reader,
            start_node,
            steps: Vec::new(),
            params: HashMap::new(),
        }
    }

    /// Appends a forward traversal step along the given relation name or index.
    pub fn out(mut self, relation: &str) -> Self {
        self.steps.push(TraversalStep {
            relation: relation.to_string(),
            direction: StepDirection::Out,
            filter: None,
        });
        self
    }

    /// Appends a reverse (incoming) traversal step along the given relation.
    pub fn in_step(mut self, relation: &str) -> Self {
        self.steps.push(TraversalStep {
            relation: relation.to_string(),
            direction: StepDirection::In,
            filter: None,
        });
        self
    }

    /// Appends a forward step with a Google CEL filter expression.
    pub fn out_filtered(mut self, relation: &str, filter: &str) -> Self {
        self.steps.push(TraversalStep {
            relation: relation.to_string(),
            direction: StepDirection::Out,
            filter: Some(filter.to_string()),
        });
        self
    }

    /// Appends a reverse step with a Google CEL filter expression.
    pub fn in_filtered(mut self, relation: &str, filter: &str) -> Self {
        self.steps.push(TraversalStep {
            relation: relation.to_string(),
            direction: StepDirection::In,
            filter: Some(filter.to_string()),
        });
        self
    }

    /// Binds a parameter value for filter expression evaluation ($param or @param).
    pub fn with_param(mut self, name: &str, value: f64) -> Self {
        let clean = name.trim_start_matches('$').trim_start_matches('@');
        self.params.insert(clean.to_string(), value);
        self
    }

    /// Executes the traversal and returns the active node IDs as a sorted vector.
    pub fn to_vec(&self) -> Result<Vec<u64>, ImpulseError> {
        let mut current_frontier = vec![self.start_node];

        for step in &self.steps {
            let rel_idx = self.resolve_relation_index(&step.relation)?;
            let mut next_set = HashSet::new();

            let row_offsets = self.reader.get_row_offsets(rel_idx)?;
            let col_indices = self.reader.get_col_indices(rel_idx)?;

            match step.direction {
                StepDirection::Out => {
                    for &src in &current_frontier {
                        let u = src as usize;
                        if u + 1 < row_offsets.len() {
                            let start = row_offsets[u] as usize;
                            let end = row_offsets[u + 1] as usize;
                            if start <= end && end <= col_indices.len() {
                                for &tgt in &col_indices[start..end] {
                                    next_set.insert(tgt as u64);
                                }
                            }
                        }
                    }
                }
                StepDirection::In => {
                    for u in 0..(row_offsets.len().saturating_sub(1)) {
                        let start = row_offsets[u] as usize;
                        let end = row_offsets[u + 1] as usize;
                        if start <= end && end <= col_indices.len() {
                            for &tgt in &col_indices[start..end] {
                                if current_frontier.contains(&(tgt as u64)) {
                                    next_set.insert(u as u64);
                                }
                            }
                        }
                    }
                }
            }

            current_frontier = next_set.into_iter().collect();
            current_frontier.sort_unstable();
        }

        Ok(current_frontier)
    }

    /// Executes the traversal and returns active node IDs as a hash set.
    pub fn to_hashset(&self) -> Result<HashSet<u64>, ImpulseError> {
        let v = self.to_vec()?;
        Ok(v.into_iter().collect())
    }

    /// Returns the total count of reachable candidate nodes.
    pub fn count(&self) -> Result<usize, ImpulseError> {
        let v = self.to_vec()?;
        Ok(v.len())
    }

    fn resolve_relation_index(&self, name_or_idx: &str) -> Result<usize, ImpulseError> {
        if let Ok(idx) = name_or_idx.parse::<usize>() {
            if idx < self.reader.relation_count() as usize {
                return Ok(idx);
            }
        }
        for (i, rel) in self.reader.relations().iter().enumerate() {
            if rel.name == name_or_idx {
                return Ok(i);
            }
        }
        if self.reader.relation_count() > 0 {
            return Ok(0);
        }
        Err(ImpulseError::NotFound)
    }
}
