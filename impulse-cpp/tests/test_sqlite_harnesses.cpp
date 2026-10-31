/**
 * @file test_sqlite_harnesses.cpp
 * @brief SQLite-Inspired Verification Harnesses for Impulse Graph Engine C++:
 *        1. Static Assertion Side-Effect Scanner (SQLite tool/srcck1.c)
 *        2. HexIO Binary Patching & IMPULSE_CORRUPT_SNAPSHOT Fuzzing (SQLite test_hexio.c + CORRUPT_DB)
 *        3. Deterministic Countdown Fault-Injection Sweep (SQLite test_malloc.c + fault.c)
 *        4. Bytecode-Level Branch Coverage Verification (SQLite VdbeCoverage / test_vdbecov.c)
 *        5. Differential Optimizer Fuzzing across IMPULSE_OPT_* Masks (SQLite test/optfuzz.c)
 */

#include "impulse_assert.h"
#include "impulse_hexio.h"
#include "impulse_graph.h"
#include "impulse_vm.h"
#include "impulse_compiler.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;
using namespace impulse::compiler;

// ============================================================================
// 1. Static Assertion Side-Effect Checker (SQLite tool/srcck1.c equivalent)
// ============================================================================

static bool expr_has_side_effect(const std::string& expr) {
    if (expr.find("/*side-effects-ok*/") != std::string::npos) {
        return false;
    }
    bool in_str = false;
    bool in_char = false;
    for (size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];
        if (in_str) {
            if (c == '\\' && i + 1 < expr.size()) { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (in_char) {
            if (c == '\\' && i + 1 < expr.size()) { ++i; continue; }
            if (c == '\'') in_char = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '\'') { in_char = true; continue; }

        // Check ++ or --
        if ((c == '+' && i + 1 < expr.size() && expr[i + 1] == '+') ||
            (c == '-' && i + 1 < expr.size() && expr[i + 1] == '-')) {
            return true;
        }

        // Check assignment operators (=, +=, -=, *=, /=, %=, |=, &=, ^=)
        // while excluding ==, !=, <=, >=
        if (c == '=') {
            char prev = (i > 0) ? expr[i - 1] : '\0';
            char next = (i + 1 < expr.size()) ? expr[i + 1] : '\0';
            if (next == '=') {
                ++i; // skip ==
                continue;
            }
            if (prev == '!' || prev == '<' || prev == '>') {
                continue; // !=, <=, >=
            }
            return true;
        }
    }
    return false;
}

static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static size_t scan_source_for_side_effects(const std::string& content,
                                           const std::string& filename,
                                           size_t& out_checked_macros) {
    static const char* kMacroNames[] = {
        "IMPULSE_ASSERT_MSG",
        "IMPULSE_ASSERT",
        "IMPULSE_AUDIT_ASSERT_MSG",
        "IMPULSE_AUDIT_ASSERT",
        "IMPULSE_WELL_FORMED_ASSERT",
        "IMPULSE_ALWAYS",
        "IMPULSE_NEVER",
        "IMPULSE_TESTCASE"
    };
    size_t violations = 0;

    // Strip comments and preprocessor #define lines first so macro definitions are not mistaken for calls
    std::string cleaned = content;
    for (size_t i = 0; i < cleaned.size();) {
        // Line start #define check
        if ((i == 0 || cleaned[i - 1] == '\n') && cleaned[i] == '#') {
            while (i < cleaned.size() && cleaned[i] != '\n') {
                if (cleaned[i] == '\\' && i + 1 < cleaned.size() && cleaned[i + 1] == '\n') {
                    cleaned[i] = ' ';
                    ++i;
                } else {
                    cleaned[i] = ' ';
                }
                ++i;
            }
            continue;
        }
        // Single-line comment
        if (i + 1 < cleaned.size() && cleaned[i] == '/' && cleaned[i + 1] == '/') {
            while (i < cleaned.size() && cleaned[i] != '\n') {
                cleaned[i++] = ' ';
            }
            continue;
        }
        // Block comment (preserve /*side-effects-ok*/)
        if (i + 1 < cleaned.size() && cleaned[i] == '/' && cleaned[i + 1] == '*') {
            if (cleaned.compare(i, 19, "/*side-effects-ok*/") == 0) {
                i += 19;
                continue;
            }
            cleaned[i++] = ' ';
            cleaned[i++] = ' ';
            while (i + 1 < cleaned.size() && !(cleaned[i] == '*' && cleaned[i + 1] == '/')) {
                if (cleaned[i] != '\n') cleaned[i] = ' ';
                ++i;
            }
            if (i + 1 < cleaned.size()) {
                cleaned[i++] = ' ';
                cleaned[i++] = ' ';
            }
            continue;
        }
        ++i;
    }

    for (const char* macro : kMacroNames) {
        size_t mlen = std::strlen(macro);
        size_t pos = 0;
        while ((pos = cleaned.find(macro, pos)) != std::string::npos) {
            bool prev_ident = (pos > 0 && is_ident_char(cleaned[pos - 1]));
            bool next_ident = (pos + mlen < cleaned.size() && is_ident_char(cleaned[pos + mlen]));
            if (prev_ident || next_ident) {
                pos += mlen;
                continue;
            }
            size_t p = pos + mlen;
            while (p < cleaned.size() && (cleaned[p] == ' ' || cleaned[p] == '\t' || cleaned[p] == '\n')) ++p;
            if (p >= cleaned.size() || cleaned[p] != '(') {
                pos += mlen;
                continue;
            }
            // Extract first argument (or full parens) up to matching ')' or top-level ','
            size_t start_arg = p + 1;
            int depth = 1;
            size_t end_arg = start_arg;
            bool in_str = false;
            for (; end_arg < cleaned.size() && depth > 0; ++end_arg) {
                char c = cleaned[end_arg];
                if (in_str) {
                    if (c == '\\' && end_arg + 1 < cleaned.size()) { ++end_arg; continue; }
                    if (c == '"') in_str = false;
                    continue;
                }
                if (c == '"') { in_str = true; continue; }
                if (c == '(') depth++;
                else if (c == ')') {
                    depth--;
                    if (depth == 0) break;
                } else if (c == ',' && depth == 1 &&
                           (std::strcmp(macro, "IMPULSE_ASSERT_MSG") == 0 ||
                            std::strcmp(macro, "IMPULSE_AUDIT_ASSERT_MSG") == 0)) {
                    break;
                }
            }
            std::string arg = cleaned.substr(start_arg, end_arg - start_arg);
            out_checked_macros++;
            if (expr_has_side_effect(arg)) {
                fprintf(stderr, "[srcck1 VIOLATION] %s: %s(%s) contains side effect!\n",
                        filename.c_str(), macro, arg.c_str());
                violations++;
            }
            pos = end_arg;
        }
    }
    return violations;
}

static void test_srcck1_static_side_effect_scanner() {
    printf("--- [Harness 1] Static Assertion Side-Effect Checker (srcck1) ---\n");

    // 1. Self-test the detector on synthetic bad and good expressions
    size_t dummy_count = 0;
    assert(scan_source_for_side_effects("IMPULSE_ASSERT(x++ > 0);", "synthetic_bad1", dummy_count) == 1);
    assert(scan_source_for_side_effects("IMPULSE_ALWAYS(a = b);", "synthetic_bad2", dummy_count) == 1);
    assert(scan_source_for_side_effects("IMPULSE_TESTCASE(count += 1);", "synthetic_bad3", dummy_count) == 1);
    assert(scan_source_for_side_effects("IMPULSE_ASSERT(x == 1 && y != 2 && z <= 3 && w >= 4);", "synthetic_ok1", dummy_count) == 0);
    assert(scan_source_for_side_effects("IMPULSE_ASSERT(/*side-effects-ok*/ x++ > 0);", "synthetic_ok2", dummy_count) == 0);

    // 2. Locate impulse-cpp root directory and scan all include/ and src/ files
    fs::path root = fs::path(__FILE__).parent_path().parent_path();
    if (!fs::exists(root / "include" / "impulse_assert.h")) {
        root = fs::current_path();
        while (!root.empty() && !fs::exists(root / "include" / "impulse_assert.h")) {
            if (fs::exists(root / "impulse-cpp" / "include" / "impulse_assert.h")) {
                root = root / "impulse-cpp";
                break;
            }
            if (fs::exists(root / "impulse-graph-core" / "impulse-cpp" / "include" / "impulse_assert.h")) {
                root = root / "impulse-graph-core" / "impulse-cpp";
                break;
            }
            fs::path parent = root.parent_path();
            if (parent == root) break;
            root = parent;
        }
    }
    assert(fs::exists(root / "include" / "impulse_assert.h"));

    size_t files_scanned = 0;
    size_t macros_checked = 0;
    size_t total_violations = 0;

    for (const char* subdir : {"include", "src"}) {
        fs::path dir = root / subdir;
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".c") continue;

            std::ifstream ifs(entry.path());
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            files_scanned++;
            total_violations += scan_source_for_side_effects(
                buffer.str(), entry.path().filename().string(), macros_checked);
        }
    }

    printf("  Scanned %zu source/header files, verified %zu assertion/coverage macro invocations: %zu violations.\n",
           files_scanned, macros_checked, total_violations);
    assert(files_scanned > 0);
    assert(macros_checked > 0);
    assert(total_violations == 0);
    printf("  [PASS] test_srcck1_static_side_effect_scanner\n\n");
}

