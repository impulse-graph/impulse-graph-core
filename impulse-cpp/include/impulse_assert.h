/**
 * @file impulse_assert.h
 * @brief Zero-overhead debug, audit, coverage, fault-injection, and test-control
 *        primitives for Impulse Graph Engine (modeled on SQLite's sqliteInt.h,
 *        main.c sqlite3_test_control, vdbe.h VdbeCoverage, and fault.c).
 *
 * Distinguishes between:
 *  - IMPULSE_ASSERT: Internal invariant assertions enabled during debug / testing builds.
 *  - IMPULSE_WELL_FORMED_ASSERT: Structural snapshot/bytecode invariants gated by
 *    IMPULSE_CORRUPT_SNAPSHOT (SQLite assert(X || CORRUPT_DB) pattern).
 *  - IMPULSE_AUDIT_ASSERT: Expensive O(N) structural invariant assertions enabled only
 *    in dedicated deep audit builds.
 *  - IMPULSE_ALWAYS / IMPULSE_NEVER: Tri-mode ductile defensive branch macros
 *    (Debug trap, Coverage constant folding, Release self-healing pass-through).
 *  - IMPULSE_TESTCASE: Boundary-value & MC/DC coverage probe macro.
 *  - IMPULSE_VVA_ONLY / IMPULSE_TESTONLY: Ghost variable wrapper for assertion-only state.
 *
 * ARCHITECTURAL RULE:
 * Never conflate internal invariant assertions with untrusted external input validation.
 * Malformed snapshot files, corrupt binary headers, and invalid user query instructions
 * MUST return impulse_status_t / impulse_vm_status_t error codes and never abort the host.
 */

#ifndef IMPULSE_ASSERT_H
#define IMPULSE_ASSERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define IMPULSE_LIKELY(x)   __builtin_expect(!!(x), 1)
  #define IMPULSE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
  #define IMPULSE_LIKELY(x)   (x)
  #define IMPULSE_UNLIKELY(x) (x)
#endif

#if defined(_MSC_VER)
  #define IMPULSE_BREAKPOINT() __debugbreak()
#elif defined(__has_builtin)
  #if __has_builtin(__builtin_trap)
    #define IMPULSE_BREAKPOINT() __builtin_trap()
  #else
    #define IMPULSE_BREAKPOINT() abort()
  #endif
#else
  #define IMPULSE_BREAKPOINT() abort()
#endif

#define IMPULSE_ASSERT_FAILED_MSG(type, cond_str, file, line, func, msg) \
    do { \
        fprintf(stderr, \
            "[%s FAILED] (%s)\n  Location: %s:%d\n  Function: %s\n  Message:  %s\n", \
            type, cond_str, file, line, func, (msg)); \
        fflush(stderr); \
        IMPULSE_BREAKPOINT(); \
    } while (0)

#define IMPULSE_ASSERT_FAILED(type, cond_str, file, line, func) \
    do { \
        fprintf(stderr, \
            "[%s FAILED] (%s)\n  Location: %s:%d\n  Function: %s\n", \
            type, cond_str, file, line, func); \
        fflush(stderr); \
        IMPULSE_BREAKPOINT(); \
    } while (0)

static inline int impulse_assert_trap_expr(const char* type, const char* cond_str,
                                           const char* file, int line, const char* func) {
    fprintf(stderr,
        "[%s FAILED] (%s)\n  Location: %s:%d\n  Function: %s\n",
        type, cond_str, file, line, func);
    fflush(stderr);
    IMPULSE_BREAKPOINT();
    return 0;
}

/* ========================================================================= */
/* Runtime Test Control & State Hooks (SQLite sqlite3_test_control pattern)  */
/* ========================================================================= */

/** impulse_test_control operation codes */
#define IMPULSE_TESTCTRL_ASSERT              1 /**< Query/verify whether IMPULSE_ASSERT is active */
#define IMPULSE_TESTCTRL_ALWAYS              2 /**< Query/verify IMPULSE_ALWAYS behavior */
#define IMPULSE_TESTCTRL_NEVER_CORRUPT       3 /**< Toggle neverCorrupt flag (0=untrusted, 1=well-formed) */
#define IMPULSE_TESTCTRL_OPTIMIZATIONS       4 /**< Set/get compiler & VM optimization disable bitmask */
#define IMPULSE_TESTCTRL_FAULT_CONFIG        5 /**< Configure countdown fault injector (int delay, int repeat) */
#define IMPULSE_TESTCTRL_FAULT_STATUS        6 /**< Query fault injector stats (int* nFail, int* nBenign, int* nTotal) */
#define IMPULSE_TESTCTRL_BENIGN_FAULT        7 /**< Enter (1) or exit (0) benign allocation failure scope */
#define IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE  8 /**< Sub-ops: 0=reset, 1=enable(int), 2=summary(uint32_t*, uint32_t*, uint32_t*) */
#define IMPULSE_TESTCTRL_TESTCASE_STATS      9 /**< Sub-ops: 0=reset, 1=stats(uint64_t* hits, uint32_t* sites, uint32_t* both) */

