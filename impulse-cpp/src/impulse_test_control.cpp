/**
 * @file impulse_test_control.cpp
 * @brief Fault-injection, testcase telemetry, and MC/DC branch coverage hooks.
 */
#include "impulse_assert.h"
#include <array>
#include <atomic>
#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include "impulse_sha256.h"

static std::atomic<int> g_vm_branch_cov_fast{0};

namespace {

std::atomic<int> g_impulse_never_corrupt{0};
std::atomic<uint32_t> g_impulse_opt_disable_mask{0};

struct FaultSimState {
    int delay{0};
    int repeat{0};
    int enable{0};
    int benign_depth{0};
    int n_fail{0};
    int n_benign_fail{0};
    int n_total{0};
};
std::mutex g_faultsim_mutex;
FaultSimState g_faultsim;
std::atomic<int> g_faultsim_fast{0};

struct TestcaseStatsState {
    static constexpr size_t TABLE_SIZE = 8192;
    std::atomic<uint64_t> total_hits{0};
    std::array<std::atomic<uint8_t>, TABLE_SIZE> site_masks{};
};
TestcaseStatsState g_testcase_stats;

struct VmBranchSiteKey {
    uint32_t pc;
    uint8_t opcode;
    uint32_t site_id;
    bool operator<(const VmBranchSiteKey& o) const {
        if (pc != o.pc) return pc < o.pc;
        if (opcode != o.opcode) return opcode < o.opcode;
        return site_id < o.site_id;
    }
};

struct VmBranchSiteEntry {
    uint8_t kind_mask{IMPULSE_COV_KIND_TWO_WAY};
    uint8_t expectation{IMPULSE_COV_BOTH};
    uint8_t observed_mask{0};
};

std::atomic<int> g_vm_branch_cov_enabled{0};
std::mutex g_vm_branch_cov_mutex;
std::map<VmBranchSiteKey, VmBranchSiteEntry> g_vm_branch_cov_table;

static inline size_t testcase_site_index(const char* file, int line) {
    uintptr_t fp = reinterpret_cast<uintptr_t>(file);
    size_t h = (static_cast<size_t>(fp >> 2) * 2654435761u) ^
               (static_cast<size_t>(line) * 40503u);
    return h & (TestcaseStatsState::TABLE_SIZE - 1);
}

} // anonymous namespace

