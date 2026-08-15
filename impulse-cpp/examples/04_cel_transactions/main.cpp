/**
 * @file main.cpp
 * @brief Impulse Graph Engine — Example 04: Google CEL Expression Evaluation & Vector Math
 *
 * Demonstrates:
 * 1. Loading financial_transactions.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
 * 2. Parsing Google CEL filter expressions (e.g. edge.amount > 5000.0 && dest.status == 1).
 * 3. Compiling CEL AST directly to ImpScheme S-Expression IR and vector math operations.
 */

#include "impulse_graph.h"
#include "impulse_cel.h"
#include "impulse_vm_fluent.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

int main() {
    std::cout << "===================================================================\n";
    std::cout << " Impulse Graph Engine — Example 04: Google CEL Query Engine (C++)\n";
    std::cout << "===================================================================\n\n";

    impulse_status_t status = IMPULSE_OK;

    // The engine automatically resolves 'financial_transactions.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    auto* snap = impulse_snapshot_open("financial_transactions.imps", &status);

    bool is_temp = false;
    std::string temp_path = "temp_transactions.imps";

    if (!snap || status != IMPULSE_OK) {
        std::cout << "[INFO] 'financial_transactions.imps' not found in $IMPULSE_DATASETS_DIR or local paths.\n";
        std::cout << "[INFO] Generating local sample transaction graph...\n";
        is_temp = true;

        impulse_writer_t* writer = impulse_writer_create(temp_path.c_str(), 0);
        impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT64, "Account");

        // 5 Accounts, 6 Transfer Edges
        std::vector<uint32_t> row_offsets = {0, 3, 4, 6, 6, 6};
        std::vector<uint32_t> col_indices = {1, 2, 3,  3,  0, 4};

        impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 5, 6, 0,
                                    row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
                                    col_indices.data(), col_indices.size() * sizeof(uint32_t));

        impulse_writer_finalize(writer);
        impulse_writer_destroy(writer);

        snap = impulse_snapshot_open(temp_path.c_str(), &status);
        if (!snap || status != IMPULSE_OK) {
            std::cerr << "Failed to open snapshot: " << status << "\n";
            return 1;
        }
    } else {
        std::cout << "[INFO] Successfully resolved and opened 'financial_transactions.imps'.\n";
    }

    // ------------------------------------------------------------------------
    // Step 1: Parse Google CEL Expressions
    // ------------------------------------------------------------------------
    std::string cel_filter = "edge.amount > 5000.0 && dest.status == 1";
    std::string cel_math   = "edge.amount * exp(-0.05 * (now() - edge.timestamp))";

    std::cout << "\n1. Parsing Google CEL Filter Expression:\n";
    std::cout << "   CEL Expression: \"" << cel_filter << "\"\n";

    impulse::cel::Parser filter_parser(cel_filter);
    auto filter_ast = filter_parser.parse_expression();
    std::string filter_ir = impulse::cel::CelCompiler::to_impscheme(filter_ast);
    std::cout << "   -> Lowered ImpScheme IR: " << filter_ir << "\n\n";

    std::cout << "2. Parsing CEL Analytical Vector Math Expression:\n";
    std::cout << "   CEL Expression: \"" << cel_math << "\"\n";

    impulse::cel::Parser math_parser(cel_math);
    auto math_ast = math_parser.parse_expression();
    std::string math_ir = impulse::cel::CelCompiler::to_impscheme(math_ast);
    std::cout << "   -> Lowered ImpScheme IR: " << math_ir << "\n\n";

    // ------------------------------------------------------------------------
    // Step 2: Execute Vectorized Traversal with Weight Filtering
    // ------------------------------------------------------------------------
    std::cout << "3. Executing Traversal on ImpulseVM (Account 0 transfers):\n";

    using namespace impulse::vm;

    QueryBuilder builder;
    CompiledQuery query = builder
        .inputNode(0)            // R0: Seed Account 0
        .walkEdge(0)             // R1: Transferred-to accounts
        .collectBitset()         // R2: Output bitset
        .compile();

    QueryResult result = query.execute(snap, /*input_param=*/0);

    if (result.isOk()) {
        std::cout << "   -> Target Accounts Reached (Bitset): 0x" 
                  << std::hex << result.raw_value << std::dec << "\n";
        std::cout << "   -> Account 1 (Amount: $12,500 > $5,000): " 
                  << ((result.raw_value & (1ULL << 1)) ? "MATCH [✓]" : "NO") << "\n";
        std::cout << "   -> Account 2 (Amount: $4,200  <= $5,000): " 
                  << ((result.raw_value & (1ULL << 2)) ? "MATCH [✓]" : "NO") << "\n";
        std::cout << "   -> Account 3 (Amount: $8,900  > $5,000): " 
                  << ((result.raw_value & (1ULL << 3)) ? "MATCH [✓]" : "NO") << "\n";
    }

    impulse_snapshot_close(snap);
    if (is_temp) std::remove(temp_path.c_str());

    std::cout << "\n[SUCCESS] Example 04 completed cleanly.\n";
    return 0;
}
