/**
 * @file test_assert.cpp
 * @brief Unit tests for impulse_assert.h (IMPULSE_ASSERT, IMPULSE_AUDIT_ASSERT,
 *        IMPULSE_WELL_FORMED_ASSERT, IMPULSE_ALWAYS, IMPULSE_NEVER,
 *        IMPULSE_TESTCASE, IMPULSE_VVA_ONLY, and impulse_test_control).
 */

#include "impulse_assert.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif

static void test_assert_basic() {
    IMPULSE_ASSERT(1 == 1);
    IMPULSE_ASSERT(sizeof(uint64_t) == 8);
    IMPULSE_ASSERT_MSG(true, "basic assert message should pass");

    IMPULSE_AUDIT_ASSERT(2 + 2 == 4);
    IMPULSE_AUDIT_ASSERT_MSG(true, "audit assert message should pass");

    IMPULSE_VVA_ONLY(int ghost_counter = 123;)
    IMPULSE_ASSERT(ghost_counter == 123);

    if (!IMPULSE_ALWAYS(1 == 1)) {
        abort();
    }
    if (IMPULSE_NEVER(1 == 2)) {
        abort();
    }

    printf("  [PASS] test_assert_basic\n");
}

static void test_assert_pointer_invariants() {
    int val = 42;
    int* ptr = &val;
    IMPULSE_ASSERT(ptr != nullptr);
    IMPULSE_ASSERT(*ptr == 42);

    void* aligned_ptr = (void*)((uintptr_t)ptr & ~0x7ULL);
    (void)aligned_ptr;
    IMPULSE_ASSERT(aligned_ptr != nullptr);

    printf("  [PASS] test_assert_pointer_invariants\n");
}

static void test_well_formed_and_corrupt_snapshot_mode() {
    // Default mode: never_corrupt == 0 -> IMPULSE_CORRUPT_SNAPSHOT is true (1)
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);
    if (!IMPULSE_CORRUPT_SNAPSHOT) {
        fprintf(stderr, "FAIL: IMPULSE_CORRUPT_SNAPSHOT should be true when never_corrupt == 0\n");
        abort();
    }
    // In untrusted/corrupt mode, IMPULSE_WELL_FORMED_ASSERT(false) must NOT trap
    IMPULSE_WELL_FORMED_ASSERT(1 == 2);

    // Enable strict well-formed mode: never_corrupt == 1 -> IMPULSE_CORRUPT_SNAPSHOT is false (0)
    int prev = impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 1);
    if (prev != 0 || IMPULSE_CORRUPT_SNAPSHOT) {
        fprintf(stderr, "FAIL: IMPULSE_CORRUPT_SNAPSHOT should be false when never_corrupt == 1\n");
        abort();
    }
    IMPULSE_WELL_FORMED_ASSERT(1 == 1);

    // Restore default
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);
    printf("  [PASS] test_well_formed_and_corrupt_snapshot_mode\n");
}

static void test_testcase_probes() {
    impulse_test_control(IMPULSE_TESTCTRL_TESTCASE_STATS, 0); // reset
    for (int i = 0; i < 4; ++i) {
        IMPULSE_TESTCASE(i == 0);
        IMPULSE_TESTCASE(i == 3);
    }
#if IMPULSE_ASSERTIONS_ACTIVE || defined(IMPULSE_COVERAGE_TEST) || defined(IMPULSE_ENABLE_MCDC_COVERAGE)
    uint64_t hits = 0;
    uint32_t sites = 0;
    uint32_t both = 0;
    impulse_test_control(IMPULSE_TESTCTRL_TESTCASE_STATS, 1, &hits, &sites, &both);
    if (hits != 2 || sites < 2 || both < 2) {
        fprintf(stderr, "FAIL: unexpected testcase stats hits=%llu sites=%u both=%u\n",
                (unsigned long long)hits, sites, both);
        abort();
    }
#endif
    printf("  [PASS] test_testcase_probes\n");
}

#if !defined(_WIN32)
static void expect_child_trap(void (*fn)(), const char* label) {
    pid_t pid = fork();
    if (pid == 0) {
        if (freopen("/dev/null", "w", stderr) == nullptr) {}
        fn();
        exit(0);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            printf("  [PASS] %s (caught signal %d as expected)\n", label, WTERMSIG(status));
        } else {
            fprintf(stderr, "FAIL: %s did not abort on failed assertion!\n", label);
            abort();
        }
    }
}

static void trap_assert() { IMPULSE_ASSERT(1 == 2); }
static void trap_always() { (void)IMPULSE_ALWAYS(1 == 2); }
static void trap_never()  { (void)IMPULSE_NEVER(1 == 1); }
static void trap_well_formed() {
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 1);
    IMPULSE_WELL_FORMED_ASSERT(1 == 2);
}

static void test_assert_failure_traps() {
#if IMPULSE_ASSERTIONS_ACTIVE
    expect_child_trap(trap_assert, "test_assert_failure_traps[IMPULSE_ASSERT]");
    expect_child_trap(trap_always, "test_assert_failure_traps[IMPULSE_ALWAYS]");
    expect_child_trap(trap_never,  "test_assert_failure_traps[IMPULSE_NEVER]");
    expect_child_trap(trap_well_formed, "test_assert_failure_traps[IMPULSE_WELL_FORMED_ASSERT]");
#else
    printf("  [SKIP] test_assert_failure_traps (assertions disabled in this build)\n");
#endif
}
#endif

int main() {
    printf("=== Impulse Invariant Assertion Test Suite ===\n");
    test_assert_basic();
    test_assert_pointer_invariants();
    test_well_formed_and_corrupt_snapshot_mode();
    test_testcase_probes();
#if !defined(_WIN32)
    test_assert_failure_traps();
#endif
    printf("All assertion tests PASSED!\n");
    return 0;
}
