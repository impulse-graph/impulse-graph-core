/**
 * @file impulse_assert.h
 * @brief Zero-overhead debug and audit assertion primitives for Impulse Graph Engine.
 *
 * Distinguishes between:
 *  - IMPULSE_ASSERT: Internal invariant assertions enabled during debug / testing builds.
 *  - IMPULSE_AUDIT_ASSERT: Expensive O(N) structural invariant assertions enabled only
 *    in dedicated deep audit builds.
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

#ifdef __cplusplus
extern "C" {
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

/* ========================================================================= */
/* Tier 1: Standard Debug Invariant Assertions                               */
/* ========================================================================= */
#if defined(IMPULSE_ENABLE_ASSERTIONS) || (!defined(NDEBUG) && !defined(IMPULSE_DISABLE_ASSERTIONS))

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

#else

  #define IMPULSE_ASSERT(cond)          ((void)0)
  #define IMPULSE_ASSERT_MSG(cond, msg) ((void)0)

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

#ifdef __cplusplus
}
#endif

#endif /* IMPULSE_ASSERT_H */
