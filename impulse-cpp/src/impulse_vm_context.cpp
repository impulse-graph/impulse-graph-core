/**
 * @file impulse_vm_context.cpp
 * @brief C-ABI lifecycle, off-heap handle pool management, mock graph binding,
 *        and fault-injection / MC/DC coverage hooks for ImpulseVM.
 */
#include "impulse_vm_internal.hpp"

int g_vm_branch_cov_fast = 0;

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
int g_faultsim_fast = 0;

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
        uint64_t cur_hits = g_testcase_stats.total_hits.load(std::memory_order_relaxed);
        g_testcase_stats.total_hits.store(cur_hits + 1, std::memory_order_relaxed);
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
        g_faultsim_fast = 0;
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
            g_faultsim_fast = 0;
        }
        g_faultsim.n_fail++;
        if (g_faultsim.benign_depth > 0) {
            g_faultsim.n_benign_fail++;
        }
        return 1;
    }
    g_faultsim.enable = 0;
    g_faultsim_fast = 0;
    return 0;
}

int impulse_faultsim_step(void) {
    if (IMPULSE_LIKELY(g_faultsim_fast == 0)) return 0;
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
    g_vm_branch_cov_fast = v;
    g_vm_branch_cov_enabled.store(v, std::memory_order_relaxed);
}

int impulse_vm_branch_coverage_is_enabled(void) {
    return g_vm_branch_cov_enabled.load(std::memory_order_relaxed);
}