/** Compiler & VM optimization disable bitmask flags (IMPULSE_TESTCTRL_OPTIMIZATIONS) */
#define IMPULSE_OPT_CONSTANT_FOLDING         (1u << 0)
#define IMPULSE_OPT_KERNEL_FUSION_2HOP       (1u << 1)
#define IMPULSE_OPT_DIRECTION_SELECTION      (1u << 2)
#define IMPULSE_OPT_REGISTER_PING_PONG       (1u << 3)
#define IMPULSE_OPT_SEED_INLINING            (1u << 4)
#define IMPULSE_OPT_EARLY_EXIT               (1u << 5)
#define IMPULSE_OPT_ADAPTIVE_WALK_PULL       (1u << 6)
#define IMPULSE_OPT_SIMD_VECTOR              (1u << 7)
#define IMPULSE_OPT_ALL_DISABLED             0x00FFu

/** Bytecode branch coverage expectation modes (SQLite VdbeCoverage pattern) */
#define IMPULSE_COV_BOTH                     0 /**< Both taken (bit 0) and fallthrough (bit 1) expected */
#define IMPULSE_COV_ALWAYS_TAKEN             1 /**< Traps if fallthrough (bit 1) ever occurs */
#define IMPULSE_COV_NEVER_TAKEN              2 /**< Traps if branch taken (bit 0) ever occurs */

/** Bytecode branch coverage kind masks */
#define IMPULSE_COV_KIND_TWO_WAY             0x03u
#define IMPULSE_COV_KIND_THREE_WAY           0x07u

/** Unified C-ABI test control entrypoint */
int impulse_test_control(int op, ...);

/** Direct state accessors used by assertion, coverage, and fault-injection macros */
int impulse_test_is_never_corrupt(void);
void impulse_test_set_never_corrupt(int flag);
uint32_t impulse_test_get_opt_disable_mask(void);
void impulse_test_set_opt_disable_mask(uint32_t mask);

/** Fast-path flag for IMPULSE_TESTCASE probes */
extern int g_impulse_testcase_enabled;

/** Boundary testcase probe recording hook */
void impulse_testcase_record(const char* file, int line, int cond_val);

/** Deterministic countdown fault simulator step (returns 0 on success, non-zero on simulated fault) */
int impulse_faultsim_step(void);
void impulse_faultsim_begin_benign(void);
void impulse_faultsim_end_benign(void);

/** Bytecode-level branch coverage recorder hooks (SQLite vdbeTakeBranch equivalent) */
void impulse_vm_branch_coverage_reset(void);
void impulse_vm_branch_coverage_set_enabled(int enabled);
int impulse_vm_branch_coverage_is_enabled(void);
void impulse_vm_branch_coverage_record(uint32_t pc, uint8_t opcode, uint32_t site_id,
                                       uint8_t kind_mask, uint8_t expectation, uint8_t branch_index);
uint8_t impulse_vm_branch_coverage_get_mask(uint32_t pc, uint8_t opcode, uint32_t site_id);
void impulse_vm_branch_coverage_summary(uint32_t* out_total_sites,
                                        uint32_t* out_fully_covered_sites,
                                        uint32_t* out_missing_branches);

/**
 * SQLite CORRUPT_DB equivalent:
 * True (1) by default when untrusted/corrupt snapshots may be loaded;
 * False (0) when IMPULSE_TESTCTRL_NEVER_CORRUPT is set to 1 in well-formed test suites.
 */
#define IMPULSE_CORRUPT_SNAPSHOT (!impulse_test_is_never_corrupt())

/* ========================================================================= */
/* Tier 1: Standard Debug Invariant Assertions                               */
/* ========================================================================= */
#if defined(IMPULSE_ENABLE_ASSERTIONS) || (!defined(NDEBUG) && !defined(IMPULSE_DISABLE_ASSERTIONS))
  #define IMPULSE_ASSERTIONS_ACTIVE 1
