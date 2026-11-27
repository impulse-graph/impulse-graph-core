/**
 * @file impulse_statement_c_api.cpp
 * @brief C-ABI Query Compiler entrypoints and SQLite-style Prepared Statement API (impulse_stmt_t)
 *        backed 100% by ImpulseVM v0.11.0 2-buffer execution.
 */

#include "impulse_vm_v11.h"
#include "vm/impulse_bytecode_emitter.hpp"
#include "impulse_graph_internal.hpp"
#include "impulse_cel.h"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include "impulse_impk.hpp"
#include "impulse_impscm.hpp"
#include "impulse_sexpr.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace impulse::compiler;

thread_local std::string g_stmt_last_error;

static std::shared_ptr<ScmProgram> parse_script_to_ast(const char* script, impulse_language_t lang) {
    if (!script || script[0] == '\0') {
        throw std::invalid_argument("Empty script provided to compiler");
    }
    std::string script_str(script);
    switch (lang) {
        case IMPULSE_LANG_CYPHER: {
            auto res = CypherCompiler::compile(script_str);
            return res.ast;
        }
        case IMPULSE_LANG_IMPLOG: {
            return impulse::datalog::DatalogParser::parse(script_str);
        }
        case IMPULSE_LANG_IMPK: {
            auto stmts = impulse::impk::ImpKCompiler::parse(script_str);
            std::string ir = impulse::impk::ImpKCompiler::to_impscheme(stmts);
            return impulse::impscm::ImpScmAstBuilder::parse(ir);
        }
        case IMPULSE_LANG_IMPSCM:
        default: {
            return impulse::impscm::ImpScmAstBuilder::parse(script_str);
        }
    }
}

static impulse_language_t detect_language(const std::string& q) {
    if (q.find("MATCH") != std::string::npos || q.find("match") != std::string::npos) {
        return IMPULSE_LANG_CYPHER;
    }
    if (q.find(":-") != std::string::npos) {
        return IMPULSE_LANG_IMPLOG;
    }
    if (!q.empty() && q.front() == '(') {
        return IMPULSE_LANG_IMPSCM;
    }
    if (q.find("PageRank") != std::string::npos || q.find("mxv") != std::string::npos) {
        return IMPULSE_LANG_IMPK;
    }
    return IMPULSE_LANG_CYPHER;
}

} // anonymous namespace

struct impulse_stmt {
    const impulse_snapshot_t* snapshot{nullptr};
    std::string query_text;
    impulse_language_t lang{IMPULSE_LANG_CYPHER};
    impulse::compiler::v11::ImpulseCompiledProgram v11_program;
    impulse_vm_memory_plan_t v11_plan{};

    size_t words_per_bitset{0};
    size_t max_nodes{0};
    size_t scratch_bytes_needed{0};
    size_t return_bytes_needed{0};
    size_t total_buffer_needed{0};

    uint64_t bound_seed_node{0};
    bool has_seed_node{false};
    std::unordered_map<std::string, uint64_t> node_params;
    std::unordered_map<std::string, std::vector<uint64_t>> nodes_params;
    std::unordered_map<std::string, std::vector<uint64_t>> bitset_params;
    std::unordered_map<std::string, std::vector<uint8_t>> roaring_params;
    std::unordered_map<std::string, int64_t> int_params;
    std::unordered_map<std::string, uint64_t> uint_params;
    std::unordered_map<std::string, double> float_params;
    std::unordered_map<std::string, std::string> str_params;
    std::unordered_map<std::string, std::array<uint8_t, 16>> uuid_params;
    std::unordered_map<std::string, std::vector<float>> vector_params;

    std::vector<impulse_column_desc_t> columns;
    size_t row_count{0};
    size_t total_bytes_written{0};
};

