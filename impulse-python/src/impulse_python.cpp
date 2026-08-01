#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "impulse_graph.h"
#include <string>
#include <vector>
#include <stdexcept>

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

    bool is_reachable(uint16_t src_domain, uint32_t src_id, uint16_t tgt_domain, uint32_t tgt_id) const {
        if (!snapshot_) return false;
        return impulse_snapshot_is_reachable(snapshot_, src_domain, src_id, tgt_domain, tgt_id);
    }

    uint16_t domain_count() const {
        return snapshot_ ? impulse_snapshot_domain_count(snapshot_) : 0;
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
        res["encoding_type"] = entry.encoding_type;
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

    py::tuple sample_neighbors(uint16_t relation_index, const std::vector<uint32_t>& nodes, int k_samples, uint64_t seed) const {
        if (!snapshot_) throw std::runtime_error("Snapshot is closed");
        size_t out_count = 0;
        impulse_status_t status = impulse_snapshot_sample_neighbors(
            snapshot_, relation_index, nodes.data(), nodes.size(), k_samples, seed, nullptr, nullptr, &out_count
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to dry-run sample_neighbors: " + std::string(impulse_get_last_error()));
        }

        py::array_t<uint32_t> src_arr(out_count);
        py::array_t<uint32_t> tgt_arr(out_count);

        if (out_count > 0) {
            status = impulse_snapshot_sample_neighbors(
                snapshot_, relation_index, nodes.data(), nodes.size(), k_samples, seed,
                src_arr.mutable_data(), tgt_arr.mutable_data(), &out_count
            );
            if (status != IMPULSE_OK) {
                throw std::runtime_error("Failed to execute sample_neighbors: " + std::string(impulse_get_last_error()));
            }
        }
        return py::make_tuple(src_arr, tgt_arr);
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

PYBIND11_MODULE(_impulse_native, m) {
    m.doc() = "Impulse Graph Engine Native C-ABI Python Extension";

    py::class_<PyImpulseSnapshot>(m, "Snapshot")
        .def(py::init<const std::string&>(), py::arg("path"))
        .def("close", &PyImpulseSnapshot::close)
        .def("__enter__", [](PyImpulseSnapshot& self) { return &self; })
        .def("__exit__", [](PyImpulseSnapshot& self, py::object, py::object, py::object) { self.close(); })
        .def("domain_count", &PyImpulseSnapshot::domain_count)
        .def("relation_count", &PyImpulseSnapshot::relation_count)
        .def("get_relation", &PyImpulseSnapshot::get_relation, py::arg("index"))
        .def("get_buffer", &PyImpulseSnapshot::get_buffer, py::arg("offset"), py::arg("size"))
        .def("get_csr_row_offsets", &PyImpulseSnapshot::get_csr_row_offsets, py::arg("index"))
        .def("get_csr_col_indices", &PyImpulseSnapshot::get_csr_col_indices, py::arg("index"))
        .def("sample_neighbors", &PyImpulseSnapshot::sample_neighbors,
             py::arg("relation_index"), py::arg("nodes"), py::arg("k_samples"), py::arg("seed") = 42)
        .def("is_reachable", &PyImpulseSnapshot::is_reachable,
             py::arg("src_domain"), py::arg("src_id"), py::arg("tgt_domain"), py::arg("tgt_id"));

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