// ============================================================================
// 2. HexIO & Binary Snapshot Corruption Fuzz Harness (test_hexio.c + CORRUPT_DB)
// ============================================================================

static void build_valid_snapshot_file(const char* path) {
    impulse_writer_t* w = impulse_writer_create(path, 0);
    assert(w != nullptr);
    assert(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "User") == IMPULSE_OK);

    uint32_t offsets[5] = {0, 2, 3, 4, 4};
    uint32_t targets[4] = {1, 2, 3, 0};
    assert(impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, 4, 4, 0,
                                       offsets, sizeof(offsets),
                                       targets, sizeof(targets)) == IMPULSE_OK);
    assert(impulse_writer_finalize(w) == IMPULSE_OK);
    impulse_writer_destroy(w);
}

static void copy_file_bytes(const char* src_path, const char* dst_path) {
    std::ifstream src(src_path, std::ios::binary);
    std::ofstream dst(dst_path, std::ios::binary | std::ios::trunc);
    dst << src.rdbuf();
}

static void test_hexio_and_corrupt_snapshot_fuzzing() {
    printf("--- [Harness 2] HexIO Binary Patching & Corrupt Snapshot Fuzzing ---\n");

    // 1. Unit test in-memory hexio primitives
    uint8_t buf[16] = {0};
    assert(impulse_hexio_write_buf(buf, sizeof(buf), 0, "49 4D 50 53 09 00") == 6);
    char hex_out[32] = {0};
    assert(impulse_hexio_read_buf(buf, sizeof(buf), 0, 6, hex_out, sizeof(hex_out)) == 6);
    assert(std::strcmp(hex_out, "494D50530900") == 0);

    char u16_hex[5], u32_hex[9], u64_hex[17];
    impulse_hexio_render_u16_le(0x0009u, u16_hex);
    assert(std::strcmp(u16_hex, "0900") == 0);
    impulse_hexio_render_u32_le(0x494D5053u, u32_hex);
    assert(std::strcmp(u32_hex, "53504D49") == 0);
    impulse_hexio_render_u64_le(0x0102030405060708ULL, u64_hex);
    assert(std::strcmp(u64_hex, "0807060504030201") == 0);

    // 2. Build a clean .imps snapshot via official impulse_writer_* API
    const char* clean_path = "test_hexio_clean.imps";
    const char* mut_path   = "test_hexio_mut.imps";
    build_valid_snapshot_file(clean_path);

    // Verify header magic in clean snapshot via impulse_hexio_read_file
    assert(impulse_hexio_read_file(clean_path, 0, 4, hex_out, sizeof(hex_out)) == 4);
    assert(std::strcmp(hex_out, "53504D49") == 0); // Little-endian 0x494D5053 ('S','P','M','I')

    // 3. Open clean snapshot with strict NEVER_CORRUPT = 1 enabled
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 1);
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(clean_path, &st);
    assert(snap != nullptr && st == IMPULSE_OK);
    impulse_snapshot_close(snap);

    // 4. Switch to NEVER_CORRUPT = 0 (untrusted/fuzzing mode: IMPULSE_CORRUPT_SNAPSHOT == 1)
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);

    // Case A: Corrupt Magic at offset 0
    copy_file_bytes(clean_path, mut_path);
    assert(impulse_hexio_write_file(mut_path, 0, "DEADBEEF") == 4);
    st = IMPULSE_OK;
    snap = impulse_snapshot_open(mut_path, &st);
    assert(snap == nullptr && st == IMPULSE_ERR_INVALID_MAGIC);

    // Case B: Corrupt Version at offset 4 (0x9999 -> major=0x99, minor=0x99)
    copy_file_bytes(clean_path, mut_path);
    assert(impulse_hexio_write_file(mut_path, 4, "9999") == 2);
    st = IMPULSE_OK;
    snap = impulse_snapshot_open(mut_path, &st);
    assert(snap == nullptr && st == IMPULSE_ERR_UNSUPPORTED_VERSION);

    // Case C: Corrupt required_features bitmask at offset 16
    copy_file_bytes(clean_path, mut_path);
    assert(impulse_hexio_write_file(mut_path, 16, "FFFFFFFFFFFFFFFF") == 8);
    st = IMPULSE_OK;
    snap = impulse_snapshot_open(mut_path, &st);
    assert(snap == nullptr && st == IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE);

    // Case D: Corrupt Header CRC-16 checksum at offset 0x3E (62)
    copy_file_bytes(clean_path, mut_path);
    char orig_crc_hex[5] = {0};
    assert(impulse_hexio_read_file(mut_path, 0x3E, 2, orig_crc_hex, sizeof(orig_crc_hex)) == 2);
    const char* bad_crc_hex = (std::strcmp(orig_crc_hex, "FFFF") == 0) ? "0000" : "FFFF";
    assert(impulse_hexio_write_file(mut_path, 0x3E, bad_crc_hex) == 2);
    st = IMPULSE_OK;
    snap = impulse_snapshot_open(mut_path, &st);
    assert(snap == nullptr && st == IMPULSE_ERR_CORRUPT_CHECKSUM);

    // Case E: Corrupt String Pool leading null byte at offset 4096 + 4 = 4100
    copy_file_bytes(clean_path, mut_path);
    assert(impulse_hexio_write_file(mut_path, 4100, "41") == 1); // 'A' instead of '\0'
    st = IMPULSE_OK;
    snap = impulse_snapshot_open(mut_path, &st);
    assert(snap == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT);

