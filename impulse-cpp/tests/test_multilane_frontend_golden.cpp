/**
 * @file test_multilane_frontend_golden.cpp
 * @brief Frontend golden tests for multi-lane lowering (v0.9.1, lane P1).
 *
 * Covers:
 *   1. CEL lexer/parser: `lane`, popcount/bit_count, has_any/has_all/has_none/has_lane,
 *      first_lane/last_lane, hex/binary literals, bitwise precedence, unary ops, 64-bit
 *      boundaries, malformed/overflowing literals (must be a parse error, never a crash).
 *   2. Golden ImpScheme S-expressions + constant-folding goldens for those expressions.
 *   3. CelMultiLaneEvaluator vs. an independent C++ reference over many lane values
 *      (raw AST and AstOptimizer-folded AST).
 *   4. End-to-end: snapshot -> `.multiLane().fromSeeds().out().filter/pruneWhere/project(<CEL>)`
 *      compared with the C++ reference applied to the un-filtered per-node lane masks.
 *   5. Exact opcode/flags/dst/payload goldens for ImpK, ImpLog (Datalog) and Cypher lowering
 *      of multi-lane constructs (0xC0..0xC9), and for the fluent QueryBuilder emitters.
 *   6. Execution of compiler-emitted and QueryBuilder-emitted bytecode on the VM versus a
 *      single-lane reference oracle (fixed-point walk semantics).
 *   7. Documented current behaviour for design-spec constructs that are not (fully) implemented.
 *
 * Uses a counting CHECK macro (never compiled out) so every failure is reported.
 */

#include "impulse_cel.h"
#include "impulse_impk.hpp"
#include "impulse_datalog.hpp"
#include "impulse_cypher.hpp"
#include "impulse_compiler.hpp"
#include "impulse_sexpr.hpp"
#include "impulse_vm_fluent.hpp"
#include "impulse_graph.h"
#include "impulse_vm.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace impulse::cel;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(cond)) {                                                                   \
            ++g_failures;                                                                \
            std::printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
        }                                                                                \
    } while (0)

