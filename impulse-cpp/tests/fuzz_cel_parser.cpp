/**
 * @file fuzz_cel_parser.cpp
 * @brief Fuzzing harness for Google CEL Parser, AST Optimizer, and Lowering Compiler.
 *
 * Supports both:
 * 1. Native LLVM libFuzzer (`-fsanitize=fuzzer`) via LLVMFuzzerTestOneInput
 * 2. Standalone deterministic runner generating corrupted/mutated CEL expressions.
 */

#include "impulse_cel.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#include <string>
#include <chrono>

namespace {
struct FuzzRng {
    uint64_t state;
    explicit FuzzRng(uint64_t seed = 0xabcdef0123456789ULL) : state(seed ? seed : 1ULL) {}
    inline uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    inline uint32_t next_u32() {
        return static_cast<uint32_t>(next() >> 32);
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        return static_cast<uint32_t>(next() % bound);
    }
};
} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }

    // Treat data as string (up to 4096 bytes)
    size_t len = size > 4096 ? 4096 : size;
    std::string expr_str(reinterpret_cast<const char*>(data), len);

    // 1. Invariant: Lexer & Pratt parser must never crash or fault
    impulse::cel::Parser parser(expr_str);
    std::shared_ptr<impulse::cel::AstNode> ast = parser.parse();

    if (ast) {
        // 2. Invariant: AST Optimizer must never crash or fault on valid AST
        auto optimized = impulse::cel::AstOptimizer::optimize(ast);

        // 3. Invariant: Lowering to ImpScheme S-Expression must never crash
        try {
            std::string sexpr = impulse::cel::CelCompiler::to_impscheme(optimized ? optimized : ast);
            (void)sexpr;
        } catch (...) {
            // Handled compile error is acceptable
        }
    }

    return 0;
}

#if !defined(IMPULSE_LIBFUZZER_ENABLED)

static const char* kBaselineExpressions[] = {
    "age > 21",
    "edge.miles > 100.0 && dest.priority == 1",
    "score * 2.5 + 10.0 <= 100.0",
    "sin(angle) * cos(angle) + 1.0",
    "status == 'ACTIVE' ? bonus * 1.1 : 0.0",
    "sqrt(x * x + y * y) < 50.0",
    "user.verified && !user.suspended",
    "a + b * c - d / e % f",
    "!(x > 10) || (y < 20 && z == 30)",
    "timestamp('2026-09-07T00:00:00Z') + duration('3600s')",
    "[1, 2, 3].all(x, x > 0)",
    "node.id in [10, 20, 30, 40]"
};

static std::string mutate_cel_expression(FuzzRng& rng, const std::string& base) {
    uint32_t mode = rng.bounded(5);
    std::string mutated = base;

    switch (mode) {
        case 0: {
            // Insert random character
            size_t pos = rng.bounded(static_cast<uint32_t>(mutated.size() + 1));
            char c = static_cast<char>(32 + rng.bounded(95));
            mutated.insert(pos, 1, c);
            break;
        }
        case 1: {
            // Delete character
            if (!mutated.empty()) {
                size_t pos = rng.bounded(static_cast<uint32_t>(mutated.size()));
                mutated.erase(pos, 1);
            }
            break;
        }
        case 2: {
            // Deeply nest with unclosed or unbalanced parentheses/brackets
            size_t count = 1 + rng.bounded(50);
            for (size_t i = 0; i < count; ++i) {
                char ch = (rng.bounded(2) == 0) ? '(' : '[';
                mutated.insert(0, 1, ch);
            }
            break;
        }
        case 3: {
            // Corrupt operators or inject special characters
            const char* injections[] = {"?", ":", "==", "!=", "&&", "||", "null", "NaN", "0.0", "-1", "'''", "\"\"\""};
            size_t idx = rng.bounded(sizeof(injections) / sizeof(injections[0]));
            size_t pos = rng.bounded(static_cast<uint32_t>(mutated.size() + 1));
            mutated.insert(pos, injections[idx]);
            break;
        }
        case 4: {
            // Generate arbitrary random token sequence
            size_t len = 5 + rng.bounded(60);
            mutated.clear();
            for (size_t i = 0; i < len; ++i) {
                mutated += static_cast<char>(32 + rng.bounded(95));
            }
            break;
        }
    }
    return mutated;
}

int main(int argc, char** argv) {
    size_t iterations = 20000;
    uint64_t seed = 555555555ULL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << " Google CEL Parser Fuzzing Suite (Deterministic Mode)" << std::endl;
    std::cout << " Iterations: " << iterations << " | Seed: " << seed << std::endl;
    std::cout << "============================================================" << std::endl;

    FuzzRng rng(seed);
    auto start_time = std::chrono::steady_clock::now();

    size_t valid_count = 0;
    size_t invalid_count = 0;

    size_t baseline_count = sizeof(kBaselineExpressions) / sizeof(kBaselineExpressions[0]);

    for (size_t iter = 0; iter < iterations; ++iter) {
        const std::string& base = kBaselineExpressions[rng.bounded(static_cast<uint32_t>(baseline_count))];
        std::string test_expr = mutate_cel_expression(rng, base);

        impulse::cel::Parser parser(test_expr);
        auto ast = parser.parse();
        if (ast) {
            valid_count++;
        } else {
            invalid_count++;
        }

        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(test_expr.data()), test_expr.size());

        if ((iter + 1) % 5000 == 0 || iter + 1 == iterations) {
            std::cout << "[Fuzz CEL] Progress: " << (iter + 1) << " / " << iterations
                      << " | Parsed Valid: " << valid_count
                      << " | Rejected: " << invalid_count << std::endl;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "[Fuzz CEL] COMPLETED " << iterations << " iterations in "
              << duration_ms << " ms (" << (static_cast<double>(iterations) / (duration_ms / 1000.0))
              << " exec/sec). Zero crashes." << std::endl;

    return 0;
}
#endif
