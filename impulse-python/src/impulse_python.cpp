#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include "impulse_graph.h"
#include "impulse_vm_v11.h"
#include "vm/impulse_vm_isa.hpp"
#include "vm/impulse_impscm.hpp"
#include "impulse_cypher.hpp"
#include "impulse_cel.h"
#include "impulse_assert.h"
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace py = pybind11;

// Reference-counted lease over the underlying mmap snapshot handle so exported
// zero-copy memoryviews / NumPy arrays / PyTorch tensors cannot trigger a UAF
// if Snapshot.close() / __exit__() is called while views remain alive (#138).
struct SnapshotLease {
    impulse_snapshot_t* handle = nullptr;
    explicit SnapshotLease(impulse_snapshot_t* h) : handle(h) {}
    ~SnapshotLease() {
        if (handle) {
            impulse_snapshot_close(handle);
            handle = nullptr;
        }
    }
    SnapshotLease(const SnapshotLease&) = delete;
    SnapshotLease& operator=(const SnapshotLease&) = delete;
};

struct PySnapshotBufferView {
    std::shared_ptr<SnapshotLease> lease;
    const void* ptr = nullptr;
    size_t size = 0;
};

class PyImpulseSnapshot {
public:
    PyImpulseSnapshot(const std::string& path) {
        impulse_status_t status = IMPULSE_OK;
        impulse_snapshot_t* raw = nullptr;
        {
            py::gil_scoped_release release;
            raw = impulse_snapshot_open(path.c_str(), &status);
        }
        if (!raw || status != IMPULSE_OK) {
            throw std::runtime_error("Failed to open snapshot '" + path + "': " + std::string(impulse_get_last_error()));
        }
        IMPULSE_ASSERT(raw != nullptr);
        lease_ = std::make_shared<SnapshotLease>(raw);
        snapshot_ = raw;
    }

    ~PyImpulseSnapshot() {
        close();
    }

    void close() {
        snapshot_ = nullptr;
        lease_.reset();
    }

    const impulse_snapshot_t* raw_handle() const { return snapshot_; }
    std::shared_ptr<SnapshotLease> lease() const { return lease_; }

    bool is_reachable(uint16_t relation_index, uint64_t src_id, uint64_t tgt_id) const {
        auto lease = lease_;
        if (!lease || !lease->handle) return false;
        py::gil_scoped_release release;
        return impulse_snapshot_is_reachable(lease->handle, relation_index, src_id, tgt_id);
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
        res["node_count"] = entry.node_count;
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
        const char* rel_name = nullptr;
        if (impulse_snapshot_get_relation_name(snapshot_, index, &rel_name) == IMPULSE_OK && rel_name) {
            res["name"] = std::string(rel_name);
        } else {
            res["name"] = "";
        }
        return res;
    }

    py::memoryview make_pinned_memoryview(const void* ptr, size_t size) const {
        py::object owner = py::cast(PySnapshotBufferView{lease_, ptr, size});
        return py::memoryview(owner);
    }

