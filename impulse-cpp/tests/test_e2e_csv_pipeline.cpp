/**
 * @file test_e2e_csv_pipeline.cpp
 * @brief End-to-End CSV-to-.imps Snapshot, Multi-Frontend Query Corpus, GraphBLAS Math,
 *        and CEL Filter/Projection Verification Suite for Impulse Graph Engine (C++20 Core).
 *
 * Exercises:
 *   - Suite 1: CLI Snapshot Build (`impulse-graph build`) & `.imps` v0.9.0 Structural Verification
 *              (16 domains, 26 relations, MPHF string key round-trips, non-null & nullable attributes).
 *   - Suite 2: Shared Multi-Language Query Corpus (`queries/query_manifest.json` - 22 queries across
 *              Cypher, ImpLog, ImpK, and ImpScheme; Stage A S-Expr lowering, Stage B Opt ON vs Opt OFF,
 *              Stage C live ImpulseVM execution against `/tmp/e2e_core_test.imps`).
 *   - Suite 3: GraphBLAS Pure Math Vectors, Sparse Matrices & Algorithm Execution on `ImpulseVM`
 *              (SpMV/vxm semirings, Tropical min-plus/max-min path algebra, 1D Ring Laplacian & explicit
 *              Euler heat diffusion PDE, Markov PageRank with L1 norm diff, GNN convolution + activations,
 *              Null-aware vector math `OP_EXTRACT_VALIDITY`/`OP_COALESCE`, 64x64 boolean permutation &
 *              64-lane bit-sliced SpMM, Afforest Connected Components & K-Core Decomposition).
 *   - Suite 4: Complex CEL Filter & State Projection Expressions over `User`, `Order`, and `Sensor`.
 */

#include "impulse_graph.h"
#include "impulse_format_v0_9.h"
#include "impulse_format_v0_10.h"
#include "impulse_vm.h"
#include "impulse_vm_fluent.hpp"
#include "impulse_cel.h"
#include "impulse_sexpr.hpp"
#include "impulse_scm_ast.hpp"
#include "impulse_compiler.hpp"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include "impulse_impk.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace impulse::compiler;

namespace {

int g_total_assertions = 0;
int g_passed_assertions = 0;
int g_failed_assertions = 0;
std::vector<std::string> g_failures;

#define E2E_CHECK(cond, msg)                                                                 \
    do {                                                                                     \
        ++g_total_assertions;                                                                \
        if (cond) {                                                                          \
            ++g_passed_assertions;                                                           \
        } else {                                                                             \
            ++g_failed_assertions;                                                           \
            std::ostringstream _fail_ss;                                                     \
            _fail_ss << __FILE__ << ":" << __LINE__ << " -> " << (msg)                       \
                     << " [Condition: " << #cond << "]";                                     \
            g_failures.push_back(_fail_ss.str());                                            \
            std::cout << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> " << (msg)       \
                      << "\n";                                                               \
        }                                                                                    \
    } while (0)

std::string read_text_file(const fs::path& p) {
    std::ifstream in(p);
    E2E_CHECK(in.good(), "Failed to open file: " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

fs::path locate_e2e_suite_dir() {
    const std::vector<fs::path> candidates = {
        "spec/test-fixtures/e2e-csv-suite",
        "../spec/test-fixtures/e2e-csv-suite",
        "../../spec/test-fixtures/e2e-csv-suite",
        "../impulse-graph-spec/test-fixtures/e2e-csv-suite",
        "../../impulse-graph-spec/test-fixtures/e2e-csv-suite",
        "../../../impulse-graph-spec/test-fixtures/e2e-csv-suite"
    };
    for (const auto& c : candidates) {
        if (fs::exists(c / "manifest.json")) {
            return fs::canonical(c);
        }
    }
    E2E_CHECK(false, "Could not locate e2e-csv-suite/manifest.json");
    return {};
}

uint32_t float_to_bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(float));
    return u;
}

struct ExpectedDomainInfo {
    uint16_t id;
    const char* name;
    uint64_t node_count;
    uint16_t expected_attr_count;
};

static const ExpectedDomainInfo kExpectedDomains[16] = {
    {0,  "User",         16, 5},
    {1,  "Profile",      16, 3},
    {2,  "Department",    6, 3},
    {3,  "Product",      12, 3},
    {4,  "Order",        20, 3},
    {5,  "Tag",           8, 2},
    {6,  "Task",         12, 3},
    {7,  "Group",         8, 2},
    {8,  "Resource",     10, 2},
    {9,  "Sensor",       10, 4},
    {10, "MatEmpty0",     0, 2},
    {11, "MatVec10",     10, 6},
    {12, "MatGrid50",    50, 8},
    {13, "MatBit64",     64, 4},
    {14, "MatNet100",   100, 7},
    {15, "MatFeature50", 50, 3}
};

struct ExpectedRelationInfo {
    uint16_t id;
    const char* name;
    uint16_t src_domain;
    uint16_t tgt_domain;
    uint64_t edge_count;
    bool include_csc;
    uint8_t expected_encoding_id;
    uint16_t expected_attr_count;
};

constexpr uint8_t kEncCsr = 0;
constexpr uint8_t kEncCoo = 3;

static const ExpectedRelationInfo kExpectedRelations[26] = {
    {0,  "USER_FOLLOWS",           0,  0,  24, true,  kEncCsr, 2},
    {1,  "USER_HAS_PROFILE",       0,  1,  14, true,  kEncCsr, 1},
    {2,  "USER_WORKS_IN",          0,  2,  14, true,  kEncCsr, 1},
    {3,  "USER_RATED_PRODUCT",     0,  3,  18, false, kEncCsr, 2},
    {4,  "USER_IN_GROUP",          0,  7,  16, true,  kEncCsr, 1},
    {5,  "PROFILE_PRIMARY_USER",   1,  0,  14, true,  kEncCsr, 1},
    {6,  "DEPT_HAS_MEMBER",        2,  0,  14, true,  kEncCsr, 2},
    {7,  "DEPT_PARENT",            2,  2,   5, true,  kEncCsr, 1},
    {8,  "PRODUCT_HAS_TAG",        3,  5,  22, true,  kEncCsr, 1},
    {9,  "ORDER_PLACED_BY",        4,  0,  20, true,  kEncCsr, 1},
    {10, "ORDER_CONTAINS_PRODUCT", 4,  3,  35, true,  kEncCsr, 3},
    {11, "TAG_IMPLIED_BY",         5,  5,   6, true,  kEncCsr, 1},
    {12, "TASK_DEPENDS_ON",        6,  6,  14, true,  kEncCsr, 2},
    {13, "GROUP_INCLUDES",         7,  7,   9, true,  kEncCsr, 1},
    {14, "GROUP_CAN_ACCESS",       7,  8,  15, true,  kEncCsr, 2},
    {15, "RESOURCE_LINKED_TO",     8,  8,   8, true,  kEncCsr, 1},
    {16, "SENSOR_FEEDS_SENSOR",    9,  9,  16, true,  kEncCsr, 2},
    {17, "MAT0_EMPTY_CSR",         10, 10,  0, true,  kEncCsr, 1},
    {18, "MAT10_UPPER_TRI_CSR",    11, 11, 25, true,  kEncCsr, 3},
    {19, "MAT50_ZERO_CSR",         12, 12,  0, true,  kEncCsr, 1},
    {20, "MAT50_IDENTITY_COO",     12, 12, 50, true,  kEncCoo, 3},
    {21, "MAT50_LAPLACIAN_CSR",    12, 12, 100, true, kEncCsr, 3},
    {22, "MAT64_PERM_BOOL_CSR",    13, 13, 64, true,  kEncCsr, 1},
    {23, "MAT100_STOCHASTIC_CSR",  14, 14, 250, true, kEncCsr, 3},
    {24, "MAT_RECT_100x50_COO",    14, 15, 150, true, kEncCoo, 2},
    {25, "MAT_RECT_50x100_COO",    15, 14, 100, true, kEncCoo, 1}
};

GraphCatalog build_e2e_catalog(const impulse_snapshot_t* snap) {
    GraphCatalog catalog;
    for (uint16_t r = 0; r < 26; ++r) {
        impulse_relation_directory_entry_t entry{};
        impulse_status_t st = impulse_snapshot_get_relation_entry(snap, r, &entry);
        E2E_CHECK(st == IMPULSE_OK, "impulse_snapshot_get_relation_entry failed");
        uint64_t src_nodes = kExpectedDomains[entry.src_domain_id].node_count;
        double avg_mult = (src_nodes > 0) ? (static_cast<double>(entry.edge_count) / static_cast<double>(src_nodes)) : 0.0;
        catalog.register_relation(kExpectedRelations[r].name, r, avg_mult);
    }
    return catalog;
}

std::vector<uint32_t> extract_active_nodes(
    impulse_vm_context_t* ctx,
    const impulse_vm_state_t& state,
    uint16_t reg,
    uint32_t max_domain_nodes
) {
    std::vector<uint32_t> result;
    uint8_t rtype = state.register_types[reg];
    uint64_t rval = state.registers[reg];

    if (rtype == TYPE_BITSET_HANDLE) {
        for (uint32_t u = 0; u < max_domain_nodes; ++u) {
            if (impulse_vm_context_bitset_test(ctx, static_cast<size_t>(rval), u)) {
                result.push_back(u);
            }
        }
    } else if (rtype == TYPE_LANE_FRONTIER) {
        size_t n_nodes = impulse_vm_context_lane_frontier_node_count(ctx, static_cast<size_t>(rval));
        const uint64_t* lanes = impulse_vm_context_lane_frontier_data(ctx, static_cast<size_t>(rval));
        uint64_t active_mask = impulse_vm_context_lane_frontier_active_mask(ctx, static_cast<size_t>(rval));
        size_t bound = std::min<size_t>(max_domain_nodes, n_nodes);
        if (lanes) {
            for (size_t u = 0; u < bound; ++u) {
                if ((lanes[u] & active_mask) != 0ULL) {
                    result.push_back(static_cast<uint32_t>(u));
                }
            }
        }
    } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
        size_t len = impulse_vm_context_node_vector_size(ctx, static_cast<size_t>(rval));
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx, static_cast<size_t>(rval));
        if (ptr) {
            for (size_t i = 0; i < len; ++i) {
                result.push_back(static_cast<uint32_t>(ptr[i]));
            }
        }
    } else if (rtype == TYPE_INT64) {
        size_t alt_reg = (reg == 0) ? 1 : 0;
        if (state.register_types[alt_reg] == TYPE_BITSET_HANDLE) {
            uint64_t alt_val = state.registers[alt_reg];
            for (uint32_t u = 0; u < max_domain_nodes; ++u) {
                if (impulse_vm_context_bitset_test(ctx, static_cast<size_t>(alt_val), u)) {
                    result.push_back(u);
                }
            }
        } else {
            result.push_back(static_cast<uint32_t>(rval));
        }
    } else if (rtype == TYPE_NODE_ID) {
        result.push_back(static_cast<uint32_t>(rval));
    }
    return result;
}

std::string format_u32_vec(const std::vector<uint32_t>& v) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << v[i];
    }
    ss << "]";
    return ss.str();
}

