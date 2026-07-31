#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
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
        if (snapshot_) {
            impulse_snapshot_close(snapshot_);
            snapshot_ = nullptr;
        }
    }

    bool is_reachable(uint16_t src_domain, uint32_t src_id, uint16_t tgt_domain, uint32_t tgt_id) const {
        if (!snapshot_) return false;
        return impulse_snapshot_is_reachable(snapshot_, src_domain, src_id, tgt_domain, tgt_id);
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
        if (writer_) {
            impulse_writer_destroy(writer_);
            writer_ = nullptr;
        }
    }

    void add_domain(uint16_t domain_id, uint8_t key_type, const std::string& name) {
        if (!writer_) return;
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
        const py::bytes& col_indices_bytes
    ) {
        if (!writer_) return;
        std::string bytes_str = col_indices_bytes;
        impulse_status_t status = impulse_writer_add_relation(
            writer_, src_domain_id, tgt_domain_id, encoding_type,
            node_count, edge_count, section_features,
            row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
            bytes_str.data(), bytes_str.size()
        );
        if (status != IMPULSE_OK) {
            throw std::runtime_error("Failed to add relation: " + std::string(impulse_get_last_error()));
        }
    }

    void finalize() {
        if (!writer_) return;
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
        .def("is_reachable", &PyImpulseSnapshot::is_reachable,
             py::arg("src_domain"), py::arg("src_id"), py::arg("tgt_domain"), py::arg("tgt_id"));

    py::class_<PyImpulseWriter>(m, "Writer")
        .def(py::init<const std::string&, uint64_t>(), py::arg("output_path"), py::arg("global_features") = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED)
        .def("add_domain", &PyImpulseWriter::add_domain, py::arg("domain_id"), py::arg("key_type"), py::arg("name"))
        .def("add_relation", &PyImpulseWriter::add_relation,
             py::arg("src_domain_id"), py::arg("tgt_domain_id"), py::arg("encoding_type"),
             py::arg("node_count"), py::arg("edge_count"), py::arg("section_features"),
             py::arg("row_offsets"), py::arg("col_indices_bytes"))
        .def("finalize", &PyImpulseWriter::finalize);
}
