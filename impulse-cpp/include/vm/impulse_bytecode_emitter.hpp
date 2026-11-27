/**
 * @file impulse_v11_bytecode_emitter.hpp
 * @brief ImpulseVM v0.11.0 64-bit nibble ISA bytecode builder, ScmProgram emitter,
 *        and ImpAsm v0.11 disassembler.
 */
#ifndef IMPULSE_V11_BYTECODE_EMITTER_HPP
#define IMPULSE_V11_BYTECODE_EMITTER_HPP

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vm/impulse_vm_isa.hpp"
#include "impulse_vm_v11.h"
#include "impulse_scm_ast.hpp"
#include "impulse_graph.h"

namespace impulse::compiler::v11 {

/** Canonical constant pool entry serialized into .impb const_pool_bytes. */
struct V11ConstPoolDef {
    uint32_t id = 0;
    uint8_t  kind = 0;      // 1=Vec, 2=Frontier, 3=Str
    uint8_t  elem_tag = 0;  // 0=I64, 1=U64, 4=F64, 6=BOOL_3VL, 7=STR
    std::string str_val;
    std::vector<uint64_t> frontier_ids;
    std::vector<int64_t> i64_vals;
    std::vector<double> f64_vals;
    std::vector<uint64_t> valid_bits;
};

/**
 * Type-safe 64-bit nibble instruction builder and .impb v0.11 container serializer.
 */
class V11ProgramBuilder {
public:
    static inline uint64_t encode_word(
        uint8_t opcode,
        uint8_t subop,
        uint8_t flags,
        uint8_t dst,
        uint8_t s1,
        uint8_t s2,
        uint8_t s3,
        uint32_t imm32
    ) noexcept {
        return static_cast<uint64_t>(opcode)
             | (static_cast<uint64_t>(subop & 0x0Fu) << 8)
             | (static_cast<uint64_t>(flags & 0x0Fu) << 12)
             | (static_cast<uint64_t>(s1 & 0x0Fu) << 16)
             | (static_cast<uint64_t>(dst & 0x0Fu) << 20)
             | (static_cast<uint64_t>(s3 & 0x0Fu) << 24)
             | (static_cast<uint64_t>(s2 & 0x0Fu) << 28)
             | (static_cast<uint64_t>(imm32) << 32);
    }

    static inline uint64_t encode_word_u16(
        uint8_t opcode,
        uint8_t subop,
        uint8_t flags,
        uint8_t dst,
        uint8_t s1,
        uint8_t s2,
        uint8_t s3,
        uint16_t imm_lo,
        uint16_t imm_hi
    ) noexcept {
        uint32_t imm32 = static_cast<uint32_t>(imm_lo) | (static_cast<uint32_t>(imm_hi) << 16);
        return encode_word(opcode, subop, flags, dst, s1, s2, s3, imm32);
    }

    size_t pc() const noexcept { return words_.size(); }

    size_t emit(
        impulse::vm::Opcode op,
        uint8_t subop = 0,
        uint8_t flags = 0,
        uint8_t dst = 0,
        uint8_t s1 = 0,
        uint8_t s2 = 0,
        uint8_t s3 = 0,
        uint32_t imm32 = 0
    ) {
        size_t pos = words_.size();
        words_.push_back(encode_word(static_cast<uint8_t>(op), subop, flags, dst, s1, s2, s3, imm32));
        return pos;
    }

    size_t emit_u16(
        impulse::vm::Opcode op,
        uint8_t subop,
        uint8_t flags,
        uint8_t dst,
        uint8_t s1,
        uint8_t s2,
        uint8_t s3,
        uint16_t imm_lo,
        uint16_t imm_hi
    ) {
        size_t pos = words_.size();
        words_.push_back(encode_word_u16(static_cast<uint8_t>(op), subop, flags, dst, s1, s2, s3, imm_lo, imm_hi));
        return pos;
    }

    void patch_imm32(size_t word_idx, uint32_t imm32) noexcept {
        words_[word_idx] = (words_[word_idx] & 0x00000000FFFFFFFFULL) | (static_cast<uint64_t>(imm32) << 32);
    }

    void patch_imm16_hi(size_t word_idx, uint16_t imm_hi) noexcept {
        words_[word_idx] = (words_[word_idx] & 0x0000FFFFFFFFFFFFULL) | (static_cast<uint64_t>(imm_hi) << 48);
    }