// ============================================================================
// SUITE 1: CLI Snapshot Build & .imps v0.9.0 Structural Verification
// ============================================================================
void run_suite_1_snapshot_verification(const fs::path& suite_dir, const fs::path& imps_path, impulse_snapshot_t** out_snap) {
    std::cout << "\n=========================================================================\n";
    std::cout << "SUITE 1: CLI Snapshot Build & .imps v0.9.0 Structural Verification\n";
    std::cout << "=========================================================================\n";

    fs::path manifest_path = suite_dir / "manifest.json";
    E2E_CHECK(fs::exists(manifest_path), "Manifest file exists at " + manifest_path.string());
    E2E_CHECK(fs::exists(imps_path), "Output .imps file was created by CLI build");
    E2E_CHECK(fs::file_size(imps_path) >= 4096, "Output .imps file smaller than 4KB Page 0 header");

    impulse_status_t status = IMPULSE_ERR_IO_FAILURE;
    impulse_snapshot_t* snap = impulse_snapshot_open(imps_path.string().c_str(), &status);
    E2E_CHECK(status == IMPULSE_OK && snap != nullptr,
              "impulse_snapshot_open failed (status=" + std::to_string(status) + ")");
    *out_snap = snap;
    if (snap == nullptr) return;

    // 1. Header Magic & Version
    uint32_t magic = impulse_snapshot_magic(snap);
    uint16_t version = impulse_snapshot_version(snap);
    E2E_CHECK(magic == 0x494D5053U, "Snapshot magic must be 0x494D5053 ('IMPS')");
    E2E_CHECK(version == 10U, "Snapshot version must be 10 (v0.10.0)");
    std::cout << "[Suite 1] Verified Header: magic=0x" << std::hex << magic << std::dec
              << " ('IMPS'), version=" << version << ", size=" << fs::file_size(imps_path) << " bytes\n";

    // 2. Domain Catalog (16 domains + inline Spec v0.10.0 node attribute counts)
    uint16_t domain_count = impulse_snapshot_domain_count(snap);
    E2E_CHECK(domain_count == 16U, "Expected 16 domains in snapshot");
    for (uint16_t d = 0; d < 16; ++d) {
        impulse_domain_catalog_entry_t d_entry{};
        const char* d_name = nullptr;
        E2E_CHECK(impulse_snapshot_get_domain_entry(snap, d, &d_entry, &d_name) == IMPULSE_OK,
                  "impulse_snapshot_get_domain_entry failed");
        E2E_CHECK(d_name != nullptr && std::string(d_name) == kExpectedDomains[d].name,
                  "Domain name mismatch at domain " + std::to_string(d));
        E2E_CHECK(d_entry.domain_id == d, "Domain ID mismatch");
        E2E_CHECK(d_entry.node_count == kExpectedDomains[d].node_count,
                  "Domain node_count mismatch for " + std::string(d_name));
        E2E_CHECK(d_entry.attr_count == kExpectedDomains[d].expected_attr_count,
                  "Domain attr_count mismatch for " + std::string(d_name));
    }
    std::cout << "[Suite 1] Verified all 16 domains (User..MatFeature50), node counts, and inline attr_counts.\n";

    // 3. Relation Directory (26 relations)
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    E2E_CHECK(rel_count == 26U, "Expected 26 relations in snapshot");
    for (uint16_t r = 0; r < 26; ++r) {
        impulse_relation_directory_entry_t r_entry{};
        E2E_CHECK(impulse_snapshot_get_relation_entry(snap, r, &r_entry) == IMPULSE_OK,
                  "impulse_snapshot_get_relation_entry failed");
        E2E_CHECK(r_entry.src_domain_id == kExpectedRelations[r].src_domain,
                  "src_domain_id mismatch on relation " + std::to_string(r));
        E2E_CHECK(r_entry.tgt_domain_id == kExpectedRelations[r].tgt_domain,
                  "tgt_domain_id mismatch on relation " + std::to_string(r));
        E2E_CHECK(r_entry.edge_count == kExpectedRelations[r].edge_count,
                  "edge_count mismatch on relation " + std::string(kExpectedRelations[r].name));
        E2E_CHECK(r_entry.attr_count == kExpectedRelations[r].expected_attr_count,
                  "attr_count mismatch on relation " + std::string(kExpectedRelations[r].name));
        E2E_CHECK(r_entry.encoding_id == kExpectedRelations[r].expected_encoding_id,
                  "encoding_id mismatch on relation " + std::string(kExpectedRelations[r].name) +
                  ": expected " + std::to_string(kExpectedRelations[r].expected_encoding_id) +
                  ", got " + std::to_string(r_entry.encoding_id));

        if (kExpectedRelations[r].include_csc) {
            E2E_CHECK((r_entry.section_features & IMP_REL_FEATURE_INCLUDE_CSC) != 0,
                      "Expected IMP_REL_FEATURE_INCLUDE_CSC on relation " + std::string(kExpectedRelations[r].name));
            E2E_CHECK(r_entry.csc_row_off_offset > 0,
                      "Expected non-zero csc_row_off_offset on " + std::string(kExpectedRelations[r].name));
        } else {
            E2E_CHECK((r_entry.section_features & IMP_REL_FEATURE_INCLUDE_CSC) == 0,
                      "Expected zero IMP_REL_FEATURE_INCLUDE_CSC when include_csc=false on " + std::string(kExpectedRelations[r].name));
            E2E_CHECK(r_entry.csc_row_off_offset == 0,
                      "Expected zero csc_row_off_offset when include_csc=false on " + std::string(kExpectedRelations[r].name));
        }
    }
    std::cout << "[Suite 1] Checked all 26 relations (USER_FOLLOWS..MAT_RECT_50x100_COO).\n";

    // 4. MPHF Key Resolution & Reverse Dense ID Lookup
    size_t mphf_forward_ok = 0;
    auto verify_domain_mphf = [&](uint16_t domain_id, const std::string& prefix, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            std::string key = prefix + std::to_string(i);
            // Reverse lookup (dense ID -> string key)
            const void* key_bytes = nullptr;
            size_t key_len = 0;
            impulse_status_t st = impulse_snapshot_resolve_dense_id(snap, domain_id, i, &key_bytes, &key_len);
            E2E_CHECK(st == IMPULSE_OK && key_bytes != nullptr, "MPHF resolve_dense_id failed for ID " + std::to_string(i));
            if (key_bytes != nullptr) {
                std::string roundtrip(static_cast<const char*>(key_bytes), key_len);
                E2E_CHECK(roundtrip == key, "MPHF round-trip mismatch: expected " + key + ", got " + roundtrip);
            }

            // Forward lookup (string key -> dense ID)
            uint32_t resolved_id = 0xFFFFFFFFU;
            st = impulse_snapshot_resolve_key(
                snap, domain_id, key.data(), key.size(), &resolved_id
            );
            E2E_CHECK(st == IMPULSE_OK && resolved_id == i,
                      "MPHF resolve_key failed for key '" + key + "' in domain " + std::to_string(domain_id));
            if (st == IMPULSE_OK && resolved_id == i) {
                ++mphf_forward_ok;
            }
        }
        // Negative lookup check
        uint32_t dummy_id = 0;
        std::string missing_key = prefix + "999999";
        E2E_CHECK(impulse_snapshot_resolve_key(snap, domain_id, missing_key.data(), missing_key.size(), &dummy_id) != IMPULSE_OK,
                  "MPHF resolve_key should reject nonexistent key " + missing_key);
    };
    verify_domain_mphf(0, "u", 16);     // User: u0..u15
    verify_domain_mphf(1, "p", 16);     // Profile: p0..p15
    verify_domain_mphf(2, "d", 6);      // Department: d0..d5
    verify_domain_mphf(3, "prod", 12);  // Product: prod0..prod11
    verify_domain_mphf(5, "tag", 8);    // Tag: tag0..tag7
    verify_domain_mphf(6, "t", 12);     // Task: t0..t11
    verify_domain_mphf(7, "g", 8);      // Group: g0..g7
    verify_domain_mphf(8, "res", 10);   // Resource: res0..res9
    E2E_CHECK(mphf_forward_ok == 88U,
              "MPHF forward lookup must resolve all 88 domain keys, got " + std::to_string(mphf_forward_ok) + "/88");
    std::cout << "[Suite 1] Checked MPHF reverse lookup (88/88 keys) & forward lookup ("
              << mphf_forward_ok << "/88 resolved).\n";

    // 5. Attribute Buffer Verification (Non-null & Nullable attributes)
    {
        const void* data_ptr = nullptr;
        uint64_t data_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dim = 0;

        // USER_FOLLOWS.strength_f32 (attr 0) -> 24 edge floats (type_code 0x06)
        E2E_CHECK(impulse_snapshot_get_attribute_buffers(snap, 0, 0, &data_ptr, &data_bytes, nullptr, nullptr, &type_code, &dim) == IMPULSE_OK,
                  "get_attribute_buffers(0, 0) failed");
        E2E_CHECK(type_code == 0x06U && data_bytes == 24 * sizeof(float), "USER_FOLLOWS.strength_f32 metadata mismatch");
        if (data_ptr != nullptr) {
            const auto* strengths = static_cast<const float*>(data_ptr);
            E2E_CHECK(std::abs(strengths[0] - 0.50f) < 1e-6f && std::abs(strengths[1] - 0.75f) < 1e-6f && std::abs(strengths[2] - 1.00f) < 1e-6f,
                      "USER_FOLLOWS.strength_f32 values mismatch");
        }

        // User.age_i32 (domain 0, attr 0) -> 20, 22, ..., 50 (type_code 0x03)
        E2E_CHECK(impulse_snapshot_get_domain_attribute_buffers(snap, 0, 0, &data_ptr, &data_bytes, nullptr, nullptr, &type_code, &dim) == IMPULSE_OK,
                  "get_domain_attribute_buffers(0, 0) failed");
        E2E_CHECK(type_code == 0x03U && data_bytes == 16 * sizeof(int32_t), "User.age_i32 metadata mismatch");
        if (data_ptr != nullptr) {
            const auto* ages = static_cast<const int32_t*>(data_ptr);
            for (int i = 0; i < 16; ++i) {
                E2E_CHECK(ages[i] == 20 + i * 2, "User.age_i32 value mismatch at " + std::to_string(i));
            }
        }

        // User.score_f64 (domain 0, attr 2) -> 50.0, 53.25, ..., 98.75 (type_code 0x07)
        E2E_CHECK(impulse_snapshot_get_domain_attribute_buffers(snap, 0, 2, &data_ptr, &data_bytes, nullptr, nullptr, &type_code, &dim) == IMPULSE_OK,
                  "get_domain_attribute_buffers(0, 2) failed");
        E2E_CHECK(type_code == 0x07U && data_bytes == 16 * sizeof(double), "User.score_f64 metadata mismatch");
        if (data_ptr != nullptr) {
            const auto* scores = static_cast<const double*>(data_ptr);
            for (int i = 0; i < 16; ++i) {
                E2E_CHECK(std::abs(scores[i] - (50.0 + i * 3.25)) < 1e-9, "User.score_f64 value mismatch");
            }
        }

        // User.opt_nickname_str (domain 0, attr 3) -> nullable string (type_code & 0x80 != 0)
        E2E_CHECK(impulse_snapshot_get_domain_attribute_buffers(snap, 0, 3, &data_ptr, &data_bytes, nullptr, nullptr, &type_code, &dim) == IMPULSE_OK,
                  "get_domain_attribute_buffers(0, 3) failed");
        E2E_CHECK((type_code & 0x80U) != 0, "User.opt_nickname_str must have nullable bit 0x80 set");

        // Nullable float32 attribute via OP_LOAD_COLUMN_VECTOR directly (MatGrid50.opt_obs_f32: domain 12, attr 7, type_code 0x86)
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;
        const impulse_instruction_t load_nullable_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 12U | (7U << 16)},
            {OP_HALT,               0, 1, 0}
        };
        E2E_CHECK(impulse_vm_execute(load_nullable_prog, 2, &state, 0) == IMPULSE_VM_OK,
                  "OP_LOAD_COLUMN_VECTOR on MatGrid50.opt_obs_f32 failed");
        const float* vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[1]));
        E2E_CHECK(vec != nullptr && std::abs(vec[0] - 0.0f) < 1e-6f && std::abs(vec[1] - 1.5f) < 1e-6f,
                  "OP_LOAD_COLUMN_VECTOR on nullable MatGrid50.opt_obs_f32 must skip 128-byte validity bitmap and load vec[0]==0.0f, vec[1]==1.5f");
        impulse_vm_context_destroy(ctx);
    }
    std::cout << "[Suite 1] Checked non-null and nullable (0x80 flag) domain & edge attribute buffers.\n";
}