#define CHECK_MSG(cond, ...)                                                             \
    do {                                                                                 \
        ++g_checks;                                                                      \
        if (!(cond)) {                                                                   \
            ++g_failures;                                                                \
            std::printf("  [FAIL] %s:%d: %s :: ", __FILE__, __LINE__, #cond);            \
            std::printf(__VA_ARGS__);                                                    \
            std::printf("\n");                                                           \
        }                                                                                \
    } while (0)

namespace {

constexpr uint64_t kAllOnes = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint64_t kTopBit = 0x8000000000000000ULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse (full-consumption) and lower to ImpScheme. Returns "<ERR>" on parse failure and
// "<THROW:...>" if the parser throws (which is itself a defect for malformed input).
std::string scm_of(const std::string& src) {
    try {
        Parser p(src);
        auto ast = p.parse();
        if (!ast) return "<ERR>";
        return CelCompiler::to_impscheme(ast);
    } catch (const std::exception& e) {
        return std::string("<THROW:") + e.what() + ">";
    }
}

// Parse + AstOptimizer constant fold, then lower to ImpScheme.
std::string folded_of(const std::string& src) {
    try {
        Parser p(src);
        auto ast = p.parse();
        if (!ast) return "<ERR>";
        return CelCompiler::to_impscheme(AstOptimizer::optimize(ast));
    } catch (const std::exception& e) {
        return std::string("<THROW:") + e.what() + ">";
    }
}

uint64_t xorshift64(uint64_t& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

std::vector<uint64_t> lane_corpus() {
    std::vector<uint64_t> v = {
        0ULL, 1ULL, 2ULL, 3ULL, kTopBit, kAllOnes, 0x7FFFFFFFFFFFFFFFULL,
        0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, 0xFFFFFFFFULL,
        0xFFFFFFFF00000000ULL, 0x00000000FFFFFFFFULL, 0xFFULL, 0xFF00ULL,
        kTopBit | 1ULL, 0x00FF00FF00FF00FFULL, 0x8000000000000001ULL,
    };
    for (int b = 0; b < 64; ++b) v.push_back(1ULL << b);
    for (int k = 0; k <= 64; ++k) v.push_back(k == 64 ? kAllOnes : ((1ULL << k) - 1ULL));
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 300; ++i) v.push_back(xorshift64(s));
    return v;
}

int64_t ref_ctz(uint64_t v) { return v ? static_cast<int64_t>(std::countr_zero(v)) : -1; }
int64_t ref_last(uint64_t v) { return v ? static_cast<int64_t>(63 - std::countl_zero(v)) : -1; }

// ---------------------------------------------------------------------------
// 1. Literals, malformed input
// ---------------------------------------------------------------------------
void test_cel_literals_and_errors() {
    std::printf("[Test] CEL hex/binary literals, 64-bit boundary, malformed literals\n");

    // Valid literals (lowering is decimal int64, two's complement for >= 2^63).
    CHECK(scm_of("0xFF") == "255");
    CHECK(scm_of("0XfF") == "255");
    CHECK(scm_of("0x0") == "0");
    CHECK(scm_of("0x00") == "0");
    CHECK(scm_of("0xFFFFFFFFFFFFFFFF") == "-1");
    CHECK(scm_of("0x8000000000000000") == "-9223372036854775808");
    CHECK(scm_of("0x7FFFFFFFFFFFFFFF") == "9223372036854775807");
    CHECK(scm_of("0x0000000000000000FF") == "255");  // 18 digits, leading zeros are fine
    CHECK(scm_of("0xDEAD_BEEF") == "3735928559");
    CHECK(scm_of("0b1010") == "10");
    CHECK(scm_of("0B11") == "3");
    CHECK(scm_of("0b0") == "0");
    CHECK(scm_of("0b0011_1111") == "63");
    CHECK(scm_of("0b" + std::string(64, '1')) == "-1");
    CHECK(scm_of("0b1" + std::string(63, '0')) == "-9223372036854775808");
    CHECK(scm_of("0b" + std::string(70, '0') + "1") == "1");
    CHECK(scm_of("9223372036854775807") == "9223372036854775807");
    CHECK(scm_of("0") == "0");

    // Malformed / overflowing literals: must be a parse error (nullptr), never a throw/crash.
    const char* bad[] = {
        "0x",                      // no digits
        "0X",
        "0b",
        "0B",
        "0b2",                     // non-binary digit
        "0b102",
        "0xG",                     // non-hex digit
        "0x1G",
        "0x_",                     // separator only
        "0b_",
        "0x1.5",                   // hex float not supported
        "0x10000000000000000",     // 65 bits
        "0xFFFFFFFFFFFFFFFFF",     // 17 f's
        "0x1_0000_0000_0000_0000",
        "9223372036854775808",     // INT64_MAX + 1
        "99999999999999999999",
        "18446744073709551616",
        "1e999",                   // double overflow
        ("0b1111111111111111111111111111111111111111111111111111111111111111"
         "1"),                     // 65 binary digits
        "",                        // empty
        "   ",
    };
    for (const char* e : bad) {
        std::string r = scm_of(e);
        CHECK_MSG(r == "<ERR>", "expr='%s' got '%s'", e, r.c_str());
    }

    // Trailing garbage / truncated expressions must not parse.
    const char* bad2[] = {
        "lane &", "lane |", "lane ^", "lane <<", "lane >>", "~", "!", "-",
        "popcount(", "popcount(lane", "popcount(lane,", "has_any(lane,", "has_lane(,)",
        "(lane", "((lane)", "lane)", "1 2", "lane lane", "lane # 1", "lane $", "0 x1",
        "lane ?", "lane ? 1", "lane ? 1 :", "[1,", "1.", "lane.",
    };
    for (const char* e : bad2) {
        std::string r = scm_of(e);
        CHECK_MSG(r == "<ERR>", "expr='%s' got '%s'", e, r.c_str());
    }

    // AstOptimizer must never crash on whatever parse_expression() hands back for malformed
    // input (the fluent layer calls parse_expression() + optimize() without full-parse check).
    const char* partial[] = {"popcount(", "abs(", "foo(,)", "lane ?", "[1,", "lane ? 1", "~", "lane &"};
    for (const char* e : partial) {
        try {
            Parser p(e);
            auto ast = p.parse_expression();
            auto opt = AstOptimizer::optimize(ast);
            (void)opt;
            CHECK(true);
        } catch (const std::exception&) {
            CHECK(true);  // a clean exception is tolerated here; a crash is not
        }
    }

    // Deeply nested input must not blow the stack at moderate depth.
    {
        std::string deep(2000, '(');
        deep += "lane";
        deep += std::string(2000, ')');
        CHECK(scm_of(deep) == "lane");
        std::string tildes(2000, '~');
        std::string r = scm_of(tildes + "lane");
        CHECK(r.rfind("(~ (~ ", 0) == 0);
    }
}

// ---------------------------------------------------------------------------
// 2. Golden S-expressions (exact lowering), precedence, unary ops
// ---------------------------------------------------------------------------
struct ScmGolden {
    const char* src;
    const char* scm;
};

void test_cel_scheme_goldens() {
    std::printf("[Test] CEL -> ImpScheme goldens (lane intrinsics, precedence, unary)\n");

    static const ScmGolden kGoldens[] = {
        // lane variable and intrinsics
        {"lane", "lane"},
        {"node.lane", "(get-attr node \"lane\")"},
        {"popcount(lane)", "(popcount lane)"},
        {"bit_count(lane)", "(bit_count lane)"},
        {"popcount(lane) >= 3", "(>= (popcount lane) 3)"},
        {"popcount(lane) > 3", "(vec-cmp-gt (popcount lane) 3)"},
        {"popcount(lane) == 0", "(vec-cmp-eq (popcount lane) 0)"},
        {"popcount(lane) != 0", "(mask-not (vec-cmp-eq (popcount lane) 0))"},
        {"has_any(lane, 0xFF)", "(has_any lane 255)"},
        {"has_all(lane, 0b101)", "(has_all lane 5)"},
        {"has_none(lane, 0x8000000000000000)", "(has_none lane -9223372036854775808)"},
        {"has_lane(lane, 63)", "(has_lane lane 63)"},
        {"first_lane(lane)", "(first_lane lane)"},
        {"last_lane(lane)", "(last_lane lane)"},
        {"ctz(lane)", "(ctz lane)"},
        {"clz(lane)", "(clz lane)"},
        {"first_lane(lane) == 0 - 1", "(vec-cmp-eq (first_lane lane) (- 0 1))"},

        // bitwise precedence (this grammar: shift > & > ^ > | > comparison > && > ||; matches
        // DESIGN_SPEC §5.2 -- note: bitwise binds TIGHTER than ==, unlike C)
        {"a | b & c", "(| a (& b c))"},
        {"a & b | c", "(| (& a b) c)"},
        {"a ^ b | c", "(| (^ a b) c)"},
        {"a | b ^ c", "(| a (^ b c))"},
        {"a & b ^ c", "(^ (& a b) c)"},
        {"a ^ b & c", "(^ a (& b c))"},
        {"a & b ^ c | d", "(| (^ (& a b) c) d)"},
        {"a << 1 & b", "(& (<< a 1) b)"},
        {"a & b << 1", "(& a (<< b 1))"},
        {"a + b << 1", "(<< (+ a b) 1)"},
        {"a << b + 1", "(<< a (+ b 1))"},
        {"a * b << 1", "(<< (* a b) 1)"},
        {"a >> 1 >> 2", "(>> (>> a 1) 2)"},
        {"a << 1 << 2", "(<< (<< a 1) 2)"},
        {"a - b - c", "(- (- a b) c)"},
        {"lane >>> 1", "(>>> lane 1)"},
        {"a == b & c", "(vec-cmp-eq a (& b c))"},
        {"a & b == c", "(vec-cmp-eq (& a b) c)"},
        {"a | b == c", "(vec-cmp-eq (| a b) c)"},
        {"a == b | c", "(vec-cmp-eq a (| b c))"},
        {"a & b > c", "(vec-cmp-gt (& a b) c)"},
        {"a && b | c", "(mask-and a (| b c))"},
        {"a | b && c", "(mask-and (| a b) c)"},
        {"a || b && c", "(mask-or a (mask-and b c))"},
        {"a && b || c", "(mask-or (mask-and a b) c)"},
        {"(a | b) & c", "(& (| a b) c)"},
        {"a | (b & c)", "(| a (& b c))"},
        {"((a))", "a"},
        {"(lane & 0x7F) == 0x7F", "(vec-cmp-eq (& lane 127) 127)"},
        {"popcount(lane & 0x7F) > 3", "(vec-cmp-gt (popcount (& lane 127)) 3)"},
        {"popcount(lane) >= 3 && has_none(lane, 0x8000000000000000) || lane == 0",
         "(mask-or (mask-and (>= (popcount lane) 3) (has_none lane -9223372036854775808))"
         " (vec-cmp-eq lane 0))"},
        {"has_all(lane, 0x3) ? has_none(lane, 0xC) : has_any(lane, 0xF0)",
         "(vec-blend (has_all lane 3) (has_none lane 12) (has_any lane 240))"},

        // unary operators
        {"~lane", "(~ lane)"},
        {"~lane & 0xFF", "(& (~ lane) 255)"},
        {"~(lane & 0xFF)", "(~ (& lane 255))"},
        {"-lane", "(- 0 lane)"},
        {"- -lane", "(- 0 (- 0 lane))"},
        {"+lane", "lane"},
        {"!lane", "(mask-not lane)"},
        {"!!lane", "(mask-not (mask-not lane))"},
        {"!has_any(lane, 1)", "(mask-not (has_any lane 1))"},
        {"~~lane", "(~ (~ lane))"},
        {"-popcount(lane)", "(- 0 (popcount lane))"},
        {"!popcount(lane) == 0", "(vec-cmp-eq (mask-not (popcount lane)) 0)"},
        {"~lane << 1", "(<< (~ lane) 1)"},
        {"-a * b", "(* (- 0 a) b)"},

        // multi-lane project assignment form
        {"lane = lane & ~node.denied", "(= lane (& lane (~ (get-attr node \"denied\"))))"},
        {"lane = node.ok ? lane : lane & 0x0F",
         "(= lane (vec-blend (get-attr node \"ok\") lane (& lane 15)))"},
    };

    for (const auto& g : kGoldens) {
        std::string got = scm_of(g.src);
        CHECK_MSG(got == g.scm, "src='%s'\n      expected '%s'\n      got      '%s'", g.src, g.scm,
                  got.c_str());
    }
}

// ---------------------------------------------------------------------------
// 3. Constant-folding goldens
// ---------------------------------------------------------------------------
void test_cel_constant_folding_goldens() {
    std::printf("[Test] CEL AstOptimizer constant-folding goldens (64-bit boundary)\n");

    static const ScmGolden kFold[] = {
        {"1 << 0", "1"},
        {"1 << 62", "4611686018427387904"},
        {"1 << 63", "-9223372036854775808"},
        {"1 << 64", "0"},
        {"1 << 100", "0"},
        {"1 << (0 - 1)", "0"},
        {"0xFF >>> 64", "0"},
        {"0xFFFFFFFFFFFFFFFF >>> 63", "1"},
        {"0xFFFFFFFFFFFFFFFF >>> 1", "9223372036854775807"},
        {"0xFF & 0x0F", "15"},
        {"0xF0 | 0x0F", "255"},
        {"0xFF ^ 0x0F", "240"},
        {"0xFFFFFFFFFFFFFFFF ^ 0xFFFFFFFFFFFFFFFF", "0"},
        {"~0", "-1"},
        {"~0xFFFFFFFFFFFFFFFF", "0"},
        {"~~5", "5"},
        {"- -3", "3"},
        {"1 + 2 << 3", "24"},
        {"0xF0 & 0x3C | 0x01", "49"},
        {"0b1010 ^ 0b0110", "12"},
        // intrinsic folding on 64-bit literals (integer semantics, not via double)
        {"popcount(0)", "0"},
        {"popcount(0x7F)", "7"},
        {"popcount(0xFFFFFFFFFFFFFFFF)", "64"},
        {"bit_count(0x8000000000000000)", "1"},
        {"popcount(0x7FFFFFFFFFFFFFFF)", "63"},
        {"first_lane(0)", "-1"},
        {"first_lane(0x18)", "3"},
        {"first_lane(0x8000000000000000)", "63"},
        {"ctz(0)", "-1"},
        {"ctz(0x40)", "6"},
        {"clz(0)", "64"},
        {"clz(1)", "63"},
        {"clz(0x8000000000000000)", "0"},
        // non-foldable stays symbolic
        {"popcount(lane)", "(popcount lane)"},
        {"last_lane(0)", "(last_lane 0)"},  // no math-func id for last_lane: left to evaluator
    };
    for (const auto& g : kFold) {
        std::string got = folded_of(g.src);
        CHECK_MSG(got == g.scm, "fold src='%s'\n      expected '%s'\n      got      '%s'", g.src,
                  g.scm, got.c_str());
    }
}

// ---------------------------------------------------------------------------
// 4. Evaluator differential vs C++ reference
// ---------------------------------------------------------------------------
struct IntCase {
    const char* src;
    std::function<int64_t(uint64_t)> ref;
};
struct BoolCase {
    const char* src;
    std::function<bool(uint64_t)> ref;
};

std::vector<IntCase> int_cases() {
    auto S = [](uint64_t x) { return static_cast<int64_t>(x); };
    return {
        {"lane", [=](uint64_t L) { return S(L); }},
        {"popcount(lane)", [=](uint64_t L) { return S(std::popcount(L)); }},
        {"bit_count(lane & 0xFF00FF)", [=](uint64_t L) { return S(std::popcount(L & 0xFF00FFULL)); }},
        {"first_lane(lane)", [=](uint64_t L) { return ref_ctz(L); }},
        {"ctz(lane)", [=](uint64_t L) { return ref_ctz(L); }},
        {"last_lane(lane)", [=](uint64_t L) { return ref_last(L); }},
        {"clz(lane)", [=](uint64_t L) { return S(std::countl_zero(L)); }},
        {"lane & 0xFF | 0xF00", [=](uint64_t L) { return S((L & 0xFFULL) | 0xF00ULL); }},
        {"lane | 0xFF & 0x0F", [=](uint64_t L) { return S(L | (0xFFULL & 0x0FULL)); }},
        {"lane ^ lane", [=](uint64_t) { return int64_t{0}; }},
        {"lane ^ 0xFFFFFFFFFFFFFFFF", [=](uint64_t L) { return S(~L); }},
        {"lane ^ 0xFF | 0x100 & lane", [=](uint64_t L) { return S((L ^ 0xFFULL) | (0x100ULL & L)); }},
        {"lane << 1", [=](uint64_t L) { return S(L << 1); }},
        {"lane << 63", [=](uint64_t L) { return S(L << 63); }},
        {"lane << 64", [=](uint64_t) { return int64_t{0}; }},
        {"lane << (0 - 1)", [=](uint64_t) { return int64_t{0}; }},
        {"lane << 1 << 1", [=](uint64_t L) { return S(L << 2); }},
        {"lane >>> 1", [=](uint64_t L) { return S(L >> 1); }},
        {"lane >>> 63", [=](uint64_t L) { return S(L >> 63); }},
        {"lane >>> 64", [=](uint64_t) { return int64_t{0}; }},
        {"~lane", [=](uint64_t L) { return S(~L); }},
        {"~lane & 0xFFFF", [=](uint64_t L) { return S(~L & 0xFFFFULL); }},
        {"~~lane", [=](uint64_t L) { return S(L); }},
        {"- -lane", [=](uint64_t L) { return S(L); }},
        {"-lane", [=](uint64_t L) { return S(uint64_t{0} - L); }},
        {"popcount(lane) + popcount(~lane)", [=](uint64_t) { return int64_t{64}; }},
        {"(lane & 1) << 5 | lane >>> 63", [=](uint64_t L) { return S(((L & 1ULL) << 5) | (L >> 63)); }},
        {"lane & 1 << 5", [=](uint64_t L) { return S(L & (1ULL << 5)); }},
        {"lane + 1 << 2", [=](uint64_t L) { return S((L + 1ULL) << 2); }},
        {"lane + 1", [=](uint64_t L) { return S(L + 1ULL); }},
        {"lane - 1", [=](uint64_t L) { return S(L - 1ULL); }},
        {"lane * 3", [=](uint64_t L) { return S(L * 3ULL); }},
        {"lane * lane", [=](uint64_t L) { return S(L * L); }},
        {"lane / (0 - 1)", [=](uint64_t L) { return S(uint64_t{0} - L); }},  // INT64_MIN/-1 wraps
        {"lane % (0 - 1)", [=](uint64_t) { return int64_t{0}; }},
        {"lane / 0", [=](uint64_t) { return int64_t{0}; }},
        {"lane % 0", [=](uint64_t) { return int64_t{0}; }},
        {"popcount(lane & 0x7F) > 3 ? 1 : 0", [=](uint64_t L) { return S(std::popcount(L & 0x7FULL) > 3 ? 1 : 0); }},
        {"has_any(lane, 3) ? last_lane(lane) : first_lane(lane)",
         [=](uint64_t L) { return (L & 3ULL) ? ref_last(L) : ref_ctz(L); }},
    };
}

std::vector<BoolCase> bool_cases() {
    return {
        {"has_any(lane, 0xF0)", [](uint64_t L) { return (L & 0xF0ULL) != 0; }},
        {"has_any(lane, 0)", [](uint64_t) { return false; }},
        {"has_all(lane, 0xF0)", [](uint64_t L) { return (L & 0xF0ULL) == 0xF0ULL; }},
        {"has_all(lane, 0)", [](uint64_t) { return true; }},
        {"has_all(lane, 0xFFFFFFFFFFFFFFFF)", [](uint64_t L) { return L == kAllOnes; }},
        {"has_none(lane, 0xF0)", [](uint64_t L) { return (L & 0xF0ULL) == 0; }},
        {"has_none(lane, 0x8000000000000000)", [](uint64_t L) { return (L & kTopBit) == 0; }},
        {"has_lane(lane, 0)", [](uint64_t L) { return (L & 1ULL) != 0; }},
        {"has_lane(lane, 63)", [](uint64_t L) { return (L >> 63) != 0; }},
        {"has_lane(lane, 64)", [](uint64_t) { return false; }},
        {"has_lane(lane, 0 - 1)", [](uint64_t) { return false; }},
        {"has_lane(lane, 1000)", [](uint64_t) { return false; }},
        {"popcount(lane) >= 3 && has_none(lane, 0x8000000000000000)",
         [](uint64_t L) { return std::popcount(L) >= 3 && (L & kTopBit) == 0; }},
        {"popcount(lane) == 0 || lane == 0xFFFFFFFFFFFFFFFF",
         [](uint64_t L) { return L == 0 || L == kAllOnes; }},
        {"!has_any(lane, 3) || has_lane(lane, 5) && lane != 0",
         [](uint64_t L) { return !((L & 3ULL) != 0) || (((L >> 5) & 1ULL) && L != 0); }},
        {"lane & 0xFF == 0xFF", [](uint64_t L) { return (L & 0xFFULL) == 0xFFULL; }},
        {"(lane >>> 63) == 1", [](uint64_t L) { return (L >> 63) == 1; }},
        {"lane < 0", [](uint64_t L) { return static_cast<int64_t>(L) < 0; }},
        {"lane == 0", [](uint64_t L) { return L == 0; }},
        {"first_lane(lane) == 0 - 1", [](uint64_t L) { return L == 0; }},
        {"last_lane(lane) > first_lane(lane)", [](uint64_t L) { return ref_last(L) > ref_ctz(L); }},
        {"has_all(lane, 0x3) ? has_none(lane, 0xC) : has_any(lane, 0xF0)",
         [](uint64_t L) { return ((L & 3ULL) == 3ULL) ? ((L & 0xCULL) == 0) : ((L & 0xF0ULL) != 0); }},
        {"!(popcount(lane) > 32)", [](uint64_t L) { return !(std::popcount(L) > 32); }},
        {"popcount(lane & 0xFFFFFFFF) > popcount(lane >>> 32)",
         [](uint64_t L) { return std::popcount(L & 0xFFFFFFFFULL) > std::popcount(L >> 32); }},
        {"(lane & 0xAAAAAAAAAAAAAAAA) != 0 && (lane & 0x5555555555555555) == 0",
         [](uint64_t L) { return (L & 0xAAAAAAAAAAAAAAAAULL) != 0 && (L & 0x5555555555555555ULL) == 0; }},
    };
}

void test_cel_evaluator_differential() {
    std::printf("[Test] CelMultiLaneEvaluator vs C++ reference (raw + folded AST)\n");
    const auto lanes = lane_corpus();
    size_t evals = 0;

    for (const auto& c : int_cases()) {
        std::shared_ptr<AstNode> raw;
        try { raw = Parser(c.src).parse(); } catch (const std::exception&) {}
        CHECK_MSG(raw != nullptr, "int case failed to parse: %s", c.src);
        if (!raw) continue;
        auto folded = AstOptimizer::optimize(raw);
        int local_fail = 0;
        for (uint64_t L : lanes) {
            int64_t want = c.ref(L);
            int64_t got_raw = CelMultiLaneEvaluator::eval_int(raw, L);
            int64_t got_fold = CelMultiLaneEvaluator::eval_int(folded, L);
            ++evals;
            if (got_raw != want || got_fold != want) {
                if (local_fail++ < 3)
                    std::printf("      expr='%s' lane=0x%016llx want=%lld raw=%lld folded=%lld\n", c.src,
                                (unsigned long long)L, (long long)want, (long long)got_raw,
                                (long long)got_fold);
            }
        }
        CHECK_MSG(local_fail == 0, "int expr '%s' mismatched on %d lane values", c.src, local_fail);
    }

    for (const auto& c : bool_cases()) {
        std::shared_ptr<AstNode> raw;
        try { raw = Parser(c.src).parse(); } catch (const std::exception&) {}
        CHECK_MSG(raw != nullptr, "bool case failed to parse: %s", c.src);
        if (!raw) continue;
        auto folded = AstOptimizer::optimize(raw);
        int local_fail = 0;
        for (uint64_t L : lanes) {
            bool want = c.ref(L);
            bool got_raw = CelMultiLaneEvaluator::eval_bool(raw, L);
            bool got_fold = CelMultiLaneEvaluator::eval_bool(folded, L);
            int64_t got_int = CelMultiLaneEvaluator::eval_int(raw, L);
            ++evals;
            if (got_raw != want || got_fold != want || got_int != (want ? 1 : 0)) {
                if (local_fail++ < 3)
                    std::printf("      expr='%s' lane=0x%016llx want=%d raw=%d folded=%d int=%lld\n", c.src,
                                (unsigned long long)L, (int)want, (int)got_raw, (int)got_fold,
                                (long long)got_int);
            }
        }
        CHECK_MSG(local_fail == 0, "bool expr '%s' mismatched on %d lane values", c.src, local_fail);
    }
    std::printf("  evaluated %zu (expr, lane) pairs over %zu lane values\n", evals, lanes.size());
}

// Constant-only expressions: folded AST result == evaluator on the unfolded AST.
void test_cel_fold_vs_eval_consistency() {
    std::printf("[Test] CEL constant fold vs unfolded evaluation (literal-only expressions)\n");
    const char* exprs[] = {
        "popcount(0xFFFFFFFFFFFFFFFF)", "popcount(0x8000000000000000)", "first_lane(0)", "ctz(0)",
        "clz(0)", "clz(0x8000000000000000)", "0xFFFFFFFFFFFFFFFF >>> 1", "1 << 63", "1 << 64",
        "0x8000000000000000 / (0 - 1)", "0x8000000000000000 % (0 - 1)",
        "0x7FFFFFFFFFFFFFFF + 1", "0x8000000000000000 - 1", "0x4000000000000000 * 4",
        "0 - 0x8000000000000000", "~0 ^ 0xFF", "(0xF0 & 0x3C) | 0x01 << 4",
    };
    for (const char* e : exprs) {
        auto raw = Parser(e).parse();
        CHECK_MSG(raw != nullptr, "parse failed: %s", e);
        if (!raw) continue;
        auto folded = AstOptimizer::optimize(raw);
        int64_t a = CelMultiLaneEvaluator::eval_int(raw, 0);
        int64_t b = CelMultiLaneEvaluator::eval_int(folded, 0);
        CHECK_MSG(a == b, "expr '%s': eval(raw)=%lld eval(folded)=%lld", e, (long long)a, (long long)b);
    }
}

// ---------------------------------------------------------------------------
// 5. End-to-end fluent multiLane() CEL vs reference
// ---------------------------------------------------------------------------
constexpr uint32_t kFluentN = 128;

std::vector<uint64_t> target_masks() {
    std::vector<uint64_t> m(64, 0);
    m[0] = kAllOnes;
    m[1] = kTopBit;
    m[2] = 1ULL;
    m[3] = 0x5555555555555555ULL;
    m[4] = 0xAAAAAAAAAAAAAAAAULL;
    m[5] = 0xFFFFFFFFULL;
    m[6] = 0xFFFFFFFF00000000ULL;
    m[7] = 0x7FFFFFFFFFFFFFFFULL;
    m[8] = 0ULL;  // unreachable target
    m[9] = kTopBit | 1ULL;
    uint64_t s = 0xC0FFEE1234567ULL;
    for (size_t j = 10; j < 64; ++j) m[j] = xorshift64(s);
    return m;
}

impulse_snapshot_t* build_fluent_snapshot(const char* path) {
    const auto masks = target_masks();
    std::vector<uint32_t> offsets(kFluentN + 1, 0);
    std::vector<uint32_t> targets;
    for (uint32_t u = 0; u < kFluentN; ++u) {
        offsets[u] = static_cast<uint32_t>(targets.size());
        if (u < 64) {
            for (uint32_t j = 0; j < 64; ++j) {
                if ((masks[j] >> u) & 1ULL) targets.push_back(64 + j);
            }
        }
    }
    offsets[kFluentN] = static_cast<uint32_t>(targets.size());

    impulse_writer_t* writer = impulse_writer_create(path, 0);
    if (!writer) return nullptr;
    if (impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "Gene") != IMPULSE_OK) return nullptr;
    if (impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, kFluentN, targets.size(), 0,
                                    offsets.data(), offsets.size() * sizeof(uint32_t),
                                    targets.data(), targets.size() * sizeof(uint32_t)) != IMPULSE_OK)
        return nullptr;
    if (impulse_writer_finalize(writer) != IMPULSE_OK) return nullptr;
    impulse_writer_destroy(writer);
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(path, &st);
    return (st == IMPULSE_OK) ? snap : nullptr;
}

