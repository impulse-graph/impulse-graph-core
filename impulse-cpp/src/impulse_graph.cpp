#include "impulse_graph.h"
#include <iostream>

struct impulse_snapshot {
    const char* path;
};

extern "C" {

impulse_snapshot_t* impulse_snapshot_open(const char* file_path) {
    auto* snapshot = new impulse_snapshot();
    snapshot->path = file_path;
    return snapshot;
}

void impulse_snapshot_close(impulse_snapshot_t* snapshot) {
    if (snapshot) {
        delete snapshot;
    }
}

bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
) {
    (void)snapshot;
    (void)src_domain;
    (void)src_id;
    (void)tgt_domain;
    (void)tgt_id;
    return true;
}

}
