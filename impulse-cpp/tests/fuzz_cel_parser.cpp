/**
 * @file fuzz_cel_parser.cpp
 * @brief Fuzz target: CEL lexer + Pratt parser + AstOptimizer (constant folding) + ImpScheme lowering
 *        (impulse_cel.h). Header-only code; compiled directly into the target.
 *
 * Invariant: no crash/UB on arbitrary text. Deliberately rejecting input (nullptr AST, thrown
 * std::exception from the lowering/stoull paths) is permitted behavior.
 */

#include "fuzz_driver.hpp"
#include "impulse_cel.h"

#include <exception>
#include <memory>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!data) return 0;
    std::string expr(reinterpret_cast<const char*>(data), size > 4096 ? 4096 : size);
    try {
        impulse::cel::Parser parser(expr);
        std::shared_ptr<impulse::cel::AstNode> ast = parser.parse();
        if (ast) {
            auto opt = impulse::cel::AstOptimizer::optimize(ast);
            std::string s = impulse::cel::CelCompiler::to_impscheme(opt ? opt : ast);
            (void)s;
        }
    } catch (const std::exception&) {
        // Handled rejection (e.g. std::out_of_range from numeric literal conversion).
    }
    return 0;
}

static std::vector<impfuzz::Bytes> make_seeds() {
    static const char* kSeeds[] = {
        "age > 21", "a ? b : c", "f(1, 2.5, 'x')", "[1,2,3]", "x.y.z && !w", "1 << 70", "0x7FFFFFFFFFFFFFFF * 2",
        "((((((((((1))))))))))", "1.5e308 * 1.5e308", "'\\u00e9\\n'", "a.b(c)"};
    std::vector<impfuzz::Bytes> out;
    for (const char* s : kSeeds) out.emplace_back(s, s + std::strlen(s));
    return out;
}

IMPULSE_FUZZ_MAIN("fuzz_cel_parser", make_seeds, 512)