void test_fluent_cel_end_to_end() {
    std::printf("[Test] fluent .multiLane().filter/pruneWhere/project(CEL) vs reference\n");
    using namespace impulse::vm;

    const char* path = "test_multilane_frontend_golden.imps";
    impulse_snapshot_t* raw_snap = build_fluent_snapshot(path);
    CHECK(raw_snap != nullptr);
    if (!raw_snap) return;
    {
        SnapshotView snap(raw_snap);
        const auto masks = target_masks();

        std::vector<uint64_t> seeds(64);
        for (uint64_t i = 0; i < 64; ++i) seeds[i] = i;

        MultiLaneResult base = snap.domain("Gene").multiLane().fromSeeds(seeds).out("rel_0").collect();
        CHECK(base.laneCount() == 64);
        // Un-filtered traversal must reproduce the generating masks (VM walk sanity).
        for (uint32_t j = 0; j < 64; ++j) {
            CHECK_MSG(base.laneMaskFor(64 + j) == masks[j], "target %u mask 0x%llx want 0x%llx", j,
                      (unsigned long long)base.laneMaskFor(64 + j), (unsigned long long)masks[j]);
        }
        for (uint32_t u = 0; u < 64; ++u) CHECK(base.laneMaskFor(u) == 0ULL);

        // --- filter / pruneWhere with bool predicates ---
        for (const auto& c : bool_cases()) {
            MultiLaneResult f = snap.domain("Gene").multiLane().fromSeeds(seeds).out("rel_0").filter(c.src).collect();
            MultiLaneResult p = snap.domain("Gene").multiLane().fromSeeds(seeds).out("rel_0").pruneWhere(c.src).collect();
            int bad = 0;
            for (uint32_t u = 0; u < kFluentN; ++u) {
                uint64_t b = base.laneMaskFor(u);
                uint64_t want_f = (b != 0 && c.ref(b)) ? b : 0ULL;
                uint64_t want_p = (b != 0 && !c.ref(b)) ? b : 0ULL;
                if (f.laneMaskFor(u) != want_f || p.laneMaskFor(u) != want_p) ++bad;
            }
            CHECK_MSG(bad == 0, "fluent filter/pruneWhere '%s' mismatched on %d nodes", c.src, bad);
        }

        // --- project: lane = <int expr> ---
        struct ProjCase { const char* expr; std::function<uint64_t(uint64_t)> ref; };
        const std::vector<ProjCase> proj = {
            {"lane = lane & 0xFF00FF00FF00FF00", [](uint64_t L) { return L & 0xFF00FF00FF00FF00ULL; }},
            {"lane = lane >>> 1", [](uint64_t L) { return L >> 1; }},
            {"lane = lane << 1", [](uint64_t L) { return L << 1; }},
            {"lane = lane & ~0xFF", [](uint64_t L) { return L & ~0xFFULL; }},
            {"lane = lane ^ 0x8000000000000000", [](uint64_t L) { return L ^ kTopBit; }},
            {"lane = popcount(lane) > 32 ? lane : 0", [](uint64_t L) { return std::popcount(L) > 32 ? L : 0ULL; }},
            {"lane = has_lane(lane, 63) ? lane & 0x7FFFFFFFFFFFFFFF : lane",
             [](uint64_t L) { return ((L >> 63) != 0) ? (L & 0x7FFFFFFFFFFFFFFFULL) : L; }},
            {"lane = 0", [](uint64_t) { return 0ULL; }},
        };
        for (const auto& c : proj) {
            MultiLaneResult r = snap.domain("Gene").multiLane().fromSeeds(seeds).out("rel_0").project(c.expr).collect();
            int bad = 0;
            for (uint32_t u = 0; u < kFluentN; ++u) {
                uint64_t b = base.laneMaskFor(u);
                uint64_t want = (b != 0) ? c.ref(b) : 0ULL;
                if (r.laneMaskFor(u) != want) {
                    if (bad++ < 2)
                        std::printf("      project '%s' node %u base=0x%llx want=0x%llx got=0x%llx\n", c.expr, u,
                                    (unsigned long long)b, (unsigned long long)want,
                                    (unsigned long long)r.laneMaskFor(u));
                }
            }
            CHECK_MSG(bad == 0, "fluent project '%s' mismatched on %d nodes", c.expr, bad);
        }

        // --- K < 64: result lane words must stay inside the K active lanes ---
        {
            std::vector<uint64_t> seeds10(seeds.begin(), seeds.begin() + 10);
            MultiLaneResult b10 = snap.domain("Gene").multiLane().fromSeeds(seeds10).out("rel_0").collect();
            MultiLaneResult p10 = snap.domain("Gene").multiLane().fromSeeds(seeds10).out("rel_0")
                                      .project("lane = ~lane").collect();
            const uint64_t active = (1ULL << 10) - 1ULL;
            int leaked = 0;
            for (uint32_t u = 0; u < kFluentN; ++u) {
                if (b10.laneMaskFor(u) == 0) continue;
                if ((p10.laneMaskFor(u) & ~active) != 0) ++leaked;
                CHECK(p10.laneMaskFor(u) == ((~b10.laneMaskFor(u)) & active));
            }
            CHECK_MSG(leaked == 0, "project('lane = ~lane') with K=10 leaked bits above the active mask on %d nodes", leaked);
        }

        // --- malformed CEL in fluent steps must fail loudly (fail-closed), not silently no-op ---
        {
            const char* malformed[] = {"popcount(", "lane &", "has_any(lane,", "lane == 0x", "lane == 0b2",
                                       "lane == 0x10000000000000000", "lane == 99999999999999999999"};
            for (const char* m : malformed) {
                bool threw = false;
                try {
                    (void)snap.domain("Gene").multiLane().fromSeeds(seeds).out("rel_0").pruneWhere(m).collect();
                } catch (const std::exception&) {
                    threw = true;
                }
                CHECK_MSG(threw, "pruneWhere('%s') did not throw (denylist silently ignored)", m);
            }
        }
    }
    impulse_snapshot_close(raw_snap);
    std::remove(path);
}

