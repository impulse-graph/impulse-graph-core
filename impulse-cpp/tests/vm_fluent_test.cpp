#include "impulse_vm_fluent.hpp"
#include "impulse_graph.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

void test_fluent_basic_input_and_const() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .loadConstInt(42, 1)
           .loadConstFloat(3.14159f, 2)
           .mov(3, 1);

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 5); // 4 insts + HALT
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_LOAD_CONST_INT);
    assert(query.bytecode()[1].payload == 42);
    assert(query.bytecode()[2].opcode == OP_LOAD_CONST_FLOAT);
    assert(query.bytecode()[3].opcode == OP_MOV);
    assert(query.bytecode()[4].opcode == OP_HALT);

    impulse_vm_state_t state{};
    QueryResult res = query.executeWithContext(nullptr, &state, 100);

    assert(res.isOk());
    (void)res;
    assert(state.registers[0] == 100);
    assert(state.registers[1] == 42);
    assert(state.registers[3] == 42);

    std::cout << "[VM Fluent Test] Basic Input and Const: PASSED\n";
}

void test_fluent_graph_walk_and_sets() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .walkEdge(0)
           .filterNode(10)
           .unionWith(1)
           .collectBitset();

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 6);
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_CSR_WALK);
    assert(query.bytecode()[2].opcode == OP_NODE_FILTER);
    assert(query.bytecode()[3].opcode == OP_SET_UNION);
    assert(query.bytecode()[4].opcode == OP_COLLECT_BITSET);
    assert(query.bytecode()[5].opcode == OP_HALT);

    std::cout << "[VM Fluent Test] Graph Walk & Sets Compilation: PASSED\n";
}

void test_fluent_repeat_loops() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .repeat(3, [](QueryBuilder& q) {
               q.walkEdge(0);
           })
           .collectBitset();

    CompiledQuery query = builder.compile();

    // insts:
    // 0: INIT_INPUT_NODE
    // 1: LOAD_CONST_INT (count = 3 into counter_reg R1)
    // 2: CSR_WALK
    // 3: LOOP_DECR (R1, offset = -1) -> jump back to inst 2
    // 4: COLLECT_BITSET
    // 5: HALT
    assert(query.instructionCount() == 6);
    assert(query.bytecode()[1].opcode == OP_LOAD_CONST_INT);
    assert(query.bytecode()[1].payload == 3);
    assert(query.bytecode()[2].opcode == OP_CSR_WALK);
    assert(query.bytecode()[3].opcode == OP_LOOP_DECR);
    assert(static_cast<int32_t>(query.bytecode()[3].payload) == -1);

    std::cout << "[VM Fluent Test] Repeat Loop Jump Offsets: PASSED\n";
}

void test_fluent_repeat_until_stable() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .repeatUntilStable([](QueryBuilder& q) {
               q.walkEdge(0);
           });

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 4);
    assert(query.bytecode()[0].opcode == OP_INIT_INPUT_NODE);
    assert(query.bytecode()[1].opcode == OP_CSR_WALK);
    assert(query.bytecode()[2].opcode == OP_STABLE_CHECK);
    assert(static_cast<int32_t>(query.bytecode()[2].payload) == -1);
    assert(query.bytecode()[3].opcode == OP_HALT);

    std::cout << "[VM Fluent Test] Repeat Until Stable Jump Offsets: PASSED\n";
}

void test_fluent_extended_domain_and_graphblas() {
    using namespace impulse::vm;

    QueryBuilder builder;
    builder.inputNode(0)
           .matrixVectorMul(2, SEMIRING_PLUS_TIMES)
           .ewiseAdd(3, BINARY_OP_ADD)
           .afforest()
           .rebacCheck(101)
           .sampleNeighbors(0, 10, 42);

    CompiledQuery query = builder.compile();

    assert(query.instructionCount() == 7);
    assert(query.bytecode()[1].opcode == OP_MXV);
    assert(query.bytecode()[2].opcode == OP_EWISE_ADD);
    assert(query.bytecode()[3].opcode == OP_CC_AFFOREST);
    assert(query.bytecode()[4].opcode == OP_REBAC_CHECK);
    assert(query.bytecode()[5].opcode == OP_SAMPLE_NEIGHBORS);

    std::cout << "[VM Fluent Test] Extended Domain & GraphBLAS Compilation: PASSED\n";
}