#if !defined(_WIN32) && IMPULSE_ASSERTIONS_ACTIVE
    // 5. Verify that with NEVER_CORRUPT = 1, opening a corrupt snapshot triggers IMPULSE_WELL_FORMED_ASSERT
    pid_t pid = fork();
    if (pid == 0) {
        if (freopen("/dev/null", "w", stderr) == nullptr) {}
        impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 1);
        impulse_status_t child_st = IMPULSE_OK;
        (void)impulse_snapshot_open(mut_path, &child_st);
        exit(0);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        assert(WIFSIGNALED(status));
    }
#endif

    std::remove(clean_path);
    std::remove(mut_path);
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);
    printf("  [PASS] test_hexio_and_corrupt_snapshot_fuzzing\n\n");
}

// ============================================================================
// 3. Deterministic Countdown Fault-Injection Sweep (test_malloc.c / fault.c)
// ============================================================================

static impulse_vm_status_t run_multi_alloc_vm_program(const impulse_snapshot_t* snap, uint64_t* out_card) {
    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    if (!ctx) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    // Program that allocates bitsets, creates a (benign) scratch index, walks CSR, and computes cardinality
    impulse_instruction_t prog[] = {
        {OP_INIT_INPUT_NODE, 0, 0, 0},
        {OP_CREATE_SCRATCH_INDEX, 0, 0, 0},
        {OP_CSR_WALK, 0, 1, (0u << 16) | 0u},
        {OP_SET_UNION, 0, 2, (1u << 16) | 0u},
        {OP_LOAD_CONST_FLOAT, 0, 3, 0x3F800000u},
        {OP_VECTOR_MUL_ATTR, 0, 3, 0x40000000u},
        {OP_SET_CARDINALITY, 0, 4, 2u},
        {OP_HALT, 0, 4, 0}
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t rc = impulse_vm_execute(prog, sizeof(prog) / sizeof(prog[0]), &state, 0);
    if (rc == IMPULSE_VM_OK && out_card) {
        *out_card = state.registers[4];
    }
    impulse_vm_context_destroy(ctx);
    return rc;
}

static void test_deterministic_fault_injection_sweep() {
    printf("--- [Harness 3] Deterministic Countdown Fault-Injection Sweep (faultsim) ---\n");

    const char* snap_path = "test_faultsim.imps";
    build_valid_snapshot_file(snap_path);

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &st);
    assert(snap != nullptr && st == IMPULSE_OK);

    int hard_faults_observed = 0;
    int benign_faults_observed = 0;
    int delay = 0;
    uint64_t final_card = 0;

    for (; delay < 64; ++delay) {
        impulse_test_control(IMPULSE_TESTCTRL_FAULT_CONFIG, delay, 1);
        uint64_t card = 0;
        impulse_vm_status_t rc = run_multi_alloc_vm_program(snap, &card);

        int n_fail = 0, n_benign = 0, n_total = 0;
        impulse_test_control(IMPULSE_TESTCTRL_FAULT_STATUS, &n_fail, &n_benign, &n_total);

        if (n_fail == 0) {
            // Reached past the last allocation in the chain!
            assert(rc == IMPULSE_VM_OK);
            final_card = card;
            break;
        }
        if (n_benign > 0) {
            // Benign fault inside OP_CREATE_SCRATCH_INDEX must NOT fail the query!
            benign_faults_observed += n_benign;
            assert(rc == IMPULSE_VM_OK);
            assert(card == 3); // Node 0 union {1, 2} = {0, 1, 2} -> cardinality 3
        } else {
            hard_faults_observed += n_fail;
            assert(rc == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        }
    }

    // Disable fault injector and verify clean run
    impulse_test_control(IMPULSE_TESTCTRL_FAULT_CONFIG, 0, 0);
    assert(hard_faults_observed >= 3);
    assert(benign_faults_observed == 1);
    assert(final_card == 3);

    // Also test fault injection on impulse_snapshot_open
    impulse_test_control(IMPULSE_TESTCTRL_FAULT_CONFIG, 0, 1);
    impulse_status_t open_st = IMPULSE_OK;
    impulse_snapshot_t* failed_snap = impulse_snapshot_open(snap_path, &open_st);
    assert(failed_snap == nullptr && open_st == IMPULSE_ERR_IO_FAILURE);
    impulse_test_control(IMPULSE_TESTCTRL_FAULT_CONFIG, 0, 0);

    impulse_snapshot_close(snap);
    std::remove(snap_path);
    printf("  Sweep completed in %d steps (hard_faults=%d, benign_faults=%d, final_card=%llu).\n",
           delay, hard_faults_observed, benign_faults_observed, (unsigned long long)final_card);
    printf("  [PASS] test_deterministic_fault_injection_sweep\n\n");
}

// ============================================================================
// 4. Bytecode-Level Branch Coverage Harness (VdbeCoverage / test_vdbecov.c)
// ============================================================================

static void test_vm_bytecode_branch_coverage() {
    printf("--- [Harness 4] Bytecode-Level Branch Coverage (VdbeCoverage) ---\n");

    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 0);    // reset
    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 1, 1); // enable

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // Program with:
    // PC 0: OP_LOAD_CONST_INT R0 = 2
    // PC 1: OP_LOOP_DECR R0, offset=0 (loops until R0 == 0: exercises both taken and fallthrough!)
    // PC 2: OP_JZ offset=+2 (ZF is 1 after loop finishes -> taken!)
    // PC 3: OP_NOP
    // PC 4: OP_LOAD_CONST_INT R1 = 1
    // PC 5: OP_SET_CARDINALITY R2, R1 (sets ZF = 0)
    // PC 6: OP_JZ offset=+2 (ZF is 0 -> fallthrough!)
    // PC 7: OP_JNZ offset=+2 (ZF is 0 -> taken!)
    // PC 8: OP_NOP
    // PC 9: OP_HALT
    impulse_instruction_t prog[] = {
        {OP_LOAD_CONST_INT, 0, 0, 2u},
        {OP_LOOP_DECR,      0, 0, 0u},
        {OP_JZ,              0, 0, 2u},
        {OP_NOP,            0, 0, 0u},
        {OP_LOAD_CONST_INT, 0, 1, 1u},
        {OP_SET_CARDINALITY,0, 2, 1u},
        {OP_JZ,              0, 0, 2u},
        {OP_JNZ,             0, 0, 2u},
        {OP_NOP,            0, 0, 0u},
        {OP_HALT,           0, 0, 0u}
    };

    impulse_vm_state_t state{};
    state.query_context = ctx;
    assert(impulse_vm_execute(prog, sizeof(prog) / sizeof(prog[0]), &state, 0) == IMPULSE_VM_OK);

    // PC 1 (OP_LOOP_DECR) should have both bit 0 (taken) and bit 1 (fallthrough) -> 0x03
    uint8_t loop_mask = impulse_vm_branch_coverage_get_mask(1, OP_LOOP_DECR, 1u);
    assert(loop_mask == 0x03u);

    // PC 2 (OP_JZ) only took branch (0x01), PC 6 (OP_JZ) only fell through (0x02) -> 2 missing branches
    uint32_t total_sites = 0, covered_sites = 0, missing_branches = 0;
    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 2,
                         &total_sites, &covered_sites, &missing_branches);
    assert(total_sites == 4);
    assert(covered_sites == 1);
    assert(missing_branches == 3);

    // Now test OP_STABLE_CHECK both branches
    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 0); // reset
    impulse_instruction_t stable_prog[] = {
        {OP_LOAD_CONST_INT, 0, 1, 10u},
        {OP_LOAD_CONST_INT, 0, 2, 20u},
        {OP_STABLE_CHECK,   0, 1, 2u}, // 10 != 20 -> not stable (branch_index = 1 -> 0x02)
        {OP_MOV,            0, 1, 2u},
        {OP_STABLE_CHECK,   0, 1, 2u}, // 20 == 20 -> stable (branch_index = 0 -> 0x01)
        {OP_HALT,           0, 0, 0u}
    };
    impulse_vm_state_t state2{};
    state2.query_context = ctx;
    assert(impulse_vm_execute(stable_prog, sizeof(stable_prog) / sizeof(stable_prog[0]), &state2, 0) == IMPULSE_VM_OK);
    assert(impulse_vm_branch_coverage_get_mask(2, OP_STABLE_CHECK, 1u) == 0x02u);
    assert(impulse_vm_branch_coverage_get_mask(4, OP_STABLE_CHECK, 1u) == 0x01u);