// ---------------------------------------------------------------------------
// 6. Golden bytecode: ImpK / ImpLog / Cypher / QueryBuilder
// ---------------------------------------------------------------------------
struct Ins {
    uint8_t op;
    uint8_t flags;
    uint16_t dst;
    uint32_t payload;
};

bool match_program(const char* label, const std::vector<impulse_instruction_t>& got,
                   const std::vector<Ins>& want) {
    bool ok = got.size() == want.size();
    if (ok) {
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i].opcode != want[i].op || got[i].flags != want[i].flags ||
                got[i].dst_reg != want[i].dst || got[i].payload != want[i].payload) {
                ok = false;
                break;
            }
        }
    }
    if (!ok) {
        std::printf("  [FAIL] golden mismatch: %s (got %zu insts, want %zu)\n", label, got.size(), want.size());
        for (size_t i = 0; i < std::max(got.size(), want.size()); ++i) {
            if (i < got.size())
                std::printf("      got  [%zu] op=%02X fl=%02X dst=%u pay=%08X", i, got[i].opcode, got[i].flags,
                            got[i].dst_reg, got[i].payload);
            else
                std::printf("      got  [%zu] <none>                            ", i);
            if (i < want.size())
                std::printf("   want op=%02X fl=%02X dst=%u pay=%08X\n", want[i].op, want[i].flags, want[i].dst,
                            want[i].payload);
            else
                std::printf("   want <none>\n");
        }
    }
    ++g_checks;
    if (!ok) ++g_failures;
    return ok;
}