// ============================================================================
// SUITE 2: Shared Multi-Language Query Corpus Runner (22 Queries)
// ============================================================================
struct CorpusQueryCase {
    std::string id;
    std::string language;
    std::string query_file;
    std::string expected_scm_file;
    std::string expected_scm_cpp;
    std::string expected_scm_java;
    uint16_t seed_domain_id;
    uint16_t target_domain_id;
    uint64_t seed_node_dense_id;
    std::vector<uint64_t> seed_nodes;
    std::vector<uint32_t> expected_dense_ids;
    uint32_t expected_cardinality;
    int64_t expected_scalar_int;
    bool expect_2hop_fusion;
};

static std::vector<CorpusQueryCase> get_corpus_queries() {
    return {
        {
            "cypher_01_user_profile_1to1", "cypher",
            "queries/cypher/cypher_01_user_profile_1to1.cypher",
            "queries/expected/cypher_01_user_profile_1to1.impscm",
            "(program\n  (csr-walk \"USER_HAS_PROFILE\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"USER_HAS_PROFILE\")\n  (collect-bitset))",
            0, 1, 0, {}, {0}, 1, 1, false
        },
        {
            "cypher_02_dept_members_orders_2hop", "cypher",
            "queries/cypher/cypher_02_dept_members_orders_2hop.cypher",
            "queries/expected/cypher_02_dept_members_orders_2hop.impscm",
            "(program\n  (csr-walk \"DEPT_HAS_MEMBER\")\n  (csc-walk \"ORDER_PLACED_BY\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"DEPT_HAS_MEMBER\")\n  (csc-walk \"ORDER_PLACED_BY\")\n  (collect-bitset))",
            2, 4, 0, {}, {0, 1, 2, 3}, 4, 4, false
        },
        {
            "cypher_03_order_product_tags_2hop", "cypher",
            "queries/cypher/cypher_03_order_product_tags_2hop.cypher",
            "queries/expected/cypher_03_order_product_tags_2hop.impscm",
            "(program\n  (csr-walk \"ORDER_CONTAINS_PRODUCT\")\n  (csr-walk \"PRODUCT_HAS_TAG\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"ORDER_CONTAINS_PRODUCT\")\n  (csr-walk \"PRODUCT_HAS_TAG\")\n  (collect-bitset))",
            4, 5, 0, {}, {0, 1, 2}, 3, 3, true
        },
        {
            "cypher_04_group_kleene_rebac", "cypher",
            "queries/cypher/cypher_04_group_kleene_rebac.cypher",
            "queries/expected/cypher_04_group_kleene_rebac.impscm",
            "(program\n  (fixpoint \"GROUP_INCLUDES\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            "(program\n  (fixpoint \"\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            7, 7, 0, {}, {0, 1, 2, 3, 4}, 5, 5, false
        },
        {
            "cypher_05_user_follows_undirected_count", "cypher",
            "queries/cypher/cypher_05_user_follows_undirected_count.cypher",
            "queries/expected/cypher_05_user_follows_undirected_count.impscm",
            "(program\n  (set:union (csr-walk \"USER_FOLLOWS\") (csc-walk \"USER_FOLLOWS\"))\n  (collect-count))",
            "(program\n  (set-union (csr-walk \"USER_FOLLOWS\") (csc-walk \"USER_FOLLOWS\"))\n  (collect-scalar))",
            0, 0, 0, {}, {1, 2, 4, 5}, 4, 4, false
        },
        {
            "cypher_06_multilane_user_follows_quorum", "cypher",
            "queries/cypher/cypher_06_multilane_user_follows_quorum.cypher",
            "queries/expected/cypher_06_multilane_user_follows_quorum.impscm",
            "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"USER_FOLLOWS\")\n  (lane-filter-quorum 2 255)\n  (collect-bitset))",
            "(program\n  (lane-init-seeds (0 1 2))\n  (lane-csr-walk \"USER_FOLLOWS\")\n  (lane-filter-quorum 2)\n  (collect-bitset))",
            0, 0, 0, {0, 1, 2}, {4, 5, 6}, 3, 3, false
        },
        {
            "implog_01_group_transitive", "implog",
            "queries/implog/implog_01_group_transitive.implog",
            "queries/expected/implog_01_group_transitive.impscm",
            "(program\n  (fixpoint \"GROUP_INCLUDES\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            "(program\n  (fixpoint \"\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            7, 7, 0, {}, {0, 1, 2, 3, 4}, 5, 5, false
        },
        {
            "implog_02_task_diamond_dag", "implog",
            "queries/implog/implog_02_task_diamond_dag.implog",
            "queries/expected/implog_02_task_diamond_dag.impscm",
            "(program\n  (fixpoint \"TASK_DEPENDS_ON\" 100 (csr-walk \"TASK_DEPENDS_ON\"))\n  (collect-bitset))",
            "(program\n  (fixpoint \"\" 100 (csr-walk \"TASK_DEPENDS_ON\"))\n  (collect-bitset))",
            6, 6, 0, {}, {0, 1, 2, 3, 4, 5}, 6, 6, false
        },
        {
            "implog_03_group_resource_access", "implog",
            "queries/implog/implog_03_group_resource_access.implog",
            "queries/expected/implog_03_group_resource_access.impscm",
            "(program\n  (csr-walk \"USER_IN_GROUP\")\n  (csr-walk \"GROUP_CAN_ACCESS\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"USER_IN_GROUP\")\n  (csr-walk \"GROUP_CAN_ACCESS\")\n  (collect-bitset))",
            0, 8, 0, {}, {0, 1, 2, 3}, 4, 4, true
        },
        {
            "implog_04_multiseed_lanes", "implog",
            "queries/implog/implog_04_multiseed_lanes.implog",
            "queries/expected/implog_04_multiseed_lanes.impscm",
            "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"USER_FOLLOWS\")\n  (collect-bitset))",
            "(program\n  (lane-init-seeds (0 1 2))\n  (lane-csr-walk \"USER_FOLLOWS\")\n  (collect-bitset))",
            0, 0, 0, {0, 1, 2}, {0, 1, 2, 4, 5, 6}, 6, 6, false
        },
        {
            "impk_01_spmv_identity_and_zero", "impk",
            "queries/impk/impk_01_spmv_identity_and_zero.impk",
            "queries/expected/impk_01_spmv_identity_and_zero.impscm",
            "(program\n  (csr-walk \"MAT50_IDENTITY_COO\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT50_IDENTITY_COO\")\n  (collect-bitset))",
            12, 12, 0, {}, {0}, 1, 1, false
        },
        {
            "impk_02_laplacian_ring50", "impk",
            "queries/impk/impk_02_laplacian_ring50.impk",
            "queries/expected/impk_02_laplacian_ring50.impscm",
            "(program\n  (csr-walk \"MAT50_LAPLACIAN_CSR\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT50_LAPLACIAN_CSR\")\n  (collect-bitset))",
            12, 12, 0, {}, {1, 49}, 2, 2, false
        },
        {
            "impk_03_pagerank_stochastic100", "impk",
            "queries/impk/impk_03_pagerank_stochastic100.impk",
            "queries/expected/impk_03_pagerank_stochastic100.impscm",
            "(program\n  (csr-walk \"MAT100_STOCHASTIC_CSR\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT100_STOCHASTIC_CSR\")\n  (collect-bitset))",
            14, 14, 0, {}, {1, 2, 3, 4}, 4, 4, false
        },
        {
            "impk_04_connected_components_100", "impk",
            "queries/impk/impk_04_connected_components_100.impk",
            "queries/expected/impk_04_connected_components_100.impscm",
            "(program\n  (csr-walk \"MAT100_STOCHASTIC_CSR\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT100_STOCHASTIC_CSR\")\n  (collect-bitset))",
            14, 14, 0, {}, {1, 2, 3, 4}, 4, 4, false
        },
        {
            "impk_05_multilane_spmm_quorum", "impk",
            "queries/impk/impk_05_multilane_spmm_quorum.impk",
            "queries/expected/impk_05_multilane_spmm_quorum.impscm",
            "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"USER_FOLLOWS\")\n  (lane-filter-quorum 2 255)\n  (collect-bitset))",
            "(program\n  (lane-init-seeds (0 1 2))\n  (lane-csr-walk \"USER_FOLLOWS\")\n  (lane-filter-quorum 2)\n  (collect-bitset))",
            0, 0, 0, {0, 1, 2}, {4, 5, 6}, 3, 3, false
        },
        {
            "impk_06_multilane_set_cover", "impk",
            "queries/impk/impk_06_multilane_set_cover.impk",
            "queries/expected/impk_06_multilane_set_cover.impscm",
            "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"USER_FOLLOWS\")\n  (lane-set-cover 7 0)\n  (collect-bitset))",
            "(program\n  (lane-init-seeds (0 1 2))\n  (lane-csr-walk \"USER_FOLLOWS\")\n  (lane-set-cover 0 7)\n  (collect-vector))",
            0, 0, 0, {0, 1, 2}, {4}, 1, 1, false
        },
        {
            "impk_07_tropical_shortest_path", "impk",
            "queries/impk/impk_07_tropical_shortest_path.impk",
            "queries/expected/impk_07_tropical_shortest_path.impscm",
            "(program\n  (csr-walk \"MAT10_UPPER_TRI_CSR\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT10_UPPER_TRI_CSR\")\n  (collect-bitset))",
            11, 11, 0, {}, {1, 2, 3, 9}, 4, 4, false
        },
        {
            "impk_08_null_coalesce_and_gnn", "impk",
            "queries/impk/impk_08_null_coalesce_and_gnn.impk",
            "queries/expected/impk_08_null_coalesce_and_gnn.impscm",
            "(program\n  (csr-walk \"MAT_RECT_100x50_COO\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"MAT_RECT_100x50_COO\")\n  (collect-bitset))",
            14, 15, 0, {}, {0, 1}, 2, 2, false
        },
        {
            "impscm_01_user_profile_walk", "impscm",
            "queries/impscm/impscm_01_user_profile_walk.impscm",
            "queries/expected/impscm_01_user_profile_walk.impscm",
            "(program\n  (csr-walk \"USER_HAS_PROFILE\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"USER_HAS_PROFILE\")\n  (collect-bitset))",
            0, 1, 0, {}, {0}, 1, 1, false
        },
        {
            "impscm_02_order_tags_2hop", "impscm",
            "queries/impscm/impscm_02_order_tags_2hop.impscm",
            "queries/expected/impscm_02_order_tags_2hop.impscm",
            "(program\n  (csr-walk \"ORDER_CONTAINS_PRODUCT\")\n  (csr-walk \"PRODUCT_HAS_TAG\")\n  (collect-bitset))",
            "(program\n  (csr-walk \"ORDER_CONTAINS_PRODUCT\")\n  (csr-walk \"PRODUCT_HAS_TAG\")\n  (collect-bitset))",
            4, 5, 0, {}, {0, 1, 2}, 3, 3, true
        },
        {
            "impscm_03_group_fixpoint_rebac", "impscm",
            "queries/impscm/impscm_03_group_fixpoint_rebac.impscm",
            "queries/expected/impscm_03_group_fixpoint_rebac.impscm",
            "(program\n  (fixpoint \"GROUP_INCLUDES\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            "(program\n  (fixpoint \"GROUP_INCLUDES\" 100 (csr-walk \"GROUP_INCLUDES\"))\n  (collect-bitset))",
            7, 7, 0, {}, {0, 1, 2, 3, 4}, 5, 5, false
        },
        {
            "impscm_04_multilane_quorum_pipeline", "impscm",
            "queries/impscm/impscm_04_multilane_quorum_pipeline.impscm",
            "queries/expected/impscm_04_multilane_quorum_pipeline.impscm",
            "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"USER_FOLLOWS\")\n  (lane-filter-quorum 2 255)\n  (collect-bitset))",
            "(program\n  (lane-init-seeds () 0)\n  (lane-csr-walk \"USER_FOLLOWS\")\n  (lane-filter-quorum 2)\n  (collect-bitset))",
            0, 0, 0, {0, 1, 2}, {4, 5, 6}, 3, 3, false
        }
    };
}

void run_suite_2_query_corpus(const fs::path& suite_dir, const impulse_snapshot_t* snap, const GraphCatalog& catalog) {
    std::cout << "\n=========================================================================\n";
    std::cout << "SUITE 2: Shared Multi-Language Query Corpus Runner (22 Queries)\n";
    std::cout << "=========================================================================\n";

    fs::path qm_path = suite_dir / "queries" / "query_manifest.json";
    std::string qm_json = read_text_file(qm_path);
    E2E_CHECK(!qm_json.empty(), "queries/query_manifest.json must not be empty");

    auto queries = get_corpus_queries();
    E2E_CHECK(queries.size() == 22, "Expected 22 queries in corpus");

    for (const auto& q : queries) {
        int failures_before = g_failed_assertions;

        // Verify query ID exists in query_manifest.json
        E2E_CHECK(qm_json.find("\"" + q.id + "\"") != std::string::npos,
                  "Query ID " + q.id + " missing from query_manifest.json");

        std::string src = read_text_file(suite_dir / q.query_file);
        std::string golden_scm = read_text_file(suite_dir / q.expected_scm_file);

        // --- Stage A: Frontend Lowering to ImpScheme S-Expression (No Rewrites/Shims) ---
        std::shared_ptr<ScmProgram> prog;
        try {
            if (q.language == "cypher") {
                prog = CypherCompiler::compile(src).ast;
            } else if (q.language == "implog") {
                prog = impulse::datalog::DatalogParser::parse(src);
            } else if (q.language == "impk") {
                prog = impulse::impk::ImpKCompiler::compile(src);
            } else if (q.language == "impscm") {
                prog = impulse::impscm::ImpScmAstBuilder::parse(src);
            }
        } catch (const std::exception& ex) {
            E2E_CHECK(false, "Frontend lowering threw exception on " + q.id + ": " + std::string(ex.what()));
        }
        E2E_CHECK(prog != nullptr, "Frontend lowering returned null program for " + q.id);
        if (!prog) {
            std::cout << "  [FAIL] " << std::left << std::setw(42) << q.id
                      << " | lang=" << std::setw(6) << q.language
                      << " | Stage A frontend lowering failed\n";
            continue;
        }

        std::string actual_scm = prog->to_scm_string();
        E2E_CHECK(actual_scm == golden_scm,
                  "S-Expression mismatch against golden .impscm on " + q.id + ":\nActual:\n" + actual_scm +
                  "\nExpected:\n" + golden_scm);

        // --- Stage B: Optimizer Pass Equivalence (Opt ON vs Opt OFF) ---
        CompiledImpulseProgram comp_on = ImpulseCompiler::compile(
            prog, &catalog, {}, CompilerOptions::default_options()
        );
        CompiledImpulseProgram comp_off = ImpulseCompiler::compile(
            prog, &catalog, {}, CompilerOptions::unoptimized()
        );
        E2E_CHECK(comp_on.instruction_count() > 0 && comp_off.instruction_count() > 0,
                  "Compiled bytecode empty on " + q.id);

        bool has_2hop_on = false;
        bool has_2hop_off = false;
        for (const auto& inst : comp_on.instructions) {
            if (inst.opcode == OP_CSR_WALK_2HOP) has_2hop_on = true;
        }
        for (const auto& inst : comp_off.instructions) {
            if (inst.opcode == OP_CSR_WALK_2HOP) has_2hop_off = true;
        }
        E2E_CHECK(has_2hop_on == q.expect_2hop_fusion,
                  "2-hop fusion (OP_CSR_WALK_2HOP) expectation mismatch under Opt ON for " + q.id);
        E2E_CHECK(!has_2hop_off,
                  "OP_CSR_WALK_2HOP must not be emitted under Opt OFF for " + q.id);

        // --- Stage C: Live ImpulseVM Execution of Unpatched Bytecode (Opt ON & Opt OFF) ---
        auto execute_and_verify = [&](const CompiledImpulseProgram& compiled, bool is_opt_on) {
            impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
            E2E_CHECK(ctx != nullptr, "impulse_vm_context_create failed");
            if (!ctx) return;

            impulse_vm_state_t state{};
            state.query_context = ctx;

            impulse_vm_status_t vm_st = impulse_vm_execute(
                compiled.instructions.data(), compiled.instructions.size(), &state, q.seed_node_dense_id
            );
            E2E_CHECK(vm_st == IMPULSE_VM_OK,
                      "impulse_vm_execute failed on " + q.id + (is_opt_on ? " (Opt ON)" : " (Opt OFF)") +
                      " with status " + std::to_string(static_cast<int>(vm_st)));

            uint32_t max_tgt_nodes = static_cast<uint32_t>(kExpectedDomains[q.target_domain_id].node_count);
            std::vector<uint32_t> actual_ids = extract_active_nodes(
                ctx, state, compiled.result_register, max_tgt_nodes
            );

            E2E_CHECK(actual_ids == q.expected_dense_ids,
                      "Result dense IDs mismatch on " + q.id + (is_opt_on ? " (Opt ON)" : " (Opt OFF)") +
                      ": expected " + format_u32_vec(q.expected_dense_ids) +
                      ", got " + format_u32_vec(actual_ids));
            E2E_CHECK(actual_ids.size() == q.expected_cardinality,
                      "Cardinality mismatch on " + q.id + (is_opt_on ? " (Opt ON)" : " (Opt OFF)"));

            impulse_vm_context_destroy(ctx);
        };

        execute_and_verify(comp_on, true);
        execute_and_verify(comp_off, false);

        bool query_passed = (g_failed_assertions == failures_before);
        std::cout << (query_passed ? "  [PASS] " : "  [FAIL] ")
                  << std::left << std::setw(42) << q.id
                  << " | lang=" << std::setw(6) << q.language
                  << " | 2hop_fused=" << (has_2hop_on ? "YES" : "NO ")
                  << " | expected_nodes=" << format_u32_vec(q.expected_dense_ids) << "\n";
    }
}

// ============================================================================
// SUITE 3: GraphBLAS Pure Math Vectors, Sparse Matrices & Algorithms on ImpulseVM
// ============================================================================
double read_scalar_float(const impulse_vm_state_t& state, size_t reg) {
    if (state.register_types[reg] == TYPE_DOUBLE) {
        return std::bit_cast<double>(state.registers[reg]);
    }
    uint32_t raw_bits = static_cast<uint32_t>(state.registers[reg] & 0xFFFFFFFFULL);
    return static_cast<double>(std::bit_cast<float>(raw_bits));
}

void run_suite_3_graphblas_vm(const impulse_snapshot_t* snap) {
    std::cout << "\n=========================================================================\n";
    std::cout << "SUITE 3: GraphBLAS Pure Math Vectors, Sparse Matrices & Algorithm Execution\n";
    std::cout << "=========================================================================\n";

    // ------------------------------------------------------------------------
    // 3.1 Semiring SpMV (OP_MXV & OP_VXM)
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        // Load MatGrid50.x_f32 (domain 12, attr 0: [1, 2, ..., 50]) into R1
        // Test A: Zero matrix annihilator on MAT50_ZERO_CSR (rel 19) -> R2, reduce sum -> R3
        // Test B: Identity matrix on MAT50_IDENTITY_COO (rel 20) -> R4, reduce sum -> R5 (1275.0)
        // Test C: Scaled diagonal (MAT50_IDENTITY_COO.scale_f32, rel 20, attr 1: 2.0) * R4 -> R7, reduce sum -> R8 (2550.0)
        const uint32_t p_load_x     = 12U | (0U << 16);
        const uint32_t p_mxv_zero   = 1U | (19U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16);
        const uint32_t p_mxv_id     = 1U | (20U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16);
        const uint32_t p_load_scale = 20U | (1U << 16);
        const uint32_t p_ewise_mul  = 4U | (6U << 8) | (static_cast<uint32_t>(BINARY_OP_MUL) << 16);

        const impulse_instruction_t prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, p_load_x},
            {OP_MXV,                0, 2, p_mxv_zero},
            {OP_REDUCE,             0, 3, 2U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_MXV,                0, 4, p_mxv_id},
            {OP_REDUCE,             0, 5, 4U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_LOAD_COLUMN_VECTOR, 0, 6, p_load_scale},
            {OP_EWISE_MULT,         0, 7, p_ewise_mul},
            {OP_REDUCE,             0, 8, 7U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_HALT,               0, 8, 0}
        };
        E2E_CHECK(impulse_vm_execute(prog, sizeof(prog) / sizeof(prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "Suite 3.1 SpMV execution failed");

        double sum_zero = read_scalar_float(state, 3);
        double sum_id = read_scalar_float(state, 5);
        double sum_scaled = read_scalar_float(state, 8);
        E2E_CHECK(std::abs(sum_zero - 0.0) < 1e-6, "Zero matrix annihilator sum must be 0.0");
        E2E_CHECK(std::abs(sum_id - 1275.0) < 1e-3, "Identity SpMV sum must be 1275.0");
        E2E_CHECK(std::abs(sum_scaled - 2550.0) < 1e-3, "Scaled identity SpMV sum must be 2550.0");

        // Rectangular 100x50 (MAT_RECT_100x50_COO, rel 24) and 50x100 (MAT_RECT_50x100_COO, rel 25)
        uint64_t csc_rows_25 = 0;
        const void* d_off = nullptr;
        const void* d_tgt = nullptr;
        impulse_snapshot_get_relation_csc_raw_buffers(snap, 25, &d_off, &d_tgt, &csc_rows_25, nullptr, nullptr, nullptr);
        E2E_CHECK(csc_rows_25 == 100ULL,
                  "MAT_RECT_50x100_COO (rel 25) CSC row count must equal target domain cardinality 100, got " +
                  std::to_string(csc_rows_25));

        // Execute rectangular OP_MXV & OP_VXM directly without patching ctx->csc_node_counts:
        impulse_vm_state_t rect_state{};
        rect_state.query_context = ctx;
        int h_half50 = impulse_vm_context_acquire_float_vector(ctx);
        for (size_t i = 0; i < 50; ++i) {
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_half50), i, 0.5f);
        }
        rect_state.registers[13] = static_cast<uint64_t>(h_half50);
        rect_state.register_types[13] = TYPE_FLOAT_VECTOR;

        const impulse_instruction_t rect_prog[] = {
            // Load MatFeature50.feat_a_f32 (domain 15, attr 0: 50 floats) into R1
            {OP_LOAD_COLUMN_VECTOR, 0, 1,  15U | (0U << 16)},
            // y_100 = A_{100x50} * x_50 via OP_MXV on rel 24 -> R2, sum -> R3
            {OP_MXV,                0, 2,  1U | (24U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
            {OP_REDUCE,             0, 3,  2U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            // Load MatNet100.pr_init_f32 (domain 14, attr 0: 100 values of 0.01) into R4
            {OP_LOAD_COLUMN_VECTOR, 0, 4,  14U | (0U << 16)},
            // z_50 = B_{50x100} * p_100 via OP_MXV on rel 25 -> R5, sum -> R6 (100 edges * 0.01 = 1.0)
            {OP_MXV,                0, 5,  4U | (25U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
            {OP_REDUCE,             0, 6,  5U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            // w_50 = p_100^T * A_{100x50} via OP_VXM on rel 24 -> R7, sum -> R8 (150 edges * 0.01 = 1.5)
            {OP_VXM,                0, 7,  4U | (24U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
            {OP_REDUCE,             0, 8,  7U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            // LHS of bilinear form: p_100^T (A_{100x50} x_50) = sum(R4 * R2) -> R10
            {OP_EWISE_MULT,         0, 9,  4U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MUL) << 16)},
            {OP_REDUCE,             0, 10, 9U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            // RHS of bilinear form: (p_100^T A_{100x50}) x_50 = sum(R7 * R1) -> R12
            {OP_EWISE_MULT,         0, 11, 7U | (1U << 8) | (static_cast<uint32_t>(BINARY_OP_MUL) << 16)},
            {OP_REDUCE,             0, 12, 11U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            // y_100 = x_50^T * B_{50x100} via OP_VXM on rel 25 -> R14 (expect y_100[99] == 0.5f)
            {OP_VXM,                0, 14, 13U | (25U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
            {OP_HALT,               0, 14, 0}
        };
        E2E_CHECK(impulse_vm_execute(rect_prog, sizeof(rect_prog) / sizeof(rect_prog[0]), &rect_state, 0) == IMPULSE_VM_OK,
                  "Suite 3.1 rectangular SpMV/vxm failed");
        double rect_mxv_24_sum = read_scalar_float(rect_state, 3);
        double rect_mxv_25_sum = read_scalar_float(rect_state, 6);
        double rect_vxm_24_sum = read_scalar_float(rect_state, 8);
        double bilinear_lhs    = read_scalar_float(rect_state, 10);
        double bilinear_rhs    = read_scalar_float(rect_state, 12);

        const float* y100_vxm = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(rect_state.registers[14]));
        float y99_val = (y100_vxm != nullptr) ? y100_vxm[99] : 0.0f;

        E2E_CHECK(rect_mxv_24_sum > 100.0, "Rectangular 100x50 OP_MXV sum must be positive");
        E2E_CHECK(std::abs(rect_mxv_25_sum - 1.0) < 1e-4,
                  "Rectangular 50x100 OP_MXV sum (100 edges * 0.01) must be 1.0, got " + std::to_string(rect_mxv_25_sum));
        E2E_CHECK(std::abs(y99_val - 0.5f) < 1e-5f,
                  "Rectangular 50x100 OP_VXM output at y[99] must be 0.5f, got " + std::to_string(y99_val));
        E2E_CHECK(std::abs(rect_vxm_24_sum - 1.5) < 1e-4,
                  "Rectangular 100x50 OP_VXM sum (150 edges * 0.01) must be 1.5, got " + std::to_string(rect_vxm_24_sum));
        E2E_CHECK(std::abs(bilinear_lhs - bilinear_rhs) < 1e-3,
                  "Rectangular 100x50 OP_MXV / OP_VXM bilinear duality p^T (A x) == (p^T A) x must hold: lhs=" +
                  std::to_string(bilinear_lhs) + ", rhs=" + std::to_string(bilinear_rhs));

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.1 Semiring SpMV (OP_MXV & OP_VXM): zero=" << sum_zero
                  << ", id=" << sum_id << ", scaled_id=" << sum_scaled
                  << ", rect_50x100=" << rect_mxv_25_sum << ", rect_vxm_100x50=" << rect_vxm_24_sum << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.2 Tropical Min-Plus (min, +) & Max-Min (max, min) Path Algebra
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        // MAT10_UPPER_TRI_CSR (rel 18, 10 nodes, 25 edges):
        // Edge (0, 9) has physical weight w_f32 = 100.0 (rel 18, attr 0),
        // while 3-hop path 0 -> 3 -> 6 -> 9 has weights 1.0 + 1.0 + 1.0 = 3.0!
        // Part A: Test 4-step Tropical Min-Plus via OP_VXM(SEMIRING_MIN_PLUS) directly, expecting weighted d(0->9) == 3.0f:
        int h_dist_vxm = impulse_vm_context_acquire_float_vector(ctx);
        for (size_t i = 0; i < 10; ++i) {
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_dist_vxm), i, (i == 0) ? 0.0f : 1e9f);
        }
        state.registers[1] = static_cast<uint64_t>(h_dist_vxm);
        state.register_types[1] = TYPE_FLOAT_VECTOR;

        const uint32_t p_vxm_minplus = 1U | (18U << 8) | (static_cast<uint32_t>(SEMIRING_MIN_PLUS) << 16);
        const impulse_instruction_t minplus_vxm_prog[] = {
            {OP_VXM,       0, 2, p_vxm_minplus},
            {OP_EWISE_ADD, 0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MIN) << 16)},
            {OP_VXM,       0, 2, p_vxm_minplus},
            {OP_EWISE_ADD, 0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MIN) << 16)},
            {OP_VXM,       0, 2, p_vxm_minplus},
            {OP_EWISE_ADD, 0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MIN) << 16)},
            {OP_VXM,       0, 2, p_vxm_minplus},
            {OP_EWISE_ADD, 0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MIN) << 16)},
            {OP_HALT,      0, 1, 0}
        };
        E2E_CHECK(impulse_vm_execute(minplus_vxm_prog, sizeof(minplus_vxm_prog) / sizeof(minplus_vxm_prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "OP_VXM(SEMIRING_MIN_PLUS) execution failed");
        const float* d_vxm = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[1]));
        E2E_CHECK(d_vxm != nullptr && std::abs(d_vxm[9] - 3.0f) < 1e-5f,
                  "OP_VXM(SEMIRING_MIN_PLUS) on MAT10_UPPER_TRI_CSR must use edge weight w_f32 and find d(0->9) == 3.0f, got " +
                  std::to_string(d_vxm ? d_vxm[9] : -1.0f));

        // Part B: 4-step Bellman-Ford shortest path on ImpulseVM using OP_CSR_WALK_STREAM
        impulse_vm_state_t load_w_state{};
        load_w_state.query_context = ctx;
        const impulse_instruction_t load_w_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 10, 18U | (0U << 16)},
            {OP_HALT,               0, 10, 0}
        };
        E2E_CHECK(impulse_vm_execute(load_w_prog, 2, &load_w_state, 0) == IMPULSE_VM_OK, "Load w_f32 failed");
        uint32_t h_edge_w = static_cast<uint32_t>(load_w_state.registers[10]);

        int h_all10 = impulse_vm_context_acquire_bitset(ctx);
        for (uint32_t u = 0; u < 10; ++u) impulse_vm_context_bitset_add(ctx, static_cast<size_t>(h_all10), u);

        int h_dist = impulse_vm_context_acquire_float_vector(ctx);
        for (size_t i = 0; i < 10; ++i) {
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_dist), i, (i == 0) ? 0.0f : 10000.0f);
        }
        uint32_t h_cur_d = static_cast<uint32_t>(h_dist);

        for (int step = 0; step < 4; ++step) {
            impulse_vm_state_t bf_state{};
            bf_state.query_context = ctx;
            bf_state.registers[0] = static_cast<uint64_t>(h_all10);
            bf_state.register_types[0] = TYPE_BITSET_HANDLE;
            bf_state.registers[1] = static_cast<uint64_t>(h_cur_d);
            bf_state.register_types[1] = TYPE_FLOAT_VECTOR;

            int h_step_out = impulse_vm_context_acquire_float_vector(ctx);
            for (size_t i = 0; i < 10; ++i) {
                impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_step_out), i, 10000.0f);
            }
            bf_state.registers[2] = static_cast<uint64_t>(h_step_out);
            bf_state.register_types[2] = TYPE_FLOAT_VECTOR;

            const impulse_instruction_t bf_prog[] = {
                {OP_CSR_WALK_STREAM,       1, 3, 0U | (18U << 16)},
                {OP_STREAM_FUNC_BEGIN,     0, 0, 0},
                {OP_STREAM_LOAD_SRC,       0, 0, h_cur_d},
                {OP_STREAM_LOAD_EDGE,      0, 1, h_edge_w},
                {OP_STREAM_MATH_ADD,       0, 2, 0U | (1U << 16)},
                {OP_STREAM_SCATTER_REDUCE, 0, 2, static_cast<uint32_t>(h_step_out) | (2U << 16)},
                {OP_STREAM_FUNC_END,       0, 0, 0},
                {OP_EWISE_ADD,             0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MIN) << 16)},
                {OP_HALT,                  0, 1, 0}
            };
            E2E_CHECK(impulse_vm_execute(bf_prog, sizeof(bf_prog) / sizeof(bf_prog[0]), &bf_state, 0) == IMPULSE_VM_OK,
                      "Weighted Bellman-Ford step failed");
            h_cur_d = static_cast<uint32_t>(bf_state.registers[1]);
        }

        const float* final_d = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(h_cur_d));
        const float expected_d[10] = {0.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 3.0f, 3.0f, 3.0f};
        for (int i = 0; i < 10; ++i) {
            E2E_CHECK(final_d != nullptr && std::abs(final_d[i] - expected_d[i]) < 1e-5f,
                      "Tropical shortest path mismatch at node " + std::to_string(i));
        }

        // Verify SEMIRING_MAX_MIN (bottleneck / capacity algebra) and BINARY_OP_MAX on MatVec10.x_f32 & y_f32
        impulse_vm_state_t mm_state{};
        mm_state.query_context = ctx;
        const impulse_instruction_t mm_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 11U | (0U << 16)}, // MatVec10.x_f32: [1..10] (domain 11, attr 0)
            {OP_LOAD_COLUMN_VECTOR, 0, 2, 11U | (1U << 16)}, // MatVec10.y_f32: [0.5..9.5] (domain 11, attr 1)
            {OP_MXV,                0, 3, 1U | (18U << 8) | (static_cast<uint32_t>(SEMIRING_MAX_MIN) << 16)},
            {OP_EWISE_ADD,          0, 4, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MAX) << 16)},
            {OP_REDUCE,             0, 5, 4U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_HALT,               0, 5, 0}
        };
        E2E_CHECK(impulse_vm_execute(mm_prog, sizeof(mm_prog) / sizeof(mm_prog[0]), &mm_state, 0) == IMPULSE_VM_OK,
                  "SEMIRING_MAX_MIN execution failed");
        double max_ewise_sum = read_scalar_float(mm_state, 5);
        E2E_CHECK(std::abs(max_ewise_sum - 55.0) < 1e-4, "max(x_f32, y_f32) sum must be 55.0");

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.2 Tropical Min-Plus & Max-Min Path Algebra: d_vxm(0->9)=" << (d_vxm ? d_vxm[9] : -1.0f)
                  << ", d_stream(0->9)=" << (final_d ? final_d[9] : -1.0f) << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.3 1D Ring Graph Laplacian & Explicit Euler Heat Diffusion PDE
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        // MAT50_LAPLACIAN_CSR (rel 21) is the adjacency matrix A of the 2-regular 50-node ring C_50.
        const impulse_instruction_t nullspace_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 12U | (1U << 16)}, // R1 = MatGrid50.y_f32 = [2.0, ..., 2.0]
            {OP_MXV,                0, 2, 1U | (21U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)}, // R2 = A * f = [4.0, ..., 4.0]
            {OP_FLOAT_VECTOR_SCALE, 0, 1, float_to_bits(2.0f)}, // R1 = 2 * f = [4.0, ..., 4.0]
            {OP_L1_NORM_DIFF,       0, 3, 1U | (2U << 16)},     // R3 = ||2f - Af||_1 == 0.0
            {OP_HALT,               0, 3, 0}
        };
        E2E_CHECK(impulse_vm_execute(nullspace_prog, sizeof(nullspace_prog) / sizeof(nullspace_prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "Laplacian nullspace program failed");
        double nullspace_l1 = read_scalar_float(state, 3);
        E2E_CHECK(std::abs(nullspace_l1) < 1e-5, "Constant vector must be in the nullspace of Ring Laplacian L = D - A");

        // 5-step Explicit Euler Heat Diffusion PDE starting from MatGrid50.heat0_f32 (domain 12, attr 4):
        impulse_vm_state_t heat_state{};
        heat_state.query_context = ctx;
        const impulse_instruction_t init_heat[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 12U | (4U << 16)}, // R1 = heat0_f32
            {OP_HALT,               0, 1, 0}
        };
        E2E_CHECK(impulse_vm_execute(init_heat, 2, &heat_state, 0) == IMPULSE_VM_OK, "Heat init failed");

        double prev_peak = 100.0;
        for (int t = 1; t <= 5; ++t) {
            const impulse_instruction_t step_prog[] = {
                {OP_MXV,                0, 2, 1U | (21U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
                {OP_FLOAT_VECTOR_SCALE, 0, 2, float_to_bits(0.2f)},
                {OP_FLOAT_VECTOR_SCALE, 0, 1, float_to_bits(0.6f)},
                {OP_EWISE_ADD,          0, 1, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
                {OP_REDUCE,             0, 3, 1U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
                {OP_REDUCE,             0, 4, 1U | (static_cast<uint32_t>(BINARY_OP_MAX) << 16)},
                {OP_HALT,               0, 3, 0}
            };
            heat_state.pc = 0;
            E2E_CHECK(impulse_vm_execute(step_prog, sizeof(step_prog) / sizeof(step_prog[0]), &heat_state, 0) == IMPULSE_VM_OK,
                      "Heat diffusion step failed");
            double total_heat = read_scalar_float(heat_state, 3);
            double peak_heat = read_scalar_float(heat_state, 4);
            E2E_CHECK(std::abs(total_heat - 100.0) < 1e-3,
                      "Total heat must be conserved at 100.0 at step " + std::to_string(t) + ", got " + std::to_string(total_heat));
            E2E_CHECK(peak_heat < prev_peak,
                      "Peak temperature at node 0 must decrease monotonically at step " + std::to_string(t));
            prev_peak = peak_heat;
        }
        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.3 1D Ring Laplacian & Heat Diffusion PDE: ||Lf||_1=" << nullspace_l1
                  << ", conserved_heat=100.0, u_0^(5)=" << prev_peak << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.4 Markov Power Iteration / PageRank with OP_L1_NORM_DIFF
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        const impulse_instruction_t init_pr[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1,  14U | (0U << 16)}, // R1 = p^{(0)} (0.01)
            {OP_LOAD_COLUMN_VECTOR, 0, 3,  14U | (0U << 16)}, // R3 = scratch float vector handle (distinct from R1)
            {OP_LOAD_COLUMN_VECTOR, 0, 5,  14U | (0U << 16)}, // R5 = p^{(0)}
            {OP_FLOAT_VECTOR_SCALE, 0, 5,  float_to_bits(0.15f)}, // R5 = 0.15 * p^{(0)}
            {OP_LOAD_COLUMN_VECTOR, 0, 10, 14U | (0U << 16)}, // R10 = 0.01
            {OP_FLOAT_VECTOR_SCALE, 0, 10, float_to_bits(100.0f)}, // R10 = 1_100 = [1.0, ..., 1.0]
            {OP_MXV,                0, 8,  10U | (23U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)}, // R8 = A * 1_100 = d_out
            {OP_HALT,               0, 1, 0}
        };
        E2E_CHECK(impulse_vm_execute(init_pr, sizeof(init_pr) / sizeof(init_pr[0]), &state, 0) == IMPULSE_VM_OK,
                  "PageRank init failed");

        double first_l1_diff = 0.0;
        double last_l1_diff = 0.0;
        double final_pr_sum = 0.0;
        int64_t top_node = -1;
        for (int iter = 0; iter < 5; ++iter) {
            const impulse_instruction_t pr_step[] = {
                {OP_VECTOR_DIV,         0, 9, 1U | (8U << 16)},
                {OP_VXM,                0, 2, 9U | (23U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
                {OP_FLOAT_VECTOR_SCALE, 0, 2, float_to_bits(0.85f)},
                {OP_EWISE_ADD,          0, 3, 2U | (5U << 8) | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
                {OP_L1_NORM_DIFF,       0, 4, 3U | (1U << 16)},
                {OP_SWAP_REG,           0, 1, 3U},
                {OP_REDUCE,             0, 6, 1U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
                {OP_REDUCE,             0, 7, 1U | (static_cast<uint32_t>(BINARY_OP_ARGMAX) << 16)},
                {OP_HALT,               0, 6, 0}
            };
            state.pc = 0;
            E2E_CHECK(impulse_vm_execute(pr_step, sizeof(pr_step) / sizeof(pr_step[0]), &state, 0) == IMPULSE_VM_OK,
                      "PageRank iteration failed");
            double cur_l1 = read_scalar_float(state, 4);
            if (iter == 0) first_l1_diff = cur_l1;
            last_l1_diff = cur_l1;
            final_pr_sum = read_scalar_float(state, 6);
            top_node = static_cast<int64_t>(state.registers[7]);
            E2E_CHECK(std::abs(final_pr_sum - 1.0) < 1e-4,
                      "PageRank probability mass must sum to 1.0, got " + std::to_string(final_pr_sum));
        }
        E2E_CHECK(first_l1_diff > last_l1_diff && last_l1_diff > 0.0,
                  "PageRank L1 norm diff must contract across iterations");
        E2E_CHECK(top_node >= 0 && top_node < 100, "PageRank ARGMAX node out of bounds");
        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.4 Markov Power Iteration / PageRank: sum=" << final_pr_sum
                  << ", l1_diff(1->5)=" << first_l1_diff << "->" << last_l1_diff
                  << ", argmax_node=" << top_node << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.5 GNN Graph Convolution + Neural Activations (OP_VEC_MATH_*)
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        const impulse_instruction_t gnn_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 12U | (0U << 16)}, // R1 = MatGrid50.x_f32
            {OP_LOAD_COLUMN_VECTOR, 0, 2, 12U | (3U << 16)}, // R2 = MatGrid50.inv_sqrt_deg_f32 (1/sqrt(2))
            {OP_LOAD_COLUMN_VECTOR, 0, 3, 12U | (2U << 16)}, // R3 = MatGrid50.bias_f32 (0.25)
            {OP_EWISE_MULT,         0, 4, 1U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MUL) << 16)},
            {OP_MXV,                0, 5, 4U | (21U << 8) | (static_cast<uint32_t>(SEMIRING_PLUS_TIMES) << 16)},
            {OP_EWISE_MULT,         0, 6, 5U | (2U << 8) | (static_cast<uint32_t>(BINARY_OP_MUL) << 16)},
            {OP_EWISE_ADD,          0, 7, 6U | (3U << 8) | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_ASSERT_FINITE,      0, 7, 0},
            {OP_VEC_MATH_UNARY,     0, 8,  7U | (static_cast<uint32_t>(MATH_FUNC_RELU) << 8)},
            {OP_VEC_MATH_UNARY,     0, 9,  7U | (static_cast<uint32_t>(MATH_FUNC_GELU) << 8)},
            {OP_VEC_MATH_UNARY,     0, 10, 7U | (static_cast<uint32_t>(MATH_FUNC_SIGMOID) << 8)},
            {OP_VEC_MATH_UNARY,     0, 11, 7U | (static_cast<uint32_t>(MATH_FUNC_SILU) << 8)},
            {OP_VEC_MATH_BINARY,    0, 12, 6U | (3U << 8) | (static_cast<uint32_t>(MATH_FUNC_HYPOT) << 16)},
            {OP_VEC_MATH_TERNARY,   0, 13, 7U | (3U << 8) | (1U << 16) | (static_cast<uint32_t>(MATH_FUNC_CLAMP) << 24)},
            {OP_ASSERT_FINITE,      0, 8,  0},
            {OP_ASSERT_FINITE,      0, 9,  0},
            {OP_ASSERT_FINITE,      0, 10, 0},
            {OP_ASSERT_FINITE,      0, 11, 0},
            {OP_ASSERT_FINITE,      0, 12, 0},
            {OP_ASSERT_FINITE,      0, 13, 0},
            {OP_HALT,               0, 13, 0}
        };
        E2E_CHECK(impulse_vm_execute(gnn_prog, sizeof(gnn_prog) / sizeof(gnn_prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "GNN convolution + activations program failed");

        const float* z_vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[7]));
        const float* relu_vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[8]));
        const float* sig_vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[10]));
        const float* silu_vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[11]));
        const float* clamp_vec = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(state.registers[13]));

        E2E_CHECK(z_vec != nullptr && std::abs(z_vec[10] - 11.25f) < 1e-4f, "GNN z[10] must be 11.25");
        E2E_CHECK(clamp_vec != nullptr && std::abs(clamp_vec[10] - 11.0f) < 1e-5f, "CLAMP(z[10]=11.25, 0.25, x[10]=11.0) must be 11.0");
        E2E_CHECK(relu_vec != nullptr && std::abs(relu_vec[10] - 11.25f) < 1e-4f, "RELU(11.25) must be 11.25");
        E2E_CHECK(sig_vec != nullptr && sig_vec[10] > 0.999f && sig_vec[10] <= 1.0f, "SIGMOID(11.25) must be ~1.0");
        E2E_CHECK(silu_vec != nullptr && z_vec != nullptr && sig_vec != nullptr &&
                  std::abs(silu_vec[10] - (z_vec[10] * sig_vec[10])) < 1e-4f, "SILU(z) must equal z * SIGMOID(z)");

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.5 GNN Convolution & Activations: z[10]=" << (z_vec ? z_vec[10] : 0.0f)
                  << ", clamp[10]=" << (clamp_vec ? clamp_vec[10] : 0.0f)
                  << ", silu[10]=" << (silu_vec ? silu_vec[10] : 0.0f) << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.6 Null-Aware Vector Math (OP_EXTRACT_VALIDITY & OP_COALESCE)
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t coal_state{};
        coal_state.query_context = ctx;

        int h_fallback = impulse_vm_context_acquire_float_vector(ctx);
        for (size_t i = 0; i < 50; ++i) {
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_fallback), i, -1.0f);
        }
        coal_state.registers[2] = static_cast<uint64_t>(h_fallback);
        coal_state.register_types[2] = TYPE_FLOAT_VECTOR;

        // Load MatGrid50.opt_obs_f32 (domain 12, attr 7, type_code 0x86) directly via OP_LOAD_COLUMN_VECTOR into R1
        // without manual data_ptr + 128 offset reconstruction, then run OP_EXTRACT_VALIDITY and OP_COALESCE:
        const impulse_instruction_t coal_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 12U | (7U << 16)}, // R1 = MatGrid50.opt_obs_f32 (40 valid, 10 NULLs)
            {OP_EXTRACT_VALIDITY,   0, 3, 1U},               // R3 = bitset of valid entries in R1
            {OP_SET_CARDINALITY,    0, 4, 3U},               // R4 = count of valid entries (expected 40)
            {OP_COALESCE,           0, 5, 1U | (2U << 16)},  // R5 = coalesce(R1, R2)
            {OP_ASSERT_FINITE,      0, 5, 0U},               // Verify all NaNs eliminated
            {OP_HALT,               0, 5, 0U}
        };
        E2E_CHECK(impulse_vm_execute(coal_prog, sizeof(coal_prog) / sizeof(coal_prog[0]), &coal_state, 0) == IMPULSE_VM_OK,
                  "OP_LOAD_COLUMN_VECTOR + OP_EXTRACT_VALIDITY + OP_COALESCE failed");
        E2E_CHECK(coal_state.registers[4] == 40ULL,
                  "Expected 40 valid entries and 10 nulls in MatGrid50.opt_obs_f32, got " +
                  std::to_string(coal_state.registers[4]));
        const float* coalesced = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(coal_state.registers[5]));
        if (coalesced != nullptr) {
            for (int i = 0; i < 50; ++i) {
                if (i % 5 == 4) {
                    E2E_CHECK(std::abs(coalesced[i] - (-1.0f)) < 1e-6f,
                              "NULL slot " + std::to_string(i) + " must be coalesced to -1.0f, got " + std::to_string(coalesced[i]));
                } else {
                    E2E_CHECK(std::abs(coalesced[i] - (i * 1.5f)) < 1e-5f,
                              "Valid slot " + std::to_string(i) + " must preserve " + std::to_string(i * 1.5f) +
                              ", got " + std::to_string(coalesced[i]));
                }
            }
        }
        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.6 Null-Aware Vector Math (OP_EXTRACT_VALIDITY & OP_COALESCE): valid="
                  << coal_state.registers[4] << "/50\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.7 64x64 Boolean Permutation & 64-Lane Bit-Sliced SpMM (MAT64_PERM_BOOL_CSR, rel 22)
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        const uint64_t seeds[3] = {0, 1, 63};
        int h_seeds = impulse_vm_context_acquire_node_vector(ctx);
        impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_seeds), seeds, 3);
        state.registers[0] = static_cast<uint64_t>(h_seeds);
        state.register_types[0] = TYPE_NODE_VECTOR;

        const impulse_instruction_t perm_prog[] = {
            {OP_LANE_INIT_SEEDS,    0, 1, 0U},
            {OP_LANE_CSR_WALK,      0, 2, 1U | (22U << 16)},
            {OP_LANE_FILTER_QUORUM, 0, 3, 2U | (1U << 16)},
            {OP_LOAD_COLUMN_VECTOR, 0, 4, 13U | (2U << 16)},
            {OP_MXV,                0, 5, 4U | (22U << 8) | (static_cast<uint32_t>(SEMIRING_BOOL) << 16)},
            {OP_REDUCE,             0, 6, 5U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_HALT,               0, 3, 0}
        };
        E2E_CHECK(impulse_vm_execute(perm_prog, sizeof(perm_prog) / sizeof(perm_prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "64x64 Boolean Permutation & 64-Lane SpMM failed");

        auto reached = extract_active_nodes(ctx, state, 3, 64);
        std::vector<uint32_t> expected_perm = {0, 1, 2};
        E2E_CHECK(reached == expected_perm,
                  "Cyclic permutation of [0, 1, 63] must be [0, 1, 2], got " + format_u32_vec(reached));
        double bool_mxv_sum = read_scalar_float(state, 6);
        E2E_CHECK(std::abs(bool_mxv_sum - 64.0) < 1e-5, "SEMIRING_BOOL MXV over 64x64 permutation must sum to 64.0");

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.7 64x64 Boolean Permutation & 64-Lane SpMM: seeds=[0, 1, 63] -> "
                  << format_u32_vec(reached) << ", bool_mxv_sum=" << bool_mxv_sum << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // ------------------------------------------------------------------------
    // 3.8 Structural Graph Analytics (OP_CC_AFFOREST & OP_KCORE_DECOMPOSITION)
    // ------------------------------------------------------------------------
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        const impulse_instruction_t cc_prog[] = {
            {OP_CC_AFFOREST, 0, 1, 23U},
            {OP_HALT,        0, 1, 0}
        };
        E2E_CHECK(impulse_vm_execute(cc_prog, 2, &state, 0) == IMPULSE_VM_OK, "OP_CC_AFFOREST failed");
        const uint64_t* cc_labels = impulse_vm_context_get_node_vector(ctx, static_cast<size_t>(state.registers[1]));
        E2E_CHECK(cc_labels != nullptr, "OP_CC_AFFOREST must output TYPE_UINT64_VECTOR");
        std::set<uint64_t> distinct_comps;
        if (cc_labels != nullptr) {
            for (int u = 0; u < 100; ++u) {
                distinct_comps.insert(cc_labels[u]);
            }
            E2E_CHECK(distinct_comps.size() == 3,
                      "Expected 3 connected components on MAT100_STOCHASTIC_CSR, got " + std::to_string(distinct_comps.size()));
            for (int u = 1; u < 40; ++u) E2E_CHECK(cc_labels[u] == cc_labels[0], "Component 1 {0..39} label mismatch");
            for (int u = 41; u < 75; ++u) E2E_CHECK(cc_labels[u] == cc_labels[40], "Component 2 {40..74} label mismatch");
            for (int u = 76; u < 100; ++u) E2E_CHECK(cc_labels[u] == cc_labels[75], "Component 3 {75..99} label mismatch");
            E2E_CHECK(cc_labels[0] != cc_labels[40] && cc_labels[40] != cc_labels[75] && cc_labels[0] != cc_labels[75],
                      "The 3 components must have distinct labels");
        }

        impulse_vm_state_t kc_state{};
        kc_state.query_context = ctx;
        const impulse_instruction_t kc_prog[] = {
            {OP_LOAD_CONST_INT,      0, 1, 21U},
            {OP_KCORE_DECOMPOSITION, 0, 2, 1U},
            {OP_HALT,                0, 2, 0}
        };
        E2E_CHECK(impulse_vm_execute(kc_prog, 3, &kc_state, 0) == IMPULSE_VM_OK, "OP_KCORE_DECOMPOSITION failed");
        const float* kcore = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(kc_state.registers[2]));
        if (kcore != nullptr) {
            for (int u = 0; u < 50; ++u) {
                E2E_CHECK(std::abs(kcore[u] - 2.0f) < 1e-5f,
                          "Every node in 2-regular ring C_50 must have k-coreness == 2.0, got " + std::to_string(kcore[u]));
            }
        }

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "3.8 Structural Graph Analytics: Afforest components=" << distinct_comps.size()
                  << " ({0..39}, {40..74}, {75..99}), C_50 k-core=2.0\n";
        impulse_vm_context_destroy(ctx);
    }
}