#if !defined(_WIN32)
    // Verify IMPULSE_COV_ALWAYS_TAKEN traps if branch_index != 0
    pid_t pid = fork();
    if (pid == 0) {
        if (freopen("/dev/null", "w", stderr) == nullptr) {}
        impulse_vm_branch_coverage_record(0, OP_JZ, 1u, IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_ALWAYS_TAKEN, 1u);
        exit(0);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        assert(WIFSIGNALED(status));
    }
#endif

    impulse_vm_context_destroy(ctx);
    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 1, 0); // disable
    impulse_test_control(IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE, 0);    // reset
    printf("  [PASS] test_vm_bytecode_branch_coverage\n\n");
}

// ============================================================================
// 5. Differential Optimizer Fuzz Harness (SQLite test/optfuzz.c equivalent)
// ============================================================================

static std::vector<uint64_t> run_compiled_program_and_collect(
    const CompiledImpulseProgram& compiled,
    impulse_vm_context_t* ctx,
    uint64_t seed_node,
    size_t max_nodes)
{
    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(
        compiled.data(), compiled.instruction_count(), &state, seed_node);
    assert(st == IMPULSE_VM_OK);
    (void)st;

    uint16_t r = compiled.result_register;
    std::vector<uint64_t> out;
    if (state.register_types[r] == TYPE_BITSET_HANDLE) {
        int h = static_cast<int>(state.registers[r]);
        for (size_t u = 0; u < max_nodes; ++u) {
            if (impulse_vm_context_bitset_test(ctx, h, u)) {
                out.push_back(u);
            }
        }
    } else if (state.register_types[r] == TYPE_NODE_ID || state.register_types[r] == TYPE_INT64) {
        out.push_back(state.registers[r]);
    }
    return out;
}