impulse::compiler::GraphCatalog make_catalog() {
    impulse::compiler::GraphCatalog cat;
    cat.register_relation("edge", 0);
    cat.register_relation("REL", 1);
    cat.register_relation("member_of", 0);
    return cat;
}

void test_impk_lowering_goldens() {
    std::printf("[Test] ImpK multi-lane lowering goldens (0xC0..0xC9)\n");
    using namespace impulse::compiler;
    using impulse::impk::ImpKCompiler;
    using impulse::impk::ImpKOpType;
    const auto cat = make_catalog();

    const std::string script =
        "L0 = lane_seeds(10, 20, 30)\n"
        "Lg = lane_groups([0, 0, 1, 1], 2)\n"
        "Lm = lane_edge_mask(L0, edge, [2, 0])\n"
        "Y1 = lane_spmm(edge, Lm, 0x04)\n"
        "Y2 = lane_spmm_csc(edge, Y1, fixpoint)\n"
        "Lp = lane_prune(Y2, 1, 16, 0)\n"
        "Q = lane_quorum(Lp, 2)\n"
        "R_and = lane_reduce_and(Lp, 7)\n"
        "R_attr = lane_reduce_attr(Lp, 0, argmin)\n"
        "C = lane_set_cover(Lp, 255)\n";

    auto stmts = ImpKCompiler::parse(script);
    CHECK(stmts.size() == 10);
    CHECK(stmts[0].op_type == ImpKOpType::LaneInitSeeds);
    CHECK((stmts[0].int_list == std::vector<uint64_t>{10, 20, 30}));
    CHECK(stmts[1].op_type == ImpKOpType::LaneInitGroups);
    CHECK((stmts[1].int_list == std::vector<uint64_t>{0, 0, 1, 1}));
    CHECK(stmts[1].int_param1 == 2);
    CHECK(stmts[3].op_type == ImpKOpType::LaneSpmmCsr && stmts[3].bool_param);
    CHECK(stmts[4].op_type == ImpKOpType::LaneSpmmCsc && stmts[4].bool_param);

    // Exact ImpScheme text.
    CHECK(ImpKCompiler::to_impscheme(stmts) ==
          "(impk-pipeline (lane-init-seeds 0 0) (lane-init-groups 0 2) (lane-set-edge-mask 0 edge)"
          " (lane-walk-csr edge 1) (lane-walk-csc edge 1) (lane-prune-mask 1 16 0)"
          " (lane-filter-quorum 2 255) (lane-reduce-and) (lane-reduce-attr 0 4) (lane-set-cover 255 0)"
          " (collect-bitset))");

    // Exact bytecode via ImpScheme -> compile.
    auto ast = impulse::impscm::ImpScmAstBuilder::parse(ImpKCompiler::to_impscheme(stmts));
    auto compiled = ImpulseCompiler::compile(ast, &cat);
    match_program("impk full pipeline",
                  compiled.instructions,
                  {
                      {OP_LANE_INIT_SEEDS, 0x00, 0, 0x00000000},
                      {OP_LANE_INIT_GROUPS, 0x00, 0, 0x00020000},      // pairs_reg=0 | num_groups=2<<16
                      {OP_LANE_SET_EDGE_MASK, 0x00, 0, 0x00000000},    // pairs_reg=0 | rel 0<<16
                      {OP_LANE_CSR_WALK, 0x04, 1, 0x00000000},         // fixed-point flag 0x04
                      {OP_LANE_CSC_WALK, 0x04, 0, 0x00000001},         // src=R1
                      {OP_LANE_PRUNE_MASK, 0x01, 0, 0x00001000},       // mode=1, arg0=16<<8
                      {OP_LANE_FILTER_QUORUM, 0x01, 1, 0x00FF0200},    // src|min_q=2<<8|mask=0xFF<<16
                      {OP_LANE_REDUCE_AND, 0x00, 0, 0x00000001},
                      {OP_LANE_REDUCE_ATTR, 0x00, 0, 0x00040001},      // src=1|attr 0<<8|ARGMIN(4)<<16
                      {OP_LANE_SET_COVER, 0x00, 0, 0x0000FF01},        // src=1|mask 0xFF<<8
                      {OP_COLLECT_BITSET, 0x00, 0, 0x00000000},
                      {OP_HALT, 0x00, 0, 0x00000000},
                  });

    // Direct lowering path (ImpKCompiler::compile -> ScmProgram) for a minimal quorum pipeline.
    auto prog = ImpKCompiler::compile(
        "L0 = lane_seeds(10, 20, 30)\nY1 = lane_spmm(edge, L0, 0x04)\nQ = lane_quorum(Y1, 2)\n");
    CHECK(prog->to_scm_string() ==
          "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"edge\" 1)\n  (lane-filter-quorum 2 255)\n  (collect-bitset))");
    auto c2 = ImpulseCompiler::compile(prog, &cat);
    match_program("impk direct quorum",
                  c2.instructions,
                  {
                      {OP_LANE_INIT_SEEDS, 0x00, 0, 0x00000000},
                      {OP_LANE_CSR_WALK, 0x04, 1, 0x00000000},
                      {OP_LANE_FILTER_QUORUM, 0x01, 0, 0x00FF0201},
                      {OP_COLLECT_BITSET, 0x00, 0, 0x00000000},
                      {OP_HALT, 0x00, 0, 0x00000000},
                  });

    // Flag encodings: non-fixpoint lane_spmm -> flags 0; reduce-attr op names.
    {
        auto p = ImpKCompiler::compile("L0 = lane_seeds(1, 2)\nY = lane_spmm(edge, L0, 0)\n");
        auto c = ImpulseCompiler::compile(p, &cat);
        CHECK(c.instructions.size() >= 2 && c.instructions[1].opcode == OP_LANE_CSR_WALK &&
              c.instructions[1].flags == 0x00);
        auto p2 = ImpKCompiler::compile("L0 = lane_seeds(1, 2)\nY = lane_spmm_csc(edge, L0, fixpoint)\n");
        auto c2b = ImpulseCompiler::compile(p2, &cat);
        CHECK(c2b.instructions.size() >= 2 && c2b.instructions[1].opcode == OP_LANE_CSC_WALK &&
              c2b.instructions[1].flags == 0x04);
    }

    // Negative: structurally malformed lane statements -> clean exception, not a crash.
    const char* bad[] = {
        "L0 = lane_seeds(",
        "L0 = lane_seeds(1, 2",
        "Q = lane_quorum(",
        "Lp = lane_prune(Y)",
        "R = lane_reduce_attr(Lp)",
        "Lg = lane_groups([0, 1",
    };
    for (const char* b : bad) {
        bool ok = false;
        try {
            auto s2 = ImpKCompiler::parse(b);
            (void)s2;
        } catch (const std::exception&) {
            ok = true;
        }
        CHECK_MSG(ok, "ImpK parse accepted malformed lane statement: %s", b);
    }

    // Documented leniency (see GAP-6 in notes): optional args default silently; unknown names are
    // dropped; unparsable tails are skipped. Pin the current behaviour so changes are deliberate.
    {
        auto a = ImpKCompiler::parse("Y = lane_spmm(edge, L0)");
        CHECK(a.size() == 1 && a[0].op_type == ImpKOpType::LaneSpmmCsr && !a[0].bool_param);  // flags default 0
        auto b = ImpKCompiler::parse("R = lane_reduce_attr(Lp, 0, nosuchop)");
        CHECK(b.size() == 1 && b[0].int_param2 == 0);        // unknown reduce op silently == SUM
        auto c = ImpKCompiler::parse("L0 = lane_groups([0, 0, 1, 1])");
        CHECK(c.size() == 1 && c[0].int_param1 == 1);        // num_groups defaults to 1
        auto d = ImpKCompiler::parse("L0 = lane_seeds(0x)");
        CHECK(d.size() == 1 && d[0].int_list == std::vector<uint64_t>{0});  // "0" kept, "x" skipped
        auto e = ImpKCompiler::parse("L0 = lane_sedes(1, 2)");
        CHECK(e.empty());                                    // misspelt function silently dropped
    }
}