// ============================================================================
// SUITE 4: Complex CEL Filter & State Projection Expressions
// ============================================================================
void run_suite_4_cel_filters_and_projections(const impulse_snapshot_t* snap) {
    std::cout << "\n=========================================================================\n";
    std::cout << "SUITE 4: Complex CEL Filter & State Projection Expressions\n";
    std::cout << "=========================================================================\n";

    // 4.1 Verify CEL Parser, AST Optimizer & ImpScheme IR Lowering on Sensor, User, and Order expressions
    {
        int failures_before = g_failed_assertions;
        const std::string sensor_cel =
            "(src.voltage_f64 >= 120.0 && abs(src.phase_angle_f64) <= 0.06) ? "
            "safeDiv(src.voltage_f64 - tgt.voltage_f64, edge.impedance_f64, 0.0) * sqrt(16.0) : 0.0";
        impulse::cel::Parser parser(sensor_cel);
        auto ast = parser.parse_expression();
        E2E_CHECK(ast != nullptr, "Failed to parse Sensor CEL expression");
        auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
        std::string scm_ir = impulse::cel::CelCompiler::to_impscheme(opt_ast);
        E2E_CHECK(scm_ir.find("4.0") != std::string::npos && scm_ir.find("vec-blend") != std::string::npos,
                  "CEL optimizer must constant-fold sqrt(16.0) -> 4.0 and emit vec-blend");
        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "4.1 CEL Parser & Constant-Folding Optimizer: " << scm_ir << "\n";
    }

    // 4.2 High-Level Traversal::filter & project and Direct Vectorized Float64 CEL Filters in ImpulseVM
    {
        int failures_before = g_failed_assertions;

        // Part A: High-level SnapshotView::domain("User").all().filter(...) & .project(...)
        impulse::vm::SnapshotView view(snap);
        auto all_users = view.domain("User").all().collect();
        E2E_CHECK(all_users.size() == 16U,
                  "view.domain(\"User\").all().collect() must return all 16 users, got " + std::to_string(all_users.size()));
        auto filtered_users = view.domain("User").all().filter("score_f64 > 80.0").collect();
        std::vector<uint64_t> expected_filtered_u64 = {10, 11, 12, 13, 14, 15};
        E2E_CHECK(filtered_users == expected_filtered_u64,
                  "view.domain(\"User\").all().filter(\"score_f64 > 80.0\").collect() must return {10..15}");
        auto projected_users = view.domain("User").all().filter("score_f64 > 80.0").project("score_f64 * 2.0").collect();
        E2E_CHECK(projected_users == expected_filtered_u64,
                  "view.domain(\"User\").all().filter(...).project(...).collect() must return {10..15}");

        // Part B: Direct OP_LOAD_COLUMN_VECTOR on User.score_f64 (rel 0, attr 4, type_code == 7)
        // with TYPE_DOUBLE scalar thresholds (70.0, 90.0) without TYPE_INT64 or manual double_vector_set workaround:
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t user_state{};
        user_state.query_context = ctx;
        user_state.registers[2] = std::bit_cast<uint64_t>(70.0);
        user_state.register_types[2] = TYPE_DOUBLE;
        user_state.registers[3] = std::bit_cast<uint64_t>(90.0);
        user_state.register_types[3] = TYPE_DOUBLE;

        const impulse_instruction_t user_cel_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 0U | (2U << 16)}, // R1 = User.score_f64 (domain 0, attr 2, type_code 7 -> expect TYPE_DOUBLE_VECTOR)
            {OP_VEC_CMP_GT,         0, 4, 1U | (2U << 8)},  // R4 = score_f64 > 70.0
            {OP_VEC_CMP_LT,         0, 5, 1U | (3U << 8)},  // R5 = score_f64 < 90.0
            {OP_MASK_AND,           0, 6, 4U | (5U << 8)},  // R6 = R4 && R5
            {OP_HALT,               0, 6, 0}
        };
        impulse_vm_status_t user_st = impulse_vm_execute(
            user_cel_prog, sizeof(user_cel_prog) / sizeof(user_cel_prog[0]), &user_state, 0
        );
        E2E_CHECK(user_st == IMPULSE_VM_OK,
                  "Direct OP_LOAD_COLUMN_VECTOR + OP_VEC_CMP_*(TYPE_DOUBLE) on User.score_f64 failed with status " +
                  std::to_string(static_cast<int>(user_st)));
        E2E_CHECK(user_state.register_types[1] == TYPE_DOUBLE_VECTOR,
                  "OP_LOAD_COLUMN_VECTOR on float64 attribute User.score_f64 (type_code 7) must produce TYPE_DOUBLE_VECTOR");
        auto matched_users = extract_active_nodes(ctx, user_state, 6, 16);
        std::vector<uint32_t> expected_users = {7, 8, 9, 10, 11, 12};
        E2E_CHECK(matched_users == expected_users,
                  "User CEL filter (70.0 < score_f64 < 90.0) mismatch: expected [7, 8, 9, 10, 11, 12], got " +
                  format_u32_vec(matched_users));

        // Part C: Direct OP_LOAD_COLUMN_VECTOR on Sensor.voltage_f64 (rel 16, attr 2, type_code == 7)
        // with TYPE_DOUBLE threshold 119.0 and Sensor.reading_f32 < 60.0f:
        impulse_vm_state_t sensor_state{};
        sensor_state.query_context = ctx;
        sensor_state.registers[3] = std::bit_cast<uint64_t>(119.0);
        sensor_state.register_types[3] = TYPE_DOUBLE;

        const impulse_instruction_t sensor_cel_prog[] = {
            {OP_LOAD_COLUMN_VECTOR, 0, 1, 9U | (0U << 16)}, // R1 = Sensor.voltage_f64 (float64, domain 9, attr 0)
            {OP_LOAD_COLUMN_VECTOR, 0, 2, 9U | (2U << 16)}, // R2 = Sensor.reading_f32 (float32, domain 9, attr 2)
            {OP_LOAD_CONST_FLOAT,   0, 4, float_to_bits(60.0f)},
            {OP_VEC_CMP_GT,         0, 5, 1U | (3U << 8)},   // R5 = voltage_f64 > 119.0
            {OP_VEC_CMP_LT,         0, 6, 2U | (4U << 8)},   // R6 = reading_f32 < 60.0
            {OP_MASK_AND,           0, 7, 5U | (6U << 8)},   // R7 = R5 && R6 -> [4, 5, 6, 7]
            {OP_HALT,               0, 7, 0}
        };
        impulse_vm_status_t sensor_st = impulse_vm_execute(
            sensor_cel_prog, sizeof(sensor_cel_prog) / sizeof(sensor_cel_prog[0]), &sensor_state, 0
        );
        E2E_CHECK(sensor_st == IMPULSE_VM_OK,
                  "Direct OP_LOAD_COLUMN_VECTOR + OP_VEC_CMP_*(TYPE_DOUBLE) on Sensor.voltage_f64 failed with status " +
                  std::to_string(static_cast<int>(sensor_st)));
        E2E_CHECK(sensor_state.register_types[1] == TYPE_DOUBLE_VECTOR,
                  "OP_LOAD_COLUMN_VECTOR on float64 attribute Sensor.voltage_f64 (type_code 7) must produce TYPE_DOUBLE_VECTOR");
        auto matched_sensors = extract_active_nodes(ctx, sensor_state, 7, 10);
        std::vector<uint32_t> expected_sensors = {4, 5, 6, 7};
        E2E_CHECK(matched_sensors == expected_sensors,
                  "Sensor CEL filter mismatch: expected [4, 5, 6, 7], got " + format_u32_vec(matched_sensors));

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "4.2 Vectorized CEL Attribute Filters: User=" << format_u32_vec(matched_users)
                  << ", Sensor=" << format_u32_vec(matched_sensors) << "\n";
        impulse_vm_context_destroy(ctx);
    }

    // 4.3 Edge-Stream CEL Filter & State Projection (OP_CSR_WALK_STREAM) over SENSOR_FEEDS_SENSOR (rel 16)
    {
        int failures_before = g_failed_assertions;
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t state{};
        state.query_context = ctx;

        // Seed active frontier with matched sensors [4, 5, 6, 7] in R0.
        // Walk SENSOR_FEEDS_SENSOR (rel 16) using OP_CSR_WALK_STREAM directly referencing snapshot attribute indices
        // (attr 1 = edge.capacity_f32 in OP_STREAM_LOAD_EDGE, attr 4 = src.reading_f32 in OP_STREAM_LOAD_SRC)
        // WITHOUT preloading them into float_vectors via OP_LOAD_COLUMN_VECTOR:
        int h_seed_bs = impulse_vm_context_acquire_bitset(ctx);
        for (uint32_t u : {4U, 5U, 6U, 7U}) {
            impulse_vm_context_bitset_add(ctx, static_cast<size_t>(h_seed_bs), u);
        }
        state.registers[0] = static_cast<uint64_t>(h_seed_bs);
        state.register_types[0] = TYPE_BITSET_HANDLE;
        state.pc = 0;

        const impulse_instruction_t stream_cel_prog[] = {
            {OP_CSR_WALK_STREAM,   1, 1, 0U | (16U << 16)},
            {OP_STREAM_FUNC_BEGIN, 0, 0, 0},
            {OP_STREAM_LOAD_EDGE,  0, 0, 1U},                    // S0 = edge.capacity_f32 (rel 16, attr 1)
            {OP_STREAM_LOAD_CONST, 0, 1, float_to_bits(155.0f)}, // S1 = 155.0f
            {OP_STREAM_CMP_GT,     0, 2, 0U | (1U << 16)},       // S2 = (S0 > S1)
            {OP_STREAM_FILTER,     0, 2, 0},                     // abort if S2 == 0.0f
            {OP_STREAM_LOAD_SRC,   0, 3, 2U},                    // S3 = src.reading_f32 (Sensor domain 9, attr 2)
            {OP_STREAM_MATH_MUL,   0, 4, 0U | (3U << 16)},       // S4 = S0 * S3
            {OP_STREAM_REDUCE,     0, 4, 2U | (0U << 16)},       // R2[tgt] += S4 (monoid 0 = SUM)
            {OP_STREAM_YIELD,      0, 1, 0},
            {OP_STREAM_FUNC_END,   0, 0, 0},
            {OP_REDUCE,            0, 3, 2U | (static_cast<uint32_t>(BINARY_OP_ADD) << 16)},
            {OP_HALT,              0, 3, 0}
        };
        E2E_CHECK(impulse_vm_execute(stream_cel_prog, sizeof(stream_cel_prog) / sizeof(stream_cel_prog[0]), &state, 0) == IMPULSE_VM_OK,
                  "Edge-stream CEL filter & state projection failed");

        auto yielded_sensors = extract_active_nodes(ctx, state, 1, 10);
        std::vector<uint32_t> expected_yielded = {6, 7, 8};
        E2E_CHECK(yielded_sensors == expected_yielded,
                  "Stream CEL filter yielded wrong sensors: expected [6, 7, 8], got " + format_u32_vec(yielded_sensors));

        double projected_sum = read_scalar_float(state, 3);
        E2E_CHECK(std::abs(projected_sum - 45340.0) < 1e-2,
                  "Stream CEL state projection sum mismatch: expected 45340.0, got " + std::to_string(projected_sum));

        std::cout << ((g_failed_assertions == failures_before) ? "  [PASS] " : "  [FAIL] ")
                  << "4.3 Edge-Stream CEL Filter & State Projection (OP_CSR_WALK_STREAM): targets="
                  << format_u32_vec(yielded_sensors) << ", projected_sum=" << projected_sum << "\n";
        impulse_vm_context_destroy(ctx);
    }
}

} // anonymous namespace

