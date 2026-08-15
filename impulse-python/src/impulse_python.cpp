#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include "impulse_graph.h"
#include "impulse_vm.h"
#include "impulse_vm_fluent.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>

namespace py = pybind11;

class PyImpulseSnapshot {
public:
    PyImpulseSnapshot(const std::string& path) {
        impulse_status_t status = IMPULSE_OK;
        snapshot_ = impulse_snapshot_open(path.c_str(), &status);
        if (!snapshot_ || status != IMPULSE_OK) {
            throw std::runtime_error("Failed to open snapshot '" + path + "': " + std::string(impulse_get_last_error()));
        }
    }

    ~PyImpulseSnapshot() {
        close();
    }

    void close() {
        if (snapshot_) {
            impulse_snapshot_close(snapshot_);
            snapshot_ = nullptr;
        }
    }

    const impulse_snapshot_t* raw_handle() const { return snapshot_; }

    bool is_reachable(uint16_t relation_index, uint64_t src_id, uint64_t tgt_id) const {
        if (!snapshot_) return false;
        return impulse_snapshot_is_reachable(snapshot_, relation_index, src_id, tgt_id);
    }

    uint16_t domain_count() const {
        return snapshot_ ? impulse_snapshot_domain_count(snapshot_) : 0;
    }

    py::dict get_domain(uint16_t index) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        impulse_domain_catalog_entry_t entry;
        const char* name = nullptr;
        impulse_status_t status = impulse_snapshot_get_domain_entry(snapshot_, index, &entry, &name);
        if (status != IMPULSE_OK) {
            throw std::out_of_range("Invalid domain index: " + std::to_string(index));
        }

        py::dict res;
        res["domain_id"] = entry.domain_id;
        res["key_type"] = entry.key_type;
        res["name"] = name ? std::string(name) : "";
        return res;
    }

    uint16_t relation_count() const {
        return snapshot_ ? impulse_snapshot_relation_count(snapshot_) : 0;
    }

    py::dict get_relation(uint16_t index) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        impulse_relation_directory_entry_t entry;
        impulse_status_t status = impulse_snapshot_get_relation_entry(snapshot_, index, &entry);
        if (status != IMPULSE_OK) {
            throw std::out_of_range("Invalid relation index: " + std::to_string(index));
        }

        py::dict res;
        res["src_domain_id"] = entry.src_domain_id;
        res["tgt_domain_id"] = entry.tgt_domain_id;
        res["encoding_type"] = entry.encoding_id;
        res["node_count"] = entry.node_count;
        res["edge_count"] = entry.edge_count;
        res["section_features"] = entry.section_features;
        res["csr_row_off_offset"] = entry.csr_row_off_offset;
        res["csr_row_off_bytes"] = entry.csr_row_off_bytes;
        res["csr_col_idx_offset"] = entry.csr_col_idx_offset;
        res["csr_col_idx_bytes"] = entry.csr_col_idx_bytes;
        return res;
    }

    py::memoryview get_buffer(uint64_t offset, uint64_t size) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        const void* ptr = impulse_snapshot_get_buffer(snapshot_, offset, size);
        if (!ptr) throw std::runtime_error("Invalid buffer offset or size");
        return py::memoryview::from_memory(const_cast<void*>(ptr), size, true);
    }

    py::memoryview get_csr_row_offsets(uint16_t index) const {
        py::dict rel = get_relation(index);
        uint64_t offset = rel["csr_row_off_offset"].cast<uint64_t>();
        uint64_t bytes = rel["csr_row_off_bytes"].cast<uint64_t>();
        return get_buffer(offset, bytes);
    }

    py::memoryview get_csr_col_indices(uint16_t index) const {
        py::dict rel = get_relation(index);
        uint64_t offset = rel["csr_col_idx_offset"].cast<uint64_t>();
        uint64_t bytes = rel["csr_col_idx_bytes"].cast<uint64_t>();
        return get_buffer(offset, bytes);
    }

    py::tuple sample_neighbors(uint16_t relation_index, const std::vector<uint64_t>& nodes, int k_samples, uint64_t seed) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        size_t out_count = 0;
        impulse_status_t status = impulse_snapshot_sample_neighbors(
            snapshot_, relation_index, nodes.data(), nodes.size(), k_samples, seed, nullptr, nullptr, 0, &out_count
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to dry-run sample_neighbors: " + std::string(impulse_get_last_error()));
        }

        py::array_t<uint64_t> src_arr(out_count);
        py::array_t<uint64_t> tgt_arr(out_count);

        if (out_count > 0) {
            status = impulse_snapshot_sample_neighbors(
                snapshot_, relation_index, nodes.data(), nodes.size(), k_samples, seed,
                src_arr.mutable_data(), tgt_arr.mutable_data(), out_count, &out_count
            );
            if (status != IMPULSE_OK) {
                throw std::runtime_error("Failed to execute sample_neighbors: " + std::string(impulse_get_last_error()));
            }
        }
        return py::make_tuple(src_arr, tgt_arr);
    }


    impulse::vm::QueryResult execute_query(const impulse::vm::CompiledQuery& query, uint64_t input_param = 0) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        return query.execute(snapshot_, input_param);
    }