void test_implog_lowering_goldens() {
    std::printf("[Test] ImpLog (Datalog) multi-lane lowering goldens\n");
    using namespace impulse::compiler;
    using impulse::datalog::DatalogParser;
    const auto cat = make_catalog();

    // multi-seed recursive -> C0 + C3(fixpoint 0x04)
    {
        auto d = DatalogParser::parse_multi_seed(
            "reach(X,Y) :- edge(X,Y).\nreach(X,Z) :- reach(X,Y), edge(Y,Z).\n", {0, 1, 2});
        CHECK(d->to_scm_string() ==
              "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"edge\" 1)\n  (collect-bitset))");
        auto c = ImpulseCompiler::compile(d, &cat);
        match_program("implog multi-seed fixpoint", c.instructions,
                      {
                          {OP_LANE_INIT_SEEDS, 0x00, 0, 0x00000000},
                          {OP_LANE_CSR_WALK, 0x04, 1, 0x00000000},
                          {OP_COLLECT_BITSET, 0x00, 1, 0x00000001},
                          {OP_HALT, 0x00, 1, 0x00000000},
                      });
        auto first = std::static_pointer_cast<ScmLaneInitSeeds>(d->steps[0]);
        CHECK((first->inline_seeds == std::vector<uint64_t>{0, 1, 2}));
    }
    // groups -> C1 (num_groups<<16) + C3 (non-fixpoint)
    {
        auto d = DatalogParser::parse_groups("reach(X,Y) :- edge(X,Y).\n", {5, 0, 6, 1}, 2);
        CHECK(d->to_scm_string() ==
              "(program\n  (lane-init-groups 0 2)\n  (lane-walk-csr \"edge\")\n  (collect-bitset))");
        auto c = ImpulseCompiler::compile(d, &cat);
        match_program("implog groups", c.instructions,
                      {
                          {OP_LANE_INIT_GROUPS, 0x00, 0, 0x00020000},
                          {OP_LANE_CSR_WALK, 0x00, 1, 0x00000000},
                          {OP_COLLECT_BITSET, 0x00, 1, 0x00000001},
                          {OP_HALT, 0x00, 1, 0x00000000},
                      });
    }
    // allow/deny split -> C5 mode 1 with arg0 = |allow| lanes
    {
        auto d = DatalogParser::parse_allow_deny_split("reach(X,Y) :- edge(X,Y).\n", {1, 2, 3}, {4});
        CHECK(d->to_scm_string() ==
              "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"edge\")\n  (lane-prune-mask 1 3 0)\n  (collect-bitset))");
        auto c = ImpulseCompiler::compile(d, &cat);
        match_program("implog allow/deny split", c.instructions,
                      {
                          {OP_LANE_INIT_SEEDS, 0x00, 0, 0x00000000},
                          {OP_LANE_CSR_WALK, 0x00, 1, 0x00000000},
                          {OP_LANE_PRUNE_MASK, 0x01, 1, 0x00000301},   // src=1 | arg0=3<<8
                          {OP_COLLECT_BITSET, 0x00, 1, 0x00000001},
                          {OP_HALT, 0x00, 1, 0x00000000},
                      });
    }
    // allow/deny size limits: > 32 lanes per half is rejected with a clean exception
    {
        std::vector<uint64_t> big(33);
        for (uint64_t i = 0; i < 33; ++i) big[i] = i;
        bool threw = false;
        try { (void)DatalogParser::parse_allow_deny_split("reach(X,Y) :- edge(X,Y).\n", big, {1}); }
        catch (const std::exception&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { (void)DatalogParser::parse_allow_deny_split("reach(X,Y) :- edge(X,Y).\n", {}, {}); }
        catch (const std::exception&) { threw = true; }
        CHECK(threw);
        // GAP-7: halves of unequal length are padded with node id 0 (a real node), so
        // allow={5} / deny={6,7} yields combined seeds [5, 0, 6, 7] -> lane 1 is a phantom seed of node 0.
        auto padded = DatalogParser::parse_allow_deny_split("reach(X,Y) :- edge(X,Y).\n", {5}, {6, 7});
        auto init = std::static_pointer_cast<ScmLaneInitSeeds>(padded->steps[0]);
        CHECK((init->inline_seeds == std::vector<uint64_t>{5, 0, 6, 7}));
    }
}

void test_cypher_lowering_goldens() {
    std::printf("[Test] Cypher multi-lane lowering goldens\n");
    using namespace impulse::compiler;
    const auto cat = make_catalog();

    auto res = CypherCompiler::compile(
        "MATCH (a:Entity)-[:REL]->(b:Entity) WHERE a.id IN [0, 1, 2] AND quorum >= 2 RETURN b");
    CHECK(res.ast->to_scm_string() ==
          "(program\n  (lane-init-seeds 0 0)\n  (lane-walk-csr \"REL\")\n  (lane-filter-quorum 2 255)\n  (collect-bitset))");
    auto c = ImpulseCompiler::compile(res.ast, &cat);
    match_program("cypher multi-seed IN + quorum", c.instructions,
                  {
                      {OP_LANE_INIT_SEEDS, 0x00, 0, 0x00000000},
                      {OP_LANE_CSR_WALK, 0x00, 1, 0x00010000},     // rel id 1 <<16
                      {OP_LANE_FILTER_QUORUM, 0x01, 0, 0x00FF0201},
                      {OP_COLLECT_BITSET, 0x00, 0, 0x00000000},
                      {OP_HALT, 0x00, 0, 0x00000000},
                  });

    // Kleene star -> fixpoint flag 0x04
    auto res_fp = CypherCompiler::compile(
        "MATCH (a:User)-[:KNOWS*]->(b:User) WHERE a.id IN [0, 1] AND quorum >= 2 RETURN b");
    CHECK(res_fp.ast->to_scm_string().find("(lane-walk-csr \"KNOWS\" 1)") != std::string::npos);
    GraphCatalog cat2;
    cat2.register_relation("KNOWS", 3);
    auto cfp = ImpulseCompiler::compile(res_fp.ast, &cat2);
    bool saw_fp = false;
    for (const auto& i : cfp.instructions)
        if (i.opcode == OP_LANE_CSR_WALK) saw_fp = (i.flags == 0x04) && ((i.payload >> 16) == 3);
    CHECK(saw_fp);

    // Seeds only (no quorum) must not emit a quorum filter.
    auto res_ns = CypherCompiler::compile("MATCH (a:Entity)-[:REL]->(b:Entity) WHERE a.id IN [4, 5] RETURN b");
    CHECK(res_ns.ast->to_scm_string().find("lane-filter-quorum") == std::string::npos);
}

void test_querybuilder_goldens_and_semantics() {
    std::printf("[Test] fluent QueryBuilder lane emitter goldens\n");
    using namespace impulse::vm;

    QueryBuilder qb;
    qb.laneInitSeeds(3, 2)
        .laneInitGroups(4, 5)
        .laneSetEdgeMask(6, 7)
        .laneWalkCsr(9, false)
        .laneWalkCsc(9, false)
        .lanePruneMask(1, 0x1234, 5)
        .laneFilterQuorum(3, 0xFF)
        .laneFilterQuorum(3, 0)
        .laneReduceAnd()
        .laneReduceAttr(2, 4)
        .laneSetCover(0xFF, 1)
        .vectorAnnSearch(2, 8, 3, true)
        .vectorRerankDot(2, 5, 1);
    match_program("querybuilder lane emitters", qb.rawInstructions(),
                  {
                      {OP_LANE_INIT_SEEDS, 0x02, 0, 0x00000003},
                      {OP_LANE_INIT_GROUPS, 0x00, 0, 0x00050004},
                      {OP_LANE_SET_EDGE_MASK, 0x00, 0, 0x00070006},
                      {OP_LANE_CSR_WALK, 0x00, 0, 0x00090000},
                      {OP_LANE_CSC_WALK, 0x00, 0, 0x00090000},
                      {OP_LANE_PRUNE_MASK, 0x01, 0, 0x05123400},
                      {OP_LANE_FILTER_QUORUM, 0x01, 0, 0x00FF0300},
                      {OP_LANE_FILTER_QUORUM, 0x00, 0, 0x00030000},
                      {OP_LANE_REDUCE_AND, 0x00, 0, 0x00000000},
                      {OP_LANE_REDUCE_ATTR, 0x00, 0, 0x00040200},
                      {OP_LANE_SET_COVER, 0x01, 0, 0x0000FF00},
                      {OP_VECTOR_ANN_SEARCH, 0x01, 0, 0x00030802},
                      {OP_VECTOR_RERANK_DOT, 0x01, 0, 0x00050200},
                  });

    // Differential: the fixed-point flag emitted by the fluent builder must equal the flag the
    // ImpulseCompiler emits for the same logical op (0x04, the bit the VM reads as is_fixpoint).
    {
        QueryBuilder f;
        f.laneInitSeeds(0, 0).laneWalkCsr(0, true).laneWalkCsc(0, true);
        const auto& r = f.rawInstructions();
        auto scm = impulse::compiler::ScmLaneWalk::forward("edge", true);
        impulse::compiler::GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto prog = impulse::compiler::ScmProgram::of(std::make_shared<impulse::compiler::ScmLaneInitSeeds>(0, 0), scm);
        auto cp = impulse::compiler::ImpulseCompiler::compile(prog, &cat);
        uint8_t compiler_fp_flags = 0;
        for (const auto& i : cp.instructions)
            if (i.opcode == OP_LANE_CSR_WALK) compiler_fp_flags = i.flags;
        CHECK(compiler_fp_flags == 0x04);
        CHECK_MSG(r[1].flags == compiler_fp_flags, "QueryBuilder::laneWalkCsr(fixed_point) flags=0x%02X, compiler=0x%02X",
                  r[1].flags, compiler_fp_flags);
        CHECK_MSG(r[2].flags == compiler_fp_flags, "QueryBuilder::laneWalkCsc(fixed_point) flags=0x%02X, compiler=0x%02X",
                  r[2].flags, compiler_fp_flags);
    }
}

// ---------------------------------------------------------------------------
// 7. VM execution of emitted bytecode vs single-lane oracle
// ---------------------------------------------------------------------------
struct Csr {
    uint32_t n;
    std::vector<uint32_t> off, tgt;
};

Csr make_chain_plus_mesh(uint32_t n) {
    // chain u -> u+1, plus every 5th node jumps +7: deep BFS so a single hop != fixpoint
    Csr g;
    g.n = n;
    g.off.assign(n + 1, 0);
    for (uint32_t u = 0; u < n; ++u) {
        std::vector<uint32_t> nb;
        if (u + 1 < n) nb.push_back(u + 1);
        if (u % 5 == 0 && u + 7 < n) nb.push_back(u + 7);
        std::sort(nb.begin(), nb.end());
        for (uint32_t v : nb) g.tgt.push_back(v);
        g.off[u + 1] = static_cast<uint32_t>(g.tgt.size());
    }
    return g;
}

std::set<uint32_t> oracle_reach(const Csr& g, uint32_t seed, bool csc, bool fixpoint) {
    auto step = [&](const std::set<uint32_t>& f) {
        std::set<uint32_t> nx;
        for (uint32_t u = 0; u < g.n; ++u)
            for (uint32_t e = g.off[u]; e < g.off[u + 1]; ++e) {
                uint32_t v = g.tgt[e];
                if (!csc && f.count(u)) nx.insert(v);
                if (csc && f.count(v)) nx.insert(u);
            }
        return nx;
    };
    std::set<uint32_t> init{seed};
    if (!fixpoint) return step(init);
    std::set<uint32_t> visited = init, delta = init;
    while (!delta.empty()) {
        std::set<uint32_t> nxt;
        for (uint32_t v : step(delta))
            if (visited.insert(v).second) nxt.insert(v);
        delta = std::move(nxt);
    }
    return visited;
}

// Runs `prog` (ending in HALT) with the seed node-vector bound to register `seed_reg`.
// Returns per-node lane masks of the lane frontier in register `out_reg`.
bool run_lane_program(const Csr& g, const std::vector<uint64_t>& seeds, uint16_t seed_reg, uint16_t out_reg,
                      std::vector<impulse_instruction_t> prog, std::vector<uint64_t>& masks_out) {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_mock_csr(ctx, 0, g.off.data(), g.tgt.data(), g.n, g.tgt.size());
    int h = impulse_vm_context_acquire_node_vector(ctx);
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h), seeds.data(), seeds.size());
    impulse_vm_state_t st{};
    st.query_context = ctx;
    st.registers[seed_reg] = static_cast<uint64_t>(h);
    st.register_types[seed_reg] = TYPE_NODE_VECTOR;
    prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});
    impulse_vm_status_t rc = impulse_vm_execute(prog.data(), prog.size(), &st, 0);
    bool ok = (rc == IMPULSE_VM_OK) && st.register_types[out_reg] == TYPE_LANE_FRONTIER;
    if (ok) {
        const uint64_t* lanes = impulse_vm_context_lane_frontier_data(ctx, st.registers[out_reg]);
        masks_out.assign(lanes, lanes + g.n);
    }
    impulse_vm_context_destroy(ctx);
    return ok;
}

