#include "vm/impulse_bytecode_emitter.hpp"
#include "vm_xxh3.hpp"
#include "../impulse_graph_internal.hpp"
#include "vm_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace impulse::compiler {

using namespace impulse::vm;

namespace {

void append_u16_le(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void append_u32_le(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
}

void append_u64_le(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
}

void pad8(std::vector<uint8_t>& out) {
    while ((out.size() & 7u) != 0u) out.push_back(0u);
}

int resolve_rel_id(const impulse_snapshot_t* snap, const std::string& name, int fallback_id) {
    if (fallback_id >= 0) return fallback_id;
    if (!snap || name.empty()) return 0;
    for (size_t i = 0; i < snap->relation_names.size() && i < snap->relations.size(); ++i) {
        if (snap->relation_names[i] == name) return static_cast<int>(snap->relations[i].relation_id);
    }
    return 0;
}

uint16_t resolve_rel_src_dom(const impulse_snapshot_t* snap, int rel_id, uint16_t fallback) {
    if (!snap) return fallback;
    for (const auto& rd : snap->relations) {
        if (static_cast<int>(rd.relation_id) == rel_id) return rd.src_domain_id;
    }
    return fallback;
}

uint16_t resolve_rel_tgt_dom(const impulse_snapshot_t* snap, int rel_id, uint16_t fallback) {
    if (!snap) return fallback;
    for (const auto& rd : snap->relations) {
        if (static_cast<int>(rd.relation_id) == rel_id) return rd.tgt_domain_id;
    }
    return fallback;
}

int resolve_node_attr_id(const impulse_snapshot_t* snap, uint16_t dom_id, const std::string& attr_name) {
    if (!snap || attr_name.empty() || dom_id >= snap->domain_attribute_names.size()) return 0;
    const auto& names = snap->domain_attribute_names[dom_id];
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == attr_name) return static_cast<int>(i);
    }
    return 0;
}

int resolve_edge_attr_id(const impulse_snapshot_t* snap, int rel_id, const std::string& attr_name) {
    if (!snap || attr_name.empty() || rel_id < 0 || static_cast<size_t>(rel_id) >= snap->relation_attribute_names.size()) return 0;
    const auto& names = snap->relation_attribute_names[static_cast<size_t>(rel_id)];
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == attr_name) return static_cast<int>(i);
    }
    return 0;
}

/**
 * Emits a morsel-body expression (evaluating into M[dst_m], where dst_m in 8..15).
 * M0 (8)=src_id/node_id, M1 (9)=tgt_id, M2 (10)=edge_idx, M3 (11)=lane_mask.
 * Scratch morsel vector registers: 12..15 (M4..M7).
 */