void test_fluent_all_operations_exhaustive() {
    using namespace impulse::vm;
    std::cout << "[VM Fluent Test] Testing All Fluent QueryBuilder and QueryResult APIs..." << std::endl;

    QueryBuilder qb;
    const char* keys[] = {"k1", "k2"};
    qb.inputSet(0)
      .loadConstStrPrefix("prefix", 1)
      .loadKeys(keys, 2, 2)
      .walkEdgeFiltered(0, 101)
      .walkEdgePredicate(0, 102)
      .walkDegree(0)
      .walkReduceSum(0, 3)
      .walkCsc(0)
      .filterNodeStrPrefix("pref")
      .filterCel("age > 21")
      .intersectWith(1)
      .differenceWith(2)
      .cardinality()
      .vectorMulAttr(3)
      .vectorReduceSum()
      .vectorDiv(4)
      .l1NormDiff(5)
      .vectorMatrixMul(2, SEMIRING_PLUS_TIMES)
      .ewiseMult(3, BINARY_OP_MUL)
      .reduce(BINARY_OP_ADD)
      .tcSweepBatch()
      .brandesForward()
      .brandesBackward()
      .deltaStepRelax(4)
      .randomWalk(0, 10, 123)
      .scatterGather()
      .roaringBitmapAnd(2)
      .islandDetect(3)
      .sparseMatVec()
      .louvainModularity()
      .kcoreDecomposition()
      .motifMatch3()
      .graphIsomorphism()
      .clearReg(1)
      .loadIndirect(2, 0, 1, 0)
      .loadInlineArray(3, 0, 4)
      .initMockGraph(0, 0, 10)
      .throwErr(99)
      .assertVal(0, 100, 0)
      .trap(1)
      .nop()
      .jmp(2)
      .jz(1)
      .jnz(-1)
      .collectArray()
      .mapDenseToKeys()
      .collectValueMap();

    uint16_t cur_r = qb.currentRegister();
    qb.setCurrentRegister(cur_r + 1);
    assert(qb.currentRegister() == cur_r + 1);
    uint16_t reg = qb.allocateRegister();
    assert(reg >= 1);
    assert(!qb.rawInstructions().empty());

    CompiledQuery cq = qb.compile();
    assert(cq.instructionCount() > 40);

    // Test QueryResult accessors
    QueryResult qr;
    qr.status = IMPULSE_VM_OK;
    qr.raw_value = 0x40490fdb; // ~3.14159f in uint32
    assert(qr.isOk());
    assert(qr.asInt() == 0x40490fdb);
    assert(std::fabs(qr.asFloat() - 3.14159f) < 1e-4);

    double dval = 2.718281828;
    std::memcpy(&qr.raw_value, &dval, sizeof(double));
    assert(std::fabs(qr.asDouble() - 2.718281828) < 1e-6);

    assert(!qr.testBitset(nullptr, 0));
    qr.result_type = TYPE_BITSET_HANDLE;
    assert(!qr.testBitset(nullptr, 0));

    // Execute with null snapshot
    QueryResult ex_res = cq.execute(nullptr, 0);
    (void)ex_res;
}

static void test_fluent_query_builder_multilane_and_vector_ops() {
    using namespace impulse::vm;
    std::cout << "[VM Fluent Test] Testing QueryBuilder Multi-Lane & Vector Search Opcodes (0xC0..0xCB)..." << std::endl;

    QueryBuilder qb;
    qb.inputSet(0)
      .laneInitSeeds(0, 0)
      .laneInitGroups(1, 4)
      .laneSetEdgeMask(1, 0)
      .laneWalkCsr(0, false)
      .laneWalkCsc(0, true)
      .lanePruneMask(2, 500, 2)
      .laneFilterQuorum(3, 0xFF)
      .laneReduceAnd()
      .laneReduceAttr(0, 0)
      .laneSetCover(0xFF, 0)
      .vectorAnnSearch(2, 8, 0, true)
      .vectorRerankDot(2, 0, 1)
      .walkEdge2Hop(0, 1, 0)
      .floatVectorScale(0.5f)
      .vectorTimeValidAt(3, 1, 2)
      .reduceMin()
      .reduceMax()
      .argMin()
      .argMax();

    CompiledQuery cq = qb.compile();
    const auto& bc = cq.bytecode();
    assert(bc[1].opcode == OP_LANE_INIT_SEEDS);
    assert(bc[2].opcode == OP_LANE_INIT_GROUPS);
    assert(bc[3].opcode == OP_LANE_SET_EDGE_MASK);
    assert(bc[4].opcode == OP_LANE_CSR_WALK);
    assert(bc[5].opcode == OP_LANE_CSC_WALK);
    assert(bc[5].flags == 1);
    assert(bc[6].opcode == OP_LANE_PRUNE_MASK);
    assert(bc[7].opcode == OP_LANE_FILTER_QUORUM);
    assert(bc[8].opcode == OP_LANE_REDUCE_AND);
    assert(bc[9].opcode == OP_LANE_REDUCE_ATTR);
    assert(bc[10].opcode == OP_LANE_SET_COVER);
    assert(bc[11].opcode == OP_VECTOR_ANN_SEARCH);
    assert(bc[11].flags == 1);
    assert(bc[12].opcode == OP_VECTOR_RERANK_DOT);
    assert(bc[12].flags == 1);
    assert(bc[13].opcode == OP_CSR_WALK_2HOP);
    assert(bc[14].opcode == OP_FLOAT_VECTOR_SCALE);
    assert(bc[15].opcode == OP_VECTOR_TIME_VALID_AT);
    assert(bc[16].opcode == OP_REDUCE && bc[16].payload == BINARY_OP_MIN);
    assert(bc[17].opcode == OP_REDUCE && bc[17].payload == BINARY_OP_MAX);
    assert(bc[18].opcode == OP_REDUCE && bc[18].payload == BINARY_OP_ARGMIN);
    assert(bc[19].opcode == OP_REDUCE && bc[19].payload == BINARY_OP_ARGMAX);

    std::cout << "[VM Fluent Test] QueryBuilder Multi-Lane & Vector Search Opcodes: PASSED\n";
}