void check_masks_against_oracle(const char* label, const Csr& g, const std::vector<uint64_t>& seeds,
                                const std::vector<uint64_t>& masks, bool csc, bool fixpoint) {
    int bad = 0;
    for (size_t b = 0; b < seeds.size(); ++b) {
        auto want = oracle_reach(g, static_cast<uint32_t>(seeds[b]), csc, fixpoint);
        for (uint32_t u = 0; u < g.n; ++u) {
            bool got = (masks[u] >> b) & 1ULL;
            if (got != (want.count(u) != 0)) ++bad;
        }
    }
    CHECK_MSG(bad == 0, "%s: %d (lane,node) mismatches vs single-lane oracle", label, bad);
}

void test_vm_execution_of_emitted_bytecode() {
    std::printf("[Test] VM execution of compiler- and fluent-emitted lane bytecode vs oracle\n");
    using namespace impulse::compiler;
    using namespace impulse::vm;

    const Csr g = make_chain_plus_mesh(96);
    std::vector<uint64_t> seeds;
    for (uint64_t i = 0; i < 64; ++i) seeds.push_back((i * 3) % 40);

    // (a) compiler-emitted: ImpLog multi-seed recursive reachability (fixed point, forward).
    {
        auto d = impulse::datalog::DatalogParser::parse_multi_seed(
            "reach(X,Y) :- edge(X,Y).\nreach(X,Z) :- reach(X,Y), edge(Y,Z).\n", seeds);
        GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto c = ImpulseCompiler::compile(d, &cat);
        // First two instructions are C0 (seeds in R0) and C3 (fixed point R0 -> R1).
        std::vector<impulse_instruction_t> prog(c.instructions.begin(), c.instructions.begin() + 2);
        std::vector<uint64_t> masks;
        bool ok = run_lane_program(g, seeds, /*seed_reg=*/0, /*out_reg=*/1, prog, masks);
        CHECK(ok);
        if (ok) check_masks_against_oracle("implog multi-seed fixpoint", g, seeds, masks, false, true);
    }
    // (b) compiler-emitted: non-fixpoint 1-hop forward and reverse via ScmProgram.
    for (int mode = 0; mode < 2; ++mode) {
        bool csc = (mode == 1);
        auto walk = csc ? ScmLaneWalk::reverse("edge", false) : ScmLaneWalk::forward("edge", false);
        auto prog = ScmProgram::of(std::make_shared<ScmLaneInitSeeds>(0, 0), walk);
        GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto c = ImpulseCompiler::compile(prog, &cat);
        std::vector<impulse_instruction_t> p(c.instructions.begin(), c.instructions.begin() + 2);
        std::vector<uint64_t> masks;
        bool ok = run_lane_program(g, seeds, 0, 1, p, masks);
        CHECK(ok);
        if (ok) check_masks_against_oracle(csc ? "compiler 1-hop csc" : "compiler 1-hop csr", g, seeds, masks, csc, false);
    }
    // (c) compiler-emitted: reverse fixpoint.
    {
        auto prog = ScmProgram::of(std::make_shared<ScmLaneInitSeeds>(0, 0), ScmLaneWalk::reverse("edge", true));
        GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto c = ImpulseCompiler::compile(prog, &cat);
        std::vector<impulse_instruction_t> p(c.instructions.begin(), c.instructions.begin() + 2);
        std::vector<uint64_t> masks;
        bool ok = run_lane_program(g, seeds, 0, 1, p, masks);
        CHECK(ok);
        if (ok) check_masks_against_oracle("compiler fixpoint csc", g, seeds, masks, true, true);
    }
    // (d) fluent QueryBuilder program: 1-hop (flags 0) and fixed point.
    for (int fp = 0; fp < 2; ++fp) {
        QueryBuilder qb;
        qb.laneInitSeeds(0, 0).laneWalkCsr(0, fp != 0);
        std::vector<uint64_t> masks;
        bool ok = run_lane_program(g, seeds, 0, 0, qb.rawInstructions(), masks);
        CHECK(ok);
        if (ok)
            check_masks_against_oracle(fp ? "QueryBuilder fixed-point csr" : "QueryBuilder 1-hop csr", g, seeds,
                                       masks, false, fp != 0);
    }
}