static void test_differential_optimizer_fuzzing() {
    printf("--- [Harness 5] Differential Optimizer Fuzzing (optfuzz) ---\n");

    const char* snap_path = "test_optfuzz.imps";
    const size_t num_nodes = 64;
    {
        impulse_writer_t* w = impulse_writer_create(snap_path, 0);
        assert(w != nullptr);
        assert(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "Node") == IMPULSE_OK);

        // Rel 0 (RelA): each node u -> (u+1)%64, (u+3)%64
        std::vector<uint32_t> offA(num_nodes + 1, 0);
        std::vector<uint32_t> tgtA;
        for (size_t u = 0; u < num_nodes; ++u) {
            offA[u] = static_cast<uint32_t>(tgtA.size());
            if (u != 13) { // make node 13 a dead-end to exercise HALT_ON_EMPTY early exit!
                tgtA.push_back(static_cast<uint32_t>((u + 1) % num_nodes));
                tgtA.push_back(static_cast<uint32_t>((u + 3) % num_nodes));
            }
        }
        offA[num_nodes] = static_cast<uint32_t>(tgtA.size());
        assert(impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, num_nodes, tgtA.size(), 0,
                                           offA.data(), offA.size() * sizeof(uint32_t),
                                           tgtA.data(), tgtA.size() * sizeof(uint32_t)) == IMPULSE_OK);

        // Rel 1 (RelB): each node u -> (u*2 + 1)%64
        std::vector<uint32_t> offB(num_nodes + 1, 0);
        std::vector<uint32_t> tgtB;
        for (size_t u = 0; u < num_nodes; ++u) {
            offB[u] = static_cast<uint32_t>(tgtB.size());
            tgtB.push_back(static_cast<uint32_t>((u * 2 + 1) % num_nodes));
        }
        offB[num_nodes] = static_cast<uint32_t>(tgtB.size());
        assert(impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, num_nodes, tgtB.size(), 0,
                                           offB.data(), offB.size() * sizeof(uint32_t),
                                           tgtB.data(), tgtB.size() * sizeof(uint32_t)) == IMPULSE_OK);

        assert(impulse_writer_finalize(w) == IMPULSE_OK);
        impulse_writer_destroy(w);
    }

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &st);
    assert(snap != nullptr && st == IMPULSE_OK);
    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    GraphCatalog catalog;
    catalog.register_relation("RelA", 0, 1.2);
    catalog.register_relation("RelB", 1, 1.0);

    // Corpus of ImpScheme programs exercising 1-hop, 2-hop (fusable), and 3-hop pipelines
    std::vector<std::shared_ptr<ScmProgram>> programs = {
        ScmProgram::of(
            ScmWalk::forward("RelA"),
            ScmCollect::bitset()
        ),
        ScmProgram::of(
            ScmWalk::forward("RelA"),
            ScmWalk::forward("RelB"),
            ScmCollect::bitset()
        ),
        ScmProgram::of(
            ScmWalk::forward("RelA"),
            ScmWalk::forward("RelB"),
            ScmWalk::forward("RelA"),
            ScmCollect::bitset()
        )
    };

    // Masks to test: all enabled (0), all disabled (IMPULSE_OPT_ALL_DISABLED), and each individual bit
    std::vector<uint32_t> masks = {0u, IMPULSE_OPT_ALL_DISABLED};
    for (uint32_t bit = 0; bit < 8; ++bit) {
        masks.push_back(1u << bit);
        masks.push_back(IMPULSE_OPT_ALL_DISABLED & ~(1u << bit));
    }

    size_t comparisons = 0;
    for (const auto& prog : programs) {
        for (uint64_t seed : {0ULL, 5ULL, 13ULL, 42ULL}) {
            // Reference result with all optimizations enabled (mask = 0)
            impulse_test_control(IMPULSE_TESTCTRL_OPTIMIZATIONS, 0u);
            auto ref_compiled = ImpulseCompiler::compile(prog, &catalog);
            auto ref_nodes = run_compiled_program_and_collect(ref_compiled, ctx, seed, num_nodes);

            for (uint32_t mask : masks) {
                impulse_test_control(IMPULSE_TESTCTRL_OPTIMIZATIONS, mask);
                auto test_compiled = ImpulseCompiler::compile(prog, &catalog);
                auto test_nodes = run_compiled_program_and_collect(test_compiled, ctx, seed, num_nodes);
                assert(test_nodes == ref_nodes);
                comparisons++;
            }
        }
    }

    impulse_test_control(IMPULSE_TESTCTRL_OPTIMIZATIONS, 0u);
    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(snap_path);

    printf("  Verified 100%% bitwise equivalence across %zu (query, seed, opt_mask) configurations.\n",
           comparisons);
    printf("  [PASS] test_differential_optimizer_fuzzing\n\n");
}

int main() {
    printf("=========================================================================\n");
    printf("       IMPULSE GRAPH C++ SQLITE-STYLE VERIFICATION HARNESS SUITE         \n");
    printf("=========================================================================\n\n");

    test_srcck1_static_side_effect_scanner();
    test_hexio_and_corrupt_snapshot_fuzzing();
    test_deterministic_fault_injection_sweep();
    test_vm_bytecode_branch_coverage();
    test_differential_optimizer_fuzzing();

    printf("=========================================================================\n");
    printf("            ALL SQLITE-STYLE C++ VERIFICATION HARNESSES PASSED!          \n");
    printf("=========================================================================\n");
    return 0;
}
