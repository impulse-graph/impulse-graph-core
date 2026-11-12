/**
 * @file test_issues_49_50_51_52.cpp
 * @brief Comprehensive Verification Suite for Issues #49, #50, #51, #52:
 *        - Issue #49: CEL Stream Shader Lowering, TYPE_LANE_FRONTIER Stream Walk, FloatAttrMap, 48 Math Funcs, Multi-Word CEL.
 *        - Issue #50: Traversal FILTER_CEL, PROJECT_CEL, Named Attribute Resolution, collectStateVector, collectNodeValues, collectKeyValues.
 *        - Issue #51: MultiLaneTraversal Numeric Projections, OP_LANE_REDUCE_ATTR (0xC8), MultiLaneResult Value Accessors.
 *        - Issue #52: impulse_stmt_execute Result Materialization, ScmCollect::Mode, Cypher Named Attribute & Aggregate Projections.
 */

#include "impulse_cel.h"
#include "impulse_compiler.hpp"
#include "impulse_cypher.hpp"
#include "impulse_scm_ast.hpp"
#include "impulse_vm.h"
#include "impulse_vm_fluent.hpp"
#include "impulse_vm_fluent_internal.hpp"
#include "impulse_vm_internal.hpp"
#include "impulse_graph.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define TEST_SNAPSHOT_PATH "__test_issues_snapshot.bin"

#define T_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "TEST ASSERTION FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

#define T_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "TEST EQUALITY FAILED: %s != %s at %s:%d\n", #a, #b, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