private:
    impulse_snapshot_t* snapshot_ = nullptr;
};

class PyImpulseWriter {
public:
    PyImpulseWriter(const std::string& output_path, uint64_t global_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED) {
        writer_ = impulse_writer_create(output_path.c_str(), global_features);
        if (!writer_) {
            throw std::runtime_error("Failed to create snapshot writer: " + std::string(impulse_get_last_error()));
        }
    }

    ~PyImpulseWriter() {
        destroy();
    }

    void destroy() {
        if (writer_) {
            impulse_writer_destroy(writer_);
            writer_ = nullptr;
        }
    }

    void add_domain(uint16_t domain_id, uint8_t key_type, const std::string& name) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        impulse_status_t status = impulse_writer_add_domain(writer_, domain_id, key_type, name.c_str());
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add domain: " + std::string(impulse_get_last_error()));
        }
    }

    void add_relation(
        uint16_t src_domain_id,
        uint16_t tgt_domain_id,
        uint8_t encoding_type,
        uint64_t node_count,
        uint64_t edge_count,
        uint64_t section_features,
        const std::vector<uint32_t>& row_offsets,
        const std::vector<uint32_t>& col_indices
    ) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        impulse_status_t status = impulse_writer_add_relation(
            writer_, src_domain_id, tgt_domain_id, encoding_type,
            node_count, edge_count, section_features,
            row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
            col_indices.data(), col_indices.size() * sizeof(uint32_t)
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add relation: " + std::string(impulse_get_last_error()));
        }
    }

    void finalize() {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        impulse_status_t status = impulse_writer_finalize(writer_);
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to finalize snapshot: " + std::string(impulse_get_last_error()));
        }
    }

private:
    impulse_writer_t* writer_ = nullptr;
};

class PyImpulseVmContext {
public:
    PyImpulseVmContext(const PyImpulseSnapshot* snapshot = nullptr) {
        const impulse_snapshot_t* snap_ptr = snapshot ? snapshot->raw_handle() : nullptr;
        ctx_ = impulse_vm_context_create(snap_ptr);
        if (!ctx_) {
            throw std::runtime_error("Failed to create impulse_vm_context");
        }
    }

    ~PyImpulseVmContext() {
        destroy();
    }

    void destroy() {
        if (ctx_) {
            impulse_vm_context_destroy(ctx_);
            ctx_ = nullptr;
        }
    }

    impulse_vm_context_t* raw_handle() const { return ctx_; }

    size_t vector_size() const {
        return ctx_ ? impulse_vm_context_get_vector_size(ctx_) : 0;
    }