int main() {
    std::cout << "=========================================================================\n";
    std::cout << "   IMPULSE GRAPH ENGINE - C++20 CORE E2E CSV-TO-QUERY & GRAPHBLAS SUITE  \n";
    std::cout << "=========================================================================\n";

    fs::path suite_dir = locate_e2e_suite_dir();
    fs::path imps_path = fs::temp_directory_path() / "e2e_core_test.imps";
    std::cout << "[Config] E2E Fixture Suite Dir : " << suite_dir.string() << "\n";
    std::cout << "[Config] Snapshot Output Path  : " << imps_path.string() << "\n";

    impulse_snapshot_t* snap = nullptr;
    run_suite_1_snapshot_verification(suite_dir, imps_path, &snap);
    if (!snap) {
        return 1;
    }

    GraphCatalog catalog = build_e2e_catalog(snap);
    run_suite_2_query_corpus(suite_dir, snap, catalog);
    run_suite_3_graphblas_vm(snap);
    run_suite_4_cel_filters_and_projections(snap);

    impulse_snapshot_close(snap);
    std::error_code ec;
    fs::remove(imps_path, ec);

    std::cout << "\n=========================================================================\n";
    std::cout << "E2E SUITE EXECUTION SUMMARY (STRICT TDD MODE - ZERO SHIMS)\n";
    std::cout << "=========================================================================\n";
    std::cout << "  Total Assertions  : " << g_total_assertions << "\n";
    std::cout << "  Passed Assertions : " << g_passed_assertions << "\n";
    std::cout << "  Failed Assertions : " << g_failed_assertions << "\n";
    std::cout << "=========================================================================\n";
    return (g_failed_assertions == 0) ? 0 : 1;
}
