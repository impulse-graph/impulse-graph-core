/**
 * @file impulse_impscm.hpp
 * @brief ImpScheme v0.11.0 SSA IR Parser, Static Domain/Type Validator, DECIMAL(P, S)
 *        Boundary Folder, and .impb Compiler.
 */
#ifndef IMPULSE_V11_IMPSCM_HPP
#define IMPULSE_V11_IMPSCM_HPP

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "impulse_graph.h"
#include "vm/impulse_bytecode_emitter.hpp"

namespace impulse::compiler::v11 {

inline constexpr std::array<int64_t, 19> kDecimalPow10 = {
    1LL,
    10LL,
    100LL,
    1000LL,
    10000LL,
    100000LL,
    1000000LL,
    10000000LL,
    100000000LL,
    1000000000LL,
    10000000000LL,
    100000000000LL,
    1000000000000LL,
    10000000000000LL,
    100000000000000LL,
    1000000000000000LL,
    10000000000000000LL,
    100000000000000000LL,
    1000000000000000000LL,
};

enum class DecimalCmpOp { EQ, NE, LT, LE, GT, GE };

enum class DecimalFoldKind { COMPARE, ALWAYS_TRUE, ALWAYS_FALSE };

struct FoldedDecimalComparison {
    DecimalFoldKind kind = DecimalFoldKind::COMPARE;
    DecimalCmpOp op = DecimalCmpOp::EQ;
    int64_t threshold_unscaled = 0;

    static FoldedDecimalComparison compare(DecimalCmpOp op, int64_t threshold_unscaled) noexcept {
        return {DecimalFoldKind::COMPARE, op, threshold_unscaled};
    }
    static FoldedDecimalComparison always_true() noexcept {
        return {DecimalFoldKind::ALWAYS_TRUE, DecimalCmpOp::NE, 0};
    }
    static FoldedDecimalComparison always_false() noexcept {
        return {DecimalFoldKind::ALWAYS_FALSE, DecimalCmpOp::EQ, 0};
    }
};

/**
 * Compile-time literal comparison boundary folder for DECIMAL(col_precision, col_scale)
 * columns compared against a constant literal (lit_unscaled * 10^-lit_scale).
 */
FoldedDecimalComparison fold_decimal_comparison(
    DecimalCmpOp op,
    uint16_t col_precision,
    uint8_t col_scale,
    int64_t lit_unscaled,
    uint8_t lit_scale
);

/**
 * Exact base-10 integer parser returning unscaled int64 for DECIMAL(precision, scale)
 * without touching strtod / binary floating point.
 */
int64_t parse_decimal_unscaled_exact(
    const std::string& text,
    uint16_t precision,
    uint8_t scale
);

/**
 * Infers minimal exact (precision, scale) from a base-10 decimal literal string
 * without touching strtod / binary floating point.
 */
std::pair<uint16_t, uint8_t> infer_decimal_precision_scale(const std::string& text);

struct OutputColumnMeta {
    std::string name;
    std::string type; // "I64", "U64", "F64", "BOOL", "STR", "FRONTIER", "DECIMAL", "UNKNOWN"
    bool is_decimal = false;
    uint16_t precision = 0;
    uint8_t scale = 0;
};

struct ParamMeta {
    std::string name;
    uint32_t param_id = 0;
    std::string type; // "I64", "U64", "F64", "BOOL", "STR", "ID", "FRONTIER", "DECIMAL"
    bool is_decimal = false;
    uint16_t precision = 0;
    uint8_t scale = 0;
    uint16_t domain_id = 0xFFFFu;
};

struct ImpScmV11CompileResult {
    std::vector<uint8_t> impb_bytes;
    std::vector<uint64_t> code_words;
    std::unordered_map<std::string, uint32_t> param_name_to_id;
    std::vector<ParamMeta> param_schema;
    std::vector<std::string> output_names;
    std::vector<OutputColumnMeta> output_schema;
};

class ImpScmV11Compiler {
public:
    /**
     * Parses, statically validates (IMPSCM_SPECIFICATION_v0_11.md §7.1), and lowers
     * an ImpScheme v0.11 (query ...) program into a validated .impb v0.11 container.
     * Throws std::runtime_error prefixed with error category (DOMAIN_MISMATCH, TYPE_MISMATCH,
     * UNKNOWN_SCHEMA_ENTITY, UNBOUND_SYMBOL, PARSE_ERROR) on failure.
     */
    static ImpScmV11CompileResult compile(
        const std::string& source,
        const impulse_snapshot_t* snapshot,
        bool enable_optimizations = true
    );
};

} // namespace impulse::compiler::v11

#endif /* IMPULSE_V11_IMPSCM_HPP */
