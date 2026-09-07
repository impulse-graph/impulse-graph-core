/**
 * @file fuzz_snapshot.cpp
 * @brief Fuzzing harness for Impulse Graph binary snapshot parser and validator.
 *
 * Supports both:
 * 1. Native LLVM libFuzzer (`-fsanitize=fuzzer`) via LLVMFuzzerTestOneInput
 * 2. Standalone deterministic runner with seeded pseudo-random mutation battery.
 */

#include "impulse_graph.h"
#include "impulse_format_v0_9.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>
#include <string>
#include <chrono>

// Deterministic PRNG for standalone mode
namespace {
struct FuzzRng {
    uint64_t state;
    explicit FuzzRng(uint64_t seed = 0x853c49e6748fea9bULL) : state(seed ? seed : 1ULL) {}
    inline uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    inline uint32_t next_u32() {
        return static_cast<uint32_t>(next() >> 32);
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        return static_cast<uint32_t>(next() % bound);
    }
};
} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }

    // 1. Invariant: Validation must never crash, trap, or fault on any buffer
    impulse_status_t val_status = impulse_snapshot_validate_buffer(data, size);
    (void)val_status;

    // 2. Invariant: Opening buffer must never crash or leak
    impulse_status_t open_status = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open_buffer(data, size, &open_status);

    if (snap) {
        // If the buffer was parsed as a valid snapshot, all accessors must remain memory-safe
        uint32_t magic = impulse_snapshot_magic(snap);
        uint16_t ver = impulse_snapshot_version(snap);
        uint16_t dom_cnt = impulse_snapshot_domain_count(snap);
        uint16_t rel_cnt = impulse_snapshot_relation_count(snap);

        (void)magic;
        (void)ver;

        // Inspect domain entries
        uint16_t inspect_domains = dom_cnt > 16 ? 16 : dom_cnt;
        for (uint16_t d = 0; d < inspect_domains; ++d) {
            impulse_domain_catalog_entry_t dom_entry{};
            const char* dname = nullptr;
            impulse_snapshot_get_domain_entry(snap, d, &dom_entry, &dname);
        }

        // Inspect relation entries
        uint16_t inspect_relations = rel_cnt > 16 ? 16 : rel_cnt;
        for (uint16_t r = 0; r < inspect_relations; ++r) {
            impulse_relation_directory_entry_t rel_entry{};
            impulse_snapshot_get_relation_entry(snap, r, &rel_entry);

            // Test boundary reachability check without crashing
            impulse_snapshot_is_reachable(snap, r, 0, 0);
            impulse_snapshot_is_reachable(snap, r, 0xFFFFFFFFULL, 0xFFFFFFFFULL);
        }

        impulse_snapshot_close(snap);
    }

    return 0;
}

#if !defined(IMPULSE_LIBFUZZER_ENABLED)