    py::array_t<float> get_float_vector(size_t handle) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        const float* ptr = impulse_vm_context_get_float_vector(ctx_, handle);
        size_t sz = vector_size();
        if (!ptr || sz == 0) return py::array_t<float>(0);
        return py::array_t<float>(sz, ptr);
    }

    py::array_t<double> get_double_vector(size_t handle) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        const double* ptr = impulse_vm_context_get_double_vector(ctx_, handle);
        size_t sz = vector_size();
        if (!ptr || sz == 0) return py::array_t<double>(0);
        return py::array_t<double>(sz, ptr);
    }

    int acquire_bitset() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_bitset(ctx_);
    }

    void release_bitset(size_t handle) {
        if (ctx_) impulse_vm_context_release_bitset(ctx_, handle);
    }

    void bitset_add(size_t handle, uint64_t node_id) {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        impulse_vm_context_bitset_add(ctx_, handle, node_id);
    }

    bool bitset_test(size_t handle, uint64_t node_id) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_bitset_test(ctx_, handle, node_id);
    }

    void bitset_fill(size_t handle, uint64_t count) {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        impulse_vm_context_bitset_fill(ctx_, handle, count);
    }

    uint64_t bitset_get_word(size_t handle, size_t word_idx) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_bitset_get_word(ctx_, handle, word_idx);
    }

    int acquire_float_vector() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_float_vector(ctx_);
    }

    void release_float_vector(size_t handle) {
        if (ctx_) impulse_vm_context_release_float_vector(ctx_, handle);
    }

    void float_vector_set(size_t handle, size_t index, float val) {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        impulse_vm_context_float_vector_set(ctx_, handle, index, val);
    }

    int acquire_double_vector() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_double_vector(ctx_);
    }

    void release_double_vector(size_t handle) {
        if (ctx_) impulse_vm_context_release_double_vector(ctx_, handle);
    }

    void double_vector_set(size_t handle, size_t index, double val) {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        impulse_vm_context_double_vector_set(ctx_, handle, index, val);
    }

    int acquire_node_vector() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_node_vector(ctx_);
    }

    void release_node_vector(size_t handle) {
        if (ctx_) impulse_vm_context_release_node_vector(ctx_, handle);
    }

    py::array_t<uint64_t> get_node_vector(size_t handle) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx_, handle);
        size_t sz = vector_size();
        if (!ptr || sz == 0) return py::array_t<uint64_t>(0);
        return py::array_t<uint64_t>(sz, ptr);
    }

    int acquire_string_vector() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_string_vector(ctx_);
    }

    void release_string_vector(size_t handle) {
        if (handle < 8) string_storage_[handle].clear();
        if (ctx_) impulse_vm_context_release_string_vector(ctx_, handle);
    }

    void string_vector_add(size_t handle, const std::string& str) {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        if (handle < 8) {
            string_storage_[handle].push_back(str);
            impulse_vm_context_string_vector_add(ctx_, handle, string_storage_[handle].back().c_str());
        }
    }


    size_t string_vector_size(size_t handle) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_string_vector_size(ctx_, handle);
    }

    std::string string_vector_get(size_t handle, size_t index) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        const char* str = impulse_vm_context_string_vector_get(ctx_, handle, index);
        return str ? std::string(str) : std::string();
    }

    int acquire_value_map() {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_acquire_value_map(ctx_);
    }

    void release_value_map(size_t handle) {
        if (ctx_) impulse_vm_context_release_value_map(ctx_, handle);
    }

    size_t value_map_size(size_t handle) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_value_map_size(ctx_, handle);
    }

    std::string value_map_get_key(size_t handle, size_t index) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        const char* str = impulse_vm_context_value_map_get_key(ctx_, handle, index);
        return str ? std::string(str) : std::string();
    }

    float value_map_get_value(size_t handle, size_t index) const {
        if (!ctx_) throw std::runtime_error("Context is destroyed");
        return impulse_vm_context_value_map_get_value(ctx_, handle, index);
    }

private:
    impulse_vm_context_t* ctx_ = nullptr;
    std::array<std::vector<std::string>, 8> string_storage_;
};