extern "C" {

impulse_status_t impulse_compile_to_impas(
    const impulse_snapshot_t* snapshot,
    const char* script,
    impulse_language_t lang,
    char* out_impas_buffer,
    size_t out_capacity,
    size_t* out_bytes_written)
{
    if (!script || !out_impas_buffer || out_capacity == 0) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    try {
        auto ast = parse_script_to_ast(script, lang);
        auto compiled = impulse::compiler::v11::ImpulseBytecodeEmitter::emit(ast, snapshot, 0);
        std::string impas = impulse::compiler::v11::ImpulseBytecodeEmitter::disassemble(compiled.code_words);
        if (impas.size() + 1 > out_capacity) {
            return IMPULSE_ERR_BUFFER_OVERFLOW;
        }
        std::memcpy(out_impas_buffer, impas.c_str(), impas.size() + 1);
        if (out_bytes_written) {
            *out_bytes_written = impas.size() + 1;
        }
        return IMPULSE_OK;
    } catch (const std::exception& e) {
        g_stmt_last_error = e.what() ? e.what() : "impulse_compile_to_impas error";
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    } catch (...) {
        g_stmt_last_error = "impulse_compile_to_impas unknown exception";
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    }
}

impulse_status_t impulse_stmt_prepare(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    impulse_stmt_t** out_stmt)
{
    if (!query_text || !out_stmt) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
    try {
        auto stmt = std::make_unique<impulse_stmt>();
        stmt->snapshot = snapshot;
        stmt->query_text = query_text;
        stmt->lang = detect_language(stmt->query_text);

        auto ast = parse_script_to_ast(query_text, stmt->lang);
        stmt->v11_program = impulse::compiler::v11::ImpulseBytecodeEmitter::emit(ast, snapshot, 0);
        if (stmt->v11_program.impb_bytes.empty()) {
            return IMPULSE_ERR_INVALID_ARGUMENT;
        }

        stmt->max_nodes = snapshot ? impulse_snapshot_max_node_count(snapshot) : 1024;
        if (stmt->max_nodes == 0) stmt->max_nodes = 1024;
        stmt->words_per_bitset = (stmt->max_nodes + 63) / 64;
        size_t min_ret_bytes = std::max<size_t>(
            stmt->words_per_bitset * sizeof(uint64_t),
            stmt->max_nodes * sizeof(uint64_t)
        );

        if (snapshot != nullptr) {
            (void)impulse_vm_v11_prepare_snapshot(snapshot);
            if (impulse_vm_plan_memory(
                    stmt->v11_program.impb_bytes.data(),
                    stmt->v11_program.impb_bytes.size(),
                    snapshot,
                    1,
                    &stmt->v11_plan) != IMPULSE_VM_V11_OK) {
                return IMPULSE_ERR_INVALID_ARGUMENT;
            }
            size_t planned_scratch = std::max<size_t>(stmt->v11_plan.guaranteed_scratch_bytes, 4096);
            stmt->scratch_bytes_needed = ((planned_scratch + 127) & ~127ULL) + 128;
            size_t planned_ret = std::max<size_t>(stmt->v11_plan.max_result_bytes, min_ret_bytes);
            stmt->return_bytes_needed = (planned_ret + 127) & ~127ULL;
        } else {
            stmt->scratch_bytes_needed = 8192;
            stmt->return_bytes_needed = (min_ret_bytes + 127) & ~127ULL;
        }
        stmt->total_buffer_needed = stmt->scratch_bytes_needed + stmt->return_bytes_needed + 1024;
        stmt->columns.reserve(8);

        *out_stmt = stmt.release();
        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
}

size_t impulse_stmt_buffer_size(const impulse_stmt_t* stmt) {
    return stmt ? stmt->total_buffer_needed : 0;
}

void impulse_stmt_finalize(impulse_stmt_t* stmt) {
    delete stmt;
}

impulse_status_t impulse_stmt_bind_node(impulse_stmt_t* stmt, const char* param, uint64_t node_id) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->bound_seed_node = node_id;
    stmt->has_seed_node = true;
    stmt->node_params[param] = node_id;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_nodes(impulse_stmt_t* stmt, const char* param, const uint64_t* node_ids, size_t count) {
    if (!stmt || !param || (!node_ids && count > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->nodes_params[param] = std::vector<uint64_t>(node_ids, node_ids + count);
    if (count > 0 && !stmt->has_seed_node) {
        stmt->bound_seed_node = node_ids[0];
        stmt->has_seed_node = true;
    }
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_bitset(impulse_stmt_t* stmt, const char* param, const uint64_t* words, size_t word_count) {
    if (!stmt || !param || (!words && word_count > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->bitset_params[param] = std::vector<uint64_t>(words, words + word_count);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_roaring(impulse_stmt_t* stmt, const char* param, const uint8_t* bytes, size_t len) {
    if (!stmt || !param || (!bytes && len > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->roaring_params[param] = std::vector<uint8_t>(bytes, bytes + len);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_int(impulse_stmt_t* stmt, const char* param, int64_t val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->int_params[param] = val;
    if (!stmt->has_seed_node && val >= 0) {
        stmt->bound_seed_node = static_cast<uint64_t>(val);
        stmt->has_seed_node = true;
    }
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_uint(impulse_stmt_t* stmt, const char* param, uint64_t val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->uint_params[param] = val;
    if (!stmt->has_seed_node) {
        stmt->bound_seed_node = val;
        stmt->has_seed_node = true;
    }
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_float(impulse_stmt_t* stmt, const char* param, double val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->float_params[param] = val;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_str(impulse_stmt_t* stmt, const char* param, const char* str) {
    if (!stmt || !param || !str) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->str_params[param] = str;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_uuid(impulse_stmt_t* stmt, const char* param, const uint8_t uuid_bytes[16]) {
    if (!stmt || !param || !uuid_bytes) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::array<uint8_t, 16> arr{};
    std::memcpy(arr.data(), uuid_bytes, 16);
    stmt->uuid_params[param] = arr;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_vector(impulse_stmt_t* stmt, const char* param, const float* data, size_t dim) {
    if (!stmt || !param || (!data && dim > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->vector_params[param] = std::vector<float>(data, data + dim);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_execute(impulse_stmt_t* stmt, void* buffer, size_t buffer_size) {
    if (!stmt || !buffer || buffer_size < stmt->total_buffer_needed || !stmt->snapshot) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    uint8_t* raw_buf = static_cast<uint8_t*>(buffer);
    size_t scratch_size = stmt->scratch_bytes_needed;
    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_buf);
    uintptr_t aligned_addr = (raw_addr + 127u) & ~static_cast<uintptr_t>(127u);
    size_t pad = static_cast<size_t>(aligned_addr - raw_addr);
    if (pad >= scratch_size) {
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    uintptr_t ret_raw = raw_addr + scratch_size;
    uintptr_t ret_aligned = (ret_raw + 63u) & ~static_cast<uintptr_t>(63u);
    uint8_t* return_area = reinterpret_cast<uint8_t*>(ret_aligned);
    uintptr_t buf_end = raw_addr + buffer_size;
    size_t avail_return_bytes = (buf_end > ret_aligned) ? static_cast<size_t>(buf_end - ret_aligned) : 0;

    impulse_vm_workspace_v11_t* ws = nullptr;
    if (impulse_workspace_init(reinterpret_cast<void*>(aligned_addr), scratch_size - pad, &ws) != IMPULSE_VM_V11_OK) {
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    std::vector<impulse_vm_param_binding_v11_t> bindings;
    uint64_t single_seed = stmt->has_seed_node ? stmt->bound_seed_node : 0ULL;
    {
        impulse_vm_param_binding_v11_t seed_b{};
        seed_b.param_id = 0;
        seed_b.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
        seed_b.domain_id = stmt->v11_program.start_domain_id;
        if (!stmt->nodes_params.empty()) {
            const auto& vec = stmt->nodes_params.begin()->second;
            if (vec.size() > UINT32_MAX) {
                return IMPULSE_ERR_BUFFER_OVERFLOW;
            }
            seed_b.count = static_cast<uint32_t>(vec.size());
            seed_b.data_ptr = vec.data();
        } else {
            seed_b.count = 1;
            seed_b.data_ptr = &single_seed;
        }
        bindings.push_back(seed_b);
    }
    uint32_t next_pid = 1;
    for (const auto& kv : stmt->int_params) {
        impulse_vm_param_binding_v11_t pb{};
        pb.param_id = next_pid++;
        pb.kind = IMPULSE_VM_V11_PARAM_SCALAR_I64;
        pb.scalar_bits = static_cast<uint64_t>(kv.second);
        bindings.push_back(pb);
    }
    for (const auto& kv : stmt->uint_params) {
        impulse_vm_param_binding_v11_t pb{};
        pb.param_id = next_pid++;
        pb.kind = IMPULSE_VM_V11_PARAM_SCALAR_U64;
        pb.scalar_bits = kv.second;
        bindings.push_back(pb);
    }
    for (const auto& kv : stmt->float_params) {
        impulse_vm_param_binding_v11_t pb{};
        pb.param_id = next_pid++;
        pb.kind = IMPULSE_VM_V11_PARAM_SCALAR_F64;
        uint64_t fbits = 0;
        std::memcpy(&fbits, &kv.second, sizeof(double));
        pb.scalar_bits = fbits;
        bindings.push_back(pb);
    }
    for (const auto& kv : stmt->str_params) {
        impulse_vm_param_binding_v11_t pb{};
        pb.param_id = next_pid++;
        pb.kind = IMPULSE_VM_V11_PARAM_SCALAR_STR;
        pb.data_ptr = kv.second.c_str();
        pb.count = static_cast<uint32_t>(kv.second.size());
        bindings.push_back(pb);
    }

    impulse_vm_params_v11_t v11_params{};
    v11_params.gas_limit = UINT64_MAX;
    v11_params.bindings = bindings.data();
    v11_params.binding_count = static_cast<uint32_t>(bindings.size());

    size_t out_res_bytes = 0;
    impulse_vm_status_v11_t v11_rc = impulse_vm_execute_v11(
        stmt->v11_program.impb_bytes.data(),
        stmt->v11_program.impb_bytes.size(),
        stmt->snapshot,
        &v11_params,
        ws,
        return_area,
        avail_return_bytes,
        &out_res_bytes
    );
    if (v11_rc != IMPULSE_VM_V11_OK) {
        switch (v11_rc) {
            case IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW: return IMPULSE_ERR_BUFFER_OVERFLOW;
            case IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS:
            case IMPULSE_VM_V11_ERR_INVALID_OPERAND:
            case IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH:
            case IMPULSE_VM_V11_ERR_TYPE_MISMATCH:
            case IMPULSE_VM_V11_ERR_VALIDATION_FAILED:
            case IMPULSE_VM_V11_ERR_NULL_SNAPSHOT: return IMPULSE_ERR_INVALID_ARGUMENT;
            default: return IMPULSE_ERR_IO_FAILURE;
        }
    }

    stmt->columns.clear();
    impulse_column_desc_t col{};
    std::strncpy(col.name, "result", sizeof(col.name) - 1);
    col.is_nullable = false;
    col.byte_offset_in_buf = scratch_size;
    col.null_bitmap = nullptr;

    impulse_vm_output_view_v11_t ov{};
    if (impulse_vm_v11_get_output_count(ws) > 0 && impulse_vm_v11_get_output(ws, 0, &ov) == 1) {
        if (ov.elem_tag == 6 /* BOOL_3VL */) {
            size_t n_words = (ov.length + 63u) / 64u;
            if (n_words == 0) n_words = 1;
            size_t cap_u64 = avail_return_bytes / sizeof(uint64_t);
            size_t n_copy = std::min(n_words, cap_u64);
            uint64_t* out_words = reinterpret_cast<uint64_t*>(return_area);
            if (ov.bool_data_bits && n_copy > 0) {
                std::memcpy(out_words, ov.bool_data_bits, n_copy * sizeof(uint64_t));
            } else if (n_copy > 0) {
                std::memset(out_words, 0, n_copy * sizeof(uint64_t));
            }
            col.type_code = 0x08;
            col.element_size = sizeof(uint64_t);
            col.dimension = 1;
            col.data_ptr = out_words;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(uint64_t);
        } else if (ov.elem_tag == 4 /* F64 */) {
            size_t cap_f64 = avail_return_bytes / sizeof(double);
            size_t n_copy = std::min<size_t>(ov.length, cap_f64);
            double* out_f64 = reinterpret_cast<double*>(return_area);
            if (ov.f64_data && n_copy > 0) {
                std::memcpy(out_f64, ov.f64_data, n_copy * sizeof(double));
            }
            col.type_code = 0x0A;
            col.element_size = sizeof(double);
            col.dimension = 1;
            col.data_ptr = out_f64;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(double);
        } else {
            size_t cap_u64 = avail_return_bytes / sizeof(uint64_t);
            size_t n_copy = std::min<size_t>(ov.length, cap_u64);
            int64_t* out_i64 = reinterpret_cast<int64_t*>(return_area);
            if (ov.i64_data && n_copy > 0) {
                std::memcpy(out_i64, ov.i64_data, n_copy * sizeof(int64_t));
            }
            col.type_code = 0x04;
            col.element_size = sizeof(int64_t);
            col.dimension = 1;
            col.data_ptr = out_i64;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(int64_t);
        }
    } else {
        uint64_t* out_words = reinterpret_cast<uint64_t*>(return_area);
        if (avail_return_bytes >= sizeof(uint64_t)) out_words[0] = 0ULL;
        col.type_code = 0x08;
        col.element_size = sizeof(uint64_t);
        col.dimension = 1;
        col.data_ptr = out_words;
        stmt->columns.push_back(col);
        stmt->row_count = (avail_return_bytes >= sizeof(uint64_t)) ? 1 : 0;
        stmt->total_bytes_written = stmt->row_count * sizeof(uint64_t);
    }

    return IMPULSE_OK;
}

size_t impulse_stmt_row_count(const impulse_stmt_t* stmt) {
    return stmt ? stmt->row_count : 0;
}

uint32_t impulse_stmt_column_count(const impulse_stmt_t* stmt) {
    return stmt ? static_cast<uint32_t>(stmt->columns.size()) : 0;
}

const char* impulse_stmt_column_name(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return "";
    return stmt->columns[col_idx].name;
}

uint8_t impulse_stmt_column_type(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return 0;
    return stmt->columns[col_idx].type_code;
}

uint32_t impulse_stmt_column_dim(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return 1;
    return stmt->columns[col_idx].dimension;
}

const void* impulse_stmt_column_data(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return nullptr;
    return stmt->columns[col_idx].data_ptr;
}

bool impulse_stmt_column_is_null(const impulse_stmt_t* stmt, uint32_t col_idx, size_t row_idx) {
    if (!stmt || col_idx >= stmt->columns.size() || row_idx >= stmt->row_count) return true;
    const auto& col = stmt->columns[col_idx];
    if (!col.is_nullable || !col.null_bitmap) return false;
    return (col.null_bitmap[row_idx >> 6] & (1ULL << (row_idx & 63))) == 0;
}

impulse_status_t impulse_exec(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    uint64_t seed_node,
    impulse_execution_result_t* out_result)
{
    if (!snapshot || !query_text || !out_result) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    impulse_stmt_t* raw_stmt = nullptr;
    impulse_status_t rc = impulse_stmt_prepare(snapshot, query_text, &raw_stmt);
    if (rc != IMPULSE_OK) {
        return rc;
    }
    std::unique_ptr<impulse_stmt_t, decltype(&impulse_stmt_finalize)> stmt(raw_stmt, &impulse_stmt_finalize);

    rc = impulse_stmt_bind_node(stmt.get(), "$seed", seed_node);
    if (rc != IMPULSE_OK) {
        return rc;
    }

    static thread_local std::vector<uint8_t> tls_exec_buf;
    size_t buf_size = impulse_stmt_buffer_size(stmt.get());
    if (tls_exec_buf.size() < buf_size) {
        tls_exec_buf.resize(buf_size);
    }

    rc = impulse_stmt_execute(stmt.get(), tls_exec_buf.data(), tls_exec_buf.size());
    if (rc == IMPULSE_OK) {
        out_result->status = IMPULSE_VM_OK;
        out_result->row_count = impulse_stmt_row_count(stmt.get());
        out_result->column_count = impulse_stmt_column_count(stmt.get());
        out_result->total_bytes_written = stmt->total_bytes_written;
        out_result->data_ptr = impulse_stmt_column_data(stmt.get(), 0);
        out_result->scalar_value = seed_node;
    } else {
        out_result->data_ptr = nullptr;
        out_result->row_count = 0;
        out_result->column_count = 0;
        out_result->total_bytes_written = 0;
    }
    return rc;
}

} // extern "C"