// Baseline valid snapshot buffer for mutation fuzzing
static std::vector<uint8_t> create_baseline_snapshot() {
    std::vector<uint8_t> buffer(4096 + 1024, 0);

    impulse_snapshot_header_t* hdr = reinterpret_cast<impulse_snapshot_header_t*>(buffer.data());
    hdr->magic = IMPULSE_MAGIC;
    hdr->version = 9;
    hdr->data_offset = 4096;
    hdr->required_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
    hdr->domain_count = 1;
    hdr->relation_count = 0;
    hdr->index_count = 0;

    // Compute CRC16
    uint16_t crc = 0xFFFF;
    const uint8_t* data = buffer.data();
    for (size_t i = 0; i < 0x3E; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    hdr->header_checksum = crc;

    // String table at offset 4096: length 8, string pool "\0User\0\0"
    uint32_t str_bytes = 8;
    std::memcpy(buffer.data() + 4096, &str_bytes, 4);
    buffer[4096 + 4] = '\0';
    std::memcpy(buffer.data() + 4096 + 5, "User", 5);

    // Domain entry at 4096 + 128 (aligned):
    size_t dom_offset = 4096 + 128;
    impulse_domain_catalog_entry_t* dom = reinterpret_cast<impulse_domain_catalog_entry_t*>(buffer.data() + dom_offset);
    dom->domain_id = 0;
    dom->key_type = IMPULSE_KEY_TYPE_INT32;
    dom->reserved = 0;
    dom->name_offset = 1; // "User"
    dom->node_count = 10;

    return buffer;
}

int main(int argc, char** argv) {
    size_t iterations = 20000;
    uint64_t seed = 123456789ULL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << " Impulse Graph Snapshot Fuzzing Suite (Deterministic Mode)" << std::endl;
    std::cout << " Iterations: " << iterations << " | Seed: " << seed << std::endl;
    std::cout << "============================================================" << std::endl;

    FuzzRng rng(seed);
    const auto baseline = create_baseline_snapshot();
    auto start_time = std::chrono::steady_clock::now();

    size_t accepted_valid_count = 0;
    size_t rejected_error_count = 0;

    for (size_t iter = 0; iter < iterations; ++iter) {
        uint32_t mode = rng.bounded(5);
        std::vector<uint8_t> test_buffer;

        switch (mode) {
            case 0: {
                // Completely random noise of random length
                size_t len = rng.bounded(16384);
                test_buffer.resize(len);
                for (size_t b = 0; b < len; ++b) {
                    test_buffer[b] = static_cast<uint8_t>(rng.next_u32());
                }
                break;
            }
            case 1: {
                // Truncated slice of baseline
                size_t len = rng.bounded(static_cast<uint32_t>(baseline.size()));
                test_buffer.assign(baseline.begin(), baseline.begin() + len);
                break;
            }
            case 2: {
                // Mutate baseline by flipping random bits/bytes
                test_buffer = baseline;
                size_t mutations = 1 + rng.bounded(16);
                for (size_t m = 0; m < mutations; ++m) {
                    size_t idx = rng.bounded(static_cast<uint32_t>(test_buffer.size()));
                    test_buffer[idx] ^= static_cast<uint8_t>(rng.next_u32() & 0xFF);
                }
                break;
            }
            case 3: {
                // Mutate header fields (magic, version, offsets)
                test_buffer = baseline;
                uint32_t target_field = rng.bounded(6);
                if (target_field == 0) {
                    // Mutate magic
                    *reinterpret_cast<uint32_t*>(test_buffer.data()) = rng.next_u32();
                } else if (target_field == 1) {
                    // Mutate version
                    *reinterpret_cast<uint16_t*>(test_buffer.data() + 4) = static_cast<uint16_t>(rng.next_u32());
                } else if (target_field == 2) {
                    // Mutate data offset
                    *reinterpret_cast<uint32_t*>(test_buffer.data() + 6) = rng.next_u32();
                } else if (target_field == 3) {
                    // Mutate domain count
                    *reinterpret_cast<uint16_t*>(test_buffer.data() + 10) = static_cast<uint16_t>(rng.next_u32());
                } else if (target_field == 4) {
                    // Mutate checksum
                    *reinterpret_cast<uint16_t*>(test_buffer.data() + 62) = static_cast<uint16_t>(rng.next_u32());
                } else {
                    // Mutate required features
                    *reinterpret_cast<uint64_t*>(test_buffer.data() + 22) = rng.next();
                }
                break;
            }
            case 4: {
                // Append random garbage to baseline
                test_buffer = baseline;
                size_t extra = rng.bounded(4096);
                for (size_t e = 0; e < extra; ++e) {
                    test_buffer.push_back(static_cast<uint8_t>(rng.next_u32()));
                }
                break;
            }
        }

        impulse_status_t val_st = impulse_snapshot_validate_buffer(test_buffer.data(), test_buffer.size());
        if (val_st == IMPULSE_OK) {
            accepted_valid_count++;
        } else {
            rejected_error_count++;
        }

        LLVMFuzzerTestOneInput(test_buffer.data(), test_buffer.size());

        if ((iter + 1) % 5000 == 0 || iter + 1 == iterations) {
            std::cout << "[Fuzz Snapshot] Progress: " << (iter + 1) << " / " << iterations
                      << " | Valid: " << accepted_valid_count
                      << " | Rejected: " << rejected_error_count << std::endl;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "[Fuzz Snapshot] COMPLETED " << iterations << " iterations in "
              << duration_ms << " ms (" << (static_cast<double>(iterations) / (duration_ms / 1000.0))
              << " exec/sec). Zero crashes." << std::endl;

    return 0;
}
#endif