uint8_t emit_morsel_expr(
    const AstPtr& expr,
    VmProgramBuilder& b,
    uint8_t& next_mreg,
    uint8_t& next_rreg,
    const impulse_snapshot_t* snap,
    uint16_t tgt_dom,
    int rel_id,
    bool is_node_morsel
) {
    auto alloc_m = [&]() -> uint8_t {
        if (next_mreg > 15u) throw std::runtime_error("v11 morsel vector register exhaustion (M4..M7)");
        return next_mreg++;
    };
    auto alloc_r = [&]() -> uint8_t {
        if (next_rreg > 15u) throw std::runtime_error("v11 scalar register exhaustion");
        return next_rreg++;
    };

    if (expr->kind() == NodeKind::LIST) {
        auto list = std::static_pointer_cast<ScmList>(expr);
        if (!list->elements.empty() && list->elements[0]->kind() == NodeKind::SYMBOL) {
            const std::string& op = std::static_pointer_cast<ScmSymbol>(list->elements[0])->name;
            auto cmp_subop = [&](const std::string& name, uint8_t& out_sub) -> bool {
                if (name == "stream-cmp-eq" || name == "vec-cmp-eq" || name == "=")  { out_sub = subop::kVEC_CMP_EQ; return true; }
                if (name == "stream-cmp-ne" || name == "vec-cmp-ne" || name == "!=") { out_sub = subop::kVEC_CMP_NE; return true; }
                if (name == "stream-cmp-lt" || name == "vec-cmp-lt" || name == "<")  { out_sub = subop::kVEC_CMP_LT; return true; }
                if (name == "stream-cmp-le" || name == "vec-cmp-le" || name == "<=") { out_sub = subop::kVEC_CMP_LE; return true; }
                if (name == "stream-cmp-gt" || name == "vec-cmp-gt" || name == ">")  { out_sub = subop::kVEC_CMP_GT; return true; }
                if (name == "stream-cmp-ge" || name == "vec-cmp-ge" || name == ">=") { out_sub = subop::kVEC_CMP_GE; return true; }
                return false;
            };
            uint8_t csub = 0;
            if (cmp_subop(op, csub) && list->elements.size() >= 3) {
                uint8_t lhs = emit_morsel_expr(list->elements[1], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t dst = alloc_m();
                if (list->elements[2]->kind() == NodeKind::LITERAL) {
                    auto lit = std::static_pointer_cast<ScmLiteral>(list->elements[2]);
                    uint8_t r = alloc_r();
                    if (lit->type == ScmLiteral::LitType::INT) b.emit_load_const_i64(r, lit->int_val);
                    else if (lit->type == ScmLiteral::LitType::FLOAT) b.emit_load_const_f64(r, lit->float_val);
                    else b.emit_load_const_i64(r, 0);
                    b.emit(Opcode::OP_VEC_CMP, csub, FLAG_SCALAR_RHS, dst, lhs, r, 0, 0);
                } else {
                    uint8_t rhs = emit_morsel_expr(list->elements[2], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                    b.emit(Opcode::OP_VEC_CMP, csub, 0, dst, lhs, rhs, 0, 0);
                }
                return dst;
            }
            if ((op == "stream-logic-and" || op == "mask-and" || op == "and") && list->elements.size() >= 3) {
                uint8_t lhs = emit_morsel_expr(list->elements[1], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t rhs = emit_morsel_expr(list->elements[2], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t dst = alloc_m();
                b.emit(Opcode::OP_VEC_3VL_OP, subop::kVEC_3VL_OP_AND, 0, dst, lhs, rhs, 0, 0);
                return dst;
            }
            if ((op == "stream-logic-or" || op == "mask-or" || op == "or") && list->elements.size() >= 3) {
                uint8_t lhs = emit_morsel_expr(list->elements[1], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t rhs = emit_morsel_expr(list->elements[2], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t dst = alloc_m();
                b.emit(Opcode::OP_VEC_3VL_OP, subop::kVEC_3VL_OP_OR, 0, dst, lhs, rhs, 0, 0);
                return dst;
            }
            if ((op == "stream-logic-not" || op == "not") && list->elements.size() >= 2) {
                uint8_t lhs = emit_morsel_expr(list->elements[1], b, next_mreg, next_rreg, snap, tgt_dom, rel_id, is_node_morsel);
                uint8_t dst = alloc_m();
                b.emit(Opcode::OP_VEC_3VL_OP, subop::kVEC_3VL_OP_NOT, 0, dst, lhs, 0, 0, 0);
                return dst;
            }
            if (op == "stream-load-attr" || op == "get-attr" || op == "node-attr") {
                std::string aname;
                if (list->elements.size() >= 2) {
                    if (list->elements[1]->kind() == NodeKind::LITERAL)
                        aname = std::static_pointer_cast<ScmLiteral>(list->elements[1])->str_val;
                    else if (list->elements[1]->kind() == NodeKind::SYMBOL)
                        aname = std::static_pointer_cast<ScmSymbol>(list->elements[1])->name;
                }
                int aid = resolve_node_attr_id(snap, tgt_dom, aname);
                uint8_t dst = alloc_m();
                uint8_t idx_reg = is_node_morsel ? 8u : 9u; // M0 for node morsel, M1 (tgt_id) for walk morsel
                b.emit_u16(Opcode::OP_GATHER_NODE_ATTR, subop::kGATHER_NODE_ATTR_VEC, 0, dst, idx_reg, 0, 0,
                           tgt_dom, static_cast<uint16_t>(aid));
                return dst;
            }
            if (op == "edge-attr") {
                std::string aname;
                if (list->elements.size() >= 2) {
                    if (list->elements[1]->kind() == NodeKind::LITERAL)
                        aname = std::static_pointer_cast<ScmLiteral>(list->elements[1])->str_val;
                    else if (list->elements[1]->kind() == NodeKind::SYMBOL)
                        aname = std::static_pointer_cast<ScmSymbol>(list->elements[1])->name;
                }
                int aid = resolve_edge_attr_id(snap, rel_id, aname);
                uint8_t dst = alloc_m();
                b.emit_u16(Opcode::OP_GATHER_EDGE_ATTR, subop::kGATHER_EDGE_ATTR_VEC, 0, dst, 10u /*M2=edge_idx*/, 0, 0,
                           static_cast<uint16_t>(rel_id), static_cast<uint16_t>(aid));
                return dst;
            }
        }
    }
    // Fallback: return M1 (tgt_id) or M0 (node_id)
    return is_node_morsel ? 8u : 9u;
}

} // namespace

std::vector<uint8_t> VmProgramBuilder::build_impb(
    uint8_t max_call_depth,
    uint32_t entry_pc,
    uint16_t required_domains
) const {
    // 1. Serialize const_pool (canonical little-endian, 8-byte aligned)
    std::vector<uint8_t> cp_bytes;
    if (!const_pool_.empty()) {
        append_u32_le(cp_bytes, static_cast<uint32_t>(const_pool_.size()));
        append_u32_le(cp_bytes, 0u); // reserved
        for (const auto& e : const_pool_) {
            append_u32_le(cp_bytes, e.id);
            cp_bytes.push_back(e.kind);
            if (e.kind == 3) { // Str
                cp_bytes.push_back(0u); // elem_tag
                append_u16_le(cp_bytes, 0u); // flags
                append_u32_le(cp_bytes, static_cast<uint32_t>(e.str_val.size()));
                append_u32_le(cp_bytes, 0u); // extra
                cp_bytes.insert(cp_bytes.end(), e.str_val.begin(), e.str_val.end());
                pad8(cp_bytes);
            } else if (e.kind == 2) { // Frontier
                cp_bytes.push_back(0u);
                append_u16_le(cp_bytes, 0u);
                append_u32_le(cp_bytes, static_cast<uint32_t>(e.frontier_ids.size()));
                append_u32_le(cp_bytes, 0u);
                for (uint64_t id : e.frontier_ids) append_u64_le(cp_bytes, id);
            } else if (e.kind == 1) { // Vec
                bool has_valid = !e.valid_bits.empty();
                cp_bytes.push_back(e.elem_tag);
                append_u16_le(cp_bytes, has_valid ? 1u : 0u);
                uint32_t cnt = (e.elem_tag == 4u)
                    ? static_cast<uint32_t>(e.f64_vals.size())
                    : static_cast<uint32_t>(e.i64_vals.size());
                append_u32_le(cp_bytes, cnt);
                append_u32_le(cp_bytes, 0u);
                if (has_valid) {
                    for (uint64_t vw : e.valid_bits) append_u64_le(cp_bytes, vw);
                }
                if (e.elem_tag == 4u) {
                    for (double d : e.f64_vals) {
                        uint64_t bits = 0;
                        std::memcpy(&bits, &d, 8);
                        append_u64_le(cp_bytes, bits);
                    }
                } else {
                    for (int64_t v : e.i64_vals) append_u64_le(cp_bytes, static_cast<uint64_t>(v));
                }
            }
        }
        pad8(cp_bytes);
    }

    // 2. Build payload (code words + const_pool) and compute XXH3-64 checksum
    const size_t code_bytes_len = words_.size() * sizeof(uint64_t);
    const size_t payload_len = code_bytes_len + cp_bytes.size();
    std::vector<uint8_t> out(sizeof(impulse_impb_header_t) + payload_len, 0u);

    uint8_t* payload_ptr = out.data() + sizeof(impulse_impb_header_t);
    if (code_bytes_len > 0u) {
        std::memcpy(payload_ptr, words_.data(), code_bytes_len);
    }
    if (!cp_bytes.empty()) {
        std::memcpy(payload_ptr + code_bytes_len, cp_bytes.data(), cp_bytes.size());
    }

    uint64_t checksum = VmXxh3::hash64(payload_ptr, payload_len);

    impulse_impb_header_t hdr{};
    hdr.magic = kImpbMagic;
    hdr.isa_version = kImpbVersion;
    hdr.header_flags = cp_bytes.empty() ? 0u : 1u;
    hdr.max_call_depth = max_call_depth;
    hdr.code_word_count = static_cast<uint32_t>(words_.size());
    hdr.const_pool_bytes = static_cast<uint32_t>(cp_bytes.size());
    hdr.entry_pc = entry_pc;
    hdr.required_domains = required_domains;
    hdr.reserved = 0u;
    hdr.program_xxh3_64 = checksum;
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    return out;
}

ImpulseCompiledProgram ImpulseBytecodeEmitter::emit(
    const std::shared_ptr<impulse::compiler::ImpScmNode>& ast,
    const impulse_snapshot_t* snapshot,
    uint16_t start_domain_id
) {
    if (!ast || ast->kind() != NodeKind::PROGRAM) {
        throw std::invalid_argument("ImpulseBytecodeEmitter expects a ScmProgram root");
    }

    VmProgramBuilder b;
    ImpulseCompiledProgram result;
    result.start_domain_id = start_domain_id;

    uint8_t cur_f = 0;        // Ping-pong active frontier F0 <-> F1
    uint8_t cur_v = 0;        // Active vector V0
    uint8_t cur_r = 0;        // Active scalar R0
    uint16_t cur_dom = start_domain_id;
    bool frontier_initialized = false;
    bool active_is_vector = false;
    bool active_is_scalar = false;
    bool active_is_multilane = false;
    uint16_t max_dom_seen = start_domain_id;

    auto ensure_seed_frontier = [&](uint16_t dom_id) {
        if (!frontier_initialized) {
            // Param 0 is the implicit input seed frontier
            b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, cur_f, 0, 0, 0, 0u, dom_id);
            result.start_domain_id = dom_id;
            result.param_name_to_id["$seed"] = 0u;
            frontier_initialized = true;
            cur_dom = dom_id;
            max_dom_seen = std::max(max_dom_seen, dom_id);
        }
    };

    auto prog = std::static_pointer_cast<ScmProgram>(ast);
    for (const auto& step : prog->steps) {
        uint8_t src_f = cur_f;
        uint8_t dst_f = static_cast<uint8_t>(1u - cur_f);

        if (step->kind() == NodeKind::WALK) {
            auto walk = std::static_pointer_cast<ScmWalk>(step);
            int rel_id = resolve_rel_id(snapshot, walk->relation_name, walk->physical_rel_id);
            result.relation_id_map[walk->relation_name] = static_cast<uint32_t>(rel_id);
            uint16_t src_dom = (walk->direction == WalkDirection::REVERSE_CSC)
                ? resolve_rel_tgt_dom(snapshot, rel_id, cur_dom)
                : resolve_rel_src_dom(snapshot, rel_id, cur_dom);
            uint16_t tgt_dom = (walk->direction == WalkDirection::REVERSE_CSC)
                ? resolve_rel_src_dom(snapshot, rel_id, cur_dom)
                : resolve_rel_tgt_dom(snapshot, rel_id, cur_dom);
            ensure_seed_frontier(src_dom);
            max_dom_seen = std::max(max_dom_seen, tgt_dom);

            if (!walk->shader_steps.empty()) {
                // Pre-initialize dst_f as empty in tgt_dom so MORSEL_COMMIT.yield_frontier.accum can accumulate
                b.emit_u16(Opcode::OP_FSEED, subop::kFSEED_EMPTY, 0, dst_f, 0, 0, 0, tgt_dom, 0u);
                uint8_t morsel_sub = (walk->direction == WalkDirection::REVERSE_CSC)
                    ? subop::kWALK_MORSEL_REV : subop::kWALK_MORSEL_FWD;
                size_t hdr_pc = b.emit_u16(Opcode::OP_WALK_MORSEL, morsel_sub, 0, 0, src_f, 0, 0,
                                           static_cast<uint16_t>(rel_id), 0u);

                uint8_t next_mreg = 12u; // M4..M7
                uint8_t next_rreg = 1u;  // R1..R15
                uint8_t active_mask_f = 8u; // MF0 = active mask
                for (const auto& sstep : walk->shader_steps) {
                    if (sstep->kind() == NodeKind::STREAM_FILTER) {
                        auto sf = std::static_pointer_cast<ScmStreamFilter>(sstep);
                        uint8_t pred_m = emit_morsel_expr(sf->predicate, b, next_mreg, next_rreg, snapshot, tgt_dom, rel_id, false);
                        b.emit(Opcode::OP_SET_CONVERT, subop::kSET_CONVERT_B2F, 0, 9u /*MF1*/, pred_m, 0, 0, 0);
                        active_mask_f = 9u;
                    }
                }
                b.emit(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_YIELD_FRONTIER,
                       FLAG_ACCUM | FLAG_MASKED_S3, dst_f, 9u /*M1=tgt_id*/, 0, active_mask_f, 0);
                size_t end_pc = b.emit(Opcode::OP_MORSEL_END, subop::kMORSEL_END_END, 0, 0, 0, 0, 0, 0);
                b.patch_imm16_hi(hdr_pc, static_cast<uint16_t>(end_pc - hdr_pc));
            } else {
                uint8_t wsub = (walk->direction == WalkDirection::REVERSE_CSC)
                    ? subop::kWALK_REV : subop::kWALK_FWD;
                b.emit_u16(Opcode::OP_WALK, wsub, 0, dst_f, src_f, 0, 0, static_cast<uint16_t>(rel_id), 0u);
            }
            cur_f = dst_f;
            cur_dom = tgt_dom;
            active_is_vector = false;
            active_is_scalar = false;
        } else if (step->kind() == NodeKind::WALK_2HOP) {
            auto hop2 = std::static_pointer_cast<ScmWalk2Hop>(step);
            int rel1 = resolve_rel_id(snapshot, hop2->rel1_name, hop2->rel1_id);
            int rel2 = resolve_rel_id(snapshot, hop2->rel2_name, hop2->rel2_id);
            result.relation_id_map[hop2->rel1_name] = static_cast<uint32_t>(rel1);
            result.relation_id_map[hop2->rel2_name] = static_cast<uint32_t>(rel2);
            uint16_t d0 = resolve_rel_src_dom(snapshot, rel1, cur_dom);
            uint16_t d1 = resolve_rel_tgt_dom(snapshot, rel1, d0);
            uint16_t d2 = resolve_rel_tgt_dom(snapshot, rel2, d1);
            ensure_seed_frontier(d0);
            max_dom_seen = std::max({max_dom_seen, d1, d2});
            b.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, dst_f, src_f, 0, 0, static_cast<uint16_t>(rel1), 0u);
            b.emit_u16(Opcode::OP_WALK, subop::kWALK_FWD, 0, src_f, dst_f, 0, 0, static_cast<uint16_t>(rel2), 0u);
            cur_f = src_f;
            cur_dom = d2;
            active_is_vector = false;
            active_is_scalar = false;
        } else if (step->kind() == NodeKind::FIXPOINT) {
            auto fix = std::static_pointer_cast<ScmFixpoint>(step);
            if (fix->body_steps.empty() && !fix->relation_name.empty()) {
                int rel_id = resolve_rel_id(snapshot, fix->relation_name, fix->physical_rel_id);
                result.relation_id_map[fix->relation_name] = static_cast<uint32_t>(rel_id);
                uint16_t src_dom = resolve_rel_src_dom(snapshot, rel_id, cur_dom);
                ensure_seed_frontier(src_dom);
                uint8_t fsub = (fix->direction == WalkDirection::REVERSE_CSC)
                    ? subop::kWALK_REV_FIXPOINT_STAR : subop::kWALK_FWD_FIXPOINT_STAR;
                b.emit_u16(Opcode::OP_WALK, fsub, 0, dst_f, src_f, 0, 0, static_cast<uint16_t>(rel_id), 0u);
                cur_f = dst_f;
            } else if (fix->body_steps.size() == 1 && fix->body_steps[0]->kind() == NodeKind::WALK) {
                auto walk = std::static_pointer_cast<ScmWalk>(fix->body_steps[0]);
                int rel_id = resolve_rel_id(snapshot, walk->relation_name, walk->physical_rel_id);
                result.relation_id_map[walk->relation_name] = static_cast<uint32_t>(rel_id);
                uint16_t src_dom = resolve_rel_src_dom(snapshot, rel_id, cur_dom);
                ensure_seed_frontier(src_dom);
                uint8_t fsub = (walk->direction == WalkDirection::REVERSE_CSC)
                    ? subop::kWALK_REV_FIXPOINT_STAR : subop::kWALK_FWD_FIXPOINT_STAR;
                b.emit_u16(Opcode::OP_WALK, fsub, 0, dst_f, src_f, 0, 0, static_cast<uint16_t>(rel_id), 0u);
                cur_f = dst_f;
            } else if (!fix->body_steps.empty()) {
                ensure_seed_frontier(cur_dom);
                uint8_t prev_f = 2u;
                size_t loop_pc = b.pc();
                b.emit(Opcode::OP_MOV, subop::kMOV_F, 0, prev_f, cur_f, 0, 0, 0);
                for (const auto& sub : fix->body_steps) {
                    if (sub->kind() == NodeKind::WALK) {
                        auto w = std::static_pointer_cast<ScmWalk>(sub);
                        int rid = resolve_rel_id(snapshot, w->relation_name, w->physical_rel_id);
                        uint8_t wsub = (w->direction == WalkDirection::REVERSE_CSC) ? subop::kWALK_REV : subop::kWALK_FWD;
                        uint8_t nxt = static_cast<uint8_t>(1u - cur_f);
                        b.emit_u16(Opcode::OP_WALK, wsub, 0, nxt, cur_f, 0, 0, static_cast<uint16_t>(rid), 0u);
                        cur_f = nxt;
                    }
                }
                b.emit(Opcode::OP_SET_OP, subop::kSET_OP_UNION, 0, cur_f, cur_f, prev_f, 0, 0);
                b.emit(Opcode::OP_STABLE_CHECK, subop::kSTABLE_CHECK_FRONTIER_EQ, 0, 0, cur_f, prev_f, 0, 0);
                int32_t rel_off = static_cast<int32_t>(loop_pc) - static_cast<int32_t>(b.pc());
                b.emit(Opcode::OP_JCC, subop::kJCC_JNST, 0, 0, 0, 0, 0, static_cast<uint32_t>(rel_off));
            }
            active_is_vector = false;
            active_is_scalar = false;
        } else if (step->kind() == NodeKind::SET_OP) {
            auto sop = std::static_pointer_cast<ScmSetOp>(step);
            auto emit_branch = [&](const AstPtr& branch, uint8_t in_f, uint8_t out_f) -> uint16_t {
                if (!branch) return cur_dom;
                if (branch->kind() == NodeKind::WALK) {
                    auto w = std::static_pointer_cast<ScmWalk>(branch);
                    int rid = resolve_rel_id(snapshot, w->relation_name, w->physical_rel_id);
                    result.relation_id_map[w->relation_name] = static_cast<uint32_t>(rid);
                    uint16_t sdom = (w->direction == WalkDirection::REVERSE_CSC)
                        ? resolve_rel_tgt_dom(snapshot, rid, cur_dom)
                        : resolve_rel_src_dom(snapshot, rid, cur_dom);
                    uint16_t tdom = (w->direction == WalkDirection::REVERSE_CSC)
                        ? resolve_rel_src_dom(snapshot, rid, cur_dom)
                        : resolve_rel_tgt_dom(snapshot, rid, cur_dom);
                    ensure_seed_frontier(sdom);
                    uint8_t wsub = (w->direction == WalkDirection::REVERSE_CSC) ? subop::kWALK_REV : subop::kWALK_FWD;
                    b.emit_u16(Opcode::OP_WALK, wsub, 0, out_f, in_f, 0, 0, static_cast<uint16_t>(rid), 0u);
                    max_dom_seen = std::max(max_dom_seen, tdom);
                    return tdom;
                }
                if (branch->kind() == NodeKind::PROGRAM) {
                    auto sp = std::static_pointer_cast<ScmProgram>(branch);
                    uint8_t cur_b = in_f;
                    uint16_t bdom = cur_dom;
                    for (const auto& sub : sp->steps) {
                        if (sub->kind() == NodeKind::WALK) {
                            auto w = std::static_pointer_cast<ScmWalk>(sub);
                            int rid = resolve_rel_id(snapshot, w->relation_name, w->physical_rel_id);
                            uint16_t sdom = (w->direction == WalkDirection::REVERSE_CSC)
                                ? resolve_rel_tgt_dom(snapshot, rid, bdom)
                                : resolve_rel_src_dom(snapshot, rid, bdom);
                            bdom = (w->direction == WalkDirection::REVERSE_CSC)
                                ? resolve_rel_src_dom(snapshot, rid, bdom)
                                : resolve_rel_tgt_dom(snapshot, rid, bdom);
                            ensure_seed_frontier(sdom);
                            uint8_t wsub = (w->direction == WalkDirection::REVERSE_CSC) ? subop::kWALK_REV : subop::kWALK_FWD;
                            uint8_t nxt = (cur_b == out_f) ? static_cast<uint8_t>(out_f + 4u) : out_f;
                            b.emit_u16(Opcode::OP_WALK, wsub, 0, nxt, cur_b, 0, 0, static_cast<uint16_t>(rid), 0u);
                            cur_b = nxt;
                        }
                    }
                    if (cur_b != out_f) {
                        b.emit(Opcode::OP_MOV, subop::kMOV_F, 0, out_f, cur_b, 0, 0, 0);
                    }
                    return bdom;
                }
                return cur_dom;
            };
            uint8_t ssub = subop::kSET_OP_UNION;
            if (sop->op == ScmSetOp::INTERSECT) ssub = subop::kSET_OP_INTERSECT;
            else if (sop->op == ScmSetOp::DIFFERENCE) ssub = subop::kSET_OP_DIFF;

            if (sop->lhs) {
                uint16_t out_dom = emit_branch(sop->lhs, src_f, 2u);
                emit_branch(sop->rhs, src_f, 3u);
                b.emit(Opcode::OP_SET_OP, ssub, 0, dst_f, 2u, 3u, 0, 0);
                cur_dom = out_dom;
            } else {
                ensure_seed_frontier(cur_dom);
                emit_branch(sop->rhs, src_f, 2u);
                b.emit(Opcode::OP_SET_OP, ssub, 0, dst_f, src_f, 2u, 0, 0);
            }
            cur_f = dst_f;
            active_is_vector = false;
            active_is_scalar = false;
            active_is_multilane = false;
        } else if (step->kind() == NodeKind::REDUCE) {
            auto red = std::static_pointer_cast<ScmReduce>(step);
            if (!active_is_vector) {
                ensure_seed_frontier(cur_dom);
                b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, cur_r, cur_f, 0, 0, 0);
            } else {
                uint8_t rsub = subop::kVEC_REDUCE_SUM;
                switch (red->op_val) {
                    case ScmReduce::Op::SUM:    rsub = subop::kVEC_REDUCE_SUM; break;
                    case ScmReduce::Op::COUNT:  rsub = subop::kVEC_REDUCE_COUNT_VALID; break;
                    case ScmReduce::Op::MIN:    rsub = subop::kVEC_REDUCE_MIN; break;
                    case ScmReduce::Op::MAX:    rsub = subop::kVEC_REDUCE_MAX; break;
                    case ScmReduce::Op::ARGMIN: rsub = subop::kVEC_REDUCE_ARGMIN; break;
                    case ScmReduce::Op::ARGMAX: rsub = subop::kVEC_REDUCE_ARGMAX; break;
                    default:                    rsub = subop::kVEC_REDUCE_SUM; break;
                }
                b.emit(Opcode::OP_VEC_REDUCE, rsub, 0, cur_r, cur_v, 0, 0, 0);
            }
            active_is_scalar = true;
            active_is_vector = false;
        } else if (step->kind() == NodeKind::LANE_INIT_SEEDS) {
            auto n = std::static_pointer_cast<ScmLaneInitSeeds>(step);
            frontier_initialized = true;
            // Load seed list from param 0 as V0
            b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_VEC, 0, 0, 0, 0, 0, 0u, 0xFFFFu);
            uint8_t lsub = (n->width_flag == 64) ? subop::kLANE_INIT_SEEDS_FORCE_W64 : subop::kLANE_INIT_SEEDS_AUTO_WIDTH;
            b.emit_u16(Opcode::OP_LANE_INIT_SEEDS, lsub, 0, dst_f, 0, 0, 0, cur_dom, 0u);
            cur_f = dst_f;
            active_is_vector = false;
            active_is_scalar = false;
            active_is_multilane = true;
        } else if (step->kind() == NodeKind::LANE_WALK) {
            auto n = std::static_pointer_cast<ScmLaneWalk>(step);
            int rel_id = resolve_rel_id(snapshot, n->relation_name, n->physical_rel_id);
            result.relation_id_map[n->relation_name] = static_cast<uint32_t>(rel_id);
            uint8_t lwsub = (n->direction == WalkDirection::REVERSE_CSC)
                ? (n->fixed_point ? subop::kLANE_WALK_REV_FIXPOINT_STAR : subop::kLANE_WALK_REV)
                : (n->fixed_point ? subop::kLANE_WALK_FWD_FIXPOINT_STAR : subop::kLANE_WALK_FWD);
            b.emit_u16(Opcode::OP_LANE_WALK, lwsub, 0, dst_f, src_f, 0, 0, static_cast<uint16_t>(rel_id), 0u);
            cur_f = dst_f;
            active_is_multilane = true;
        } else if (step->kind() == NodeKind::LANE_FILTER_QUORUM) {
            auto n = std::static_pointer_cast<ScmLaneFilterQuorum>(step);
            b.emit_u16(Opcode::OP_LANE_FILTER_QUORUM, subop::kLANE_FILTER_QUORUM_AT_LEAST, 0,
                       dst_f, src_f, 0, 0, static_cast<uint16_t>(n->min_quorum), 0u);
            cur_f = dst_f;
            active_is_multilane = false;
        } else if (step->kind() == NodeKind::LANE_REDUCE_AND) {
            b.emit(Opcode::OP_LANE_REDUCE_AND, subop::kLANE_REDUCE_AND_REDUCE_AND, 0, dst_f, src_f, 0, 0, 0);
            cur_f = dst_f;
            active_is_multilane = false;
        } else if (step->kind() == NodeKind::LANE_REDUCE_ATTR) {
            auto n = std::static_pointer_cast<ScmLaneReduceAttr>(step);
            uint8_t rsub = static_cast<uint8_t>(n->reduce_op & 0x03u);
            b.emit_u16(Opcode::OP_LOAD_COLUMN, subop::kLOAD_COLUMN_NODE_ATTR, 0, 1u, 0, 0, 0,
                       cur_dom, static_cast<uint16_t>(n->attr_id));
            b.emit(Opcode::OP_LANE_REDUCE_ATTR, rsub, 0, cur_v, src_f, 1u, 0, 0);
            active_is_vector = true;
            active_is_scalar = false;
            active_is_multilane = false;
        } else if (step->kind() == NodeKind::COLLECT) {
            auto collect = std::static_pointer_cast<ScmCollect>(step);
            if (active_is_multilane) {
                b.emit_u16(Opcode::OP_LANE_FILTER_QUORUM, subop::kLANE_FILTER_QUORUM_AT_LEAST, 0,
                           dst_f, cur_f, 0, 0, 1u, 0u);
                cur_f = dst_f;
                active_is_multilane = false;
            }
            if (active_is_scalar) {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, cur_r, 0, 0, 0u, 0u);
            } else if (active_is_vector) {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_VEC, 0, 0, cur_v, 0, 0, 0u, 0u);
            } else {
                ensure_seed_frontier(cur_dom);
                if (collect->output_type == ScmCollect::Mode::COUNT ||
                    collect->output_type == ScmCollect::Mode::SCALAR) {
                    b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, cur_r, cur_f, 0, 0, 0);
                    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, cur_r, 0, 0, 0u, 0u);
                } else if (collect->output_type == ScmCollect::Mode::VECTOR ||
                           collect->output_type == ScmCollect::Mode::LIST ||
                           collect->output_type == ScmCollect::Mode::ARRAY) {
                    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_IDS, 0, 0, cur_f, 0, 0, 0u, 0u);
                } else {
                    b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_BITSET, 0, 0, cur_f, 0, 0, 0u, 0u);
                }
            }
        } else if (step->kind() == NodeKind::LIST) {
            auto list = std::static_pointer_cast<ScmList>(step);
            if (!list->elements.empty() && list->elements[0]->kind() == NodeKind::SYMBOL) {
                const std::string& op_name = std::static_pointer_cast<ScmSymbol>(list->elements[0])->name;
                if (op_name == "domain-all") {
                    b.emit_u16(Opcode::OP_INIT_DOMAIN_SPAN, subop::kINIT_DOMAIN_SPAN_ALL_SPAN, 0,
                               dst_f, 0, 0, 0, cur_dom, 0u);
                    cur_f = dst_f;
                    frontier_initialized = true;
                } else if (op_name == "repeat" && list->elements.size() >= 3) {
                    int repeat_count = 1;
                    if (list->elements[1]->kind() == NodeKind::LITERAL) {
                        repeat_count = static_cast<int>(std::static_pointer_cast<ScmLiteral>(list->elements[1])->int_val);
                    }
                    ensure_seed_frontier(cur_dom);
                    uint8_t cnt_r = 2u;
                    b.emit_load_const_i64(cnt_r, repeat_count);
                    size_t loop_pc = b.pc();
                    if (list->elements[2]->kind() == NodeKind::PROGRAM) {
                        auto sub_prog = std::static_pointer_cast<ScmProgram>(list->elements[2]);
                        for (const auto& sub : sub_prog->steps) {
                            if (sub->kind() == NodeKind::WALK) {
                                auto w = std::static_pointer_cast<ScmWalk>(sub);
                                int rid = resolve_rel_id(snapshot, w->relation_name, w->physical_rel_id);
                                uint8_t wsub = (w->direction == WalkDirection::REVERSE_CSC) ? subop::kWALK_REV : subop::kWALK_FWD;
                                uint8_t nxt = static_cast<uint8_t>(1u - cur_f);
                                b.emit_u16(Opcode::OP_WALK, wsub, 0, nxt, cur_f, 0, 0, static_cast<uint16_t>(rid), 0u);
                                cur_f = nxt;
                            }
                        }
                    }
                    if (cur_f != src_f) {
                        b.emit(Opcode::OP_MOV, subop::kMOV_F, 0, src_f, cur_f, 0, 0, 0);
                        cur_f = src_f;
                    }
                    int32_t rel_off = static_cast<int32_t>(loop_pc) - static_cast<int32_t>(b.pc());
                    b.emit(Opcode::OP_LOOP_DECR, subop::kLOOP_DECR_DECR, 0, cnt_r, 0, 0, 0, static_cast<uint32_t>(rel_off));
                }
            }
        }
    }

    b.emit(Opcode::OP_HALT, subop::kHALT_OK, 0, 0, 0, 0, 0, 0);

    uint16_t req_doms = snapshot ? static_cast<uint16_t>(snapshot->domains.size()) : static_cast<uint16_t>(max_dom_seen + 1u);
    result.code_words = b.words();
    result.final_domain_id = cur_dom;
    result.required_domains = req_doms;
    result.impb_bytes = b.build_impb(0u, 0u, req_doms);

    // Static verification post-condition: emitted .impb MUST pass impulse_vm_validate
    impulse_vm_validation_t vr = impulse_vm_validate(result.impb_bytes.data(), result.impb_bytes.size());
    if (vr.status != IMPULSE_VM_OK) {
        throw std::runtime_error("ImpulseBytecodeEmitter produced invalid v0.11 bytecode at pc=" +
                                 std::to_string(vr.bad_pc) + " status=" + std::to_string(vr.status));
    }

    return result;
}

std::string ImpulseBytecodeEmitter::disassemble(const std::vector<uint64_t>& words) {
    std::ostringstream ss;
    ss << ".version 0.11.0\n.instructions " << words.size() << "\n\n";
    for (size_t pc = 0; pc < words.size(); ++pc) {
        uint64_t w = words[pc];
        uint8_t op = IMP_OPCODE(w);
        const auto& meta = kOpcodeMeta[op];
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "  0x%04x:  %-20s sub=%u flg=0x%x dst=%u s1=%u s2=%u s3=%u imm=0x%08x\n",
                      static_cast<unsigned>(pc),
                      meta.defined ? meta.mnemonic.data() : "UNKNOWN",
                      static_cast<unsigned>(IMP_SUBOP(w)),
                      static_cast<unsigned>(IMP_FLAGS(w)),
                      static_cast<unsigned>(IMP_DST(w)),
                      static_cast<unsigned>(IMP_S1(w)),
                      static_cast<unsigned>(IMP_S2(w)),
                      static_cast<unsigned>(IMP_S3(w)),
                      static_cast<unsigned>(IMP_IMM32(w)));
        ss << buf;
    }
    return ss.str();
}

} // namespace impulse::compiler
