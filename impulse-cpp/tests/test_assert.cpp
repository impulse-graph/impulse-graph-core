/**
 * @file test_assert.cpp
 * @brief Unit tests for impulse_assert.h (IMPULSE_ASSERT and IMPULSE_AUDIT_ASSERT).
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

#if !defined(_WIN32)
static void test_assert_failure_traps() {
#if defined(IMPULSE_ENABLE_ASSERTIONS)
    // Only run child death test if assertions are actively enabled
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: intentionally trigger failure, redirect stderr to /dev/null
        if (freopen("/dev/null", "w", stderr) == nullptr) {
            // Ignore failure to redirect
        }
        IMPULSE_ASSERT(1 == 2);
        exit(0); // Should never be reached
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        // Expect abnormal termination (SIGTRAP, SIGABRT, or SIGILL)
        if (WIFSIGNALED(status)) {
            printf("  [PASS] test_assert_failure_traps (caught signal %d as expected)\n", WTERMSIG(status));
        } else {
            fprintf(stderr, "FAIL: child process did not abort on failed assertion!\n");
            abort();
        }
    }
#else
    printf("  [SKIP] test_assert_failure_traps (assertions disabled in this build)\n");
#endif
}
#endif

int main() {
    printf("=== Impulse Invariant Assertion Test Suite ===\n");
    test_assert_basic();
    test_assert_pointer_invariants();
#if !defined(_WIN32)
    test_assert_failure_traps();
#endif
    printf("All assertion tests PASSED!\n");
    return 0;
}
