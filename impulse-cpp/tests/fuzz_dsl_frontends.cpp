/**
 * @file fuzz_dsl_frontends.cpp
 * @brief Fuzz target: in-kernel v0.11 compiler front ends (ImpScheme / ImpK / ImpLog / Cypher / CEL)
 *        through impulse_compile_to_impas() and impulse_stmt_prepare().
 *
 * Invariant: no crash/UB; failure must surface as a non-OK status.
 */

#include "fuzz_snapshot_util.hpp"
#include "impulse_vm.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static impulse_snapshot_t* g_snap = impfuzz::open_baseline_snapshot(1, "dslsnap");
    if (!data) return 0;
    std::string script(reinterpret_cast<const char*>(data), size > 4096 ? 4096 : size);
    char impas[16384];
    static const int only_lang = [] {
        const char* e = std::getenv("IMPULSE_FUZZ_DSL_LANG");
        return e ? std::atoi(e) : -1;
    }();
    for (int lang = IMPULSE_LANG_IMPSCM; lang <= IMPULSE_LANG_CEL; ++lang) {
        if (only_lang >= 0 && lang != only_lang) continue;
        for (int use_snap = 0; use_snap < 2; ++use_snap) {
            const impulse_snapshot_t* snap = use_snap ? g_snap : nullptr;
            if (use_snap && !snap) continue;
            size_t written = 0;
            (void)impulse_compile_to_impas(
                snap, script.c_str(), static_cast<impulse_language_t>(lang),
                impas, sizeof(impas), &written);
        }
    }
    if (g_snap) {
        impulse_stmt_t* stmt = nullptr;
        if (impulse_stmt_prepare(g_snap, script.c_str(), &stmt) == IMPULSE_OK && stmt) {
            impulse_stmt_finalize(stmt);
        }
    }
    return 0;
}

static std::vector<impfuzz::Bytes> make_seeds() {
    static const char* kSeeds[] = {
        "(pipeline (vector-ann-search 2 4 0 1) (lane-walk-csr INTERACTS_GiG) (lane-filter-quorum 2))",
        "(pipeline (lane-prune-mask 2 0 0) (lane-prune-mask 2 65 65) (vector-rerank-dot 0 0 0))",
        "path(X, Y) :- edge(X, Z), edge(Z, Y).",
        "MATCH (d:Disease)-[:DaG]->(g:Gene) WHERE d.id = $id RETURN g",
        "age > 21 && score < 3.5",
        "(impk-pipeline (define-query q (g:walk-csr edge)))"};
    std::vector<impfuzz::Bytes> out;
    for (const char* s : kSeeds) out.emplace_back(s, s + std::strlen(s));
    return out;
}

IMPULSE_FUZZ_MAIN("fuzz_dsl_frontends", make_seeds, 512)
