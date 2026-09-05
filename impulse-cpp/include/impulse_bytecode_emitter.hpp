/**
 * @file impulse_bytecode_emitter.hpp
 * @brief C++ port of ImpOpsBytecodeEmitter for Impulse Graph.
 */

#ifndef IMPULSE_BYTECODE_EMITTER_HPP
#define IMPULSE_BYTECODE_EMITTER_HPP

#include "impulse_compiler.hpp"
#include "impulse_vm.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

namespace impulse::compiler::emitter {

struct relation_instruction_patch_t {
    size_t pc;
    std::string logical_relation_name;
    uint16_t src_reg;
    uint16_t dst_reg;
};

struct impulse_vm_program_t {
    std::vector<impulse_instruction_t> instruction_list;
    size_t instruction_count;
    std::vector<relation_instruction_patch_t> patches;
    std::unordered_map<std::string, int> relation_id_map;
    std::vector<std::string> string_pool;
};

class ImpOpsBytecodeEmitter {
public:
    static constexpr uint8_t FLAG_HALT_ON_EMPTY = 0x01;
    static constexpr uint8_t FLAG_INPUT_SEED = 0x02;

    static impulse_vm_program_t emit(
        const std::shared_ptr<impulse::compiler::ImpScmNode>& ast,
        const impulse_snapshot_t* snapshot
    );
};

} // namespace impulse::compiler::emitter

#endif // IMPULSE_BYTECODE_EMITTER_HPP