PYBIND11_MODULE(_impulse_native, m) {
    m.doc() = "Impulse Graph Engine Native C-ABI & Bytecode VM Python Extension";

    // Opcodes Sub-module / Constants
    py::module_ op = m.def_submodule("opcodes", "Impulse VM Bytecode Opcodes");
    op.attr("OP_NOP") = OP_NOP;
    op.attr("OP_INIT_INPUT_NODE") = OP_INIT_INPUT_NODE;
    op.attr("OP_INIT_INPUT_SET") = OP_INIT_INPUT_SET;
    op.attr("OP_LOAD_CONST_INT") = OP_LOAD_CONST_INT;
    op.attr("OP_MAP_KEYS_TO_DENSE") = OP_MAP_KEYS_TO_DENSE;
    op.attr("OP_LOAD_CONST_FLOAT") = OP_LOAD_CONST_FLOAT;
    op.attr("OP_LOAD_CONST_STR_PREFIX") = OP_LOAD_CONST_STR_PREFIX;

    op.attr("OP_CSR_WALK") = OP_CSR_WALK;
    op.attr("OP_CSR_WALK_FILTERED") = OP_CSR_WALK_FILTERED;
    op.attr("OP_CSR_DEGREE") = OP_CSR_DEGREE;
    op.attr("OP_CSR_WALK_PREDICATE") = OP_CSR_WALK_PREDICATE;
    op.attr("OP_NODE_FILTER") = OP_NODE_FILTER;
    op.attr("OP_NODE_FILTER_STR_PREFIX") = OP_NODE_FILTER_STR_PREFIX;
    op.attr("OP_CSR_WALK_REDUCE_SUM") = OP_CSR_WALK_REDUCE_SUM;
    op.attr("OP_CSR_WALK_REDUCE") = OP_CSR_WALK_REDUCE;
    op.attr("OP_CSC_WALK") = OP_CSC_WALK;

    op.attr("OP_SET_UNION") = OP_SET_UNION;
    op.attr("OP_SET_INTERSECT") = OP_SET_INTERSECT;
    op.attr("OP_SET_DIFFERENCE") = OP_SET_DIFFERENCE;
    op.attr("OP_SET_CARDINALITY") = OP_SET_CARDINALITY;
    op.attr("OP_VECTOR_MUL_ATTR") = OP_VECTOR_MUL_ATTR;
    op.attr("OP_VECTOR_REDUCE_SUM") = OP_VECTOR_REDUCE_SUM;
    op.attr("OP_VECTOR_DIV") = OP_VECTOR_DIV;
    op.attr("OP_VECTOR_STR_CONCAT") = OP_VECTOR_STR_CONCAT;
    op.attr("OP_FLOAT_VECTOR_SCALE") = OP_FLOAT_VECTOR_SCALE;
    op.attr("OP_L1_NORM_DIFF") = OP_L1_NORM_DIFF;

    op.attr("OP_CC_AFFOREST") = OP_CC_AFFOREST;
    op.attr("OP_MXV") = OP_MXV;
    op.attr("OP_VXM") = OP_VXM;
    op.attr("OP_EWISE_ADD") = OP_EWISE_ADD;
    op.attr("OP_EWISE_MULT") = OP_EWISE_MULT;
    op.attr("OP_REDUCE") = OP_REDUCE;
    op.attr("OP_TC_SWEEP_BATCH") = OP_TC_SWEEP_BATCH;
    op.attr("OP_BRANDES_FORWARD") = OP_BRANDES_FORWARD;
    op.attr("OP_BRANDES_BACKWARD") = OP_BRANDES_BACKWARD;
    op.attr("OP_DELTA_STEP_RELAX") = OP_DELTA_STEP_RELAX;
    op.attr("OP_READ_EDGE_WEIGHT") = OP_READ_EDGE_WEIGHT;

    op.attr("OP_SAMPLE_NEIGHBORS") = OP_SAMPLE_NEIGHBORS;
    op.attr("OP_RANDOM_WALK") = OP_RANDOM_WALK;
    op.attr("OP_SCATTER_GATHER") = OP_SCATTER_GATHER;
    op.attr("OP_REBAC_CHECK") = OP_REBAC_CHECK;
    op.attr("OP_ROARING_BITMAP_AND") = OP_ROARING_BITMAP_AND;
    op.attr("OP_ISLAND_DETECT") = OP_ISLAND_DETECT;
    op.attr("OP_SPARSE_MATVEC") = OP_SPARSE_MATVEC;
    op.attr("OP_LOUVAIN_MODULARITY") = OP_LOUVAIN_MODULARITY;
    op.attr("OP_KCORE_DECOMPOSITION") = OP_KCORE_DECOMPOSITION;
    op.attr("OP_MOTIF_MATCH_3") = OP_MOTIF_MATCH_3;
    op.attr("OP_GRAPH_ISOMORPHISM") = OP_GRAPH_ISOMORPHISM;

    op.attr("OP_JMP") = OP_JMP;
    op.attr("OP_JZ") = OP_JZ;
    op.attr("OP_JNZ") = OP_JNZ;
    op.attr("OP_LOOP_DECR") = OP_LOOP_DECR;
    op.attr("OP_STABLE_CHECK") = OP_STABLE_CHECK;
    op.attr("OP_CALL") = OP_CALL;
    op.attr("OP_RET") = OP_RET;

    op.attr("OP_MOV") = OP_MOV;
    op.attr("OP_CLEAR_REG") = OP_CLEAR_REG;
    op.attr("OP_COLLECT_BITSET") = OP_COLLECT_BITSET;
    op.attr("OP_COLLECT_ARRAY") = OP_COLLECT_ARRAY;
    op.attr("OP_MAP_DENSE_TO_KEYS") = OP_MAP_DENSE_TO_KEYS;
    op.attr("OP_COLLECT_VALUE_MAP") = OP_COLLECT_VALUE_MAP;
    op.attr("OP_HALT") = OP_HALT;

    // Register Types Enum
    py::enum_<impulse_register_type_t>(m, "RegisterType")
        .value("TYPE_NULL", TYPE_NULL)
        .value("TYPE_INT64", TYPE_INT64)
        .value("TYPE_NODE_ID", TYPE_NODE_ID)
        .value("TYPE_RELATION_ID", TYPE_RELATION_ID)
        .value("TYPE_BITSET_HANDLE", TYPE_BITSET_HANDLE)
        .value("TYPE_NODE_VECTOR", TYPE_NODE_VECTOR)
        .value("TYPE_CSR_SPAN", TYPE_CSR_SPAN)
        .value("TYPE_BOOLEAN", TYPE_BOOLEAN)
        .value("TYPE_FLOAT", TYPE_FLOAT)
        .value("TYPE_DOUBLE", TYPE_DOUBLE)
        .value("TYPE_VALUE_MAP", TYPE_VALUE_MAP)
        .value("TYPE_STRING_VECTOR", TYPE_STRING_VECTOR)
        .value("TYPE_FLOAT_VECTOR", TYPE_FLOAT_VECTOR)
        .value("TYPE_DOUBLE_VECTOR", TYPE_DOUBLE_VECTOR)
        .value("TYPE_UINT64_VECTOR", TYPE_UINT64_VECTOR)
        .export_values();

    // VM Status Enum
    py::enum_<impulse_vm_status_t>(m, "VmStatus")
        .value("OK", IMPULSE_VM_OK)
        .value("ERR_INVALID_OPCODE", IMPULSE_VM_ERR_INVALID_OPCODE)
        .value("ERR_OUT_OF_BOUNDS", IMPULSE_VM_ERR_OUT_OF_BOUNDS)
        .value("ERR_NULL_SNAPSHOT", IMPULSE_VM_ERR_NULL_SNAPSHOT)
        .value("ERR_STACK_OVERFLOW", IMPULSE_VM_ERR_STACK_OVERFLOW)
        .value("ERR_STACK_UNDERFLOW", IMPULSE_VM_ERR_STACK_UNDERFLOW)
        .value("ERR_INVALID_REGISTER", IMPULSE_VM_ERR_INVALID_REGISTER)
        .export_values();

    py::class_<impulse_instruction_t>(m, "Instruction")
        .def(py::init<>())
        .def_readwrite("opcode", &impulse_instruction_t::opcode)
        .def_readwrite("flags", &impulse_instruction_t::flags)
        .def_readwrite("dst_reg", &impulse_instruction_t::dst_reg)
        .def_readwrite("payload", &impulse_instruction_t::payload);

    py::class_<impulse_vm_state_t>(m, "VmState")
        .def(py::init<>())
        .def_readwrite("pc", &impulse_vm_state_t::pc)
        .def_readwrite("flags", &impulse_vm_state_t::flags)
        .def_readwrite("call_stack_depth", &impulse_vm_state_t::call_stack_depth)
        .def("get_register", [](const impulse_vm_state_t& self, size_t idx) -> uint64_t {
            if (idx >= 64) throw std::out_of_range("Register index out of bounds [0..63]");
            return self.registers[idx];
        })
        .def("set_register", [](impulse_vm_state_t& self, size_t idx, uint64_t val) {
            if (idx >= 64) throw std::out_of_range("Register index out of bounds [0..63]");
            self.registers[idx] = val;
        })
        .def("get_register_type", [](const impulse_vm_state_t& self, size_t idx) -> uint8_t {
            if (idx >= 64) throw std::out_of_range("Register index out of bounds [0..63]");
            return self.register_types[idx];
        })
        .def("set_register_type", [](impulse_vm_state_t& self, size_t idx, uint8_t type_tag) {
            if (idx >= 64) throw std::out_of_range("Register index out of bounds [0..63]");
            self.register_types[idx] = type_tag;
        });

    py::class_<PyImpulseVmContext>(m, "VmContext")
        .def(py::init<const PyImpulseSnapshot*>(), py::arg("snapshot") = nullptr)
        .def("destroy", &PyImpulseVmContext::destroy)
        .def("__enter__", [](PyImpulseVmContext& self) { return &self; })
        .def("__exit__", [](PyImpulseVmContext& self, py::object, py::object, py::object) { self.destroy(); })
        .def("vector_size", &PyImpulseVmContext::vector_size)
        .def("get_float_vector", &PyImpulseVmContext::get_float_vector, py::arg("handle"))
        .def("get_double_vector", &PyImpulseVmContext::get_double_vector, py::arg("handle"))
        .def("acquire_bitset", &PyImpulseVmContext::acquire_bitset)
        .def("release_bitset", &PyImpulseVmContext::release_bitset, py::arg("handle"))
        .def("bitset_add", &PyImpulseVmContext::bitset_add, py::arg("handle"), py::arg("node_id"))
        .def("bitset_test", &PyImpulseVmContext::bitset_test, py::arg("handle"), py::arg("node_id"))
        .def("bitset_fill", &PyImpulseVmContext::bitset_fill, py::arg("handle"), py::arg("count"))
        .def("bitset_get_word", &PyImpulseVmContext::bitset_get_word, py::arg("handle"), py::arg("word_idx"))
        .def("acquire_float_vector", &PyImpulseVmContext::acquire_float_vector)
        .def("release_float_vector", &PyImpulseVmContext::release_float_vector, py::arg("handle"))
        .def("float_vector_set", &PyImpulseVmContext::float_vector_set, py::arg("handle"), py::arg("index"), py::arg("val"))
        .def("acquire_double_vector", &PyImpulseVmContext::acquire_double_vector)
        .def("release_double_vector", &PyImpulseVmContext::release_double_vector, py::arg("handle"))
        .def("double_vector_set", &PyImpulseVmContext::double_vector_set, py::arg("handle"), py::arg("index"), py::arg("val"))
        .def("acquire_node_vector", &PyImpulseVmContext::acquire_node_vector)
        .def("release_node_vector", &PyImpulseVmContext::release_node_vector, py::arg("handle"))
        .def("get_node_vector", &PyImpulseVmContext::get_node_vector, py::arg("handle"))
        .def("acquire_string_vector", &PyImpulseVmContext::acquire_string_vector)
        .def("release_string_vector", &PyImpulseVmContext::release_string_vector, py::arg("handle"))
        .def("string_vector_add", &PyImpulseVmContext::string_vector_add, py::arg("handle"), py::arg("str"))
        .def("string_vector_size", &PyImpulseVmContext::string_vector_size, py::arg("handle"))
        .def("string_vector_get", &PyImpulseVmContext::string_vector_get, py::arg("handle"), py::arg("index"))
        .def("acquire_value_map", &PyImpulseVmContext::acquire_value_map)
        .def("release_value_map", &PyImpulseVmContext::release_value_map, py::arg("handle"))
        .def("value_map_size", &PyImpulseVmContext::value_map_size, py::arg("handle"))
        .def("value_map_get_key", &PyImpulseVmContext::value_map_get_key, py::arg("handle"), py::arg("index"))
        .def("value_map_get_value", &PyImpulseVmContext::value_map_get_value, py::arg("handle"), py::arg("index"));

    py::class_<impulse::vm::QueryResult>(m, "QueryResult")
        .def(py::init<>())
        .def_readwrite("status", &impulse::vm::QueryResult::status)
        .def_readwrite("result_register", &impulse::vm::QueryResult::result_register)
        .def_readwrite("result_type", &impulse::vm::QueryResult::result_type)
        .def_readwrite("raw_value", &impulse::vm::QueryResult::raw_value)
        .def("is_ok", &impulse::vm::QueryResult::isOk)
        .def("as_int", &impulse::vm::QueryResult::asInt)
        .def("as_float", &impulse::vm::QueryResult::asFloat)
        .def("as_double", &impulse::vm::QueryResult::asDouble)
        .def("test_bitset", [](const impulse::vm::QueryResult& self, const PyImpulseVmContext& ctx, uint64_t node_id) {
            return self.testBitset(ctx.raw_handle(), node_id);
        });

    py::class_<impulse::vm::CompiledQuery>(m, "CompiledQuery")
        .def(py::init<std::vector<impulse_instruction_t>, uint16_t>(), py::arg("instructions"), py::arg("result_reg"))
        .def("bytecode", &impulse::vm::CompiledQuery::bytecode)
        .def("result_register", &impulse::vm::CompiledQuery::resultRegister)
        .def("instruction_count", &impulse::vm::CompiledQuery::instructionCount)
        .def("execute", [](const impulse::vm::CompiledQuery& self, const PyImpulseSnapshot* snap, uint64_t input_param) {
            const impulse_snapshot_t* s_ptr = snap ? snap->raw_handle() : nullptr;
            return self.execute(s_ptr, input_param);
        }, py::arg("snapshot") = nullptr, py::arg("input_param") = 0)
        .def("execute_with_context", [](const impulse::vm::CompiledQuery& self, PyImpulseVmContext& ctx, impulse_vm_state_t& state, uint64_t input_param) {
            return self.executeWithContext(ctx.raw_handle(), &state, input_param);
        }, py::arg("context"), py::arg("state"), py::arg("input_param") = 0);

    py::class_<impulse::vm::QueryBuilder>(m, "QueryBuilder")
        .def(py::init<>())
        .def(py::init<uint16_t>(), py::arg("start_register"))
        .def("input_node", &impulse::vm::QueryBuilder::inputNode, py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("input_set", &impulse::vm::QueryBuilder::inputSet, py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("load_const_int", &impulse::vm::QueryBuilder::loadConstInt, py::arg("value"), py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("load_const_float", &impulse::vm::QueryBuilder::loadConstFloat, py::arg("value"), py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("load_const_str_prefix", &impulse::vm::QueryBuilder::loadConstStrPrefix, py::arg("prefix"), py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("load_keys", [](impulse::vm::QueryBuilder& self, const std::vector<std::string>& keys, uint16_t dst_reg) -> impulse::vm::QueryBuilder& {
            std::vector<const char*> ptrs;
            ptrs.reserve(keys.size());
            for (const auto& k : keys) ptrs.push_back(k.c_str());
            return self.loadKeys(ptrs.data(), ptrs.size(), dst_reg);
        }, py::arg("keys"), py::arg("dst_reg") = 0, py::return_value_policy::reference)
        .def("walk_edge", &impulse::vm::QueryBuilder::walkEdge, py::arg("relation_id"), py::arg("flags") = 0, py::return_value_policy::reference)
        .def("walk_edge_filtered", &impulse::vm::QueryBuilder::walkEdgeFiltered, py::arg("relation_id"), py::arg("filter_id"), py::return_value_policy::reference)
        .def("walk_edge_predicate", &impulse::vm::QueryBuilder::walkEdgePredicate, py::arg("relation_id"), py::arg("filter_id"), py::return_value_policy::reference)
        .def("walk_degree", &impulse::vm::QueryBuilder::walkDegree, py::arg("relation_id"), py::return_value_policy::reference)
        .def("walk_reduce_sum", &impulse::vm::QueryBuilder::walkReduceSum, py::arg("relation_id"), py::arg("val_reg"), py::return_value_policy::reference)
        .def("walk_csc", &impulse::vm::QueryBuilder::walkCsc, py::arg("relation_id"), py::return_value_policy::reference)
        .def("filter_node", &impulse::vm::QueryBuilder::filterNode, py::arg("filter_id"), py::return_value_policy::reference)
        .def("filter_node_str_prefix", &impulse::vm::QueryBuilder::filterNodeStrPrefix, py::arg("prefix"), py::return_value_policy::reference)
        .def("union_with", &impulse::vm::QueryBuilder::unionWith, py::arg("src_reg"), py::return_value_policy::reference)
        .def("intersect_with", &impulse::vm::QueryBuilder::intersectWith, py::arg("src_reg"), py::return_value_policy::reference)
        .def("difference_with", &impulse::vm::QueryBuilder::differenceWith, py::arg("src_reg"), py::return_value_policy::reference)
        .def("cardinality", &impulse::vm::QueryBuilder::cardinality, py::return_value_policy::reference)
        .def("vector_mul_attr", &impulse::vm::QueryBuilder::vectorMulAttr, py::arg("attr_reg"), py::return_value_policy::reference)
        .def("vector_reduce_sum", &impulse::vm::QueryBuilder::vectorReduceSum, py::return_value_policy::reference)
        .def("vector_div", &impulse::vm::QueryBuilder::vectorDiv, py::arg("denom_reg"), py::return_value_policy::reference)
        .def("l1_norm_diff", &impulse::vm::QueryBuilder::l1NormDiff, py::arg("other_reg"), py::return_value_policy::reference)
        .def("matrix_vector_mul", &impulse::vm::QueryBuilder::matrixVectorMul, py::arg("matrix_reg"), py::arg("semiring_id") = 0, py::return_value_policy::reference)
        .def("vector_matrix_mul", &impulse::vm::QueryBuilder::vectorMatrixMul, py::arg("matrix_reg"), py::arg("semiring_id") = 0, py::return_value_policy::reference)
        .def("ewise_add", &impulse::vm::QueryBuilder::ewiseAdd, py::arg("other_reg"), py::arg("binary_op") = 0, py::return_value_policy::reference)
        .def("ewise_mult", &impulse::vm::QueryBuilder::ewiseMult, py::arg("other_reg"), py::arg("binary_op") = 1, py::return_value_policy::reference)
        .def("reduce", &impulse::vm::QueryBuilder::reduce, py::arg("binary_op") = 0, py::return_value_policy::reference)
        .def("afforest", &impulse::vm::QueryBuilder::afforest, py::return_value_policy::reference)
        .def("tc_sweep_batch", &impulse::vm::QueryBuilder::tcSweepBatch, py::return_value_policy::reference)
        .def("brandes_forward", &impulse::vm::QueryBuilder::brandesForward, py::return_value_policy::reference)
        .def("brandes_backward", &impulse::vm::QueryBuilder::brandesBackward, py::return_value_policy::reference)
        .def("delta_step_relax", &impulse::vm::QueryBuilder::deltaStepRelax, py::arg("weight_reg"), py::return_value_policy::reference)
        .def("sample_neighbors", &impulse::vm::QueryBuilder::sampleNeighbors, py::arg("relation_id"), py::arg("k_samples"), py::arg("seed") = 0, py::return_value_policy::reference)
        .def("random_walk", &impulse::vm::QueryBuilder::randomWalk, py::arg("relation_id"), py::arg("steps"), py::arg("seed") = 0, py::return_value_policy::reference)
        .def("scatter_gather", &impulse::vm::QueryBuilder::scatterGather, py::return_value_policy::reference)
        .def("rebac_check", &impulse::vm::QueryBuilder::rebacCheck, py::arg("permission_id"), py::return_value_policy::reference)
        .def("roaring_bitmap_and", &impulse::vm::QueryBuilder::roaringBitmapAnd, py::arg("other_reg"), py::return_value_policy::reference)
        .def("island_detect", &impulse::vm::QueryBuilder::islandDetect, py::arg("secondary_reg"), py::return_value_policy::reference)
        .def("sparse_mat_vec", &impulse::vm::QueryBuilder::sparseMatVec, py::return_value_policy::reference)
        .def("louvain_modularity", &impulse::vm::QueryBuilder::louvainModularity, py::return_value_policy::reference)
        .def("kcore_decomposition", &impulse::vm::QueryBuilder::kcoreDecomposition, py::return_value_policy::reference)
        .def("motif_match_3", &impulse::vm::QueryBuilder::motifMatch3, py::return_value_policy::reference)
        .def("graph_isomorphism", &impulse::vm::QueryBuilder::graphIsomorphism, py::return_value_policy::reference)
        .def("mov", &impulse::vm::QueryBuilder::mov, py::arg("dst_reg"), py::arg("src_reg"), py::return_value_policy::reference)
        .def("clear_reg", &impulse::vm::QueryBuilder::clearReg, py::arg("reg"), py::return_value_policy::reference)
        .def("nop", &impulse::vm::QueryBuilder::nop, py::return_value_policy::reference)
        .def("repeat", [](impulse::vm::QueryBuilder& self, int count, const std::function<void(impulse::vm::QueryBuilder&)>& body) -> impulse::vm::QueryBuilder& {
            return self.repeat(count, body);
        }, py::arg("count"), py::arg("body"), py::return_value_policy::reference)
        .def("repeat_until_stable", [](impulse::vm::QueryBuilder& self, const std::function<void(impulse::vm::QueryBuilder&)>& body) -> impulse::vm::QueryBuilder& {
            return self.repeatUntilStable(body);
        }, py::arg("body"), py::return_value_policy::reference)
        .def("jmp", &impulse::vm::QueryBuilder::jmp, py::arg("instruction_offset"), py::return_value_policy::reference)
        .def("jz", &impulse::vm::QueryBuilder::jz, py::arg("instruction_offset"), py::return_value_policy::reference)
        .def("jnz", &impulse::vm::QueryBuilder::jnz, py::arg("instruction_offset"), py::return_value_policy::reference)
        .def("collect_bitset", &impulse::vm::QueryBuilder::collectBitset, py::return_value_policy::reference)
        .def("collect_array", &impulse::vm::QueryBuilder::collectArray, py::return_value_policy::reference)
        .def("map_dense_to_keys", &impulse::vm::QueryBuilder::mapDenseToKeys, py::return_value_policy::reference)
        .def("collect_value_map", &impulse::vm::QueryBuilder::collectValueMap, py::return_value_policy::reference)
        .def("allocate_register", &impulse::vm::QueryBuilder::allocateRegister)
        .def_property("current_register", &impulse::vm::QueryBuilder::currentRegister, &impulse::vm::QueryBuilder::setCurrentRegister)
        .def("raw_instructions", &impulse::vm::QueryBuilder::rawInstructions)
        .def("compile", &impulse::vm::QueryBuilder::compile);

    py::class_<PyImpulseSnapshot>(m, "Snapshot")
        .def(py::init<const std::string&>(), py::arg("path"))
        .def("close", &PyImpulseSnapshot::close)
        .def("__enter__", [](PyImpulseSnapshot& self) { return &self; })
        .def("__exit__", [](PyImpulseSnapshot& self, py::object, py::object, py::object) { self.close(); })
        .def("domain_count", &PyImpulseSnapshot::domain_count)
        .def("get_domain", &PyImpulseSnapshot::get_domain, py::arg("index"))
        .def("relation_count", &PyImpulseSnapshot::relation_count)
        .def("get_relation", &PyImpulseSnapshot::get_relation, py::arg("index"))
        .def("get_buffer", &PyImpulseSnapshot::get_buffer, py::arg("offset"), py::arg("size"))
        .def("get_csr_row_offsets", &PyImpulseSnapshot::get_csr_row_offsets, py::arg("index"))
        .def("get_csr_col_indices", &PyImpulseSnapshot::get_csr_col_indices, py::arg("index"))
        .def("sample_neighbors", &PyImpulseSnapshot::sample_neighbors,
             py::arg("relation_index"), py::arg("nodes"), py::arg("k_samples"), py::arg("seed") = 42)
        .def("is_reachable", &PyImpulseSnapshot::is_reachable,
             py::arg("relation_index"), py::arg("src_id"), py::arg("tgt_id"))

        .def("execute_query", &PyImpulseSnapshot::execute_query, py::arg("query"), py::arg("input_param") = 0);

    py::class_<PyImpulseWriter>(m, "Writer")
        .def(py::init<const std::string&, uint64_t>(), py::arg("output_path"), py::arg("global_features") = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED)
        .def("destroy", &PyImpulseWriter::destroy)
        .def("__enter__", [](PyImpulseWriter& self) { return &self; })
        .def("__exit__", [](PyImpulseWriter& self, py::object, py::object, py::object) { self.destroy(); })
        .def("add_domain", &PyImpulseWriter::add_domain, py::arg("domain_id"), py::arg("key_type"), py::arg("name"))
        .def("add_relation", &PyImpulseWriter::add_relation,
             py::arg("src_domain_id"), py::arg("tgt_domain_id"), py::arg("encoding_type"),
             py::arg("node_count"), py::arg("edge_count"), py::arg("section_features"),
             py::arg("row_offsets"), py::arg("col_indices"))
        .def("finalize", &PyImpulseWriter::finalize);
}