// ---------------------------------------------------------------------------
// 8. Documented gaps (current behaviour pinned; see notes for the design-spec reference)
// ---------------------------------------------------------------------------
void test_documented_gaps() {
    std::printf("[Test] Documented design-spec gaps (current behaviour pinned)\n");
    using namespace impulse::compiler;

    // GAP-1: DESIGN_SPEC §3.2/§5.2 `last_lane` lowers to MATH_FUNC_CLZ, `has_*` to bit-test-*; the
    //        implemented CEL->ImpScheme lowering emits plain call names and has no math-func id for
    //        last_lane/has_*; they are only executable through CelMultiLaneEvaluator.
    CHECK(CelCompiler::resolve_math_func("last_lane") < 0);
    CHECK(CelCompiler::resolve_math_func("has_any") < 0);
    CHECK(CelCompiler::resolve_math_func("has_all") < 0);
    CHECK(CelCompiler::resolve_math_func("has_none") < 0);
    CHECK(CelCompiler::resolve_math_func("has_lane") < 0);
    CHECK(CelCompiler::resolve_math_func("popcount") == MATH_FUNC_POPCOUNT);
    CHECK(CelCompiler::resolve_math_func("bit_count") == MATH_FUNC_POPCOUNT);
    CHECK(CelCompiler::resolve_math_func("first_lane") == MATH_FUNC_CTZ);
    CHECK(CelCompiler::resolve_math_func("ctz") == MATH_FUNC_CTZ);
    CHECK(CelCompiler::resolve_math_func("clz") == MATH_FUNC_CLZ);
    // spec names (bit-and, bit-shl, bit-test-any ...) are not emitted:
    CHECK(scm_of("a & b").find("bit-and") == std::string::npos);
    CHECK(scm_of("a << 1").find("bit-shl") == std::string::npos);
    CHECK(scm_of("has_any(a, b)").find("bit-test-any") == std::string::npos);

    // GAP-2: spec says `>>` is a LOGICAL unsigned shift; implementation: `>>` is arithmetic on int64
    //        (sign-propagating) and only `>>>` is logical.
    {
        auto ast = Parser("lane >> 63").parse();
        CHECK(ast != nullptr);
        CHECK(CelMultiLaneEvaluator::eval_int(ast, kTopBit) == -1);                 // spec would say 1
        CHECK(CelMultiLaneEvaluator::eval_int(Parser("lane >>> 63").parse(), kTopBit) == 1);
    }

    // GAP-3: inline seeds / group pairs / deny nodes are retained in the AST but are neither
    //        serialised to ImpScheme nor materialised by the bytecode emitter; the emitted
    //        OP_LANE_INIT_SEEDS only references an (unpopulated) seed register.
    {
        auto prog = impulse::impk::ImpKCompiler::compile("L0 = lane_seeds(10, 20, 30)\nY = lane_spmm(edge, L0, 0)\n");
        auto init = std::static_pointer_cast<ScmLaneInitSeeds>(prog->steps[0]);
        CHECK((init->inline_seeds == std::vector<uint64_t>{10, 20, 30}));
        std::string s = prog->to_scm_string();
        CHECK(s.find("10") == std::string::npos && s.find("20") == std::string::npos);
        GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto c = ImpulseCompiler::compile(prog, &cat);
        CHECK(c.instructions[0].opcode == OP_LANE_INIT_SEEDS);
        CHECK(c.instructions[0].payload == 0u);  // no seed constant/array carried in bytecode
        for (const auto& i : c.instructions) CHECK(i.payload != 10u && i.payload != 20u && i.payload != 30u);
    }

    // GAP-4: ImpK lane_reduce_and(..., 7) / lane_edge_mask(..., [pairs]) arguments are parsed but
    //        dropped by lowering (`(lane-reduce-and)` and `(lane-set-edge-mask 0 edge)` carry no operands).
    {
        auto stmts = impulse::impk::ImpKCompiler::parse(
            "L0 = lane_seeds(1)\nLm = lane_edge_mask(L0, edge, [2, 0])\nR = lane_reduce_and(Lm, 7)\n");
        std::string s = impulse::impk::ImpKCompiler::to_impscheme(stmts);
        CHECK(s.find("(lane-set-edge-mask 0 edge)") != std::string::npos);
        CHECK(s.find("(lane-reduce-and)") != std::string::npos);
    }

    // GAP-5: disassembler labels flags&1 of lane opcodes as "[early-exit]" although for 0xC5/0xC9
    //        bit 0 carries the *mode* (cosmetic).
    {
        GraphCatalog cat;
        cat.register_relation("edge", 0);
        auto prog = ScmProgram::of(std::make_shared<ScmLaneInitSeeds>(0, 0), std::make_shared<ScmLanePruneMask>(1, 3, 0));
        auto c = ImpulseCompiler::compile(prog, &cat);
        std::string dis = c.to_impas_string(&cat);
        CHECK(dis.find("OP_LANE_PRUNE_MASK") != std::string::npos && dis.find("[early-exit]") != std::string::npos);
    }
}

}  // namespace

int main() {
    std::printf("================================================================\n");
    std::printf(" Multi-Lane Frontend Golden Test Suite (CEL / ImpK / ImpLog / Cypher / fluent)\n");
    std::printf("================================================================\n");

    test_cel_literals_and_errors();
    test_cel_scheme_goldens();
    test_cel_constant_folding_goldens();
    test_cel_evaluator_differential();
    test_cel_fold_vs_eval_consistency();
    test_impk_lowering_goldens();
    test_implog_lowering_goldens();
    test_cypher_lowering_goldens();
    test_querybuilder_goldens_and_semantics();
    test_vm_execution_of_emitted_bytecode();
    test_fluent_cel_end_to_end();
    test_documented_gaps();

    std::printf("================================================================\n");
    std::printf(" checks=%d failures=%d\n", g_checks, g_failures);
    std::printf("================================================================\n");
    return g_failures == 0 ? 0 : 1;
}
