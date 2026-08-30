#include "impulse_bytecode_emitter.hpp"
#include "impulse_graph.h"
#include <cstring>
#include <algorithm>

namespace impulse::compiler::emitter {

class StreamRegisterAllocator {
    int next_reg = 1; // 0 is TGT_ID implicitly
public:
    int allocate() {
        if (next_reg >= 16) throw std::runtime_error("Stream register exhaustion");
        return next_reg++;
    }
};

static int resolve_relation_id(const impulse_snapshot_t* snapshot, const std::string& rel_name) {
    if (!snapshot || rel_name.empty()) return 0;
    uint16_t count = impulse_snapshot_relation_count(snapshot);
    
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    std::string rel_name_lower = to_lower(rel_name);

    for (uint16_t i = 0; i < count; ++i) {
        char name_buf[256];
        if (impulse_snapshot_get_relation_entry(snapshot, i, name_buf, sizeof(name_buf), nullptr, nullptr) == IMPULSE_OK) {
            std::string n = name_buf;
            if (to_lower(n) == rel_name_lower) return i;
        }
    }
    
    for (uint16_t i = 0; i < count; ++i) {
        char name_buf[256];
        if (impulse_snapshot_get_relation_entry(snapshot, i, name_buf, sizeof(name_buf), nullptr, nullptr) == IMPULSE_OK) {
            std::string n = to_lower(name_buf);
            if (n.size() >= rel_name_lower.size() && 
                n.compare(n.size() - rel_name_lower.size(), rel_name_lower.size(), rel_name_lower) == 0) {
                return i;
            }
        }
    }
    return 0;
}

static int emit_stream_expr(const AstPtr& expr, std::vector<impulse_instruction_t>& instr_list, StreamRegisterAllocator& alloc, const impulse_snapshot_t* snapshot) {
    if (expr->kind() == NodeKind::LIST) {
        auto list = std::static_pointer_cast<ScmList>(expr);
        if (!list->elements.empty() && list->elements[0]->kind() == NodeKind::SYMBOL) {
            auto sym = std::static_pointer_cast<ScmSymbol>(list->elements[0]);
            std::string op = sym->name;
            if (op == "stream-cmp-gt" || op == "vec-cmp-gt") {
                int lhs = emit_stream_expr(list->elements[1], instr_list, alloc, snapshot);
                int rhs = emit_stream_expr(list->elements[2], instr_list, alloc, snapshot);
                int dst = alloc.allocate();
                impulse_instruction_t inst{};
                inst.opcode = OP_STREAM_CMP_GT;
                inst.dst_reg = dst;
                inst.payload = (lhs & 0xFFFF) | ((rhs & 0xFFFF) << 16);
                instr_list.push_back(inst);
                return dst;
            } else if (op == "stream-load-attr" || op == "get-attr") {
                // String attr = ...
                int dst = alloc.allocate();
                impulse_instruction_t inst{};
                inst.opcode = OP_STREAM_LOAD_TGT;
                inst.dst_reg = dst;
                instr_list.push_back(inst);
                return dst;
            } else if (op == "stream-logic-and" || op == "mask-and") {
                int lhs = emit_stream_expr(list->elements[1], instr_list, alloc, snapshot);
                int rhs = emit_stream_expr(list->elements[2], instr_list, alloc, snapshot);
                int dst = alloc.allocate();
                impulse_instruction_t inst{};
                inst.opcode = OP_STREAM_LOGIC_AND;
                inst.dst_reg = dst;
                inst.payload = (lhs & 0xFFFF) | ((rhs & 0xFFFF) << 16);
                instr_list.push_back(inst);
                return dst;
            }
        }
    } else if (expr->kind() == NodeKind::LITERAL) {
        auto lit = std::static_pointer_cast<ScmLiteral>(expr);
        int dst = alloc.allocate();
        uint32_t payload = 0;
        if (lit->type == ScmLiteral::LitType::INT) {
            float f = static_cast<float>(lit->int_val);
            std::memcpy(&payload, &f, sizeof(f));
        } else if (lit->type == ScmLiteral::LitType::FLOAT) {
            float f = static_cast<float>(lit->float_val);
            std::memcpy(&payload, &f, sizeof(f));
        }
        impulse_instruction_t inst{};
        inst.opcode = OP_STREAM_LOAD_CONST;
        inst.dst_reg = dst;
        inst.payload = payload;
        instr_list.push_back(inst);
        return dst;
    }
    
    int dst = alloc.allocate();
    impulse_instruction_t inst{};
    inst.opcode = OP_STREAM_LOAD_CONST;
    inst.dst_reg = dst;
    inst.payload = 0;
    instr_list.push_back(inst);
    return dst;
}

static void emit_stream_step(const AstPtr& node, std::vector<impulse_instruction_t>& instr_list, StreamRegisterAllocator& alloc, const impulse_snapshot_t* snapshot) {
    if (node->kind() == NodeKind::STREAM_FILTER) {
        auto filter = std::static_pointer_cast<ScmStreamFilter>(node);
        int reg = emit_stream_expr(filter->predicate, instr_list, alloc, snapshot);
        impulse_instruction_t inst{};
        inst.opcode = OP_STREAM_FILTER;
        inst.dst_reg = reg;
        instr_list.push_back(inst);
    } else if (node->kind() == NodeKind::STREAM_PROJECT) {
        auto project = std::static_pointer_cast<ScmStreamProject>(node);
        int reg = emit_stream_expr(project->expr, instr_list, alloc, snapshot);
        impulse_instruction_t inst{};
        inst.opcode = OP_STREAM_SCATTER_REDUCE;
        inst.dst_reg = reg;
        inst.payload = (5 & 0xFFFF) | ((0 & 0xFF) << 16);
        instr_list.push_back(inst);
    } else if (node->kind() == NodeKind::LIST) {
        auto list = std::static_pointer_cast<ScmList>(node);
        if (!list->elements.empty() && list->elements[0]->kind() == NodeKind::SYMBOL) {
            auto sym = std::static_pointer_cast<ScmSymbol>(list->elements[0]);
            if (sym->name == "project-state") {
                // Do nothing as in Java
            }
        }
    }
}

static short emit_sub_steps(const std::vector<AstPtr>& sub_steps, const impulse_snapshot_t* snapshot,
                            std::vector<impulse_instruction_t>& instr_list,
                            std::vector<relation_instruction_patch_t>& patches,
                            std::unordered_map<std::string, int>& relation_id_map,
                            short start_reg) {
    short current_reg = start_reg;
    for (const auto& step : sub_steps) {
        if (step->kind() == NodeKind::WALK) {
            auto walk = std::static_pointer_cast<ScmWalk>(step);
            int rel_id = walk->physical_rel_id;
            std::string logical_rel_name = walk->relation_name;
            if (rel_id < 0 && !logical_rel_name.empty() && snapshot != nullptr) {
                rel_id = resolve_relation_id(snapshot, logical_rel_name);
            }
            if (rel_id >= 0) {
                relation_id_map[logical_rel_name] = rel_id;
            }

            short src_reg = current_reg;
            short dst_reg = (short) (1 - current_reg);
            current_reg = dst_reg;

            long pc = instr_list.size();
            patches.push_back({(size_t)pc, logical_rel_name, (uint16_t)src_reg, (uint16_t)dst_reg});

            int payload = ((rel_id & 0xFFFF) << 16) | (src_reg & 0xFFFF);
            impulse_instruction_t inst{};
            inst.opcode = OP_CSR_WALK;
            inst.dst_reg = dst_reg;
            inst.payload = payload;
            instr_list.push_back(inst);
        }
    }
    return current_reg;
}

impulse_vm_program_t ImpOpsBytecodeEmitter::emit(
    const std::shared_ptr<impulse::compiler::ImpScmNode>& ast,
    const impulse_snapshot_t* snapshot) 
{
    impulse_vm_program_t prog_out;
    std::vector<impulse_instruction_t> instr_list;
    std::vector<relation_instruction_patch_t> patches;
    std::unordered_map<std::string, int> relation_id_map;
    std::vector<std::string> string_pool;

    short current_reg = 0;
    bool first_step_emitted = false;

    if (ast && ast->kind() == NodeKind::PROGRAM) {
        auto prog = std::static_pointer_cast<ScmProgram>(ast);
        for (const auto& step : prog->steps) {
            short src_reg = current_reg;
            short dst_reg = (short)(1 - current_reg);
            current_reg = dst_reg;

            if (step->kind() == NodeKind::WALK) {
                auto walk = std::static_pointer_cast<ScmWalk>(step);
                int rel_id = walk->physical_rel_id;
                std::string logical_rel_name = walk->relation_name;
                if (rel_id < 0 && !logical_rel_name.empty() && snapshot != nullptr) {
                    rel_id = resolve_relation_id(snapshot, logical_rel_name);
                }
                if (rel_id >= 0) {
                    relation_id_map[logical_rel_name] = rel_id;
                }

                long pc = instr_list.size();
                patches.push_back({(size_t)pc, logical_rel_name, (uint16_t)src_reg, (uint16_t)dst_reg});

                bool is_stream = !walk->shader_steps.empty();
                uint8_t opcode = (walk->direction == WalkDirection::REVERSE_CSC) 
                    ? (is_stream ? OP_CSC_WALK_STREAM : OP_CSC_WALK) 
                    : (is_stream ? OP_CSR_WALK_STREAM : OP_CSR_WALK);

                uint8_t flags = FLAG_HALT_ON_EMPTY;
                if (!first_step_emitted) {
                    flags |= FLAG_INPUT_SEED;
                    first_step_emitted = true;
                }

                uint32_t payload = ((rel_id & 0xFFFF) << 16) | (src_reg & 0xFFFF);
                
                if (is_stream) {
                    int walk_instr_idx = instr_list.size();
                    impulse_instruction_t inst{};
                    inst.opcode = opcode;
                    inst.flags = flags;
                    inst.dst_reg = dst_reg;
                    inst.payload = payload;
                    instr_list.push_back(inst);
                    
                    impulse_instruction_t begin{};
                    begin.opcode = OP_STREAM_FUNC_BEGIN;
                    instr_list.push_back(begin);
                    
                    int shader_pc_start = instr_list.size();
                    instr_list[walk_instr_idx].flags = (uint8_t)shader_pc_start;
                    
                    StreamRegisterAllocator s_reg_alloc;
                    for (const auto& shader_step : walk->shader_steps) {
                        emit_stream_step(shader_step, instr_list, s_reg_alloc, snapshot);
                    }
                    
                    impulse_instruction_t yield{};
                    yield.opcode = OP_STREAM_YIELD;
                    instr_list.push_back(yield);
                    
                    impulse_instruction_t end{};
                    end.opcode = OP_STREAM_FUNC_END;
                    instr_list.push_back(end);
                    
                } else {
                    impulse_instruction_t inst{};
                    inst.opcode = opcode;
                    inst.flags = flags;
                    inst.dst_reg = dst_reg;
                    inst.payload = payload;
                    instr_list.push_back(inst);
                }
            } else if (step->kind() == NodeKind::WALK_2HOP) {
                auto hop2 = std::static_pointer_cast<ScmWalk2Hop>(step);
                int rel1_id = hop2->rel1_id;
                int rel2_id = hop2->rel2_id;
                if (rel1_id < 0 && !hop2->rel1_name.empty() && snapshot != nullptr) {
                    rel1_id = resolve_relation_id(snapshot, hop2->rel1_name);
                }
                if (rel2_id < 0 && !hop2->rel2_name.empty() && snapshot != nullptr) {
                    rel2_id = resolve_relation_id(snapshot, hop2->rel2_name);
                }
                if (rel1_id >= 0) relation_id_map[hop2->rel1_name] = rel1_id;
                if (rel2_id >= 0) relation_id_map[hop2->rel2_name] = rel2_id;

                uint8_t flags = FLAG_HALT_ON_EMPTY;
                if (!first_step_emitted) {
                    flags |= FLAG_INPUT_SEED;
                    first_step_emitted = true;
                }

                uint32_t payload = ((rel2_id & 0xFFFF) << 16) | (rel1_id & 0xFFFF);
                impulse_instruction_t inst{};
                inst.opcode = OP_CSR_WALK_2HOP;
                inst.flags = flags;
                inst.dst_reg = dst_reg;
                inst.payload = payload;
                instr_list.push_back(inst);
            } else if (step->kind() == NodeKind::VECTOR_FILTER) {
                uint8_t flags = 0;
                if (!first_step_emitted) {
                    flags |= FLAG_INPUT_SEED;
                    first_step_emitted = true;
                }
                uint32_t payload = ((src_reg & 0xFFFF) << 16) | (src_reg & 0xFFFF);
                impulse_instruction_t inst{};
                inst.opcode = OP_NODE_FILTER;
                inst.flags = flags;
                inst.dst_reg = dst_reg;
                inst.payload = payload;
                instr_list.push_back(inst);
            } else if (step->kind() == NodeKind::REDUCE) {
                auto red = std::static_pointer_cast<ScmReduce>(step);
                uint8_t opcode = OP_REDUCE;
                if (red->op_val == ScmReduce::Op::SUM || red->op_val == ScmReduce::Op::COUNT) {
                    opcode = OP_VECTOR_REDUCE_SUM;
                }
                int op_id = 0;
                switch (red->op_val) {
                    case ScmReduce::Op::SUM:
                    case ScmReduce::Op::COUNT: op_id = 0; break;
                    case ScmReduce::Op::MIN: op_id = 1; break;
                    case ScmReduce::Op::MAX: op_id = 2; break;
                    case ScmReduce::Op::ARGMIN: op_id = 3; break;
                    case ScmReduce::Op::ARGMAX: op_id = 4; break;
                    case ScmReduce::Op::FIRST: op_id = 5; break;
                }
                uint32_t payload = (op_id << 16) | (src_reg & 0xFFFF);
                impulse_instruction_t inst{};
                inst.opcode = opcode;
                inst.dst_reg = src_reg; // In Java, srcReg is used
                inst.payload = payload;
                instr_list.push_back(inst);
            } else if (step->kind() == NodeKind::COLLECT) {
                auto collect = std::static_pointer_cast<ScmCollect>(step);
                uint8_t flags = 0;
                if (!first_step_emitted) {
                    flags |= FLAG_INPUT_SEED;
                    first_step_emitted = true;
                }
                uint8_t opcode = OP_COLLECT_BITSET;
                if (collect->output_type == ScmCollect::OutputType::VECTOR || 
                    collect->output_type == ScmCollect::OutputType::LIST) {
                    opcode = OP_COLLECT_ARRAY;
                }
                impulse_instruction_t inst{};
                inst.opcode = opcode;
                inst.flags = flags;
                inst.dst_reg = src_reg;
                inst.payload = (src_reg << 16) | src_reg;
                instr_list.push_back(inst);
            } else if (step->kind() == NodeKind::LIST) {
                auto list = std::static_pointer_cast<ScmList>(step);
                if (!list->elements.empty()) {
                    auto head = list->elements[0];
                    std::string op_name = "";
                    if (head->kind() == NodeKind::SYMBOL) {
                        op_name = std::static_pointer_cast<ScmSymbol>(head)->name;
                    }
                    
                    if (op_name == "repeat" || op_name == "repeat-until-stable") {
                        int repeat_count = 1;
                        if (op_name == "repeat") {
                            if (list->elements.size() > 1 && list->elements[1]->kind() == NodeKind::LITERAL) {
                                auto cnt = std::static_pointer_cast<ScmLiteral>(list->elements[1]);
                                if (cnt->type == ScmLiteral::LitType::INT) repeat_count = (int)cnt->int_val;
                            }
                            short count_reg = (short)(current_reg + 2);
                            impulse_instruction_t load{};
                            load.opcode = OP_LOAD_CONST_INT;
                            load.dst_reg = count_reg;
                            load.payload = repeat_count;
                            instr_list.push_back(load);
                            
                            long loop_start_pc = instr_list.size();
                            if (list->elements.size() > 2 && list->elements[2]->kind() == NodeKind::PROGRAM) {
                                auto sub_prog = std::static_pointer_cast<ScmProgram>(list->elements[2]);
                                current_reg = emit_sub_steps(sub_prog->steps, snapshot, instr_list, patches, relation_id_map, current_reg);
                            }
                            
                            impulse_instruction_t decr{};
                            decr.opcode = OP_LOOP_DECR;
                            decr.dst_reg = count_reg;
                            decr.payload = (uint32_t)loop_start_pc;
                            instr_list.push_back(decr);
                        } else {
                            long loop_start_pc = instr_list.size();
                            if (list->elements.size() > 1 && list->elements[1]->kind() == NodeKind::PROGRAM) {
                                auto sub_prog = std::static_pointer_cast<ScmProgram>(list->elements[1]);
                                current_reg = emit_sub_steps(sub_prog->steps, snapshot, instr_list, patches, relation_id_map, current_reg);
                            }
                            impulse_instruction_t stab{};
                            stab.opcode = OP_STABLE_CHECK;
                            stab.dst_reg = current_reg;
                            instr_list.push_back(stab);
                            
                            impulse_instruction_t jnz{};
                            jnz.opcode = OP_JNZ;
                            jnz.payload = (uint32_t)loop_start_pc;
                            instr_list.push_back(jnz);
                        }
                    } else if (op_name == "project-expression") {
                        std::string attr_name = "";
                        if (list->elements.size() > 1 && list->elements[1]->kind() == NodeKind::SYMBOL) {
                            attr_name = std::static_pointer_cast<ScmSymbol>(list->elements[1])->name;
                        }
                        auto it = std::find(string_pool.begin(), string_pool.end(), attr_name);
                        int name_idx = 0;
                        if (it == string_pool.end()) {
                            name_idx = string_pool.size();
                            string_pool.push_back(attr_name);
                        } else {
                            name_idx = std::distance(string_pool.begin(), it);
                        }
                        
                        uint32_t payload = ((src_reg & 0xFFFF) << 16) | (name_idx & 0xFFFF);
                        impulse_instruction_t inst{};
                        inst.opcode = OP_VECTOR_LOAD_ATTR;
                        inst.dst_reg = dst_reg;
                        inst.payload = payload;
                        instr_list.push_back(inst);
                    } else if (op_name == "island-detect") {
                        impulse_instruction_t inst{};
                        inst.opcode = OP_ISLAND_DETECT;
                        inst.dst_reg = dst_reg;
                        instr_list.push_back(inst);
                    } else if (op_name == "rebac-check") {
                        impulse_instruction_t inst{};
                        inst.opcode = OP_REBAC_CHECK;
                        inst.dst_reg = dst_reg;
                        instr_list.push_back(inst);
                    } else if (op_name == "motif-match-3") {
                        impulse_instruction_t inst{};
                        inst.opcode = OP_MOTIF_MATCH_3;
                        inst.dst_reg = dst_reg;
                        instr_list.push_back(inst);
                    }
                }
            }
        }
    }

    impulse_instruction_t halt{};
    halt.opcode = OP_HALT;
    instr_list.push_back(halt);

    prog_out.instruction_list = std::move(instr_list);
    prog_out.instruction_count = prog_out.instruction_list.size();
    prog_out.patches = std::move(patches);
    prog_out.relation_id_map = std::move(relation_id_map);
    prog_out.string_pool = std::move(string_pool);

    return prog_out;
}

} // namespace impulse::compiler::emitter