#else
  #define IMPULSE_ASSERTIONS_ACTIVE 0
#endif

#if IMPULSE_ASSERTIONS_ACTIVE

  #define IMPULSE_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            IMPULSE_ASSERT_FAILED("IMPULSE_ASSERT", #cond, __FILE__, __LINE__, __func__); \
        } \
    } while (0)

  #define IMPULSE_ASSERT_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            IMPULSE_ASSERT_FAILED_MSG("IMPULSE_ASSERT", #cond, __FILE__, __LINE__, __func__, (msg)); \
        } \
    } while (0)

  #define IMPULSE_WELL_FORMED_ASSERT(cond) \
    do { \
        if (!((cond) || IMPULSE_CORRUPT_SNAPSHOT)) { \
            IMPULSE_ASSERT_FAILED("IMPULSE_WELL_FORMED_ASSERT", #cond, __FILE__, __LINE__, __func__); \
        } \
    } while (0)

  #define IMPULSE_VVA_ONLY(X)  X
  #define IMPULSE_TESTONLY(X)  X

#else

  #define IMPULSE_ASSERT(cond)             ((void)0)
  #define IMPULSE_ASSERT_MSG(cond, msg)    ((void)0)
  #define IMPULSE_WELL_FORMED_ASSERT(cond) ((void)0)
  #define IMPULSE_VVA_ONLY(X)
  #define IMPULSE_TESTONLY(X)

#endif

/* ========================================================================= */
/* Tier 2: Expensive Structural Audit Assertions (O(N) invariants)           */
/* ========================================================================= */
#if defined(IMPULSE_ENABLE_AUDIT_ASSERTIONS)

  #define IMPULSE_AUDIT_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            IMPULSE_ASSERT_FAILED("IMPULSE_AUDIT_ASSERT", #cond, __FILE__, __LINE__, __func__); \
        } \
    } while (0)

  #define IMPULSE_AUDIT_ASSERT_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            IMPULSE_ASSERT_FAILED_MSG("IMPULSE_AUDIT_ASSERT", #cond, __FILE__, __LINE__, __func__, (msg)); \
        } \
    } while (0)

#else

  #define IMPULSE_AUDIT_ASSERT(cond)          ((void)0)
  #define IMPULSE_AUDIT_ASSERT_MSG(cond, msg) ((void)0)

#endif

/* ========================================================================= */
/* Tier 3: Ductile Defensive Branch Macros (SQLite ALWAYS / NEVER pattern)   */
/* ========================================================================= */
#if defined(IMPULSE_COVERAGE_TEST)
  #define IMPULSE_ALWAYS(X) (1)
  #define IMPULSE_NEVER(X)  (0)
#elif IMPULSE_ASSERTIONS_ACTIVE
  #define IMPULSE_ALWAYS(X) \
    ((X) ? 1 : impulse_assert_trap_expr("IMPULSE_ALWAYS", #X, __FILE__, __LINE__, __func__))
  #define IMPULSE_NEVER(X) \
    ((X) ? (impulse_assert_trap_expr("IMPULSE_NEVER", #X, __FILE__, __LINE__, __func__), 1) : 0)
#else
  #define IMPULSE_ALWAYS(X) (X)
  #define IMPULSE_NEVER(X)  (X)
#endif

/* ========================================================================= */
/* Tier 4: Boundary-Value Coverage Probe (SQLite testcase(X) pattern)        */
/* ========================================================================= */
#if IMPULSE_ASSERTIONS_ACTIVE || defined(IMPULSE_COVERAGE_TEST) || defined(IMPULSE_ENABLE_MCDC_COVERAGE)
  #define IMPULSE_TESTCASE(X) \
    do { \
        if (IMPULSE_UNLIKELY(g_impulse_testcase_enabled)) { \
            impulse_testcase_record(__FILE__, __LINE__, (X) ? 1 : 0); \
        } \
    } while (0)
#else
  #define IMPULSE_TESTCASE(X) ((void)0)
#endif

/** Marks pure optimization fast-paths whose truth value does not alter semantics */
#define IMPULSE_OK_IF_ALWAYS_TRUE(X)  (X)
#define IMPULSE_OK_IF_ALWAYS_FALSE(X) (X)

#ifdef __cplusplus
}
#endif

#endif /* IMPULSE_ASSERT_H */