void impulse_vm_branch_coverage_record(uint32_t pc, uint8_t opcode, uint32_t site_id,
                                       uint8_t kind_mask, uint8_t expectation, uint8_t branch_index) {
    if (IMPULSE_LIKELY(g_vm_branch_cov_fast == 0 && expectation == IMPULSE_COV_BOTH)) return;
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
            g_faultsim_fast = g_faultsim.enable;
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


extern "C" {

impulse_vm_context_t* impulse_vm_context_create(const impulse_snapshot_t* snapshot) {
    if (impulse_faultsim_step() != 0) {
        return nullptr;
    }
    auto* ctx = new impulse_vm_context();
    IMPULSE_ASSERT(ctx != nullptr);
    ctx->snapshot = snapshot;
    ctx->stack_pointer = 0;
    ctx->call_stack.fill(0);

    uint64_t max_nodes = 0;
    if (snapshot) {
        max_nodes = impulse_snapshot_max_node_count(snapshot);
    }
    if (max_nodes == 0) {
        max_nodes = 1024 * 1024; // Default fallback for tests
    }
    ctx->max_nodes = max_nodes;
    uint64_t bitset_capacity = std::max<uint64_t>(max_nodes, 65536ULL);
    ctx->words_per_bitset = (bitset_capacity + 63) / 64;
    IMPULSE_ASSERT(ctx->words_per_bitset > 0);

    // Pre-allocate off-heap bitsets in a contiguous memory block
    ctx->arena_memory = new uint64_t[VM_MAX_BITSET_HANDLES * ctx->words_per_bitset]();
    IMPULSE_ASSERT(ctx->arena_memory != nullptr);
    for (size_t i = 0; i < VM_MAX_BITSET_HANDLES; ++i) {
        ctx->bitsets[i].words = ctx->arena_memory + (i * ctx->words_per_bitset);
        ctx->bitsets[i].word_count = ctx->words_per_bitset;
        ctx->bitset_allocated[i] = false;
    }

    ctx->private_arena_memory = nullptr;
#if defined(_OPENMP)
    ctx->max_threads = omp_get_max_threads();
#else
    ctx->max_threads = 1;
#endif
    {
        int priv_count = std::max(1, ctx->max_threads);
        ctx->private_arena_memory = new uint64_t[static_cast<size_t>(priv_count) * ctx->words_per_bitset]();
        ctx->private_bitsets.resize(static_cast<size_t>(priv_count));
        for (int i = 0; i < priv_count; ++i) {
            ctx->private_bitsets[i].words = ctx->private_arena_memory + (static_cast<size_t>(i) * ctx->words_per_bitset);
            ctx->private_bitsets[i].word_count = ctx->words_per_bitset;
        }
    }

    // Pre-allocate float and double vector buffers
    size_t vec_cap = std::max<size_t>(ctx->max_nodes, VM_MOCK_INIT_CAP);
    for (size_t i = 0; i < VM_MAX_VECTOR_HANDLES; ++i) {
        ctx->float_vectors[i].resize(vec_cap, 0.0f);
        ctx->float_vectors_allocated[i] = false;
        ctx->double_vectors[i].resize(vec_cap, 0.0);
        ctx->double_vectors_allocated[i] = false;
        ctx->node_vectors[i].resize(vec_cap, 0);
        ctx->node_vectors_allocated[i] = false;
        ctx->string_vectors[i].reserve(vec_cap * 2);
        ctx->string_vectors_allocated[i] = false;
        ctx->value_maps[i].keys.reserve(vec_cap);
        ctx->value_maps[i].values.reserve(vec_cap);
        ctx->value_maps_allocated[i] = false;
    }

    // Pre-allocate multi-lane frontiers and hot-path scratch buffers
    size_t lane_cap = snapshot ? std::max<size_t>(ctx->max_nodes, VM_MOCK_INIT_CAP) : VM_MOCK_INIT_CAP;
    size_t lane_word_cap = (lane_cap + 63) / 64;
    ctx->lane_frontiers_count = 0;
    for (size_t i = 0; i < VM_MAX_LANE_FRONTIER_HANDLES; ++i) {
        ctx->lane_frontiers[i].lanes.reserve(lane_cap);
        ctx->lane_frontiers[i].active_words.reserve(lane_word_cap);
        ctx->lane_frontiers[i].edge_trip_active_mask = 0;
        for (size_t r = 0; r < VM_MAX_REL_SLOTS; ++r) {
            ctx->lane_frontiers[i].edge_trip_masks[r].reserve(1024);
        }
    }
    ctx->scratch_lanes.reserve(lane_cap);
    ctx->scratch_lanes_b.reserve(lane_cap);
    ctx->scratch_active_words.reserve(lane_word_cap);
    ctx->scratch_active_words_b.reserve(lane_word_cap);
    ctx->scratch_u32_a.reserve(lane_cap);
    ctx->scratch_u32_b.reserve(lane_cap);
    ctx->scratch_u64_a.reserve(lane_cap);
    ctx->scratch_f32_a.reserve(lane_cap);
    ctx->scratch_int_deg.reserve(lane_cap);
    ctx->scratch_int_vert.reserve(lane_cap);
    ctx->scratch_int_pos.reserve(lane_cap);
    ctx->scratch_int_bin.reserve(lane_cap + 1);
    ctx->node_buffer.reserve(vec_cap);
    size_t bfs_threads = static_cast<size_t>(std::max(1, ctx->max_threads));
    ctx->bfs_visited_words.resize(bfs_threads);
    ctx->bfs_queues.resize(bfs_threads);
    for (size_t t = 0; t < bfs_threads; ++t) {
        ctx->bfs_visited_words[t].reserve((lane_cap + 63) / 64);
        ctx->bfs_queues[t].reserve(lane_cap);
    }
    for (size_t i = 0; i < VM_MAX_MOCK_ATTRS; ++i) {
        ctx->mock_node_attrs[i].float_data.reserve(VM_MOCK_INIT_CAP);
        ctx->mock_node_attrs[i].int_data.reserve(VM_MOCK_INIT_CAP);
        ctx->mock_edge_attrs[i].float_data.reserve(VM_MOCK_INIT_CAP);
        ctx->mock_edge_attrs[i].int_data.reserve(VM_MOCK_INIT_CAP);
    }

    // Pre-index snapshot relations if a snapshot is present
    if (snapshot) {
        uint16_t rel_count = impulse_snapshot_relation_count(snapshot);
        ctx->slots.resize(rel_count);
        ctx->attribute_slots.resize(rel_count);
        for (uint16_t r = 0; r < rel_count; ++r) {
            // CSR buffers
            const void* offsets = nullptr;
            const void* targets = nullptr;
            uint64_t node_count = 0;
            uint64_t edge_count = 0;
            uint8_t node_id_width = 4;
            uint8_t edge_index_width = 4;
            impulse_snapshot_get_relation_raw_buffers(
                snapshot, r, &offsets, &targets, &node_count, &edge_count, &node_id_width, &edge_index_width
            );
            ctx->slots[r].offsets_ptr = offsets;
            ctx->slots[r].targets_ptr = targets;
            ctx->slots[r].node_count = node_count;
            ctx->slots[r].edge_count = edge_count;
            ctx->slots[r].node_id_width = node_id_width;
            ctx->slots[r].edge_index_width = edge_index_width;

            // CSC buffers
            const void* csc_offsets = nullptr;
            const void* csc_targets = nullptr;
            uint64_t csc_node_count = 0;
            uint8_t csc_n_w = 4;
            uint8_t csc_e_w = 4;
            impulse_snapshot_get_relation_csc_raw_buffers(
                snapshot, r, &csc_offsets, &csc_targets, &csc_node_count, nullptr, &csc_n_w, &csc_e_w
            );
            ctx->slots[r].csc_offsets_ptr = csc_offsets;
            ctx->slots[r].csc_targets_ptr = csc_targets;
            ctx->slots[r].csc_node_count = csc_node_count;
            ctx->slots[r].csc_node_id_width = csc_n_w;
            ctx->slots[r].csc_edge_index_width = csc_e_w;

            // Attributes
            impulse_relation_directory_entry_t rel_entry{};
            impulse_snapshot_get_relation_entry(snapshot, r, &rel_entry);
            uint16_t attr_count = rel_entry.attr_count;
            ctx->attribute_slots[r].resize(attr_count);
            for (uint16_t a = 0; a < attr_count; ++a) {
                const void* data = nullptr;
                uint64_t data_bytes = 0;
                const void* offsets = nullptr;
                uint64_t offsets_bytes = 0;
                uint8_t type_code = 0;
                uint32_t dimension = 0;
                impulse_snapshot_get_attribute_buffers(
                    snapshot, r, a, &data, &data_bytes, &offsets, &offsets_bytes, &type_code, &dimension
                );
                ctx->attribute_slots[r][a].data_ptr = data;
                ctx->attribute_slots[r][a].data_bytes = data_bytes;
                ctx->attribute_slots[r][a].offsets_ptr = offsets;
                ctx->attribute_slots[r][a].offsets_bytes = offsets_bytes;
                ctx->attribute_slots[r][a].type_code = type_code;
                ctx->attribute_slots[r][a].dimension = dimension;
            }
        }
    } else {
        ctx->slots.resize(16);
        ctx->attribute_slots.resize(16);
        for (auto& slot : ctx->slots) {
            slot.dynamic_csc_offsets.reserve(VM_MOCK_INIT_CAP);
            slot.dynamic_csc_targets.reserve(VM_MOCK_INIT_CAP);
        }
    }

    return ctx;
}

void impulse_vm_context_destroy(impulse_vm_context_t* ctx) {
    if (ctx) {
        delete[] ctx->private_arena_memory;
        delete[] ctx->arena_memory;
        delete ctx;
    }
}

void impulse_vm_context_set_fuel(impulse_vm_context_t* ctx, uint64_t fuel) {
    if (ctx) {
        ctx->fuel = fuel;
        ctx->fuel_enabled = (fuel > 0);
    }
}

size_t impulse_vm_context_get_vector_size(const impulse_vm_context_t* ctx) {
    return ctx ? ctx->node_buffer.size() : 0;
}

const float* impulse_vm_context_get_float_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle]) {
        return ctx->float_vectors[handle].data();
    }
    return nullptr;
}