static void test_domain_multilane_17_boundaries_and_fluent_pipeline() {
    using namespace impulse::vm;
    std::cout << "[VM Fluent Test] Testing High-Level DomainView::multiLane() across 17 Seed Boundaries..." << std::endl;

    const char* snap_path = "test_fluent_multilane_17.imps";
    impulse_writer_t* writer = impulse_writer_create(snap_path, 0);
    assert(writer != nullptr);
    assert(impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "Gene") == IMPULSE_OK);

    // 200 nodes: node i has edges to (i + 1) % 200, (i + 2) % 200, and hub node 199 (for i < 150)
    const uint32_t N = 200;
    std::vector<uint32_t> offsets(N + 1, 0);
    std::vector<uint32_t> targets;
    for (uint32_t u = 0; u < N; ++u) {
        offsets[u] = static_cast<uint32_t>(targets.size());
        uint32_t t1 = (u + 1) % N;
        uint32_t t2 = (u + 2) % N;
        targets.push_back(std::min(t1, t2));
        targets.push_back(std::max(t1, t2));
        if (u < 150 && t1 != 199 && t2 != 199) {
            targets.push_back(199);
        }
    }
    offsets[N] = static_cast<uint32_t>(targets.size());

    assert(impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, N, targets.size(), 0,
                                       offsets.data(), offsets.size() * sizeof(uint32_t),
                                       targets.data(), targets.size() * sizeof(uint32_t)) == IMPULSE_OK);

    // Add float node attribute "score" = (u + 1) * 1.5f
    std::vector<float> scores(N);
    for (uint32_t u = 0; u < N; ++u) {
        scores[u] = static_cast<float>(u + 1) * 1.5f;
    }
    assert(impulse_writer_add_attribute(writer, 0, "score", IMPULSE_ATTR_TYPE_FLOAT32, 1,
                                        scores.data(), scores.size() * sizeof(float),
                                        nullptr, 0) == IMPULSE_OK);

    assert(impulse_writer_finalize(writer) == IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* raw_snap = impulse_snapshot_open(snap_path, &st);
    assert(raw_snap != nullptr && st == IMPULSE_OK);

    SnapshotView snap(raw_snap);

    // 1. Verify all 17 exact seed-count boundaries against K independent 1-way traversals!
    const int kBoundaries[17] = {0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129};
    for (int K : kBoundaries) {
        std::vector<uint64_t> seeds;
        seeds.reserve(static_cast<size_t>(K));
        for (int i = 0; i < K; ++i) {
            seeds.push_back(static_cast<uint64_t>(i));
        }

        MultiLaneResult ml_res = snap.domain("Gene").multiLane()
            .fromSeeds(seeds)
            .out("rel_0")
            .collect();

        assert(static_cast<int>(ml_res.laneCount()) == K);

        if (K == 0) {
            assert(ml_res.nodesWithQuorum(1).empty());
            continue;
        }

        // Verify for every lane b in 0..K-1 that the set of nodes with lane b set matches 1-way traversal from seed b
        for (int b = 0; b < K; ++b) {
            std::vector<uint64_t> single_hop = snap.domain("Gene").from({static_cast<uint64_t>(b)}).out("rel_0").collect();
            for (uint64_t target_u : single_hop) {
                std::vector<uint32_t> attr_lanes = ml_res.attributionFor(static_cast<uint32_t>(target_u));
                bool found = false;
                for (uint32_t lb : attr_lanes) {
                    if (static_cast<int>(lb) == b) { found = true; break; }
                }
                assert(found);
            }
        }

        // Hub node 199 is reached by all seeds < 150
        std::vector<uint32_t> hub_lanes = ml_res.attributionFor(199);
        assert(static_cast<int>(hub_lanes.size()) == K);
    }

    // 2. Test named-set helpers (.mustHitAtLeast & .mustAvoidAll), set cover, and per-lane reductions
    std::vector<std::string> gene_keys = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };
    std::vector<std::string> driver_keys = {"0", "1", "2", "3", "4", "5", "6", "7"};
    std::vector<std::string> safety_keys = {"8", "9"};

    MultiLaneResult panel_res = snap.domain("Gene").multiLane()
        .fromKeys(gene_keys)
        .out("rel_0")
        .mustHitAtLeast(2, driver_keys)
        .mustAvoidAll(safety_keys)
        .collect();

    // Node 2 is reached by seed 0 (0+2) and seed 1 (1+1) -> 2 drivers, 0 safety!
    auto reached_quorum = panel_res.nodesWithQuorum(2);
    bool has_node_2 = false;
    for (uint32_t u : reached_quorum) {
        if (u == 2) has_node_2 = true;
        // Node 199 was reached by safety seeds 8 and 9 too, so mustAvoidAll(safety_keys) excluded it!
        assert(u != 199);
    }
    assert(has_node_2);

    // 3. Test per-lane reductions (SUM, MIN, MAX, ARGMIN, ARGMAX)
    auto ml_base_trav = snap.domain("Gene").multiLane().fromSeeds({0, 1, 2}).out("rel_0");
    std::vector<double> sums = ml_base_trav.reducePerLane("score", ReducerOp::SUM);
    std::vector<int64_t> argmins = ml_base_trav.argMinNodesPerLane("score");
    std::vector<int64_t> argmaxs = ml_base_trav.argMaxNodesPerLane("score");
    assert(sums.size() == 3);
    assert(argmins.size() == 3);
    assert(argmaxs.size() == 3);
    // Seed 0 reaches {1, 2, 199}: min score is node 1, max score is node 199
    assert(argmins[0] == 1);
    assert(argmaxs[0] == 199);
    // Seed 1 reaches {2, 3, 199}: min score is node 2, max score is node 199
    assert(argmins[1] == 2);
    assert(argmaxs[1] == 199);

    // 4. Test Single-Lane Traversal Completeness Methods (Part 5)
    auto t_2hop = snap.domain("Gene").from({0}).out2Hop("rel_0", "rel_0").collect();
    assert(!t_2hop.empty());

    auto t_union = snap.domain("Gene").from({0}).out("rel_0")
        .unionWith(snap.domain("Gene").from({10}).out("rel_0"))
        .collect();
    assert(t_union.size() >= 4);

    auto t_inter = snap.domain("Gene").from({0}).out("rel_0")
        .intersect(snap.domain("Gene").from({1}).out("rel_0"))
        .collect();
    // Both 0 and 1 reach node 2 and node 199
    assert(t_inter.size() == 2);

    impulse_snapshot_close(raw_snap);
    std::remove(snap_path);

    std::cout << "[VM Fluent Test] High-Level DomainView::multiLane() 17 Boundaries & Single-Lane Completeness: PASSED\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "       Impulse VM C++ Fluent Style API Test Suite         \n";
    std::cout << "=========================================================\n";

    test_fluent_basic_input_and_const();
    test_fluent_graph_walk_and_sets();
    test_fluent_repeat_loops();
    test_fluent_repeat_until_stable();
    test_fluent_extended_domain_and_graphblas();
    test_fluent_all_operations_exhaustive();
    test_fluent_query_builder_multilane_and_vector_ops();
    test_domain_multilane_17_boundaries_and_fluent_pipeline();

    std::cout << "=========================================================\n";
    std::cout << " ALL VM FLUENT API TESTS PASSED SUCCESSFULLY!            \n";
    std::cout << "=========================================================\n";
    return 0;
}
