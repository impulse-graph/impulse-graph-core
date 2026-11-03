/**
 * @file fuzz_dsl_frontends.cpp
 * @brief Fuzz target: in-kernel compiler front ends (ImpScheme S-expr / ImpK / ImpLog Datalog / Cypher / CEL)
 *        through impulse_compile_query() and impulse_compile_to_impas() (ImpAsm text emission).
 *
 * NOTE (what is and is not covered): core C++ has NO .impas *parser*; ImpAsm is only produced by
 * the disassembler (`to_impas_string`, reached via impulse_compile_to_impas). The .impas text
 * parsers live in Python (impulse-graph-spec/tools) and Rust/tooling (`impulse assemble`). This
 * target therefore fuzzes every text front end that exists in core C++ plus the ImpAsm emitter.
 * Each input is fed to all five language front ends, with and without a real snapshot catalog.
 *
 * Invariant: no crash/UB; failure must surface as a non-OK status.
 */

#include "fuzz_snapshot_util.hpp"
#include "impulse_vm.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static impulse_snapshot_t* g_snap = impfuzz::open_baseline_snapshot(1, "dslsnap");
    if (!data) return 0;
    std::string script(reinterpret_cast<const char*>(data), size > 4096 ? 4096 : size);
    impulse_instruction_t instrs[256];
    char impas[16384];
    for (int lang = IMPULSE_LANG_IMPSCM; lang <= IMPULSE_LANG_CEL; ++lang) {
        for (int use_snap = 0; use_snap < 2; ++use_snap) {
            const impulse_snapshot_t* snap = use_snap ? g_snap : nullptr;
            if (use_snap && !snap) continue;
            size_t count = 0;
            impulse_status_t st = impulse_compile_query(snap, script.c_str(), static_cast<impulse_language_t>(lang),
                                                        instrs, 256, &count);
            if (st == IMPULSE_OK && count <= 256) {
                // Whatever the compiler emits must at least survive the static validator.
                (void)impulse_vm_validate(instrs, count);
            }
            size_t written = 0;
            (void)impulse_compile_to_impas(snap, script.c_str(), static_cast<impulse_language_t>(lang), impas,
                                           sizeof(impas), &written);
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