const double* impulse_vm_context_get_double_vector(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->double_vectors_allocated[handle]) {
        return ctx->double_vectors[handle].data();
    }
    return nullptr;
}

int impulse_vm_context_acquire_bitset(impulse_vm_context_t* ctx) {
    return acquire_bitset(ctx);
}

void impulse_vm_context_release_bitset(impulse_vm_context_t* ctx, size_t handle) {
    release_bitset(ctx, handle);
}

void impulse_vm_context_bitset_add(impulse_vm_context_t* ctx, size_t handle, uint64_t node_id) {
    if (ctx && handle < VM_MAX_BITSET_HANDLES && ctx->bitset_allocated[handle]) {
        bitset_add(ctx->bitsets[handle], node_id, ctx->max_nodes);
    }
}

bool impulse_vm_context_bitset_test(const impulse_vm_context_t* ctx, size_t handle, uint64_t node_id) {
    if (ctx && handle < VM_MAX_BITSET_HANDLES && ctx->bitset_allocated[handle]) {
        return bitset_test(ctx->bitsets[handle], node_id, ctx->max_nodes);
    }
    return false;
}

int impulse_vm_context_acquire_float_vector(impulse_vm_context_t* ctx) {
    return acquire_float_vector(ctx);
}

void impulse_vm_context_release_float_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_float_vector(ctx, handle);
}

void impulse_vm_context_float_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, float val) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle] && index < ctx->max_nodes) {
        ctx->float_vectors[handle][index] = val;
    }
}

int impulse_vm_context_acquire_double_vector(impulse_vm_context_t* ctx) {
    return acquire_double_vector(ctx);
}

