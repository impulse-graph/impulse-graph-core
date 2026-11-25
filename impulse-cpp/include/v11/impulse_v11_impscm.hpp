/**
 * @file impulse_v11_impscm.hpp
 * @brief ImpScheme v0.11.0 SSA IR Parser, Static Domain/Type Validator, and .impb Compiler.
 */
#ifndef IMPULSE_V11_IMPSCM_HPP
#define IMPULSE_V11_IMPSCM_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse_graph.h"
#include "v11/impulse_v11_bytecode_emitter.hpp"

namespace impulse::compiler::v11 {

struct ImpScmV11CompileResult {
    std::vector<uint8_t> impb_bytes;
    std::vector<uint64_t> code_words;
    std::unordered_map<std::string, uint32_t> param_name_to_id;
    std::vector<std::string> output_names;
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
