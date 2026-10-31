//! SQLite-Style Tiered Assertion & Coverage Taxonomy for Impulse Rust Kernel
//!
//! Mirrors C++ `impulse_assert.h`, Java `ImpulseAssert.java`, and OCaml `impulse_assert.ml`:
//! 1. `check(cond, msg)`: Core execution invariant (panics on failure).
//! 2. `check_well_formed(cond, msg)`: File-format well-formedness check gated by `NEVER_CORRUPT`
//!    (`SQLITE_TESTCTRL_NEVER_CORRUPT`). When `NEVER_CORRUPT` is `false` (default), returns cleanly
//!    so the caller can propagate `Err(...)` on corrupted snapshots without panicking.
//! 3. `audit(f, msg)`: Expensive $O(N)$ / $O(E)$ structural & state invariant audit gated by `AUDIT_ENABLED`.
//! 4. `always(cond)` / `never(cond)`: SQLite `ALWAYS(X)` / `NEVER(X)` defensive branch wrappers.
//! 5. `testcase(id, cond)`: SQLite `testcase(X)` boundary-condition hit counter (1024-site atomic bitmap).

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

static NEVER_CORRUPT: AtomicBool = AtomicBool::new(false);
static AUDIT_ENABLED: AtomicBool = AtomicBool::new(false);
static OPT_DISABLE_MASK: AtomicU32 = AtomicU32::new(0);

const TESTCASE_WORDS: usize = 16; // 16 * 64 = 1024 testcase sites
static TESTCASE_BITMAP: [AtomicU64; TESTCASE_WORDS] = [
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
    AtomicU64::new(0),
];

/// Enables or disables `NEVER_CORRUPT` (`SQLITE_TESTCTRL_NEVER_CORRUPT`).
#[inline]
pub fn set_never_corrupt(enabled: bool) {
    NEVER_CORRUPT.store(enabled, Ordering::SeqCst);
}

/// Returns whether `NEVER_CORRUPT` is currently active.
#[inline]
pub fn is_never_corrupt() -> bool {
    NEVER_CORRUPT.load(Ordering::Relaxed)
}

/// Enables or disables expensive $O(N)$ structural/post-op invariant auditing.
#[inline]
pub fn set_audit_enabled(enabled: bool) {
    AUDIT_ENABLED.store(enabled, Ordering::SeqCst);
}

/// Returns whether `AUDIT_ENABLED` is currently active.
#[inline]
pub fn is_audit_enabled() -> bool {
    AUDIT_ENABLED.load(Ordering::Relaxed)
}

/// Sets the optimization-disable bitmask (`SQLITE_TESTCTRL_OPTIMIZATIONS`).
#[inline]
pub fn set_opt_disable_mask(mask: u32) {
    OPT_DISABLE_MASK.store(mask, Ordering::SeqCst);
}

/// Returns the current optimization-disable bitmask.
#[inline]
pub fn opt_disable_mask() -> u32 {
    OPT_DISABLE_MASK.load(Ordering::Relaxed)
}

/// Returns `true` if optimization flag `opt_bit` is enabled (not masked out).
#[inline]
pub fn optimization_enabled(opt_bit: u32) -> bool {
    (opt_disable_mask() & opt_bit) == 0
}

/// Tier 1: Core execution invariant (`IMPULSE_ASSERT`).
#[inline]
pub fn check(cond: bool, msg: &str) {
    assert!(cond, "IMPULSE_ASSERT failed: {}", msg);
}

/// Tier 2: Snapshot well-formedness invariant (`IMPULSE_ASSERT_WELL_FORMED`).
/// Asserts only when `NEVER_CORRUPT` is `true`; otherwise allows graceful error propagation.
#[inline]
pub fn check_well_formed(cond: bool, msg: &str) {
    if !cond && is_never_corrupt() {
        panic!("IMPULSE_ASSERT_WELL_FORMED failed (NEVER_CORRUPT=1): {}", msg);
    }
}

/// Tier 3: Expensive $O(N)$ structural/state audit (`IMPULSE_AUDIT`).
#[inline]
pub fn audit<F: FnOnce() -> bool>(f: F, msg: &str) {
    if is_audit_enabled() {
        assert!(f(), "IMPULSE_AUDIT failed: {}", msg);
    }
}

/// SQLite `ALWAYS(X)`: Condition known to be true in valid execution; asserts in debug/audit mode.
#[inline]
pub fn always(cond: bool) -> bool {
    if !cond && (cfg!(debug_assertions) || is_audit_enabled()) {
        panic!("IMPULSE_ALWAYS(true) violated");
    }
    cond
}

/// SQLite `NEVER(X)`: Condition known to be false in valid execution; asserts in debug/audit mode.
#[inline]
pub fn never(cond: bool) -> bool {
    if cond && (cfg!(debug_assertions) || is_audit_enabled()) {
        panic!("IMPULSE_NEVER(false) violated");
    }
    cond
}

/// SQLite `testcase(X)`: Records that boundary condition `id` (`0..1023`) was exercised when `cond` is true.
#[inline]
pub fn testcase(id: usize, cond: bool) {
    if cond && id < TESTCASE_WORDS * 64 {
        let word = id >> 6;
        let bit = 1u64 << (id & 63);
        TESTCASE_BITMAP[word].fetch_or(bit, Ordering::Relaxed);
    }
}

/// Returns whether `testcase(id)` has been hit at least once since the last reset.
#[inline]
pub fn testcase_was_hit(id: usize) -> bool {
    if id >= TESTCASE_WORDS * 64 {
        return false;
    }
    let word = id >> 6;
    let bit = 1u64 << (id & 63);
    (TESTCASE_BITMAP[word].load(Ordering::Relaxed) & bit) != 0
}

/// Returns the total count of distinct `testcase(id)` boundary sites hit.
pub fn testcase_hit_count() -> u32 {
    TESTCASE_BITMAP
        .iter()
        .map(|w| w.load(Ordering::Relaxed).count_ones())
        .sum()
}

/// Resets all `testcase(id)` boundary hit bits.
pub fn reset_testcase_hits() {
    for w in &TESTCASE_BITMAP {
        w.store(0, Ordering::SeqCst);
    }
}
