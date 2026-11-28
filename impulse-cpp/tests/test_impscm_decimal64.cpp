#include "vm/impulse_impscm.hpp"
#include "impulse_cypher.hpp"
#include "impulse_graph.h"
#include "impulse_vm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace impulse::compiler;
using namespace impulse::compiler;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)

namespace {

struct AlignedWorkspace {
    void* buf = nullptr;
    impulse_vm_workspace_t* ws = nullptr;
    explicit AlignedWorkspace(size_t bytes = 4 * 1024 * 1024) {
        size_t aligned = ((bytes + 127) / 128) * 128;
#if defined(_MSC_VER)
        buf = _aligned_malloc(aligned, 128);
#else
        if (posix_memalign(&buf, 128, aligned) != 0) buf = nullptr;
#endif
        CHECK(buf != nullptr);
        std::memset(buf, 0, aligned);
        impulse_vm_status_t rc = impulse_workspace_init(buf, aligned, &ws);
        CHECK(rc == IMPULSE_VM_OK && ws != nullptr);
    }
    ~AlignedWorkspace() {
        if (buf) {
#if defined(_MSC_VER)
            _aligned_free(buf);
#else
            std::free(buf);
#endif
        }
    }
};

static int64_t exec_scalar_i64(
    const ImpScmCompileResult& compiled,
    const impulse_snapshot_t* snap,
    const std::vector<impulse_vm_param_binding_t>& bindings = {},
    impulse_vm_status_t expected_status = IMPULSE_VM_OK
) {
    AlignedWorkspace aw;
    impulse_vm_params_t params{};
    params.gas_limit = UINT64_MAX;
    params.bindings = bindings.empty() ? nullptr : bindings.data();
    params.binding_count = static_cast<uint32_t>(bindings.size());
    size_t out_bytes = 0;
    impulse_vm_status_t rc = impulse_vm_execute(
        compiled.impb_bytes.data(), compiled.impb_bytes.size(),
        snap, &params, aw.ws, nullptr, 0, &out_bytes);
    CHECK(rc == expected_status);
    if (expected_status != IMPULSE_VM_OK) return 0;
    impulse_vm_output_view_t view{};
    CHECK(impulse_vm_get_output(aw.ws, 0, &view) != 0);
    CHECK(view.length == 1 && view.i64_data != nullptr);
    return view.i64_data[0];
}

static std::vector<int64_t> exec_vec_i64(
    const ImpScmCompileResult& compiled,
    const impulse_snapshot_t* snap,
    const std::vector<impulse_vm_param_binding_t>& bindings = {}
) {
    AlignedWorkspace aw;
    impulse_vm_params_t params{};
    params.gas_limit = UINT64_MAX;
    params.bindings = bindings.empty() ? nullptr : bindings.data();
    params.binding_count = static_cast<uint32_t>(bindings.size());
    size_t out_bytes = 0;
    impulse_vm_status_t rc = impulse_vm_execute(
        compiled.impb_bytes.data(), compiled.impb_bytes.size(),
        snap, &params, aw.ws, nullptr, 0, &out_bytes);
    if (rc != IMPULSE_VM_OK) {
        std::cerr << "exec_vec_i64 failed: rc=" << rc << "\n";
    }
    CHECK(rc == IMPULSE_VM_OK);
    impulse_vm_output_view_t view{};
    CHECK(impulse_vm_get_output(aw.ws, 0, &view) != 0);
    if (view.length == 0) return {};
    CHECK(view.i64_data != nullptr);
    return std::vector<int64_t>(view.i64_data, view.i64_data + view.length);
}

static void assert_compile_error(const std::string& src, const impulse_snapshot_t* snap, const std::string& expected_prefix) {
    bool threw = false;
    try {
        (void)ImpScmCompiler::compile(src, snap, true);
    } catch (const std::exception& ex) {
        threw = true;
        std::string msg = ex.what();
        CHECK(msg.find(expected_prefix) != std::string::npos);
    }
    CHECK(threw);
}

void test_exact_parser_and_boundary_folder() {
    CHECK(parse_decimal_unscaled_exact("19.99", 18, 2) == 1999LL);
    CHECK(parse_decimal_unscaled_exact("-0.05", 10, 2) == -5LL);
    CHECK(parse_decimal_unscaled_exact("42", 10, 2) == 4200LL);
    // Above 2^53 (exact base-10, no double drift)
    CHECK(parse_decimal_unscaled_exact("9007199254740993.01", 18, 2) == 900719925474099301LL);

    bool threw = false;
    try { (void)parse_decimal_unscaled_exact("1.234", 10, 2); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    threw = false;
    try { (void)parse_decimal_unscaled_exact("1000.00", 5, 2); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Boundary folding: DECIMAL(10, 2) vs 19.995 (scale 3)
    auto f_gt = fold_decimal_comparison(DecimalCmpOp::GT, 10, 2, 19995LL, 3);
    CHECK(f_gt.kind == DecimalFoldKind::COMPARE && f_gt.op == DecimalCmpOp::GE && f_gt.threshold_unscaled == 2000LL);

    auto f_le = fold_decimal_comparison(DecimalCmpOp::LE, 10, 2, 19995LL, 3);
    CHECK(f_le.kind == DecimalFoldKind::COMPARE && f_le.op == DecimalCmpOp::LE && f_le.threshold_unscaled == 1999LL);

    auto f_eq = fold_decimal_comparison(DecimalCmpOp::EQ, 10, 2, 19995LL, 3);
    CHECK(f_eq.kind == DecimalFoldKind::ALWAYS_FALSE);

    auto f_ne = fold_decimal_comparison(DecimalCmpOp::NE, 10, 2, 19995LL, 3);
    CHECK(f_ne.kind == DecimalFoldKind::ALWAYS_TRUE);

    // Negative fractional boundary folding: DECIMAL(10, 2) vs -19.995 (floorDiv(-19995, 10) == -2000)
    auto fn_gt = fold_decimal_comparison(DecimalCmpOp::GT, 10, 2, -19995LL, 3);
    CHECK(fn_gt.kind == DecimalFoldKind::COMPARE && fn_gt.op == DecimalCmpOp::GE && fn_gt.threshold_unscaled == -1999LL);

    auto fn_lt = fold_decimal_comparison(DecimalCmpOp::LT, 10, 2, -19995LL, 3);
    CHECK(fn_lt.kind == DecimalFoldKind::COMPARE && fn_lt.op == DecimalCmpOp::LE && fn_lt.threshold_unscaled == -2000LL);

    // Out-of-range saturation
    auto f_above = fold_decimal_comparison(DecimalCmpOp::GT, 4, 2, 10000LL, 2);
    CHECK(f_above.kind == DecimalFoldKind::ALWAYS_FALSE);
    auto f_below = fold_decimal_comparison(DecimalCmpOp::GT, 4, 2, -10000LL, 2);
    CHECK(f_below.kind == DecimalFoldKind::ALWAYS_TRUE);

    std::cout << "  [PASS] exact base-10 parser & DecimalBoundaryFolder\n";
}

void test_coercion_matrix_and_casts() {
    // 1. DECIMAL(10, 2) + DECIMAL(10, 4) -> scale 4
    {
        auto r = ImpScmCompiler::compile(
            "(query (collect :scalar \"sum\" (+ (dec \"10.50\" 10 2) (dec \"1.2345\" 10 4))))",
            nullptr, true);
        CHECK(r.output_schema.size() == 1);
        CHECK(r.output_schema[0].is_decimal && r.output_schema[0].scale == 4);
        CHECK(exec_scalar_i64(r, nullptr) == 117345LL);
    }

    // 2. DECIMAL(10, 2) + INT (exact promotion to DECIMAL(18, 0))
    {
        auto r = ImpScmCompiler::compile(
            "(query (collect :scalar \"sum\" (+ (dec \"19.99\" 10 2) 5)))",
            nullptr, true);
        CHECK(r.output_schema[0].is_decimal && r.output_schema[0].scale == 2);
        CHECK(exec_scalar_i64(r, nullptr) == 2499LL);
    }

    // 3. DECIMAL * DECIMAL
    {
        auto r = ImpScmCompiler::compile(
            "(query (collect :scalar \"prod\" (* (dec \"12.50\" 10 2) (dec \"2.00\" 10 2))))",
            nullptr, true);
        CHECK(r.output_schema[0].is_decimal && r.output_schema[0].scale == 4);
        CHECK(exec_scalar_i64(r, nullptr) == 250000LL);
    }

    // 4. DECIMAL / DECIMAL and safe-div (0-guarded)
    {
        auto r = ImpScmCompiler::compile(
            "(query (collect :scalar \"q\" (/ (dec \"10.00\" 10 2) (dec \"4.00\" 10 2))))",
            nullptr, true);
        CHECK(r.output_schema[0].is_decimal && r.output_schema[0].scale == 6);
        CHECK(exec_scalar_i64(r, nullptr) == 2500000LL);

        // safe-div by zero returns NULL (status OK, valid_bits == 0)
        auto r_safe = ImpScmCompiler::compile(
            "(query (collect :scalar \"q\" (safe-div (dec \"10.00\" 10 2) (dec \"0.00\" 10 2))))",
            nullptr, true);
        AlignedWorkspace aw;
        impulse_vm_params_t params{};
        params.gas_limit = UINT64_MAX;
        size_t out_bytes = 0;
        CHECK(impulse_vm_execute(
            r_safe.impb_bytes.data(), r_safe.impb_bytes.size(),
            nullptr, &params, aw.ws, nullptr, 0, &out_bytes) == IMPULSE_VM_OK);
        impulse_vm_output_view_t view{};
        CHECK(impulse_vm_get_output(aw.ws, 0, &view) != 0);
        CHECK(view.valid_bits != nullptr && (view.valid_bits[0] & 1ULL) == 0ULL);
    }

    // 5. Explicit casts: DECIMAL -> DECIMAL (round_scale), INT -> DECIMAL, DECIMAL -> I64
    {
        auto r1 = ImpScmCompiler::compile(
            "(query (collect :scalar \"c\" (cast (dec \"19.995\" 10 3) (decimal 10 2))))",
            nullptr, true);
        CHECK(r1.output_schema[0].precision == 10 && r1.output_schema[0].scale == 2);
        CHECK(exec_scalar_i64(r1, nullptr) == 2000LL);

        auto r2 = ImpScmCompiler::compile(
            "(query (collect :scalar \"c\" (cast (dec \"19.99\" 10 2) i64)))",
            nullptr, true);
        CHECK(!r2.output_schema[0].is_decimal && r2.output_schema[0].type == "I64");
        CHECK(exec_scalar_i64(r2, nullptr) == 19LL);
    }

    // 6. Strict TYPE_MISMATCH rejections
    assert_compile_error("(query (+ (dec \"1.00\" 10 2) 1.5))", nullptr, "TYPE_MISMATCH");
    assert_compile_error("(query (+ (dec \"1.00\" 10 2) \"abc\"))", nullptr, "TYPE_MISMATCH");
    assert_compile_error("(query (+ (dec \"1.00\" 10 2) #t))", nullptr, "TYPE_MISMATCH");
    assert_compile_error("(query (log (dec \"1.00\" 10 2)))", nullptr, "TYPE_MISMATCH");

    std::cout << "  [PASS] DECIMAL coercion matrix, safe-div, casts, and TYPE_MISMATCH rejections\n";
}

void test_e2e_financial_ledger_and_cypher_v11() {
    const std::string snap_path =
        (std::filesystem::temp_directory_path() / "test_impscm_decimal64_ledger.imps").string();

    // Build a 4-account financial ledger .imps snapshot with DECIMAL(18, 2) balances
    // Includes values > 2^53 to verify exact 64-bit integer min/max with zero f64 loss.
    const int64_t balances[4] = {
        1000LL,                  // Account 0: 10.00
        2550LL,                  // Account 1: 25.50
        900719925474099301LL,    // Account 2: 9007199254740993.01 (> 2^53)
        900719925474099302LL     // Account 3: 9007199254740993.02 (> 2^53, differs by 1 cent)
    };
    const uint32_t row_offsets[5] = {0, 2, 3, 3, 3};
    const uint32_t col_indices[3] = {1, 2, 3};
    const int64_t transfer_amounts[3] = {525LL, 1000LL, 250LL}; // 5.25, 10.00, 2.50

    impulse_writer_t* w = impulse_writer_create(snap_path.c_str(), IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
    CHECK(w != nullptr);
    CHECK(impulse_writer_add_domain(w, 0, 1, "Account") == IMPULSE_OK);
    CHECK(impulse_writer_set_domain_node_count(w, 0, 4) == IMPULSE_OK);
    CHECK(impulse_writer_add_domain_attribute_decimal64(
        w, 0, "balance", 0x15, 1, 18, 2, balances, sizeof(balances)) == IMPULSE_OK);
    CHECK(impulse_writer_add_relation(
        w, 0, 0, 0, 4, 3, 0,
        row_offsets, sizeof(row_offsets),
        col_indices, sizeof(col_indices)) == IMPULSE_OK);
    CHECK(impulse_writer_set_metadata(w, "relation.0.name", "TRANSFER") == IMPULSE_OK);
    CHECK(impulse_writer_add_attribute_decimal64(
        w, 0, "amount", 0x15, 1, 18, 2, transfer_amounts, sizeof(transfer_amounts)) == IMPULSE_OK);
    CHECK(impulse_writer_finalize(w) == IMPULSE_OK);
    impulse_writer_destroy(w);

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path.c_str(), &st);
    CHECK(snap != nullptr && st == IMPULSE_OK);

    // 1. Exact reduce :max and :min above 2^53 (must distinguish ...302 vs ...301)
    {
        auto r_max = ImpScmCompiler::compile(
            "(query (let ((b (load-node-attr \"Account\" \"balance\"))) (collect :scalar \"mx\" (reduce :max b))))",
            snap, true);
        CHECK(r_max.output_schema[0].is_decimal && r_max.output_schema[0].precision == 18 && r_max.output_schema[0].scale == 2);
        CHECK(exec_scalar_i64(r_max, snap) == 900719925474099302LL);

        auto r_min = ImpScmCompiler::compile(
            "(query (let ((b (load-node-attr \"Account\" \"balance\"))) (collect :scalar \"mn\" (reduce :min b))))",
            snap, true);
        CHECK(exec_scalar_i64(r_min, snap) == 1000LL);
    }

    // 2. node-filter with compile-time boundary folding (19.995 -> >= 20.00 -> Accounts 1, 2, 3)
    {
        auto r = ImpScmCompiler::compile(
            "(query (let ((f (node-filter (domain-all \"Account\") \"balance\" > (dec \"19.995\" 5 3)))) "
            "  (collect :frontier-ids \"ids\" f)))",
            snap, true);
        auto ids = exec_vec_i64(r, snap);
        CHECK(ids.size() == 3 && ids[0] == 1 && ids[1] == 2 && ids[2] == 3);
    }

    // 3. edge-stream scatter-add (0x37.scatter_add_chk) over TRANSFER.amount
    {
        auto r = ImpScmCompiler::compile(
            "(query (let ((acc (vec-fill \"Account\" (decimal 18 2) 0)) "
            "             (f (domain-all \"Account\"))) "
            "  (edge-stream :fwd \"TRANSFER\" f (lambda (u v e) "
            "    (scatter-add acc v (edge-attr e \"amount\")))) "
            "  (collect :vec \"inflow\" acc)))",
            snap, true);
        CHECK(r.output_schema[0].is_decimal && r.output_schema[0].scale == 2);
        auto inflow = exec_vec_i64(r, snap);
        CHECK(inflow.size() == 4);
        CHECK(inflow[0] == 0LL && inflow[1] == 525LL && inflow[2] == 1000LL && inflow[3] == 250LL);
    }

    // 4. Cypher -> ImpScheme v11 with $min_bal DECIMAL(18, 2) param and 19.99d literal
    {
        std::unordered_map<std::string, CypherCompiler::ParamTypeSpec> ptypes;
        ptypes["min_bal"] = {"DECIMAL", 18, 2, ""};
        std::string scm = CypherCompiler::lower_to_impscm(
            "MATCH (a:Account) WHERE a.balance >= $min_bal RETURN count(a)", ptypes);
        auto compiled = ImpScmCompiler::compile(scm, snap, true);
        CHECK(compiled.param_schema.size() == 1);
        CHECK(compiled.param_schema[0].is_decimal && compiled.param_schema[0].scale == 2);

        impulse_vm_param_binding_t pb{};
        pb.param_id = 0;
        pb.kind = IMPULSE_VM_PARAM_SCALAR_I64;
        pb.scalar_bits = static_cast<uint64_t>(1999LL); // 19.99
        CHECK(exec_scalar_i64(compiled, snap, {pb}) == 3LL);
    }
    {
        std::string scm = CypherCompiler::lower_to_impscm(
            "MATCH (a:Account) WHERE a.balance >= 19.99d AND a.balance <= CAST(30.00 AS DECIMAL(18, 2)) RETURN sum(a.balance)");
        auto compiled = ImpScmCompiler::compile(scm, snap, true);
        CHECK(compiled.output_schema[0].is_decimal && compiled.output_schema[0].scale == 2);
        CHECK(exec_scalar_i64(compiled, snap) == 2550LL);
    }

    // 5. Audit regression #2: Cypher string escaping
    {
        std::string scm = CypherCompiler::lower_to_impscm(
            "MATCH (a:Account) WHERE a.tag = 'foo\"bar\\baz' RETURN count(a)");
        CHECK(scm.find("\"foo\\\"bar\\\\baz\"") != std::string::npos);
    }

    // 6. Audit regression #4: mixed-scale +/- inside filtered edge-stream (morsel V12..V15 reuse)
    //    and safe-div vec/vec inside edge-stream
    {
        auto r_ms = ImpScmCompiler::compile(
            "(query (let ((acc (vec-fill \"Account\" (decimal 18 4) 0)) "
            "             (f (domain-all \"Account\"))) "
            "  (edge-stream :fwd \"TRANSFER\" f (lambda (u v e) "
            "    (where (> (edge-attr e \"amount\") (dec \"1.00\" 10 2))) "
            "    (scatter-add acc v (+ (edge-attr e \"amount\") (cast (attr u \"balance\") (decimal 18 4)))))) "
            "  (collect :vec \"inflow\" acc)))",
            snap, true);
        auto res_ms = exec_vec_i64(r_ms, snap);
        CHECK(res_ms.size() == 4);

        auto r_sdiv_morsel = ImpScmCompiler::compile(
            "(query (let ((acc (vec-fill \"Account\" (decimal 18 6) 0)) "
            "             (f (domain-all \"Account\"))) "
            "  (edge-stream :fwd \"TRANSFER\" f (lambda (u v e) "
            "    (where (> (edge-attr e \"amount\") (dec \"1.00\" 10 2))) "
            "    (scatter-add acc v (safe-div (edge-attr e \"amount\") (attr u \"balance\"))))) "
            "  (collect :vec \"ratio\" acc)))",
            snap, true);
        auto res_sd = exec_vec_i64(r_sdiv_morsel, snap);
        CHECK(res_sd.size() == 4);
    }

    // 7. Audit regression #5 & #7: unary '-' in CAST and rounding CAST(19.995d AS DECIMAL(10, 2))
    {
        std::unordered_map<std::string, CypherCompiler::ParamTypeSpec> ptypes;
        ptypes["p"] = {"DECIMAL", 18, 2, ""};
        std::string scm_neg = CypherCompiler::lower_to_impscm(
            "MATCH (a:Account) WHERE a.balance >= CAST(-$p AS DECIMAL(18, 2)) RETURN count(a)", ptypes);
        CHECK(scm_neg.find("(cast (- 0 $p) (decimal 18 2))") != std::string::npos);

        bool threw_neg_str = false;
        try {
            (void)CypherCompiler::lower_to_impscm(
                "MATCH (a:Account) WHERE a.balance >= CAST(-'19.99' AS DECIMAL(18, 2)) RETURN count(a)");
        } catch (const std::exception&) {
            threw_neg_str = true;
        }
        CHECK(threw_neg_str);

        std::string scm_round = CypherCompiler::lower_to_impscm(
            "MATCH (a:Account) WHERE a.balance >= CAST(19.995d AS DECIMAL(10, 2)) RETURN count(a)");
        CHECK(scm_round.find("(cast (dec \"19.995\" 5 3) (decimal 10 2))") != std::string::npos);
        auto comp_round = ImpScmCompiler::compile(scm_round, snap, true);
        CHECK(exec_scalar_i64(comp_round, snap) == 3LL);
    }

    // 8. Audit regression #6: scalar NULL divisor in safe-div with raw_k > 18 must return NULL without overflow
    {
        auto r_null_div = ImpScmCompiler::compile(
            "(query (param $d (decimal 18 16)) "
            "  (collect :scalar \"q\" (safe-div (dec \"9999999999999999.00\" 18 2) $d)))",
            snap, true);
        AlignedWorkspace aw;
        impulse_vm_param_binding_t pb{};
        pb.param_id = 0;
        pb.kind = IMPULSE_VM_PARAM_SCALAR_NULL;
        impulse_vm_params_t params{};
        params.gas_limit = UINT64_MAX;
        params.bindings = &pb;
        params.binding_count = 1;
        size_t out_bytes = 0;
        CHECK(impulse_vm_execute(
            r_null_div.impb_bytes.data(), r_null_div.impb_bytes.size(),
            snap, &params, aw.ws, nullptr, 0, &out_bytes) == IMPULSE_VM_OK);
        impulse_vm_output_view_t view{};
        CHECK(impulse_vm_get_output(aw.ws, 0, &view) != 0);
        CHECK(view.valid_bits != nullptr && (view.valid_bits[0] & 1ULL) == 0ULL);
    }

    // 9. Audit regression #8 & #11: non-decimal scalar - vec, scalar + vec, and vec-fill type mismatch
    {
        auto exec_vec_f64 = [&](const ImpScmCompileResult& compiled) -> std::vector<double> {
            AlignedWorkspace aw;
            impulse_vm_params_t params{};
            params.gas_limit = UINT64_MAX;
            size_t out_bytes = 0;
            CHECK(impulse_vm_execute(
                compiled.impb_bytes.data(), compiled.impb_bytes.size(),
                snap, &params, aw.ws, nullptr, 0, &out_bytes) == IMPULSE_VM_OK);
            impulse_vm_output_view_t view{};
            CHECK(impulse_vm_get_output(aw.ws, 0, &view) != 0);
            CHECK(view.length > 0 && view.f64_data != nullptr);
            return std::vector<double>(view.f64_data, view.f64_data + view.length);
        };
        auto r_sub = ImpScmCompiler::compile(
            "(query (collect :vec \"v\" (- 100.0 (vec-fill \"Account\" f64 30.0))))", snap, true);
        auto v_sub = exec_vec_f64(r_sub);
        CHECK(v_sub.size() == 4 && v_sub[0] == 70.0 && v_sub[3] == 70.0);

        auto r_add = ImpScmCompiler::compile(
            "(query (collect :vec \"v\" (+ 10.0 (vec-fill \"Account\" f64 30.0))))", snap, true);
        auto v_add = exec_vec_f64(r_add);
        CHECK(v_add.size() == 4 && v_add[0] == 40.0);

        assert_compile_error("(query (vec-fill \"Account\" f64 10))", snap, "TYPE_MISMATCH");
    }

    impulse_snapshot_close(snap);
    std::remove(snap_path.c_str());
    std::cout << "  [PASS] E2E financial ledger (.imps) + Cypher v11 DECIMAL64 execution\n";
}

} // namespace

int main() {
    std::cout << "Running test_impscm_decimal64...\n";
    test_exact_parser_and_boundary_folder();
    test_coercion_matrix_and_casts();
    test_e2e_financial_ledger_and_cypher_v11();
    std::cout << "ALL test_impscm_decimal64 TESTS PASSED!\n";
    return 0;
}
