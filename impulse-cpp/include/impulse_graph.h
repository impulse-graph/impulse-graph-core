#ifndef IMPULSE_GRAPH_H
#define IMPULSE_GRAPH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern C {
#endif

#define IMPULSE_MAGIC 0x494D5053
#define IMPULSE_VERSION 2

typedef struct impulse_snapshot impulse_snapshot_t;
typedef struct impulse_graph impulse_graph_t;
typedef struct impulse_query impulse_query_t;

// Snapshot C-ABI API
impulse_snapshot_t* impulse_snapshot_open(const char* file_path);
void impulse_snapshot_close(impulse_snapshot_t* snapshot);
bool impulse_snapshot_is_reachable(
    const impulse_snapshot_t* snapshot,
    uint16_t src_domain, uint32_t src_id,
    uint16_t tgt_domain, uint32_t tgt_id
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_GRAPH_H