void impulse_vm_context_release_double_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_double_vector(ctx, handle);
}

void impulse_vm_context_double_vector_set(impulse_vm_context_t* ctx, size_t handle, size_t index, double val) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->double_vectors_allocated[handle] && index < ctx->max_nodes) {
        ctx->double_vectors[handle][index] = val;
    }
}

int impulse_vm_context_acquire_node_vector(impulse_vm_context_t* ctx) {
    return acquire_node_vector(ctx);
}

void impulse_vm_context_release_node_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_node_vector(ctx, handle);
}

const uint64_t* impulse_vm_context_get_node_vector(const impulse_vm_context_t* ctx, size_t handle) {
    const uint64_t* ptr = nullptr;
    size_t len = 0;
    if (get_node_vector_slice(ctx, static_cast<uint64_t>(handle), &ptr, &len)) {
        return ptr;
    }
    return nullptr;
}

int impulse_vm_context_acquire_string_vector(impulse_vm_context_t* ctx) {
    return acquire_string_vector(ctx);
}

void impulse_vm_context_release_string_vector(impulse_vm_context_t* ctx, size_t handle) {
    release_string_vector(ctx, handle);
}

void impulse_vm_context_string_vector_add(impulse_vm_context_t* ctx, size_t handle, const char* str) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle]) {
        ctx->string_vectors[handle].push_back(str);
    }
}

size_t impulse_vm_context_string_vector_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle]) {
        return ctx->string_vectors[handle].size();
    }
    return 0;
}

const char* impulse_vm_context_string_vector_get(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->string_vectors_allocated[handle] && index < ctx->string_vectors[handle].size()) {
        return ctx->string_vectors[handle][index];
    }
    return nullptr;
}

int impulse_vm_context_acquire_value_map(impulse_vm_context_t* ctx) {
    return acquire_value_map(ctx);
}

void impulse_vm_context_release_value_map(impulse_vm_context_t* ctx, size_t handle) {
    release_value_map(ctx, handle);
}

size_t impulse_vm_context_value_map_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle]) {
        return ctx->value_maps[handle].keys.size();
    }
    return 0;
}

const char* impulse_vm_context_value_map_get_key(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].keys.size()) {
        return ctx->value_maps[handle].keys[index];
    }
    return nullptr;
}

float impulse_vm_context_value_map_get_value(const impulse_vm_context_t* ctx, size_t handle, size_t index) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->value_maps_allocated[handle] && index < ctx->value_maps[handle].values.size()) {
        return ctx->value_maps[handle].values[index];
    }
    return 0.0f;
}

void impulse_vm_context_node_vector_set(impulse_vm_context_t* ctx, size_t handle, const uint64_t* values, size_t count) {
    if (!ctx || handle >= VM_MAX_VECTOR_HANDLES || !ctx->node_vectors_allocated[handle]) return;
    if (ctx->node_vectors[handle].size() < count) {
        ctx->node_vectors[handle].resize(count, 0ULL);
    }
    if (values && count > 0) {
        std::memcpy(ctx->node_vectors[handle].data(), values, count * sizeof(uint64_t));
    }
    ctx->node_vector_lengths[handle] = count;
    ctx->node_vector_length_explicit[handle] = true;
}

size_t impulse_vm_context_node_vector_size(const impulse_vm_context_t* ctx, size_t handle) {
    const uint64_t* ptr = nullptr;
    size_t len = 0;
    if (get_node_vector_slice(ctx, static_cast<uint64_t>(handle), &ptr, &len)) {
        return len;
    }
    return 0;
}

size_t impulse_vm_context_float_vector_size(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle]) {
        return (ctx->float_vector_lengths[handle] > 0) ? ctx->float_vector_lengths[handle] : ctx->float_vectors[handle].size();
    }
    return 0;
}

int impulse_vm_context_acquire_lane_frontier(impulse_vm_context_t* ctx) {
    return acquire_lane_frontier(ctx, ctx ? resolve_multilane_node_count(ctx, -1, 64) : 64, 64);
}

void impulse_vm_context_release_lane_frontier(impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES) {
        ctx->lane_frontiers_allocated[handle] = false;
        ctx->lane_frontiers[handle].lanes.clear();
        ctx->lane_frontiers[handle].active_words.clear();
        for (size_t r = 0; r < VM_MAX_REL_SLOTS; ++r) {
            ctx->lane_frontiers[handle].edge_trip_masks[r].clear();
        }
        ctx->lane_frontiers[handle].edge_trip_active_mask = 0;
    }
}