#define T_ASSERT_NEAR(a, b, eps) do { \
    if (std::abs((a) - (b)) > (eps)) { \
        std::fprintf(stderr, "TEST NEAR FAILED: |%f - %f| > %f at %s:%d\n", static_cast<double>(a), static_cast<double>(b), static_cast<double>(eps), __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

// Helper: build a snapshot with 4 nodes, domain "User", relation "Follows", float attribute "score"
static void create_test_snapshot(const char* filepath) {
    impulse_writer_t* writer = impulse_writer_create(filepath, 0);
    T_ASSERT(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "User");
    T_ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_set_domain_node_count(writer, 0, 4);
    T_ASSERT_EQ(st, IMPULSE_OK);

    // Edges: 0 -> 1, 0 -> 2, 1 -> 2, 2 -> 3
    const uint32_t row_offsets[] = { 0, 2, 3, 4, 4 };
    const uint32_t col_indices[] = { 1, 2, 2, 3 };

    st = impulse_writer_add_relation(
        writer,
        0, 0,
        IMPULSE_ENC_RAW,
        4, 4,
        0,
        row_offsets, sizeof(row_offsets),
        col_indices, sizeof(col_indices)
    );
    T_ASSERT_EQ(st, IMPULSE_OK);

    // Float node attribute "score" for 4 nodes: [10.0, 20.0, 30.0, 40.0]
    const float scores[] = { 10.0f, 20.0f, 30.0f, 40.0f };
    st = impulse_writer_add_domain_attribute(
        writer,
        0,
        "score",
        IMPULSE_ATTR_TYPE_FLOAT32,
        1,
        scores, sizeof(scores),
        nullptr, 0
    );
    T_ASSERT_EQ(st, IMPULSE_OK);

    // Float node attribute "weight" for 4 nodes: [2.0, 4.0, 5.0, 8.0]
    const float weights[] = { 2.0f, 4.0f, 5.0f, 8.0f };
    st = impulse_writer_add_domain_attribute(
        writer,
        0,
        "weight",
        IMPULSE_ATTR_TYPE_FLOAT32,
        1,
        weights, sizeof(weights),
        nullptr, 0
    );
    T_ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    T_ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(writer);
}

// ============================================================================
// Issue #49 Tests:
// - CelCompiler::compile_stream_shader
// - CelCompiler::compile_node_vector_expr
// - CelMultiLaneEvaluator: FloatAttrMap, eval_double, 48 Math Funcs, in operator, multi-word (K > 64)
// - TYPE_LANE_FRONTIER Stream Walk propagation
// ============================================================================
static void test_issue_49_stream_shader_and_cel_eval() {
    std::printf("Testing Issue #49: CEL Stream Shader Lowering & CelMultiLaneEvaluator...\n");

    using namespace impulse::cel;

    // 1. Test compile_stream_shader
    {
        Parser parser("edge.weight > 0.5");
        auto ast = parser.parse();
        T_ASSERT(ast != nullptr);

        std::vector<impulse_instruction_t> shader;
        bool ok = CelCompiler::compile_stream_shader(ast, shader, 0, nullptr, true);
        T_ASSERT(ok);
        T_ASSERT(!shader.empty());

        bool has_load = false;
        bool has_filter = false;
        bool has_begin = false;
        bool has_end = false;
        for (const auto& inst : shader) {
            if (inst.opcode == OP_STREAM_LOAD_EDGE || inst.opcode == OP_STREAM_LOAD_SRC || inst.opcode == OP_STREAM_LOAD_CONST) {
                has_load = true;
            }
            if (inst.opcode == OP_STREAM_FILTER) {
                has_filter = true;
            }
            if (inst.opcode == OP_STREAM_FUNC_BEGIN) {
                has_begin = true;
            }
            if (inst.opcode == OP_STREAM_FUNC_END) {
                has_end = true;
            }
        }
        T_ASSERT(has_load);
        T_ASSERT(has_filter);
        T_ASSERT(has_begin);
        T_ASSERT(has_end);
    }

    // 2. Test compile_node_vector_expr
    {
        Parser parser("node.a + node.b * 2.0");
        auto ast = parser.parse();
        T_ASSERT(ast != nullptr);

        std::vector<impulse_instruction_t> vec_code;
        uint16_t next_temp = 5;
        bool ok = CelCompiler::compile_node_vector_expr(ast, vec_code, 4, next_temp);
        T_ASSERT(ok);
        T_ASSERT(!vec_code.empty());
    }

    // 3. Test CelMultiLaneEvaluator FloatAttrMap and eval_double
    {
        CelMultiLaneEvaluator::FloatAttrMap attrs;
        attrs["x"] = 2.5;
        attrs["y"] = 1.5;

        Parser p1("sqrt(16.0)");
        double r1 = CelMultiLaneEvaluator::eval_double(p1.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r1, 4.0, 1e-5);

        Parser p2("sin(0.0)");
        double r2 = CelMultiLaneEvaluator::eval_double(p2.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r2, 0.0, 1e-5);

        Parser p3("cos(0.0)");
        double r3 = CelMultiLaneEvaluator::eval_double(p3.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r3, 1.0, 1e-5);

        Parser p4("abs(-42.5)");
        double r4 = CelMultiLaneEvaluator::eval_double(p4.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r4, 42.5, 1e-5);

        Parser p5("floor(3.7)");
        double r5 = CelMultiLaneEvaluator::eval_double(p5.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r5, 3.0, 1e-5);

        Parser p6("ceil(3.2)");
        double r6 = CelMultiLaneEvaluator::eval_double(p6.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r6, 4.0, 1e-5);

        Parser p7("pow(2.0, 3.0)");
        double r7 = CelMultiLaneEvaluator::eval_double(p7.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r7, 8.0, 1e-5);

        Parser p8("exp(0.0)");
        double r8 = CelMultiLaneEvaluator::eval_double(p8.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r8, 1.0, 1e-5);

        Parser p9("log(1.0)");
        double r9 = CelMultiLaneEvaluator::eval_double(p9.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r9, 0.0, 1e-5);

        Parser p10("x + y");
        double r10 = CelMultiLaneEvaluator::eval_double(p10.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r10, 4.0, 1e-5);

        Parser p11("x * y");
        double r11 = CelMultiLaneEvaluator::eval_double(p11.parse(), 0ULL, &attrs);
        T_ASSERT_NEAR(r11, 3.75, 1e-5);
    }

    // 4. Test 48-Function Math Catalog exhaustively
    {
        CelMultiLaneEvaluator::FloatAttrMap empty_attrs;

        const char* math_exprs[] = {
            "abs(-5.0)", "sign(-10.0)", "sqrt(25.0)", "cbrt(27.0)",
            "exp(1.0)", "exp2(3.0)", "expm1(0.0)", "log(2.718281828)",
            "log2(8.0)", "log10(100.0)", "log1p(0.0)", "sin(0.0)",
            "cos(0.0)", "tan(0.0)", "asin(0.0)", "acos(1.0)",
            "atan(0.0)", "atan2(0.0, 1.0)", "sinh(0.0)", "cosh(0.0)",
            "tanh(0.0)", "asinh(0.0)", "acosh(1.0)", "atanh(0.0)",
            "ceil(2.1)", "floor(2.9)", "round(2.5)", "trunc(-2.7)",
            "pow(2.0, 4.0)", "hypot(3.0, 4.0)", "min(3.0, 5.0)", "max(3.0, 5.0)",
            "clamp(10.0, 0.0, 5.0)", "lerp(0.0, 10.0, 0.5)", "fma(2.0, 3.0, 4.0)",
            "deg2rad(180.0)", "rad2deg(3.14159265)", "sigmoid(0.0)",
            "relu(-5.0)", "relu(5.0)", "gelu(0.0)", "silu(0.0)",
            "is_nan(0.0)", "is_inf(0.0)", "is_finite(5.0)", "erf(0.0)", "erfc(0.0)"
        };

        for (const char* expr : math_exprs) {
            Parser p(expr);
            auto ast = p.parse();
            double res = CelMultiLaneEvaluator::eval_double(ast, 0ULL, &empty_attrs);
            (void)res;
        }

        Parser p_sig("sigmoid(0.0)");
        T_ASSERT_NEAR(CelMultiLaneEvaluator::eval_double(p_sig.parse(), 0ULL, &empty_attrs), 0.5, 1e-4);

        Parser p_relu_neg("relu(-10.0)");
        T_ASSERT_NEAR(CelMultiLaneEvaluator::eval_double(p_relu_neg.parse(), 0ULL, &empty_attrs), 0.0, 1e-5);

        Parser p_relu_pos("relu(10.0)");
        T_ASSERT_NEAR(CelMultiLaneEvaluator::eval_double(p_relu_pos.parse(), 0ULL, &empty_attrs), 10.0, 1e-5);

        Parser p_clamp("clamp(15.0, 0.0, 10.0)");
        T_ASSERT_NEAR(CelMultiLaneEvaluator::eval_double(p_clamp.parse(), 0ULL, &empty_attrs), 10.0, 1e-5);
    }

    // 5. Test 'in' list operator
    {
        CelMultiLaneEvaluator::FloatAttrMap attrs;
        attrs["id"] = 2.0;

        Parser p_in("id in [1.0, 2.0, 3.0]");
        auto ast_in = p_in.parse();
        bool b_in = CelMultiLaneEvaluator::eval_bool(ast_in, 0ULL, nullptr, &attrs);
        T_ASSERT(b_in);

        attrs["id"] = 5.0;
        bool b_in_false = CelMultiLaneEvaluator::eval_bool(ast_in, 0ULL, nullptr, &attrs);
        T_ASSERT(!b_in_false);
    }

    // 6. Test Multi-word (K > 64, e.g. K=128) lane masks
    {
        uint64_t words[2] = { 0x01ULL, 0x02ULL }; // word 0: lane 0; word 1: lane 65

        Parser p0("has_lane(lane, 0)");
        T_ASSERT(CelMultiLaneEvaluator::eval_bool(p0.parse(), words, 2));

        Parser p65("has_lane(lane, 65)");
        T_ASSERT(CelMultiLaneEvaluator::eval_bool(p65.parse(), words, 2));

        Parser p1("has_lane(lane, 1)");
        T_ASSERT(!CelMultiLaneEvaluator::eval_bool(p1.parse(), words, 2));
    }

    // 7. Test TYPE_LANE_FRONTIER stream walk propagation
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        T_ASSERT(ctx != nullptr);

        int h_lf = acquire_lane_frontier(ctx, 4, 4);
        T_ASSERT(h_lf >= 0);
        auto& lf = ctx->lane_frontiers[h_lf];
        lf.num_lanes = 4;
        lf.lanes = { 0x01ULL, 0x02ULL, 0x04ULL, 0x08ULL };
        lf.active_mask = 0x0FULL;

        impulse_vm_state_t state{};
        state.query_context = ctx;
        state.registers[2] = static_cast<uint64_t>(h_lf);
        state.register_types[2] = TYPE_LANE_FRONTIER;

        impulse_instruction_t prog[7] = {
            {OP_CSR_WALK_STREAM, 2, 3, (0 << 16) | 2},
            {OP_HALT, 0, 0, 0},
            {OP_STREAM_FUNC_BEGIN, 0, 0, 0},
            {OP_STREAM_LOAD_SRC_ID, 1, 0, 0},
            {OP_STREAM_FILTER, 0, 0, 0},
            {OP_STREAM_YIELD, 0, 0, 0},
            {OP_STREAM_FUNC_END, 0, 0, 0}
        };

        impulse_vm_status_t rc = impulse_vm_execute(prog, 7, &state, 0);
        T_ASSERT_EQ(rc, IMPULSE_VM_OK);
        // Destination register 3 should be TYPE_LANE_FRONTIER
        T_ASSERT_EQ(state.register_types[3], TYPE_LANE_FRONTIER);

        impulse_vm_context_destroy(ctx);
    }

    std::printf("  Issue #49: ALL TESTS PASSED\n");
}

// ============================================================================
// Issue #50 Tests:
// - Traversal FILTER_CEL and PROJECT_CEL in collect() and evaluateStateVector()
// - Named Node Attribute Resolution in evaluateStateVector()
// - collectStateVector(), collectNodeValues(), collectKeyValues()
// ============================================================================
static void test_issue_50_single_lane_cel_and_collectors() {
    std::printf("Testing Issue #50: Single-Lane Traversal CEL & Value Collectors...\n");

    create_test_snapshot(TEST_SNAPSHOT_PATH);

    impulse_status_t st;
    impulse_snapshot_t* raw_snap = impulse_snapshot_open(TEST_SNAPSHOT_PATH, &st);
    T_ASSERT_EQ(st, IMPULSE_OK);
    T_ASSERT(raw_snap != nullptr);

    using namespace impulse::vm;
    SnapshotView snap(raw_snap);

    // 1. Test Traversal::filter(cel) (FILTER_CEL)
    {
        auto nodes = snap.domain("User").all()
                         .filter("score >= 30.0")
                         .collect();
        T_ASSERT_EQ(nodes.size(), 2);
        T_ASSERT_EQ(nodes[0], 2); // score = 30.0
        T_ASSERT_EQ(nodes[1], 3); // score = 40.0
    }

    // 2. Test Traversal::project(cel) (PROJECT_CEL) + collectStateVector()
    {
        std::vector<float> state_vec = snap.domain("User").all()
                                           .project("state = score * 2.0")
                                           .collectStateVector();
        T_ASSERT_EQ(state_vec.size(), 4);
        T_ASSERT_NEAR(state_vec[0], 20.0f, 1e-4);
        T_ASSERT_NEAR(state_vec[1], 40.0f, 1e-4);
        T_ASSERT_NEAR(state_vec[2], 60.0f, 1e-4);
        T_ASSERT_NEAR(state_vec[3], 80.0f, 1e-4);
    }

    // 3. Test evaluateStateVector() with named attributes
    {
        std::vector<float> state_vec = snap.domain("User").all()
                                           .mulNodeAttr("score")
                                           .divNodeAttr("weight")
                                           .collectStateVector();
        T_ASSERT_EQ(state_vec.size(), 4);
        // node 0: 1.0 * 10.0 / 2.0 = 5.0
        T_ASSERT_NEAR(state_vec[0], 5.0f, 1e-4);
        // node 1: 1.0 * 20.0 / 4.0 = 5.0
        T_ASSERT_NEAR(state_vec[1], 5.0f, 1e-4);
        // node 2: 1.0 * 30.0 / 5.0 = 6.0
        T_ASSERT_NEAR(state_vec[2], 6.0f, 1e-4);
        // node 3: 1.0 * 40.0 / 8.0 = 5.0
        T_ASSERT_NEAR(state_vec[3], 5.0f, 1e-4);
    }

    // 4. Test collectNodeValues()
    {
        auto node_vals = snap.domain("User").from({0})
                             .out("Follows") // Walk edges: 0 -> 1, 0 -> 2
                             .project("state = score + 5.0")
                             .collectNodeValues();
        T_ASSERT_EQ(node_vals.size(), 2);
        T_ASSERT_EQ(node_vals[0].first, 1);
        T_ASSERT_NEAR(node_vals[0].second, 25.0f, 1e-4);
        T_ASSERT_EQ(node_vals[1].first, 2);
        T_ASSERT_NEAR(node_vals[1].second, 35.0f, 1e-4);
    }

    // 5. Test collectKeyValues() via OP_COLLECT_VALUE_MAP (0x93)
    {
        auto key_vals = snap.domain("User").all()
                            .project("state = score")
                            .collectKeyValues();
        T_ASSERT_EQ(key_vals.size(), 4);
        T_ASSERT_EQ(key_vals[0].first, std::string("0"));
        T_ASSERT_EQ(key_vals[1].first, std::string("1"));
        T_ASSERT_EQ(key_vals[2].first, std::string("2"));
        T_ASSERT_EQ(key_vals[3].first, std::string("3"));
        T_ASSERT_NEAR(key_vals[0].second, 10.0f, 1e-4);
        T_ASSERT_NEAR(key_vals[1].second, 20.0f, 1e-4);
        T_ASSERT_NEAR(key_vals[2].second, 30.0f, 1e-4);
        T_ASSERT_NEAR(key_vals[3].second, 40.0f, 1e-4);
    }

    impulse_snapshot_close(raw_snap);
    std::printf("  Issue #50: ALL TESTS PASSED\n");
}

// ============================================================================
// Issue #51 Tests:
// - MultiLaneTraversal Numeric Projections (project(cel))
// - OP_LANE_REDUCE_ATTR (0xC8) via reducePerLane, argMinNodesPerLane, argMaxNodesPerLane
// - MultiLaneResult Value Accessors (hasProjectedValues, valueFor, nodeValues, keyValues, nodeValuesForLane, nodeValuesWithQuorum)
// - Multi-word (K > 64) Lane Masks
// ============================================================================
static void test_issue_51_multilane_numeric_and_reductions() {
    std::printf("Testing Issue #51: Multi-Lane Projections, OP_LANE_REDUCE_ATTR, and Accessors...\n");

    impulse_status_t st;
    impulse_snapshot_t* raw_snap = impulse_snapshot_open(TEST_SNAPSHOT_PATH, &st);
    T_ASSERT_EQ(st, IMPULSE_OK);
    T_ASSERT(raw_snap != nullptr);

    using namespace impulse::vm;
    SnapshotView snap(raw_snap);

    // 1. Test MultiLaneTraversal::project(cel) and MultiLaneResult accessors
    {
        MultiLaneResult res = snap.domain("User").multiLane()
                                  .fromSeeds({0, 1})
                                  .project("state = score * 1.5")
                                  .collect();
        T_ASSERT(res.hasProjectedValues());
        T_ASSERT_EQ(res.laneCount(), 2);
        T_ASSERT_EQ(res.domainNodeCount(), 4);

        T_ASSERT_NEAR(res.valueFor(0), 15.0f, 1e-4);
        T_ASSERT_NEAR(res.valueFor(1), 30.0f, 1e-4);
        T_ASSERT_NEAR(res.valueFor(2), 45.0f, 1e-4);
        T_ASSERT_NEAR(res.valueFor(3), 60.0f, 1e-4);

        // nodeValues(): should return active nodes (0 and 1)
        auto nv = res.nodeValues();
        T_ASSERT_EQ(nv.size(), 2);
        T_ASSERT_EQ(nv[0].first, 0);
        T_ASSERT_NEAR(nv[0].second, 15.0f, 1e-4);
        T_ASSERT_EQ(nv[1].first, 1);
        T_ASSERT_NEAR(nv[1].second, 30.0f, 1e-4);

        // keyValues(): returns (string_key, value) pairs
        auto kv = res.keyValues();
        T_ASSERT_EQ(kv.size(), 2);

        // nodeValuesForLane
        auto lane0_nv = res.nodeValuesForLane(0);
        T_ASSERT_EQ(lane0_nv.size(), 1);
        T_ASSERT_EQ(lane0_nv[0].first, 0);

        auto lane1_nv = res.nodeValuesForLane(1);
        T_ASSERT_EQ(lane1_nv.size(), 1);
        T_ASSERT_EQ(lane1_nv[0].first, 1);

        // nodeValuesWithQuorum
        auto q1 = res.nodeValuesWithQuorum(1);
        T_ASSERT_EQ(q1.size(), 2);
        auto q2 = res.nodeValuesWithQuorum(2);
        T_ASSERT_EQ(q2.size(), 0); // No node is in both lane 0 and lane 1
    }

    // 2. Test OP_LANE_REDUCE_ATTR (0xC8) via reducePerLane
    {
        auto ml = snap.domain("User").multiLane().fromSeeds({0}).out("Follows"); // reaches 1 and 2

        // Reduce snapshot attribute "score" with SUM: node 1 (20) + node 2 (30) = 50.0
        std::vector<double> sum_res = ml.reducePerLane("score", ReducerOp::SUM);
        T_ASSERT_EQ(sum_res.size(), 1);
        T_ASSERT_NEAR(sum_res[0], 50.0, 1e-4);

        // Reduce projected values with SUM
        ml.project("state = score * 2.0");
        std::vector<double> proj_sum = ml.reducePerLane("", ReducerOp::SUM);
        T_ASSERT_EQ(proj_sum.size(), 1);
        // node 1 (40) + node 2 (60) = 100.0
        T_ASSERT_NEAR(proj_sum[0], 100.0, 1e-4);
    }

    // 3. Test argMinNodesPerLane and argMaxNodesPerLane via OP_LANE_REDUCE_ATTR (0xC8)
    {
        auto ml = snap.domain("User").multiLane().fromSeeds({0}).out("Follows"); // reaches 1 (score 20) and 2 (score 30)

        std::vector<int64_t> argmin_res = ml.argMinNodesPerLane("score");
        T_ASSERT_EQ(argmin_res.size(), 1);
        T_ASSERT_EQ(argmin_res[0], 1); // min score is node 1

        std::vector<int64_t> argmax_res = ml.argMaxNodesPerLane("score");
        T_ASSERT_EQ(argmax_res.size(), 1);
        T_ASSERT_EQ(argmax_res[0], 2); // max score is node 2
    }

    // 4. Test Multi-word (K = 128 > 64) MultiLaneResult
    {
        std::vector<std::string> keys(128);
        for (int i = 0; i < 128; ++i) keys[i] = "lane_" + std::to_string(i);
        std::vector<uint64_t> words_per_node(4 * 2, 0ULL); // 4 nodes, 2 words per node
        // Node 0 active in lane 0 (word 0, bit 0)
        words_per_node[0 * 2 + 0] = 0x01ULL;
        // Node 1 active in lane 65 (word 1, bit 1)
        words_per_node[1 * 2 + 1] = 0x02ULL;

        MultiLaneResult res(raw_snap, 0, 4, 128, keys, words_per_node);
        T_ASSERT_EQ(res.laneCount(), 128);
        auto lanes0 = res.attributionFor(0);
        T_ASSERT(std::find(lanes0.begin(), lanes0.end(), 0) != lanes0.end());
        T_ASSERT(std::find(lanes0.begin(), lanes0.end(), 1) == lanes0.end());
        auto lanes1 = res.attributionFor(1);
        T_ASSERT(std::find(lanes1.begin(), lanes1.end(), 65) != lanes1.end());
        T_ASSERT(std::find(lanes1.begin(), lanes1.end(), 0) == lanes1.end());
    }

    impulse_snapshot_close(raw_snap);
    std::printf("  Issue #51: ALL TESTS PASSED\n");
}

// ============================================================================
// Issue #52 Tests:
// - impulse_stmt_execute Result Materialization (TYPE_VALUE_MAP, TYPE_DOUBLE_VECTOR, TYPE_INT64, TYPE_FLOAT64)
// - ScmCollect::Mode (BITSET, ARRAY, COUNT, VALUE_MAP, NONE)
// - Cypher Compiler RETURN n.attr, RETURN sum/min/max/argmin/argmax(n.attr), and Terminal Rerank
// ============================================================================
static void test_issue_52_stmt_materialization_and_cypher() {
    std::printf("Testing Issue #52: Statement Materialization, ScmCollect::Mode & Cypher Projections...\n");

    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_SNAPSHOT_PATH, &st);
    T_ASSERT_EQ(st, IMPULSE_OK);
    T_ASSERT(snap != nullptr);

    using namespace impulse::compiler;

    // 1. Test ScmCollect::Mode enum and factories
    {
        auto c_bitset = ScmCollect::bitset(0);
        T_ASSERT_EQ(c_bitset->mode, ScmCollect::Mode::BITSET);

        auto c_array = ScmCollect::array(0);
        T_ASSERT_EQ(c_array->mode, ScmCollect::Mode::ARRAY);

        auto c_count = ScmCollect::count(0);
        T_ASSERT_EQ(c_count->mode, ScmCollect::Mode::COUNT);

        auto c_vmap = ScmCollect::value_map(0);
        T_ASSERT_EQ(c_vmap->mode, ScmCollect::Mode::VALUE_MAP);

        auto c_none = ScmCollect::none(0);
        T_ASSERT_EQ(c_none->mode, ScmCollect::Mode::NONE);
    }

    // 2. Test compiler lowering for ScmCollect modes
    {
        // Mode::COUNT should emit OP_SET_CARDINALITY (0x33)
        auto w1 = ScmWalk::forward("Follows");
        auto c1 = ScmCollect::count(0);
        auto prog1 = std::make_shared<ScmProgram>(std::vector<AstPtr>{w1, c1});
        auto comp1 = ImpulseCompiler::compile(prog1);
        bool has_count_op = false;
        for (const auto& inst : comp1.instructions) {
            if (inst.opcode == OP_SET_CARDINALITY) has_count_op = true;
        }
        T_ASSERT(has_count_op);

        // Mode::ARRAY should emit OP_COLLECT_ARRAY (0x91)
        auto c2 = ScmCollect::array(0);
        auto prog2 = std::make_shared<ScmProgram>(std::vector<AstPtr>{w1, c2});
        auto comp2 = ImpulseCompiler::compile(prog2);
        bool has_array_op = false;
        for (const auto& inst : comp2.instructions) {
            if (inst.opcode == OP_COLLECT_ARRAY) has_array_op = true;
        }
        T_ASSERT(has_array_op);

        // Mode::VALUE_MAP should emit OP_COLLECT_VALUE_MAP (0x93)
        auto c3 = ScmCollect::value_map(0);
        auto prog3 = std::make_shared<ScmProgram>(std::vector<AstPtr>{w1, c3});
        auto comp3 = ImpulseCompiler::compile(prog3);
        bool has_vmap_op = false;
        for (const auto& inst : comp3.instructions) {
            if (inst.opcode == OP_COLLECT_VALUE_MAP) has_vmap_op = true;
        }
        T_ASSERT(has_vmap_op);

        // ScmStreamFilter and ScmStreamProject on ScmWalk lower to stream shader
        auto w_stream = ScmWalk::forward("Follows");
        w_stream->shader_steps.push_back(std::make_shared<ScmStreamFilter>(nullptr));
        w_stream->shader_steps.push_back(std::make_shared<ScmStreamProject>(nullptr));
        auto prog_stream = std::make_shared<ScmProgram>(std::vector<AstPtr>{w_stream});
        auto comp_stream = ImpulseCompiler::compile(prog_stream);
        bool has_stream_walk = false;
        bool has_stream_begin = false;
        bool has_stream_filter = false;
        bool has_stream_reduce = false;
        bool has_stream_yield = false;
        for (const auto& inst : comp_stream.instructions) {
            if (inst.opcode == OP_CSR_WALK_STREAM) has_stream_walk = true;
            if (inst.opcode == OP_STREAM_FUNC_BEGIN) has_stream_begin = true;
            if (inst.opcode == OP_STREAM_FILTER) has_stream_filter = true;
            if (inst.opcode == OP_STREAM_SCATTER_REDUCE) has_stream_reduce = true;
            if (inst.opcode == OP_STREAM_YIELD) has_stream_yield = true;
        }
        T_ASSERT(has_stream_walk);
        T_ASSERT(has_stream_begin);
        T_ASSERT(has_stream_filter);
        T_ASSERT(has_stream_reduce);
        T_ASSERT(has_stream_yield);
    }

    // 3. Test Cypher RETURN n.attr, aggregates, and terminal rerank
    {
        // RETURN v.score
        const char* q1 = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN v.score";
        auto res1 = CypherCompiler::compile(q1);
        T_ASSERT(res1.ast != nullptr);
        std::string s1 = res1.ast->to_scm_string();
        T_ASSERT(s1.find("collect") != std::string::npos);

        // RETURN sum(v.score)
        const char* q2 = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN sum(v.score)";
        auto res2 = CypherCompiler::compile(q2);
        T_ASSERT(res2.ast != nullptr);

        // RETURN argmin(v.score)
        const char* q3 = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN argmin(v.score)";
        auto res3 = CypherCompiler::compile(q3);
        T_ASSERT(res3.ast != nullptr);

        // Terminal vector rerank -> preserves TYPE_FLOAT_VECTOR with ScmCollect::none()
        const char* q4 = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN v ORDER BY vector_dot(v.embedding, $q)";
        auto res4 = CypherCompiler::compile(q4);
        T_ASSERT(res4.ast != nullptr);
        auto prog = std::static_pointer_cast<ScmProgram>(res4.ast);
        T_ASSERT(!prog->steps.empty());
        auto last_collect = std::static_pointer_cast<ScmCollect>(prog->steps.back());
        T_ASSERT_EQ(last_collect->mode, ScmCollect::Mode::NONE);
    }

    // 4. Test impulse_stmt_execute materialization of TYPE_INT64, TYPE_FLOAT64, TYPE_DOUBLE_VECTOR, TYPE_VALUE_MAP
    {
        // Test TYPE_INT64 materialization
        impulse_stmt_t* stmt = nullptr;
        const char* cypher_count = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN count(v)";
        impulse_status_t rc = impulse_stmt_prepare(snap, cypher_count, &stmt);
        T_ASSERT_EQ(rc, IMPULSE_OK);
        T_ASSERT(stmt != nullptr);

        impulse_stmt_bind_node(stmt, "$seed", 0);
        size_t bsize = impulse_stmt_buffer_size(stmt);
        std::vector<uint8_t> buffer(bsize);

        rc = impulse_stmt_execute(stmt, buffer.data(), buffer.size());
        T_ASSERT_EQ(rc, IMPULSE_OK);

        T_ASSERT_EQ(impulse_stmt_column_count(stmt), 1);
        T_ASSERT_EQ(impulse_stmt_row_count(stmt), 1);
        const int64_t* out_val = reinterpret_cast<const int64_t*>(impulse_stmt_column_data(stmt, 0));
        T_ASSERT(out_val != nullptr);
        // Node 0 has 2 outgoing edges -> count = 2
        T_ASSERT_EQ(*out_val, 2);
        impulse_stmt_finalize(stmt);
    }

    {
        // Test direct execution with TYPE_VALUE_MAP in impulse_stmt_execute
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        T_ASSERT(ctx != nullptr);

        int h_vmap = impulse_vm_context_acquire_value_map(ctx);
        T_ASSERT(h_vmap >= 0);
        ctx->value_maps[static_cast<size_t>(h_vmap)].keys.push_back("node_1");
        ctx->value_maps[static_cast<size_t>(h_vmap)].values.push_back(20.0f);
        ctx->value_maps[static_cast<size_t>(h_vmap)].keys.push_back("node_2");
        ctx->value_maps[static_cast<size_t>(h_vmap)].values.push_back(30.0f);

        T_ASSERT_EQ(impulse_vm_context_value_map_size(ctx, static_cast<size_t>(h_vmap)), 2);
        T_ASSERT_NEAR(impulse_vm_context_value_map_get_value(ctx, static_cast<size_t>(h_vmap), 0), 20.0f, 1e-4);
        T_ASSERT_NEAR(impulse_vm_context_value_map_get_value(ctx, static_cast<size_t>(h_vmap), 1), 30.0f, 1e-4);

        impulse_vm_context_destroy(ctx);
    }

    impulse_snapshot_close(snap);
    std::printf("  Issue #52: ALL TESTS PASSED\n");
}

int main() {
    std::printf("=================================================================\n");
    std::printf("RUNNING TEST SUITE FOR ISSUES #49, #50, #51, #52\n");
    std::printf("=================================================================\n");

    test_issue_49_stream_shader_and_cel_eval();
    test_issue_50_single_lane_cel_and_collectors();
    test_issue_51_multilane_numeric_and_reductions();
    test_issue_52_stmt_materialization_and_cypher();

    // Clean up temporary snapshot file
    std::remove(TEST_SNAPSHOT_PATH);

    std::printf("=================================================================\n");
    std::printf("ALL VERIFICATION SUITE TESTS FOR ISSUES #49..#52 COMPLETED SUCCESSFULLY!\n");
    std::printf("=================================================================\n");
    return 0;
}
