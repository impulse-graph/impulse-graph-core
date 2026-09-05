//! Graph Statistics, CBO Sketches, and Structural Multiplicity Analyzers
//! Matches Java core statistics engine (`org.impulsegraph.core.stats`).

use crate::reader::SnapshotReader;
use crate::spec::ImpulseError;
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Multiplicity {
    OneToOne,
    ManyToOne,
    OneToMany,
    ManyToMany,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Monotonicity {
    None,
    StrictlyIncreasing,
    WeaklyIncreasing,
    StrictlyDecreasing,
    WeaklyDecreasing,
    Constant,
}

#[derive(Debug, Clone)]
pub struct RelationStatistics {
    pub node_count: u64,
    pub edge_count: u64,
    pub unique_source_nodes: u64,
    pub max_out_degree: u32,
    pub avg_out_degree: f64,
    pub std_dev_degree: f64,
    pub p50_degree: u32,
    pub p90_degree: u32,
    pub p99_degree: u32,
    pub sparsity: f64,
    pub supernode_count: usize,
    pub supernode_indices: Vec<u32>,
    pub multiplicity: Multiplicity,
    pub max_in_degree: u32,
    pub avg_in_degree: f64,
    pub is_acyclic: bool,
    pub is_symmetric: bool,
    pub is_transitive: bool,
}

impl RelationStatistics {
    pub fn is_functional(&self) -> bool {
        self.multiplicity == Multiplicity::ManyToOne || self.multiplicity == Multiplicity::OneToOne
    }

    pub fn is_injective(&self) -> bool {
        self.multiplicity == Multiplicity::OneToMany || self.multiplicity == Multiplicity::OneToOne
    }

    pub fn is_bijective(&self) -> bool {
        self.multiplicity == Multiplicity::OneToOne
    }

    pub fn is_supernode(&self, node_id: u32) -> bool {
        self.supernode_indices.binary_search(&node_id).is_ok()
    }
}

#[derive(Debug, Clone)]
pub struct AttributeStatistics {
    pub name: String,
    pub min_int_val: i64,
    pub max_int_val: i64,
    pub min_float_val: f64,
    pub max_float_val: f64,
    pub min_str_val: String,
    pub max_str_val: String,
    pub null_count: u64,
    pub distinct_count: usize,
    pub monotonicity: Monotonicity,
    pub has_nulls: bool,
}

impl AttributeStatistics {
    pub fn empty(name: &str) -> Self {
        Self {
            name: name.to_string(),
            min_int_val: i64::MAX,
            max_int_val: i64::MIN,
            min_float_val: f64::MAX,
            max_float_val: -f64::MAX,
            min_str_val: String::new(),
            max_str_val: String::new(),
            null_count: 0,
            distinct_count: 0,
            monotonicity: Monotonicity::None,
            has_nulls: false,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct GraphStatistics {
    pub relation_stats: HashMap<String, RelationStatistics>,
    pub attribute_stats: HashMap<String, AttributeStatistics>,
    pub metadata_sketches: HashMap<String, String>,
}

pub struct RelationStatisticsCalculator;

impl RelationStatisticsCalculator {
    pub const DEFAULT_SUPERNODE_ZSCORE_THRESHOLD: f64 = 3.0;

    pub fn calculate(
        reader: &SnapshotReader,
        relation_index: usize,
        supernode_zscore_threshold: f64,
    ) -> Result<RelationStatistics, ImpulseError> {
        let rel = reader
            .relations()
            .get(relation_index)
            .ok_or(ImpulseError::NotFound)?;

        let node_count = rel.node_count;
        let edge_count = rel.edge_count;

        if node_count == 0 {
            return Ok(RelationStatistics {
                node_count: 0,
                edge_count: 0,
                unique_source_nodes: 0,
                max_out_degree: 0,
                avg_out_degree: 0.0,
                std_dev_degree: 0.0,
                p50_degree: 0,
                p90_degree: 0,
                p99_degree: 0,
                sparsity: 0.0,
                supernode_count: 0,
                supernode_indices: Vec::new(),
                multiplicity: Multiplicity::ManyToMany,
                max_in_degree: 0,
                avg_in_degree: 0.0,
                is_acyclic: true,
                is_symmetric: true,
                is_transitive: true,
            });
        }

        let row_offsets = reader.get_row_offsets(relation_index)?;
        let col_indices = reader.get_col_indices(relation_index)?;

        let n = node_count as usize;
        let mut out_degrees = vec![0u32; n];
        let mut in_degrees = vec![0u32; n];

        let mut unique_source_nodes = 0u64;
        let mut max_out_degree = 0u32;

        if row_offsets.len() > n {
            for i in 0..n {
                let deg = row_offsets[i + 1].saturating_sub(row_offsets[i]);
                out_degrees[i] = deg;
                if deg > 0 {
                    unique_source_nodes += 1;
                }
                if deg > max_out_degree {
                    max_out_degree = deg;
                }
            }
        }

        // Calculate in-degrees from target column indices
        let mut max_in_degree = 0u32;
        for &tgt in col_indices {
            let tgt_idx = tgt as usize;
            if tgt_idx < n {
                in_degrees[tgt_idx] += 1;
                if in_degrees[tgt_idx] > max_in_degree {
                    max_in_degree = in_degrees[tgt_idx];
                }
            }
        }

        let avg_out_degree = if node_count > 0 {
            edge_count as f64 / node_count as f64
        } else {
            0.0
        };
        let avg_in_degree = avg_out_degree;

        // Standard deviation
        let mut sum_squared_diffs = 0.0;
        for &deg in &out_degrees {
            let diff = (deg as f64) - avg_out_degree;
            sum_squared_diffs += diff * diff;
        }
        let std_dev_degree = (sum_squared_diffs / (node_count as f64)).sqrt();

        // Percentiles
        let mut sorted_degrees = out_degrees.clone();
        sorted_degrees.sort_unstable();

        let p50_degree = sorted_degrees[(n as f64 * 0.50) as usize];
        let p90_degree = sorted_degrees[std::cmp::min(n - 1, (n as f64 * 0.90) as usize)];
        let p99_degree = sorted_degrees[std::cmp::min(n - 1, (n as f64 * 0.99) as usize)];

        let sparsity = (unique_source_nodes as f64) / (node_count as f64);

        // Supernode identification
        let supernode_cutoff = avg_out_degree + (supernode_zscore_threshold * std_dev_degree);
        let effective_cutoff = supernode_cutoff.max(10.0);

        let mut supernode_indices = Vec::new();
        for (i, &deg) in out_degrees.iter().enumerate() {
            if (deg as f64) >= effective_cutoff {
                supernode_indices.push(i as u32);
            }
        }
        let supernode_count = supernode_indices.len();

        // Multiplicity Classification
        let multiplicity = if max_out_degree <= 1 && max_in_degree <= 1 {
            Multiplicity::OneToOne
        } else if max_out_degree <= 1 {
            Multiplicity::ManyToOne
        } else if max_in_degree <= 1 {
            Multiplicity::OneToMany
        } else {
            Multiplicity::ManyToMany
        };

        Ok(RelationStatistics {
            node_count,
            edge_count,
            unique_source_nodes,
            max_out_degree,
            avg_out_degree,
            std_dev_degree,
            p50_degree,
            p90_degree,
            p99_degree,
            sparsity,
            supernode_count,
            supernode_indices,
            multiplicity,
            max_in_degree,
            avg_in_degree,
            is_acyclic: false,
            is_symmetric: false,
            is_transitive: false,
        })
    }
}

pub struct AttributeStatisticsCalculator;

impl AttributeStatisticsCalculator {
    pub fn calculate_int32(name: &str, data: &[u8]) -> AttributeStatistics {
        let count = data.len() / 4;
        if count == 0 {
            return AttributeStatistics::empty(name);
        }

        let mut min = i64::MAX;
        let mut max = i64::MIN;
        let mut null_count = 0u64;
        let mut distinct = HashSet::new();

        let mut is_strict_inc = true;
        let mut is_weak_inc = true;
        let mut is_strict_dec = true;
        let mut is_weak_dec = true;
        let mut is_constant = true;

        let mut prev = 0i32;
        let mut first = true;

        for i in 0..count {
            let chunk: [u8; 4] = data[i * 4..i * 4 + 4].try_into().unwrap();
            let val = i32::from_le_bytes(chunk);

            if val == i32::MIN {
                null_count += 1;
                continue;
            }

            if (val as i64) < min {
                min = val as i64;
            }
            if (val as i64) > max {
                max = val as i64;
            }
            if distinct.len() < 10_000 {
                distinct.insert(val);
            }

            if first {
                prev = val;
                first = false;
            } else {
                if val != prev {
                    is_constant = false;
                }
                if val <= prev {
                    is_strict_inc = false;
                }
                if val < prev {
                    is_weak_inc = false;
                }
                if val >= prev {
                    is_strict_dec = false;
                }
                if val > prev {
                    is_weak_dec = false;
                }
                prev = val;
            }
        }

        let monotonicity = if is_constant {
            Monotonicity::Constant
        } else if is_strict_inc {
            Monotonicity::StrictlyIncreasing
        } else if is_weak_inc {
            Monotonicity::WeaklyIncreasing
        } else if is_strict_dec {
            Monotonicity::StrictlyDecreasing
        } else if is_weak_dec {
            Monotonicity::WeaklyDecreasing
        } else {
            Monotonicity::None
        };

        AttributeStatistics {
            name: name.to_string(),
            min_int_val: if min == i64::MAX { 0 } else { min },
            max_int_val: if max == i64::MIN { 0 } else { max },
            min_float_val: if min == i64::MAX { 0.0 } else { min as f64 },
            max_float_val: if max == i64::MIN { 0.0 } else { max as f64 },
            min_str_val: String::new(),
            max_str_val: String::new(),
            null_count,
            distinct_count: distinct.len(),
            monotonicity,
            has_nulls: null_count > 0,
        }
    }

    pub fn calculate_int64(name: &str, data: &[u8]) -> AttributeStatistics {
        let count = data.len() / 8;
        if count == 0 {
            return AttributeStatistics::empty(name);
        }

        let mut min = i64::MAX;
        let mut max = i64::MIN;
        let mut null_count = 0u64;
        let mut distinct = HashSet::new();

        let mut is_strict_inc = true;
        let mut is_weak_inc = true;
        let mut is_strict_dec = true;
        let mut is_weak_dec = true;
        let mut is_constant = true;

        let mut prev = 0i64;
        let mut first = true;

        for i in 0..count {
            let chunk: [u8; 8] = data[i * 8..i * 8 + 8].try_into().unwrap();
            let val = i64::from_le_bytes(chunk);

            if val == i64::MIN {
                null_count += 1;
                continue;
            }

            if val < min {
                min = val;
            }
            if val > max {
                max = val;
            }
            if distinct.len() < 10_000 {
                distinct.insert(val);
            }

            if first {
                prev = val;
                first = false;
            } else {
                if val != prev {
                    is_constant = false;
                }
                if val <= prev {
                    is_strict_inc = false;
                }
                if val < prev {
                    is_weak_inc = false;
                }
                if val >= prev {
                    is_strict_dec = false;
                }
                if val > prev {
                    is_weak_dec = false;
                }
                prev = val;
            }
        }

        let monotonicity = if is_constant {
            Monotonicity::Constant
        } else if is_strict_inc {
            Monotonicity::StrictlyIncreasing
        } else if is_weak_inc {
            Monotonicity::WeaklyIncreasing
        } else if is_strict_dec {
            Monotonicity::StrictlyDecreasing
        } else if is_weak_dec {
            Monotonicity::WeaklyDecreasing
        } else {
            Monotonicity::None
        };

        AttributeStatistics {
            name: name.to_string(),
            min_int_val: if min == i64::MAX { 0 } else { min },
            max_int_val: if max == i64::MIN { 0 } else { max },
            min_float_val: if min == i64::MAX { 0.0 } else { min as f64 },
            max_float_val: if max == i64::MIN { 0.0 } else { max as f64 },
            min_str_val: String::new(),
            max_str_val: String::new(),
            null_count,
            distinct_count: distinct.len(),
            monotonicity,
            has_nulls: null_count > 0,
        }
    }

    pub fn calculate_float32(name: &str, data: &[u8]) -> AttributeStatistics {
        let count = data.len() / 4;
        if count == 0 {
            return AttributeStatistics::empty(name);
        }

        let mut min = f64::MAX;
        let mut max = -f64::MAX;
        let mut null_count = 0u64;
        let mut distinct = HashSet::new();

        let mut is_strict_inc = true;
        let mut is_weak_inc = true;
        let mut is_strict_dec = true;
        let mut is_weak_dec = true;
        let mut is_constant = true;

        let mut prev = 0.0f32;
        let mut first = true;

        for i in 0..count {
            let chunk: [u8; 4] = data[i * 4..i * 4 + 4].try_into().unwrap();
            let val = f32::from_le_bytes(chunk);

            if val.is_nan() {
                null_count += 1;
                continue;
            }

            let val_f64 = val as f64;
            if val_f64 < min {
                min = val_f64;
            }
            if val_f64 > max {
                max = val_f64;
            }
            if distinct.len() < 10_000 {
                distinct.insert(val.to_bits());
            }

            if first {
                prev = val;
                first = false;
            } else {
                if val != prev {
                    is_constant = false;
                }
                if val <= prev {
                    is_strict_inc = false;
                }
                if val < prev {
                    is_weak_inc = false;
                }
                if val >= prev {
                    is_strict_dec = false;
                }
                if val > prev {
                    is_weak_dec = false;
                }
                prev = val;
            }
        }

        let monotonicity = if is_constant {
            Monotonicity::Constant
        } else if is_strict_inc {
            Monotonicity::StrictlyIncreasing
        } else if is_weak_inc {
            Monotonicity::WeaklyIncreasing
        } else if is_strict_dec {
            Monotonicity::StrictlyDecreasing
        } else if is_weak_dec {
            Monotonicity::WeaklyDecreasing
        } else {
            Monotonicity::None
        };

        AttributeStatistics {
            name: name.to_string(),
            min_int_val: if min == f64::MAX { 0 } else { min as i64 },
            max_int_val: if max == -f64::MAX { 0 } else { max as i64 },
            min_float_val: if min == f64::MAX { 0.0 } else { min },
            max_float_val: if max == -f64::MAX { 0.0 } else { max },
            min_str_val: String::new(),
            max_str_val: String::new(),
            null_count,
            distinct_count: distinct.len(),
            monotonicity,
            has_nulls: null_count > 0,
        }
    }

    pub fn calculate_float64(name: &str, data: &[u8]) -> AttributeStatistics {
        let count = data.len() / 8;
        if count == 0 {
            return AttributeStatistics::empty(name);
        }

        let mut min = f64::MAX;
        let mut max = -f64::MAX;
        let mut null_count = 0u64;
        let mut distinct = HashSet::new();

        let mut is_strict_inc = true;
        let mut is_weak_inc = true;
        let mut is_strict_dec = true;
        let mut is_weak_dec = true;
        let mut is_constant = true;

        let mut prev = 0.0f64;
        let mut first = true;

        for i in 0..count {
            let chunk: [u8; 8] = data[i * 8..i * 8 + 8].try_into().unwrap();
            let val = f64::from_le_bytes(chunk);

            if val.is_nan() {
                null_count += 1;
                continue;
            }

            if val < min {
                min = val;
            }
            if val > max {
                max = val;
            }
            if distinct.len() < 10_000 {
                distinct.insert(val.to_bits());
            }

            if first {
                prev = val;
                first = false;
            } else {
                if val != prev {
                    is_constant = false;
                }
                if val <= prev {
                    is_strict_inc = false;
                }
                if val < prev {
                    is_weak_inc = false;
                }
                if val >= prev {
                    is_strict_dec = false;
                }
                if val > prev {
                    is_weak_dec = false;
                }
                prev = val;
            }
        }

        let monotonicity = if is_constant {
            Monotonicity::Constant
        } else if is_strict_inc {
            Monotonicity::StrictlyIncreasing
        } else if is_weak_inc {
            Monotonicity::WeaklyIncreasing
        } else if is_strict_dec {
            Monotonicity::StrictlyDecreasing
        } else if is_weak_dec {
            Monotonicity::WeaklyDecreasing
        } else {
            Monotonicity::None
        };

        AttributeStatistics {
            name: name.to_string(),
            min_int_val: if min == f64::MAX { 0 } else { min as i64 },
            max_int_val: if max == -f64::MAX { 0 } else { max as i64 },
            min_float_val: if min == f64::MAX { 0.0 } else { min },
            max_float_val: if max == -f64::MAX { 0.0 } else { max },
            min_str_val: String::new(),
            max_str_val: String::new(),
            null_count,
            distinct_count: distinct.len(),
            monotonicity,
            has_nulls: null_count > 0,
        }
    }
}

/// HyperLogLog sketch matching Java `HyperLogLogSketch`
pub struct HyperLogLogSketch {
    p: u32,
    m: usize,
    registers: Vec<u8>,
}

impl HyperLogLogSketch {
    pub fn new(p: u32) -> Self {
        assert!((4..=16).contains(&p), "p must be between 4 and 16");
        let m = 1 << p;
        Self {
            p,
            m,
            registers: vec![0; m],
        }
    }

    pub fn offer(&mut self, hash: u64) {
        let idx = (hash >> (64 - self.p)) as usize;
        let rank = ((hash << self.p) | (1u64 << (self.p - 1))).leading_zeros() + 1;
        if (rank as u8) > self.registers[idx] {
            self.registers[idx] = rank as u8;
        }
    }

    pub fn offer_string(&mut self, val: &str) {
        self.offer(Self::murmur3_64(val.as_bytes()));
    }

    pub fn offer_long(&mut self, mut val: u64) {
        val ^= val >> 33;
        val = val.wrapping_mul(0xff51afd7ed558ccd);
        val ^= val >> 33;
        val = val.wrapping_mul(0xc4ceb9fe1a85ec53);
        val ^= val >> 33;
        self.offer(val);
    }

    pub fn estimate(&self) -> u64 {
        let mut sum = 0.0f64;
        let mut zero_count = 0usize;
        for &r in &self.registers {
            sum += 1.0 / (1u64 << r) as f64;
            if r == 0 {
                zero_count += 1;
            }
        }

        let alpha = match self.p {
            4 => 0.673,
            5 => 0.697,
            6 => 0.709,
            _ => 0.7213 / (1.0 + 1.079 / self.m as f64),
        };

        let mut estimate = alpha * (self.m as f64) * (self.m as f64) / sum;

        if estimate <= 2.5 * (self.m as f64) && zero_count != 0 {
            estimate = (self.m as f64) * ((self.m as f64) / (zero_count as f64)).ln();
        }

        estimate as u64
    }

    pub fn murmur3_64(data: &[u8]) -> u64 {
        let mut h: u64 = 0x123456789ABCDEF;
        let c1: u64 = 0x87c37b91114253d5;
        let c2: u64 = 0x4cf5ad432745937f;

        let len = data.len();
        let num_blocks = len / 8;

        for i in 0..num_blocks {
            let k = u64::from_le_bytes(data[i * 8..i * 8 + 8].try_into().unwrap());
            let mut k = k.wrapping_mul(c1);
            k = k.rotate_left(31);
            k = k.wrapping_mul(c2);
            h ^= k;
            h = h.rotate_left(27);
            h = h.wrapping_mul(5).wrapping_add(0x52dce729);
        }

        let mut k1 = 0u64;
        let rem_start = num_blocks * 8;
        for (i, &b) in data[rem_start..].iter().enumerate() {
            k1 |= (b as u64) << (i * 8);
        }
        if rem_start < len {
            k1 = k1.wrapping_mul(c1);
            k1 = k1.rotate_left(31);
            k1 = k1.wrapping_mul(c2);
            h ^= k1;
        }

        h ^= len as u64;
        h ^= h >> 33;
        h = h.wrapping_mul(0xff51afd7ed558ccd);
        h ^= h >> 33;
        h = h.wrapping_mul(0xc4ceb9fe1a85ec53);
        h ^= h >> 33;
        h
    }
}

/// Equi-depth histogram builder matching Java `EquiDepthHistogramBuilder`
pub struct EquiDepthHistogramBuilder {
    reservoir: Vec<f64>,
    sample_size: usize,
    count: usize,
    min: f64,
    max: f64,
    null_count: u64,
    rng_state: u64,
}

impl EquiDepthHistogramBuilder {
    pub fn new(sample_size: usize) -> Self {
        Self {
            reservoir: Vec::with_capacity(sample_size),
            sample_size,
            count: 0,
            min: f64::MAX,
            max: -f64::MAX,
            null_count: 0,
            rng_state: 0x853C49E6748FEA9B,
        }
    }

    fn next_rand(&mut self) -> usize {
        self.rng_state ^= self.rng_state << 13;
        self.rng_state ^= self.rng_state >> 7;
        self.rng_state ^= self.rng_state << 17;
        self.rng_state as usize
    }

    pub fn offer_null(&mut self) {
        self.null_count += 1;
    }

    pub fn offer(&mut self, value: f64) {
        if value < self.min {
            self.min = value;
        }
        if value > self.max {
            self.max = value;
        }

        if self.reservoir.len() < self.sample_size {
            self.reservoir.push(value);
        } else {
            let j = self.next_rand() % (self.count + 1);
            if j < self.sample_size {
                self.reservoir[j] = value;
            }
        }
        self.count += 1;
    }

    pub fn build_buckets(&self, num_buckets: usize) -> Vec<f64> {
        if self.reservoir.is_empty() {
            return Vec::new();
        }
        let mut sorted = self.reservoir.clone();
        sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

        let n = sorted.len();
        let actual_buckets = num_buckets.min(n);
        let mut buckets = Vec::with_capacity(actual_buckets);

        for i in 0..actual_buckets {
            let p = (i + 1) as f64 / actual_buckets as f64;
            let mut idx = (p * n as f64).ceil() as usize;
            idx = idx.saturating_sub(1);
            if idx >= n {
                idx = n - 1;
            }
            buckets.push(sorted[idx]);
        }

        buckets
    }

    pub fn to_json(&self, num_buckets: usize, hll_distinct: u64) -> String {
        let buckets = self.build_buckets(num_buckets);
        let buckets_json: Vec<String> = buckets.iter().map(|v| format!("{:.6}", v)).collect();
        format!(
            "{{\"approx_distinct\":{},\"min\":{:.6},\"max\":{:.6},\"null_count\":{},\"histogram\":[{}]}}",
            hll_distinct,
            if self.min == f64::MAX { 0.0 } else { self.min },
            if self.max == -f64::MAX { 0.0 } else { self.max },
            self.null_count,
            buckets_json.join(",")
        )
    }
}

/// Space-Saving Heavy Hitters sketch matching Java `HeavyHittersSketch`
pub struct HeavyHittersSketch {
    k: usize,
    counters: HashMap<String, u64>,
}

impl HeavyHittersSketch {
    pub fn new(k: usize) -> Self {
        Self {
            k,
            counters: HashMap::with_capacity(k * 2),
        }
    }

    pub fn offer(&mut self, item: &str) {
        if let Some(count) = self.counters.get_mut(item) {
            *count += 1;
        } else if self.counters.len() < self.k {
            self.counters.insert(item.to_string(), 1);
        } else {
            let min_entry = self.counters.iter().min_by_key(|(_, &c)| c);
            if let Some((min_k, &min_v)) = min_entry {
                let min_key = min_k.clone();
                self.counters.remove(&min_key);
                self.counters.insert(item.to_string(), min_v + 1);
            }
        }
    }

    pub fn to_json(&self, hll_distinct: u64, null_count: u64) -> String {
        let mut items: Vec<(&String, &u64)> = self.counters.iter().collect();
        items.sort_by(|a, b| b.1.cmp(a.1));

        let top_k_strs: Vec<String> = items
            .iter()
            .map(|(k, v)| format!("\"{}\":{}", k.replace('\"', "\\\""), v))
            .collect();

        format!(
            "{{\"approx_distinct\":{},\"null_count\":{},\"top_k\":{{{}}}}}",
            hll_distinct,
            null_count,
            top_k_strs.join(",")
        )
    }
}

/// Reservoir degree distribution sketch matching Java `DegreeDistributionSketch`
pub struct DegreeDistributionSketch {
    reservoir: Vec<u32>,
    sample_size: usize,
    count: usize,
    min: u32,
    max: u32,
    zero_count: u64,
    rng_state: u64,
}

impl DegreeDistributionSketch {
    pub fn new(sample_size: usize) -> Self {
        Self {
            reservoir: Vec::with_capacity(sample_size),
            sample_size,
            count: 0,
            min: u32::MAX,
            max: 0,
            zero_count: 0,
            rng_state: 0x9E3779B97F4A7C15,
        }
    }

    fn next_rand(&mut self) -> usize {
        self.rng_state ^= self.rng_state << 13;
        self.rng_state ^= self.rng_state >> 7;
        self.rng_state ^= self.rng_state << 17;
        self.rng_state as usize
    }

    pub fn offer(&mut self, degree: u32) {
        if degree == 0 {
            self.zero_count += 1;
        }
        if degree > self.max {
            self.max = degree;
        }
        if degree < self.min {
            self.min = degree;
        }

        if self.reservoir.len() < self.sample_size {
            self.reservoir.push(degree);
        } else {
            let j = self.next_rand() % (self.count + 1);
            if j < self.sample_size {
                self.reservoir[j] = degree;
            }
        }
        self.count += 1;
    }

    pub fn get_percentile(&self, p: f64) -> u32 {
        if self.reservoir.is_empty() {
            return 0;
        }
        let mut sorted = self.reservoir.clone();
        sorted.sort_unstable();
        let n = sorted.len();
        let mut idx = (p * n as f64).ceil() as usize;
        idx = idx.saturating_sub(1);
        if idx >= n {
            idx = n - 1;
        }
        sorted[idx]
    }

    pub fn to_json(&self) -> String {
        format!(
            "{{\"min\":{},\"max\":{},\"p50\":{},\"p90\":{},\"p99\":{},\"zero_count\":{}}}",
            if self.min == u32::MAX { 0 } else { self.min },
            self.max,
            self.get_percentile(0.50),
            self.get_percentile(0.90),
            self.get_percentile(0.99),
            self.zero_count
        )
    }
}