    py::memoryview get_buffer(uint64_t offset, uint64_t size) const {
        if (!snapshot_ || !lease_) throw std::runtime_error("Snapshot is closed");
        const void* ptr = impulse_snapshot_get_buffer(snapshot_, offset, size);
        if (!ptr) throw std::runtime_error("Invalid buffer offset or size");
        return make_pinned_memoryview(ptr, static_cast<size_t>(size));
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

    py::memoryview get_csc_row_offsets(uint16_t index) const {
        if (!snapshot_ || !lease_) throw std::runtime_error("Snapshot is closed");
        const uint32_t* csc_offsets = nullptr;
        const uint32_t* csc_targets = nullptr;
        uint64_t row_count = 0, edge_count = 0;
        impulse_status_t st = impulse_snapshot_get_relation_csc_buffers(
            snapshot_, index, &csc_offsets, &csc_targets, &row_count, &edge_count
        );
        if (st != IMPULSE_OK || !csc_offsets) {
            throw std::runtime_error("CSC buffers not available for relation " + std::to_string(index));
        }
        return make_pinned_memoryview(csc_offsets, static_cast<size_t>((row_count + 1) * sizeof(uint32_t)));
    }

    py::memoryview get_csc_col_indices(uint16_t index) const {
        if (!snapshot_ || !lease_) throw std::runtime_error("Snapshot is closed");
        const uint32_t* csc_offsets = nullptr;
        const uint32_t* csc_targets = nullptr;
        uint64_t row_count = 0, edge_count = 0;
        impulse_status_t st = impulse_snapshot_get_relation_csc_buffers(
            snapshot_, index, &csc_offsets, &csc_targets, &row_count, &edge_count
        );
        if (st != IMPULSE_OK || !csc_targets) {
            throw std::runtime_error("CSC buffers not available for relation " + std::to_string(index));
        }
        return make_pinned_memoryview(csc_targets, static_cast<size_t>(edge_count * sizeof(uint32_t)));
    }

    py::memoryview get_attribute_data(uint16_t relation_index, uint16_t attribute_index) const {
        if (!snapshot_ || !lease_) throw std::runtime_error("Snapshot is closed");
        const void* data = nullptr;
        uint64_t data_bytes = 0;
        const void* offsets = nullptr;
        uint64_t offsets_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dimension = 0;
        impulse_status_t st = impulse_snapshot_get_attribute_buffers(
            snapshot_, relation_index, attribute_index,
            &data, &data_bytes, &offsets, &offsets_bytes, &type_code, &dimension
        );
        if (st != IMPULSE_OK || !data) {
            throw std::runtime_error("Attribute buffer not available for relation " + std::to_string(relation_index) + ", attr " + std::to_string(attribute_index));
        }
        return make_pinned_memoryview(data, static_cast<size_t>(data_bytes));
    }

    py::object get_attribute_offsets(uint16_t relation_index, uint16_t attribute_index) const {
        if (!snapshot_ || !lease_) throw std::runtime_error("Snapshot is closed");
        const void* data = nullptr;
        uint64_t data_bytes = 0;
        const void* offsets = nullptr;
        uint64_t offsets_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dimension = 0;
        impulse_status_t st = impulse_snapshot_get_attribute_buffers(
            snapshot_, relation_index, attribute_index,
            &data, &data_bytes, &offsets, &offsets_bytes, &type_code, &dimension
        );
        if (st != IMPULSE_OK || !offsets || offsets_bytes == 0) {
            return py::none();
        }
        return make_pinned_memoryview(offsets, static_cast<size_t>(offsets_bytes));
    }

    py::dict get_attribute_info(uint16_t relation_index, uint16_t attribute_index) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        const void* data = nullptr;
        uint64_t data_bytes = 0;
        const void* offsets = nullptr;
        uint64_t offsets_bytes = 0;
        uint8_t type_code = 0;
        uint32_t dimension = 0;
        impulse_status_t st = impulse_snapshot_get_attribute_buffers(
            snapshot_, relation_index, attribute_index,
            &data, &data_bytes, &offsets, &offsets_bytes, &type_code, &dimension
        );
        if (st != IMPULSE_OK) {
            throw std::runtime_error("Attribute info not available for relation " + std::to_string(relation_index) + ", attr " + std::to_string(attribute_index));
        }
        py::dict res;
        res["type_code"] = type_code;
        res["dimension"] = dimension;
        res["data_bytes"] = data_bytes;
        res["offsets_bytes"] = offsets_bytes;
        return res;
    }

    py::tuple sample_neighbors(uint16_t relation_index, const std::vector<uint64_t>& nodes, int k_samples, uint64_t seed) const {
        auto lease = lease_;
        if (!lease || !lease->handle) throw std::runtime_error("Snapshot is closed");
        size_t out_count = 0;
        impulse_status_t status = IMPULSE_OK;
        {
            py::gil_scoped_release release;
            status = impulse_snapshot_sample_neighbors(
                lease->handle, relation_index, nodes.data(), nodes.size(), k_samples, seed, nullptr, nullptr, 0, &out_count
            );
        }
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to dry-run sample_neighbors: " + std::string(impulse_get_last_error()));
        }

        py::array_t<uint64_t> src_arr(out_count);
        py::array_t<uint64_t> tgt_arr(out_count);

        if (out_count > 0) {
            uint64_t* src_ptr = src_arr.mutable_data();
            uint64_t* tgt_ptr = tgt_arr.mutable_data();
            {
                py::gil_scoped_release release;
                status = impulse_snapshot_sample_neighbors(
                    lease->handle, relation_index, nodes.data(), nodes.size(), k_samples, seed,
                    src_ptr, tgt_ptr, out_count, &out_count
                );
            }
            if (status != IMPULSE_OK) {
                throw std::runtime_error("Failed to execute sample_neighbors: " + std::string(impulse_get_last_error()));
            }
        }
        return py::make_tuple(src_arr, tgt_arr);
    }


    uint32_t resolve_dense_id(uint16_t domain_id, const std::string& key) const {
        auto lease = lease_;
        if (!lease || !lease->handle) throw std::runtime_error("Snapshot is closed");
        uint32_t node_id = 0;
        impulse_status_t status = IMPULSE_OK;
        {
            py::gil_scoped_release release;
            status = impulse_snapshot_resolve_key(lease->handle, domain_id, key.data(), key.size(), &node_id);
        }
        if (status == IMPULSE_ERR_INVALID_ARGUMENT) {
            throw py::key_error("Key '" + key + "' not found in domain " + std::to_string(domain_id));
        } else if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to resolve key: " + std::string(impulse_get_last_error()));
        }
        return node_id;
    }

    py::bytes resolve_key(uint16_t domain_id, uint32_t node_id) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        const void* key_bytes = nullptr;
        size_t key_len = 0;
        impulse_status_t status = impulse_snapshot_resolve_dense_id(snapshot_, domain_id, node_id, &key_bytes, &key_len);
        if (status == IMPULSE_ERR_INVALID_ARGUMENT) {
            throw py::index_error("Node ID " + std::to_string(node_id) + " not found in domain " + std::to_string(domain_id));
        } else if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to resolve dense ID: " + std::string(impulse_get_last_error()));
        }
        return py::bytes(static_cast<const char*>(key_bytes), key_len);
    }