    void emit_load_const_i64(uint8_t dst, int64_t val) {
        using namespace impulse::vm;
        if (val >= INT32_MIN && val <= INT32_MAX) {
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_I32, 0, dst, 0, 0, 0,
                 static_cast<uint32_t>(static_cast<int32_t>(val)));
        } else {
            uint64_t bits = static_cast<uint64_t>(val);
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_I64, FLAG_EXTENDED, dst, 0, 0, 0,
                 static_cast<uint32_t>(bits & 0xFFFFFFFFULL));
            emit(Opcode::OP_EXTENSION_PAYLOAD, 0, 0, 0, 0, 0, 0,
                 static_cast<uint32_t>(bits >> 32));
        }
    }

    void emit_load_const_u64(uint8_t dst, uint64_t val) {
        using namespace impulse::vm;
        if (val <= UINT32_MAX) {
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_U32, 0, dst, 0, 0, 0,
                 static_cast<uint32_t>(val));
        } else {
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_U64, FLAG_EXTENDED, dst, 0, 0, 0,
                 static_cast<uint32_t>(val & 0xFFFFFFFFULL));
            emit(Opcode::OP_EXTENSION_PAYLOAD, 0, 0, 0, 0, 0, 0,
                 static_cast<uint32_t>(val >> 32));
        }
    }

    void emit_load_const_f64(uint8_t dst, double val) {
        using namespace impulse::vm;
        float f32 = static_cast<float>(val);
        if (static_cast<double>(f32) == val) {
            uint32_t fbits = 0;
            std::memcpy(&fbits, &f32, 4);
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_F32, 0, dst, 0, 0, 0, fbits);
        } else {
            uint64_t dbits = 0;
            std::memcpy(&dbits, &val, 8);
            emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_F64, FLAG_EXTENDED, dst, 0, 0, 0,
                 static_cast<uint32_t>(dbits & 0xFFFFFFFFULL));
            emit(Opcode::OP_EXTENSION_PAYLOAD, 0, 0, 0, 0, 0, 0,
                 static_cast<uint32_t>(dbits >> 32));
        }
    }

    uint32_t add_const_pool_str(const std::string& s) {
        uint32_t id = static_cast<uint32_t>(const_pool_.size());
        V11ConstPoolDef def;
        def.id = id;
        def.kind = 3; // Str
        def.str_val = s;
        const_pool_.push_back(std::move(def));
        return id;
    }

    uint32_t add_const_pool_frontier(const std::vector<uint64_t>& ids) {
        uint32_t id = static_cast<uint32_t>(const_pool_.size());
        V11ConstPoolDef def;
        def.id = id;
        def.kind = 2; // Frontier
        def.frontier_ids = ids;
        const_pool_.push_back(std::move(def));
        return id;
    }

    uint32_t add_const_pool_vec_u64(const std::vector<int64_t>& vals) {
        uint32_t id = static_cast<uint32_t>(const_pool_.size());
        V11ConstPoolDef def;
        def.id = id;
        def.kind = 1; // Vec
        def.elem_tag = 1; // kVecTagU64
        def.i64_vals = vals;
        const_pool_.push_back(std::move(def));
        return id;
    }

    uint32_t add_const_pool_vec_f64(const std::vector<double>& vals) {
        uint32_t id = static_cast<uint32_t>(const_pool_.size());
        V11ConstPoolDef def;
        def.id = id;
        def.kind = 1; // Vec
        def.elem_tag = 4; // kVecTagF64
        def.f64_vals = vals;
        const_pool_.push_back(std::move(def));
        return id;
    }

    const std::vector<uint64_t>& words() const noexcept { return words_; }
    const std::vector<V11ConstPoolDef>& const_pool() const noexcept { return const_pool_; }

    void append_words(const std::vector<uint64_t>& extra) {
        words_.insert(words_.end(), extra.begin(), extra.end());
    }

    /** Serializes words + const_pool into a checksummed .impb v0.11 container. */
    std::vector<uint8_t> build_impb(
        uint8_t max_call_depth = 0,
        uint32_t entry_pc = 0,
        uint16_t required_domains = 0
    ) const;

private:
    std::vector<uint64_t> words_;
    std::vector<V11ConstPoolDef> const_pool_;
};

/** Output of ImpulseBytecodeEmitter::emit(). */
struct ImpulseCompiledProgram {
    std::vector<uint8_t> impb_bytes;
    std::vector<uint64_t> code_words;
    std::unordered_map<std::string, uint32_t> relation_id_map;
    std::unordered_map<std::string, uint32_t> param_name_to_id;
    uint16_t start_domain_id = 0;
    uint16_t final_domain_id = 0;
    uint16_t required_domains = 0;
};

/** Retargeted ImpScheme ScmProgram -> ImpulseVM v0.11.0 bytecode emitter. */
class ImpulseBytecodeEmitter {
public:
    static ImpulseCompiledProgram emit(
        const std::shared_ptr<impulse::compiler::ImpScmNode>& ast,
        const impulse_snapshot_t* snapshot = nullptr,
        uint16_t start_domain_id = 0
    );

    static std::string disassemble(const std::vector<uint64_t>& words);
};

} // namespace impulse::compiler::v11

#endif /* IMPULSE_V11_BYTECODE_EMITTER_HPP */