extern "C" {

#if defined(IMPULSE_COVERAGE_TEST) || defined(IMPULSE_ENABLE_MCDC_COVERAGE)
int g_impulse_testcase_enabled = 1;
#else
int g_impulse_testcase_enabled = 0;
#endif

int impulse_test_is_never_corrupt(void) {
    return g_impulse_never_corrupt.load(std::memory_order_relaxed);
}

void impulse_test_set_never_corrupt(int flag) {
    g_impulse_never_corrupt.store(flag ? 1 : 0, std::memory_order_relaxed);
}

uint32_t impulse_test_get_opt_disable_mask(void) {
    return g_impulse_opt_disable_mask.load(std::memory_order_relaxed);
}

void impulse_test_set_opt_disable_mask(uint32_t mask) {
    g_impulse_opt_disable_mask.store(mask, std::memory_order_relaxed);
}

void impulse_testcase_record(const char* file, int line, int cond_val) {
    if (cond_val) {
        g_testcase_stats.total_hits.fetch_add(1, std::memory_order_relaxed);
    }
    uint8_t bit = cond_val ? 0x01u : 0x02u;
    size_t idx = testcase_site_index(file, line);
    uint8_t cur = g_testcase_stats.site_masks[idx].load(std::memory_order_relaxed);
    if (IMPULSE_UNLIKELY((cur & bit) == 0)) {
        g_testcase_stats.site_masks[idx].fetch_or(bit, std::memory_order_relaxed);
    }
}

static int impulse_faultsim_step_slow(void) {
    std::lock_guard<std::mutex> lock(g_faultsim_mutex);
    if (!g_faultsim.enable) {
        g_faultsim_fast.store(0, std::memory_order_relaxed);
        return 0;
    }
    g_faultsim.n_total++;
    if (g_faultsim.delay > 0) {
        g_faultsim.delay--;
        return 0;
    }
    if (g_faultsim.repeat > 0) {
        g_faultsim.repeat--;
        if (g_faultsim.repeat == 0) {
            g_faultsim.enable = 0;
            g_faultsim_fast.store(0, std::memory_order_relaxed);
        }
        g_faultsim.n_fail++;
        if (g_faultsim.benign_depth > 0) {
            g_faultsim.n_benign_fail++;
        }
        return 1;
    }
    g_faultsim.enable = 0;
    g_faultsim_fast.store(0, std::memory_order_relaxed);
    return 0;
}

int impulse_faultsim_step(void) {
    if (IMPULSE_LIKELY(g_faultsim_fast.load(std::memory_order_relaxed) == 0)) return 0;
    return impulse_faultsim_step_slow();
}

void impulse_faultsim_begin_benign(void) {
    std::lock_guard<std::mutex> lock(g_faultsim_mutex);
    g_faultsim.benign_depth++;
}

void impulse_faultsim_end_benign(void) {
    std::lock_guard<std::mutex> lock(g_faultsim_mutex);
    if (g_faultsim.benign_depth > 0) {
        g_faultsim.benign_depth--;
    }
}

void impulse_vm_branch_coverage_reset(void) {
    std::lock_guard<std::mutex> lock(g_vm_branch_cov_mutex);
    g_vm_branch_cov_table.clear();
}

void impulse_vm_branch_coverage_set_enabled(int enabled) {
    int v = enabled ? 1 : 0;
    g_vm_branch_cov_fast.store(v, std::memory_order_relaxed);
    g_vm_branch_cov_enabled.store(v, std::memory_order_relaxed);
}

int impulse_vm_branch_coverage_is_enabled(void) {
    return g_vm_branch_cov_enabled.load(std::memory_order_relaxed);
}

void impulse_vm_branch_coverage_record(uint32_t pc, uint8_t opcode, uint32_t site_id,
                                       uint8_t kind_mask, uint8_t expectation, uint8_t branch_index) {
    if (IMPULSE_LIKELY(g_vm_branch_cov_fast.load(std::memory_order_relaxed) == 0 && expectation == IMPULSE_COV_BOTH)) return;
    uint8_t bit = static_cast<uint8_t>(1u << branch_index);
    if (expectation == IMPULSE_COV_ALWAYS_TAKEN && branch_index != 0) {
        IMPULSE_ASSERT_FAILED("IMPULSE_COV_ALWAYS_TAKEN", "branch_index == 0", __FILE__, __LINE__, __func__);
    } else if (expectation == IMPULSE_COV_NEVER_TAKEN && branch_index == 0) {
        IMPULSE_ASSERT_FAILED("IMPULSE_COV_NEVER_TAKEN", "branch_index != 0", __FILE__, __LINE__, __func__);
    }
    if (!g_vm_branch_cov_enabled.load(std::memory_order_relaxed)) return;
    VmBranchSiteKey key{pc, opcode, site_id};
    std::lock_guard<std::mutex> lock(g_vm_branch_cov_mutex);
    auto it = g_vm_branch_cov_table.find(key);
    if (it == g_vm_branch_cov_table.end()) {
        g_vm_branch_cov_table.emplace(key, VmBranchSiteEntry{kind_mask, expectation, bit});
    } else {
        it->second.observed_mask |= bit;
        it->second.expectation = expectation;
    }
}

uint8_t impulse_vm_branch_coverage_get_mask(uint32_t pc, uint8_t opcode, uint32_t site_id) {
    VmBranchSiteKey key{pc, opcode, site_id};
    std::lock_guard<std::mutex> lock(g_vm_branch_cov_mutex);
    auto it = g_vm_branch_cov_table.find(key);
    return (it != g_vm_branch_cov_table.end()) ? it->second.observed_mask : 0u;
}

void impulse_vm_branch_coverage_summary(uint32_t* out_total_sites,
                                        uint32_t* out_fully_covered_sites,
                                        uint32_t* out_missing_branches) {
    uint32_t total = 0;
    uint32_t covered = 0;
    uint32_t missing = 0;
    std::lock_guard<std::mutex> lock(g_vm_branch_cov_mutex);
    for (const auto& kv : g_vm_branch_cov_table) {
        total++;
        const auto& e = kv.second;
        uint8_t req = e.kind_mask;
        if (e.expectation == IMPULSE_COV_ALWAYS_TAKEN) req = 0x01u;
        else if (e.expectation == IMPULSE_COV_NEVER_TAKEN) req = 0x02u;
        uint8_t obs = e.observed_mask & req;
        if (obs == req) {
            covered++;
        } else {
            uint8_t diff = static_cast<uint8_t>(req & ~obs);
            missing += static_cast<uint32_t>(std::popcount(diff));
        }
    }
    if (out_total_sites) *out_total_sites = total;
    if (out_fully_covered_sites) *out_fully_covered_sites = covered;
    if (out_missing_branches) *out_missing_branches = missing;
}

int impulse_test_control(int op, ...) {
    va_list ap;
    va_start(ap, op);
    int rc = 0;
    switch (op) {
        case IMPULSE_TESTCTRL_ASSERT: {
            int x = va_arg(ap, int);
            (void)x;
            IMPULSE_VVA_ONLY(rc = 1;)
            if (rc) {
                IMPULSE_ASSERT(x != 0);
            }
            break;
        }
        case IMPULSE_TESTCTRL_ALWAYS: {
            int x = va_arg(ap, int);
            rc = IMPULSE_ALWAYS(x);
            break;
        }
        case IMPULSE_TESTCTRL_NEVER_CORRUPT: {
            int flag = va_arg(ap, int);
            rc = g_impulse_never_corrupt.exchange(flag ? 1 : 0, std::memory_order_relaxed);
            break;
        }
        case IMPULSE_TESTCTRL_OPTIMIZATIONS: {
            uint32_t mask = va_arg(ap, uint32_t);
            rc = static_cast<int>(g_impulse_opt_disable_mask.exchange(mask, std::memory_order_relaxed));
            break;
        }
        case IMPULSE_TESTCTRL_FAULT_CONFIG: {
            int delay = va_arg(ap, int);
            int repeat = va_arg(ap, int);
            std::lock_guard<std::mutex> lock(g_faultsim_mutex);
            g_faultsim.delay = (delay >= 0) ? delay : 0;
            g_faultsim.repeat = (repeat >= 0) ? repeat : 0;
            g_faultsim.enable = (repeat > 0) ? 1 : 0;
            g_faultsim_fast.store(g_faultsim.enable, std::memory_order_relaxed);
            g_faultsim.n_fail = 0;
            g_faultsim.n_benign_fail = 0;
            g_faultsim.n_total = 0;
            rc = 0;
            break;
        }
        case IMPULSE_TESTCTRL_FAULT_STATUS: {
            int* out_fail = va_arg(ap, int*);
            int* out_benign = va_arg(ap, int*);
            int* out_total = va_arg(ap, int*);
            std::lock_guard<std::mutex> lock(g_faultsim_mutex);
            if (out_fail) *out_fail = g_faultsim.n_fail;
            if (out_benign) *out_benign = g_faultsim.n_benign_fail;
            if (out_total) *out_total = g_faultsim.n_total;
            rc = g_faultsim.n_fail;
            break;
        }
        case IMPULSE_TESTCTRL_BENIGN_FAULT: {
            int enter = va_arg(ap, int);
            if (enter) impulse_faultsim_begin_benign();
            else impulse_faultsim_end_benign();
            rc = 0;
            break;
        }
        case IMPULSE_TESTCTRL_VM_BRANCH_COVERAGE: {
            int subop = va_arg(ap, int);
            if (subop == 0) {
                impulse_vm_branch_coverage_reset();
            } else if (subop == 1) {
                int en = va_arg(ap, int);
                impulse_vm_branch_coverage_set_enabled(en);
            } else if (subop == 2) {
                uint32_t* p_total = va_arg(ap, uint32_t*);
                uint32_t* p_cov   = va_arg(ap, uint32_t*);
                uint32_t* p_miss  = va_arg(ap, uint32_t*);
                impulse_vm_branch_coverage_summary(p_total, p_cov, p_miss);
            }
            break;
        }
        case IMPULSE_TESTCTRL_TESTCASE_STATS: {
            int subop = va_arg(ap, int);
            if (subop == 0) {
                g_impulse_testcase_enabled = 1;
                g_testcase_stats.total_hits.store(0, std::memory_order_relaxed);
                for (auto& m : g_testcase_stats.site_masks) {
                    m.store(0, std::memory_order_relaxed);
                }
            } else if (subop == 1) {
                uint64_t* out_hits  = va_arg(ap, uint64_t*);
                uint32_t* out_sites = va_arg(ap, uint32_t*);
                uint32_t* out_both  = va_arg(ap, uint32_t*);
                if (out_hits) *out_hits = g_testcase_stats.total_hits.load(std::memory_order_relaxed);
                uint32_t sites = 0, both = 0;
                for (const auto& m : g_testcase_stats.site_masks) {
                    uint8_t v = m.load(std::memory_order_relaxed);
                    if (v != 0) {
                        sites++;
                        if ((v & 0x03u) == 0x03u) both++;
                    }
                }
                if (out_sites) *out_sites = sites;
                if (out_both) *out_both = both;
            } else if (subop == 2) {
                g_impulse_testcase_enabled = 0;
            }
            break;
        }
        default:
            rc = -1;
            break;
    }
    va_end(ap);
    return rc;
}
} // extern "C"