uint32_t impulse_vm_context_lane_frontier_num_lanes(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES && handle < ctx->lane_frontiers_count) {
        return ctx->lane_frontiers[handle].num_lanes;
    }
    return 0;
}

uint32_t impulse_vm_context_lane_frontier_width(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES && handle < ctx->lane_frontiers_count) {
        return static_cast<uint32_t>(ctx->lane_frontiers[handle].width);
    }
    return 0;
}

uint64_t impulse_vm_context_lane_frontier_active_mask(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES && handle < ctx->lane_frontiers_count) {
        return ctx->lane_frontiers[handle].active_mask;
    }
    return 0ULL;
}

size_t impulse_vm_context_lane_frontier_node_count(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES && handle < ctx->lane_frontiers_count) {
        return ctx->lane_frontiers[handle].lanes.size();
    }
    return 0;
}

const uint64_t* impulse_vm_context_lane_frontier_data(const impulse_vm_context_t* ctx, size_t handle) {
    if (ctx && handle < VM_MAX_LANE_FRONTIER_HANDLES && handle < ctx->lane_frontiers_count) {
        return ctx->lane_frontiers[handle].lanes.data();
    }
    return nullptr;
}


void impulse_vm_context_mock_csr(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csr_offsets,
    const uint32_t* csr_targets,
    uint64_t node_count,
    uint64_t edge_count
) {
    impulse_vm_context_mock_csr_typed(ctx, relation_index, csr_offsets, csr_targets, node_count, edge_count, 4, 4);
}

void impulse_vm_context_mock_csr_typed(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const void* csr_offsets,
    const void* csr_targets,
    uint64_t node_count,
    uint64_t edge_count,
    uint8_t node_id_width,
    uint8_t edge_index_width
) {
    if (ctx) {
        if (relation_index >= ctx->slots.size()) {
            ctx->slots.resize(relation_index + 1);
        }
        ctx->slots[relation_index].offsets_ptr = csr_offsets;
        ctx->slots[relation_index].targets_ptr = csr_targets;
        ctx->slots[relation_index].node_count = node_count;
        ctx->slots[relation_index].edge_count = edge_count;
        ctx->slots[relation_index].node_id_width = node_id_width ? node_id_width : 4;
        ctx->slots[relation_index].edge_index_width = edge_index_width ? edge_index_width : 4;
    }
}

void impulse_vm_context_mock_csc(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const uint32_t* csc_offsets,
    const uint32_t* csc_targets
) {
    impulse_vm_context_mock_csc_typed(ctx, relation_index, csc_offsets, csc_targets, 4, 4);
}

void impulse_vm_context_mock_csc_typed(
    impulse_vm_context_t* ctx,
    uint16_t relation_index,
    const void* csc_offsets,
    const void* csc_targets,
    uint8_t node_id_width,
    uint8_t edge_index_width
) {
    if (ctx && relation_index < ctx->slots.size()) {
        ctx->slots[relation_index].csc_offsets_ptr = csc_offsets;
        ctx->slots[relation_index].csc_targets_ptr = csc_targets;
        ctx->slots[relation_index].csc_node_id_width = node_id_width ? node_id_width : 4;
        ctx->slots[relation_index].csc_edge_index_width = edge_index_width ? edge_index_width : 4;
    }
}

void impulse_vm_context_bitset_fill(impulse_vm_context_t* ctx, size_t handle, uint64_t count) {
    if (ctx && handle < ctx->bitsets.size()) {
        auto& bs = ctx->bitsets[handle];
        bs.clear();
        size_t full_words = count / 64;
        for (size_t i = 0; i < full_words; ++i) {
            bs.words[i] = ~0ULL;
        }
        size_t rem = count % 64;
        if (rem > 0) {
            bs.words[full_words] = (1ULL << rem) - 1;
        }
    }
}

uint64_t impulse_vm_context_bitset_get_word(const impulse_vm_context_t* ctx, size_t handle, size_t word_idx) {
    if (ctx && handle < ctx->bitsets.size()) {
        const auto& bs = ctx->bitsets[handle];
        if (word_idx < bs.word_count) {
            return bs.words[word_idx];
        }
    }
    return 0;
}

void impulse_vm_context_bind_inline_data(impulse_vm_context_t* ctx, const void* data_ptr, size_t bytes) {
    if (ctx) {
        ctx->inline_data_ptr = static_cast<const uint8_t*>(data_ptr);
        ctx->inline_data_bytes = bytes;
    }
}


} // extern "C"