private:
    std::shared_ptr<SnapshotLease> lease_;
    impulse_snapshot_t* snapshot_ = nullptr;
};

class PyImpulseWriter {
public:
    PyImpulseWriter(const std::string& output_path, uint64_t global_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED) {
        writer_ = impulse_writer_create(output_path.c_str(), global_features);
        if (!writer_) {
            throw std::runtime_error("Failed to create snapshot writer: " + std::string(impulse_get_last_error()));
        }
        IMPULSE_ASSERT(writer_ != nullptr);
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

    void set_domain_node_count(uint16_t domain_index, uint64_t node_count) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        impulse_status_t status = impulse_writer_set_domain_node_count(writer_, domain_index, node_count);
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to set domain node count: " + std::string(impulse_get_last_error()));
        }
    }

    void add_domain_attribute_decimal64(
        uint16_t domain_index,
        const std::string& name,
        uint8_t type_code,
        uint32_t dimension,
        uint16_t precision,
        uint8_t scale,
        py::object data
    ) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        py::buffer_info data_info = py::buffer(data).request();
        const void* data_ptr = data_info.ptr;
        uint64_t data_bytes = static_cast<uint64_t>(data_info.size * data_info.itemsize);

        impulse_status_t status = impulse_writer_add_domain_attribute_decimal64(
            writer_, domain_index, name.c_str(), type_code, dimension,
            precision, scale, data_ptr, data_bytes
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add domain decimal64 attribute: " + std::string(impulse_get_last_error()));
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

    void add_attribute(
        uint16_t relation_index,
        const std::string& name,
        uint8_t type_code,
        uint32_t dimension,
        py::object data,
        py::object offsets = py::none()
    ) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        py::buffer_info data_info = py::buffer(data).request();
        const void* data_ptr = data_info.ptr;
        uint64_t data_bytes = static_cast<uint64_t>(data_info.size * data_info.itemsize);

        const void* offsets_ptr = nullptr;
        uint64_t offsets_bytes = 0;
        py::buffer_info offsets_info;
        if (!offsets.is_none()) {
            offsets_info = py::buffer(offsets).request();
            offsets_ptr = offsets_info.ptr;
            offsets_bytes = static_cast<uint64_t>(offsets_info.size * offsets_info.itemsize);
        }

        impulse_status_t status = impulse_writer_add_attribute(
            writer_, relation_index, name.c_str(), type_code, dimension,
            data_ptr, data_bytes, offsets_ptr, offsets_bytes
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add attribute: " + std::string(impulse_get_last_error()));
        }
    }

    void add_attribute_decimal64(
        uint16_t relation_index,
        const std::string& name,
        uint8_t type_code,
        uint32_t dimension,
        uint16_t precision,
        uint8_t scale,
        py::object data
    ) {
        if (!writer_) throw std::runtime_error("Writer is destroyed");
        py::buffer_info data_info = py::buffer(data).request();
        const void* data_ptr = data_info.ptr;
        uint64_t data_bytes = static_cast<uint64_t>(data_info.size * data_info.itemsize);

        impulse_status_t status = impulse_writer_add_attribute_decimal64(
            writer_, relation_index, name.c_str(), type_code, dimension,
            precision, scale, data_ptr, data_bytes
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add decimal64 attribute: " + std::string(impulse_get_last_error()));
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

class PyVmWorkspace {
public:
    explicit PyVmWorkspace(size_t scratch_bytes = 4 * 1024 * 1024) {
        size_t aligned = ((scratch_bytes + 127) / 128) * 128;
        if (aligned < 65536) aligned = 65536;
#if defined(_MSC_VER)
        buf_ = _aligned_malloc(aligned, 128);
#else
        if (posix_memalign(&buf_, 128, aligned) != 0) buf_ = nullptr;
#endif
        if (!buf_) throw std::bad_alloc();
        std::memset(buf_, 0, aligned);
        cap_ = aligned;
        impulse_vm_status_v11_t rc = impulse_workspace_init(buf_, cap_, &ws_);
        if (rc != IMPULSE_VM_V11_OK || !ws_) {
            destroy();
            throw std::runtime_error("impulse_workspace_init failed: status=" + std::to_string(rc));
        }
    }

    ~PyVmWorkspace() { destroy(); }

    void destroy() {
        ws_ = nullptr;
        if (buf_) {
#if defined(_MSC_VER)
            _aligned_free(buf_);
#else
            std::free(buf_);
#endif
            buf_ = nullptr;
            cap_ = 0;
        }
    }

    uint32_t execute(
        const py::bytes& impb_bytes,
        const PyImpulseSnapshot* snapshot,
        const std::vector<uint64_t>& seed_ids = {},
        uint16_t domain_id = 0,
        uint64_t gas_limit = UINT64_MAX
    ) {
        if (!ws_) throw std::runtime_error("VmWorkspace has been destroyed");
        auto lease = snapshot ? snapshot->lease() : nullptr;
        const impulse_snapshot_t* s_ptr = lease ? lease->handle : nullptr;
        std::string prog = impb_bytes;

        impulse_vm_param_binding_v11_t binding{};
        impulse_vm_params_v11_t params{};
        params.gas_limit = gas_limit;
        if (!seed_ids.empty()) {
            binding.param_id = 0;
            binding.kind = IMPULSE_VM_V11_PARAM_FRONTIER_IDS;
            binding.domain_id = domain_id;
            binding.count = static_cast<uint32_t>(seed_ids.size());
            binding.data_ptr = seed_ids.data();
            params.bindings = &binding;
            params.binding_count = 1;
        }

        size_t out_bytes = 0;
        impulse_vm_status_v11_t rc;
        {
            py::gil_scoped_release release;
            rc = impulse_vm_execute_v11(
                prog.data(), prog.size(), s_ptr, &params, ws_, nullptr, 0, &out_bytes);
        }
        return rc;
    }

    uint32_t execute_params(
        const py::bytes& impb_bytes,
        const PyImpulseSnapshot* snapshot,
        const py::list& bindings_list,
        uint64_t gas_limit = UINT64_MAX
    ) {
        if (!ws_) throw std::runtime_error("VmWorkspace has been destroyed");
        auto lease = snapshot ? snapshot->lease() : nullptr;
        const impulse_snapshot_t* s_ptr = lease ? lease->handle : nullptr;
        std::string prog = impb_bytes;

        std::vector<impulse_vm_param_binding_v11_t> bindings;
        std::vector<std::vector<uint64_t>> owned_u64_bufs;
        std::vector<std::string> owned_str_bufs;
        bindings.reserve(bindings_list.size());
        owned_u64_bufs.reserve(bindings_list.size());
        owned_str_bufs.reserve(bindings_list.size());

        for (const auto& item : bindings_list) {
            py::dict d = item.cast<py::dict>();
            impulse_vm_param_binding_v11_t b{};
            b.param_id = d["param_id"].cast<uint32_t>();
            b.kind = d["kind"].cast<uint32_t>();
            if (d.contains("domain_id")) b.domain_id = d["domain_id"].cast<uint16_t>();
            if (b.kind == IMPULSE_VM_V11_PARAM_FRONTIER_IDS) {
                owned_u64_bufs.push_back(d["ids"].cast<std::vector<uint64_t>>());
                b.count = static_cast<uint32_t>(owned_u64_bufs.back().size());
                b.data_ptr = owned_u64_bufs.back().data();
            } else if (b.kind == IMPULSE_VM_V11_PARAM_SCALAR_STR) {
                owned_str_bufs.push_back(d["str_val"].cast<std::string>());
                b.count = static_cast<uint32_t>(owned_str_bufs.back().size());
                b.data_ptr = owned_str_bufs.back().data();
            } else if (d.contains("scalar_i64")) {
                int64_t v = d["scalar_i64"].cast<int64_t>();
                b.scalar_bits = static_cast<uint64_t>(v);
            } else if (d.contains("scalar_f64")) {
                double v = d["scalar_f64"].cast<double>();
                uint64_t bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                b.scalar_bits = bits;
            } else if (d.contains("scalar_bits")) {
                b.scalar_bits = d["scalar_bits"].cast<uint64_t>();
            }
            bindings.push_back(b);
        }

        impulse_vm_params_v11_t params{};
        params.gas_limit = gas_limit;
        params.bindings = bindings.empty() ? nullptr : bindings.data();
        params.binding_count = static_cast<uint32_t>(bindings.size());

        size_t out_bytes = 0;
        impulse_vm_status_v11_t rc;
        {
            py::gil_scoped_release release;
            rc = impulse_vm_execute_v11(
                prog.data(), prog.size(), s_ptr, &params, ws_, nullptr, 0, &out_bytes);
        }
        return rc;
    }

    uint32_t output_count() const {
        return ws_ ? impulse_vm_v11_get_output_count(ws_) : 0;
    }

    py::dict get_output(uint16_t slot) const {
        if (!ws_) throw std::runtime_error("VmWorkspace has been destroyed");
        impulse_vm_output_view_v11_t view{};
        int rc = impulse_vm_v11_get_output(ws_, slot, &view);
        if (rc == 0) {
            throw std::out_of_range("Output slot " + std::to_string(slot) + " unavailable");
        }
        py::dict res;
        res["slot"] = view.slot;
        res["elem_tag"] = view.elem_tag;
        res["length"] = view.length;
        if ((view.elem_tag == 0 || view.elem_tag == 1) && view.i64_data) {
            std::vector<int64_t> vals(view.i64_data, view.i64_data + view.length);
            res["data"] = vals;
        } else if (view.elem_tag == 4 && view.f64_data) {
            std::vector<double> vals(view.f64_data, view.f64_data + view.length);
            res["data"] = vals;
        }
        return res;
    }

    std::pair<uint64_t, uint64_t> compute_ictx_v2() const {
        if (!ws_) throw std::runtime_error("VmWorkspace has been destroyed");
        impulse_vm_ictx_v2_t d = impulse_vm_v11_compute_ictx_v2(ws_);
        return {d.logical_hash, d.physical_hash};
    }

private:
    void* buf_ = nullptr;
    size_t cap_ = 0;
    impulse_vm_workspace_v11_t* ws_ = nullptr;
};

class PyStatement {
public:
    PyStatement(const PyImpulseSnapshot& snapshot, const std::string& query_text)
        : lease_(snapshot.lease()) {
        if (!lease_ || !lease_->handle) throw std::runtime_error("Snapshot is closed");
        int rc = impulse_stmt_prepare(lease_->handle, query_text.c_str(), &stmt_);
        if (rc != 0 || !stmt_) {
            throw std::runtime_error("impulse_stmt_prepare failed: status=" + std::to_string(rc));
        }
        size_t req = impulse_stmt_buffer_size(stmt_);
        buf_.resize(req > 0 ? req : 1024 * 1024);
    }

    ~PyStatement() { finalize(); }

    void finalize() {
        if (stmt_) {
            impulse_stmt_finalize(stmt_);
            stmt_ = nullptr;
        }
        lease_.reset();
    }

    void bind_node(const std::string& name, uint64_t node_id) {
        if (!stmt_) throw std::runtime_error("Statement finalized");
        impulse_stmt_bind_node(stmt_, name.c_str(), node_id);
    }
    void bind_int(const std::string& name, int64_t val) {
        if (!stmt_) throw std::runtime_error("Statement finalized");
        impulse_stmt_bind_int(stmt_, name.c_str(), val);
    }
    void bind_float(const std::string& name, double val) {
        if (!stmt_) throw std::runtime_error("Statement finalized");
        impulse_stmt_bind_float(stmt_, name.c_str(), val);
    }
    void bind_str(const std::string& name, const std::string& val) {
        if (!stmt_) throw std::runtime_error("Statement finalized");
        impulse_stmt_bind_str(stmt_, name.c_str(), val.c_str());
    }

    int execute() {
        if (!stmt_) throw std::runtime_error("Statement finalized");
        py::gil_scoped_release release;
        return impulse_stmt_execute(stmt_, buf_.data(), buf_.size());
    }

    size_t row_count() const { return stmt_ ? impulse_stmt_row_count(stmt_) : 0; }
    uint32_t column_count() const { return stmt_ ? impulse_stmt_column_count(stmt_) : 0; }
    std::vector<uint64_t> column_u64(uint32_t col_idx = 0) const {
        if (!stmt_) return {};
        size_t n = impulse_stmt_row_count(stmt_);
        const uint64_t* ptr = static_cast<const uint64_t*>(impulse_stmt_column_data(stmt_, col_idx));
        if (!ptr || n == 0) return {};
        return std::vector<uint64_t>(ptr, ptr + n);
    }

private:
    std::shared_ptr<SnapshotLease> lease_;
    impulse_stmt_t* stmt_ = nullptr;
    std::vector<uint8_t> buf_;
};


PYBIND11_MODULE(_impulse_native, m) {
    m.doc() = "Impulse Graph Engine Native C-ABI & v0.11 Bytecode VM Python Extension";

    py::module_ op = m.def_submodule("opcodes", "ImpulseVM v0.11 Bytecode Opcodes");
    for (size_t i = 0; i < 256; ++i) {
        const auto& meta = impulse::vm::kOpcodeMeta[i];
        if (meta.defined && !meta.name.empty()) {
            op.attr(std::string(meta.name).c_str()) = static_cast<uint8_t>(i);
        }
    }

    py::enum_<impulse::vm::Status>(m, "VmStatus")
        .value("OK", impulse::vm::Status::IMPULSE_VM_OK)
        .value("ERR_INVALID_OPCODE", impulse::vm::Status::IMPULSE_VM_ERR_INVALID_OPCODE)
        .value("ERR_OUT_OF_BOUNDS", impulse::vm::Status::IMPULSE_VM_ERR_OUT_OF_BOUNDS)
        .value("ERR_NULL_SNAPSHOT", impulse::vm::Status::IMPULSE_VM_ERR_NULL_SNAPSHOT)
        .value("ERR_STACK_OVERFLOW", impulse::vm::Status::IMPULSE_VM_ERR_STACK_OVERFLOW)
        .value("ERR_STACK_UNDERFLOW", impulse::vm::Status::IMPULSE_VM_ERR_STACK_UNDERFLOW)
        .value("ERR_INVALID_OPERAND", impulse::vm::Status::IMPULSE_VM_ERR_INVALID_OPERAND)
        .value("ERR_USER_THROW", impulse::vm::Status::IMPULSE_VM_ERR_USER_THROW)
        .value("ERR_ASSERTION_FAILED", impulse::vm::Status::IMPULSE_VM_ERR_ASSERTION_FAILED)
        .value("ERR_TRAP", impulse::vm::Status::IMPULSE_VM_ERR_TRAP)
        .value("ERR_DOMAIN_MISMATCH", impulse::vm::Status::IMPULSE_VM_ERR_DOMAIN_MISMATCH)
        .value("ERR_BUFFER_OVERFLOW", impulse::vm::Status::IMPULSE_VM_ERR_BUFFER_OVERFLOW)
        .value("ERR_FLOATING_POINT", impulse::vm::Status::IMPULSE_VM_ERR_FLOATING_POINT)
        .value("ERR_GAS_EXHAUSTED", impulse::vm::Status::IMPULSE_VM_ERR_GAS_EXHAUSTED)
        .value("ERR_TYPE_MISMATCH", impulse::vm::Status::IMPULSE_VM_ERR_TYPE_MISMATCH)
        .value("ERR_VALIDATION_FAILED", impulse::vm::Status::IMPULSE_VM_ERR_VALIDATION_FAILED)
        .value("ERR_ARITHMETIC_OVERFLOW", impulse::vm::Status::IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW)
        .value("ERR_DIV_BY_ZERO", impulse::vm::Status::IMPULSE_VM_ERR_DIV_BY_ZERO)
        .export_values();

    m.attr("ATTR_TYPE_BOOL") = static_cast<uint8_t>(0x0F);
    m.attr("ATTR_TYPE_DECIMAL64") = static_cast<uint8_t>(0x15);
    m.attr("IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW") = static_cast<uint32_t>(IMPULSE_VM_V11_ERR_ARITHMETIC_OVERFLOW);
    m.attr("IMPULSE_VM_ERR_DIV_BY_ZERO") = static_cast<uint32_t>(IMPULSE_VM_V11_ERR_DIV_BY_ZERO);
    m.attr("OP_SCALAR_DECIMAL_ALU") = static_cast<uint8_t>(0x15);
    m.attr("OP_VEC_DECIMAL_ALU") = static_cast<uint8_t>(0x58);

    m.def("validate_bytecode", [](const py::bytes& impb_bytes) {
        std::string prog = impb_bytes;
        impulse_vm_validation_v11_t v = impulse_vm_validate_v11(prog.data(), prog.size());
        return std::make_pair(v.status, v.bad_pc);
    }, py::arg("impb_bytes"));

    m.def("plan_memory", [](const py::bytes& impb_bytes, const PyImpulseSnapshot* snap, uint16_t max_dop) {
        std::string prog = impb_bytes;
        const impulse_snapshot_t* s_ptr = snap ? snap->raw_handle() : nullptr;
        impulse_vm_memory_plan_t plan{};
        impulse_vm_status_v11_t rc = impulse_vm_plan_memory(prog.data(), prog.size(), s_ptr, max_dop, &plan);
        if (rc != IMPULSE_VM_V11_OK) {
            throw std::runtime_error("impulse_vm_plan_memory failed: status=" + std::to_string(rc));
        }
        py::dict d;
        d["min_scratch_bytes"] = plan.min_scratch_bytes;
        d["guaranteed_scratch_bytes"] = plan.guaranteed_scratch_bytes;
        d["max_result_bytes"] = plan.max_result_bytes;
        return d;
    }, py::arg("impb_bytes"), py::arg("snapshot") = nullptr, py::arg("max_dop") = 1);

    py::class_<PyVmWorkspace>(m, "VmWorkspace")
        .def(py::init<size_t>(), py::arg("scratch_bytes") = 4 * 1024 * 1024)
        .def("destroy", &PyVmWorkspace::destroy)
        .def("__enter__", [](PyVmWorkspace& self) { return &self; })
        .def("__exit__", [](PyVmWorkspace& self, py::object, py::object, py::object) { self.destroy(); })
        .def("execute", &PyVmWorkspace::execute,
             py::arg("impb_bytes"), py::arg("snapshot") = nullptr,
             py::arg("seed_ids") = std::vector<uint64_t>{},
             py::arg("domain_id") = 0, py::arg("gas_limit") = UINT64_MAX)
        .def("execute_params", &PyVmWorkspace::execute_params,
             py::arg("impb_bytes"), py::arg("snapshot") = nullptr,
             py::arg("bindings") = py::list(),
             py::arg("gas_limit") = UINT64_MAX)
        .def("output_count", &PyVmWorkspace::output_count)
        .def("get_output", &PyVmWorkspace::get_output, py::arg("slot"))
        .def("compute_ictx_v2", &PyVmWorkspace::compute_ictx_v2);

    py::class_<PyStatement>(m, "Statement")
        .def(py::init<const PyImpulseSnapshot&, const std::string&>(), py::arg("snapshot"), py::arg("query_text"))
        .def("finalize", &PyStatement::finalize)
        .def("bind_node", &PyStatement::bind_node, py::arg("name"), py::arg("node_id"))
        .def("bind_int", &PyStatement::bind_int, py::arg("name"), py::arg("val"))
        .def("bind_float", &PyStatement::bind_float, py::arg("name"), py::arg("val"))
        .def("bind_str", &PyStatement::bind_str, py::arg("name"), py::arg("val"))
        .def("execute", &PyStatement::execute)
        .def("row_count", &PyStatement::row_count)
        .def("column_count", &PyStatement::column_count)
        .def("column_u64", &PyStatement::column_u64, py::arg("col_idx") = 0);

    py::class_<PySnapshotBufferView>(m, "_SnapshotBufferView", py::buffer_protocol())
        .def_buffer([](PySnapshotBufferView& v) -> py::buffer_info {
            return py::buffer_info(
                const_cast<void*>(v.ptr),
                sizeof(uint8_t),
                py::format_descriptor<uint8_t>::format(),
                1,
                {v.size},
                {sizeof(uint8_t)},
                /*readonly=*/true
            );
        });

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
        .def("get_csc_row_offsets", &PyImpulseSnapshot::get_csc_row_offsets, py::arg("index"))
        .def("get_csc_col_indices", &PyImpulseSnapshot::get_csc_col_indices, py::arg("index"))
        .def("get_attribute_data", &PyImpulseSnapshot::get_attribute_data, py::arg("relation_index"), py::arg("attribute_index"))
        .def("get_attribute_offsets", &PyImpulseSnapshot::get_attribute_offsets, py::arg("relation_index"), py::arg("attribute_index"))
        .def("get_attribute_info", &PyImpulseSnapshot::get_attribute_info, py::arg("relation_index"), py::arg("attribute_index"))
        .def("sample_neighbors", &PyImpulseSnapshot::sample_neighbors,
             py::arg("relation_index"), py::arg("nodes"), py::arg("k_samples"), py::arg("seed") = 42)
        .def("is_reachable", &PyImpulseSnapshot::is_reachable,
             py::arg("relation_index"), py::arg("src_id"), py::arg("tgt_id"))
        .def("resolve_key", &PyImpulseSnapshot::resolve_key,
             py::arg("domain_id"), py::arg("node_id"))
        .def("resolve_dense_id", &PyImpulseSnapshot::resolve_dense_id,
             py::arg("domain_id"), py::arg("key"));

    py::class_<PyImpulseWriter>(m, "Writer")
        .def(py::init<const std::string&, uint64_t>(), py::arg("output_path"), py::arg("global_features") = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED)
        .def("destroy", &PyImpulseWriter::destroy)
        .def("__enter__", [](PyImpulseWriter& self) { return &self; })
        .def("__exit__", [](PyImpulseWriter& self, py::object, py::object, py::object) { self.destroy(); })
        .def("add_domain", &PyImpulseWriter::add_domain, py::arg("domain_id"), py::arg("key_type"), py::arg("name"))
        .def("set_domain_node_count", &PyImpulseWriter::set_domain_node_count, py::arg("domain_index"), py::arg("node_count"))
        .def("add_domain_attribute_decimal64", &PyImpulseWriter::add_domain_attribute_decimal64,
             py::arg("domain_index"), py::arg("name"), py::arg("type_code"),
             py::arg("dimension"), py::arg("precision"), py::arg("scale"), py::arg("data"))
        .def("add_relation", &PyImpulseWriter::add_relation,
             py::arg("src_domain_id"), py::arg("tgt_domain_id"), py::arg("encoding_type"),
             py::arg("node_count"), py::arg("edge_count"), py::arg("section_features"),
             py::arg("row_offsets"), py::arg("col_indices"))
        .def("add_attribute", &PyImpulseWriter::add_attribute,
             py::arg("relation_index"), py::arg("name"), py::arg("type_code"),
             py::arg("dimension"), py::arg("data"), py::arg("offsets") = py::none())
        .def("add_attribute_decimal64", &PyImpulseWriter::add_attribute_decimal64,
             py::arg("relation_index"), py::arg("name"), py::arg("type_code"),
             py::arg("dimension"), py::arg("precision"), py::arg("scale"), py::arg("data"))
        .def("finalize", &PyImpulseWriter::finalize);

    m.def("parse_cel", [](const std::string& expr) -> std::string {
        impulse::cel::Parser parser(expr);
        auto ast = parser.parse();
        if (!ast) {
            throw std::invalid_argument("Failed to parse CEL expression: " + expr);
        }
        return impulse::cel::CelCompiler::to_impscheme(ast);
    }, py::arg("expression"), "Parse a Google CEL expression and compile to ImpScheme IR");

    m.def("optimize_cel", [](const std::string& expr) -> std::string {
        impulse::cel::Parser parser(expr);
        auto ast = parser.parse();
        if (!ast) {
            throw std::invalid_argument("Failed to parse CEL expression: " + expr);
        }
        auto optimized = impulse::cel::AstOptimizer::optimize(ast);
        return impulse::cel::CelCompiler::to_impscheme(optimized);
    }, py::arg("expression"), "Parse, optimize, and constant-fold a Google CEL expression into ImpScheme IR");

    auto pack_compile_result = [](const impulse::compiler::v11::ImpScmV11CompileResult& res) -> py::dict {
        py::dict out;
        out["impb_bytes"] = py::bytes(
            reinterpret_cast<const char*>(res.impb_bytes.data()),
            res.impb_bytes.size()
        );
        out["param_name_to_id"] = res.param_name_to_id;
        out["output_names"] = res.output_names;
        py::list pschema;
        for (const auto& pm : res.param_schema) {
            py::dict d;
            d["name"] = pm.name;
            d["param_id"] = pm.param_id;
            d["type"] = pm.type;
            d["is_decimal"] = pm.is_decimal;
            d["precision"] = pm.precision;
            d["scale"] = pm.scale;
            d["domain_id"] = pm.domain_id;
            pschema.append(d);
        }
        out["param_schema"] = pschema;
        py::list oschema;
        for (const auto& om : res.output_schema) {
            py::dict d;
            d["name"] = om.name;
            d["type"] = om.type;
            d["is_decimal"] = om.is_decimal;
            d["precision"] = om.precision;
            d["scale"] = om.scale;
            oschema.append(d);
        }
        out["output_schema"] = oschema;
        return out;
    };

    m.def("compile_impscm", [pack_compile_result](
        const std::string& source,
        const PyImpulseSnapshot* snapshot,
        bool enable_optimizations
    ) -> py::dict {
        const impulse_snapshot_t* s_ptr = snapshot ? snapshot->raw_handle() : nullptr;
        auto res = impulse::compiler::v11::ImpScmV11Compiler::compile(source, s_ptr, enable_optimizations);
        return pack_compile_result(res);
    }, py::arg("source"), py::arg("snapshot") = nullptr, py::arg("enable_optimizations") = true,
       "Compile an ImpScheme v0.11 (query ...) program into validated .impb bytecode with schema metadata");

    m.def("compile_cypher", [pack_compile_result](
        const std::string& cypher,
        const PyImpulseSnapshot* snapshot,
        const py::dict& param_types,
        bool enable_optimizations
    ) -> py::dict {
        std::unordered_map<std::string, impulse::compiler::CypherCompiler::ParamTypeSpec> pmap;
        for (auto item : param_types) {
            std::string key = py::str(item.first);
            py::handle val = item.second;
            impulse::compiler::CypherCompiler::ParamTypeSpec spec{};
            if (py::isinstance<py::dict>(val)) {
                py::dict vd = val.cast<py::dict>();
                spec.type = vd["type"].cast<std::string>();
                if (vd.contains("precision")) spec.precision = vd["precision"].cast<uint16_t>();
                if (vd.contains("scale")) spec.scale = vd["scale"].cast<uint8_t>();
                if (vd.contains("domain")) spec.domain_name = vd["domain"].cast<std::string>();
            } else if (py::isinstance<py::tuple>(val) || py::isinstance<py::list>(val)) {
                py::sequence seq = val.cast<py::sequence>();
                if (py::len(seq) >= 1) spec.type = seq[0].cast<std::string>();
                if (py::len(seq) >= 2 && spec.type == "DECIMAL") spec.precision = seq[1].cast<uint16_t>();
                if (py::len(seq) >= 3 && spec.type == "DECIMAL") spec.scale = seq[2].cast<uint8_t>();
                if (py::len(seq) >= 2 && (spec.type == "ID" || spec.type == "FRONTIER")) {
                    spec.domain_name = seq[1].cast<std::string>();
                }
            } else if (py::isinstance<py::str>(val)) {
                std::string s = val.cast<std::string>();
                std::string upper = s;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper.rfind("DECIMAL(", 0) == 0 && !upper.empty() && upper.back() == ')') {
                    std::string inner = upper.substr(8, upper.size() - 9);
                    auto comma = inner.find(',');
                    if (comma == std::string::npos) {
                        throw std::invalid_argument("Invalid DECIMAL(P, S) spec: " + s);
                    }
                    spec.type = "DECIMAL";
                    spec.precision = static_cast<uint16_t>(std::stoi(inner.substr(0, comma)));
                    spec.scale = static_cast<uint8_t>(std::stoi(inner.substr(comma + 1)));
                } else {
                    spec.type = upper;
                }
            }
            pmap[key] = std::move(spec);
        }
        std::string impscm_src = impulse::compiler::CypherCompiler::lower_to_impscm_v11(cypher, pmap);
        const impulse_snapshot_t* s_ptr = snapshot ? snapshot->raw_handle() : nullptr;
        auto res = impulse::compiler::v11::ImpScmV11Compiler::compile(impscm_src, s_ptr, enable_optimizations);
        py::dict out = pack_compile_result(res);
        out["impscm_source"] = impscm_src;
        return out;
    }, py::arg("cypher"), py::arg("snapshot") = nullptr, py::arg("param_types") = py::dict(),
       py::arg("enable_optimizations") = true,
       "Lower openCypher (with DECIMAL64 literals, CAST, and typed $params) to ImpScheme v0.11 and compile to .impb");
}
