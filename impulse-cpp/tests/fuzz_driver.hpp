/**
 * @file fuzz_driver.hpp
 * @brief Shared fuzz harness plumbing for the Impulse Graph Engine fuzz targets.
 *
 * Each target translation unit defines:
 *   - extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t)
 *   - static std::vector<impfuzz::Bytes> make_seeds()   (runtime-generated seeds; binary seeds are
 *                                                         never committed to Git, AGENTS.md 3.6)
 * and then invokes IMPULSE_FUZZ_MAIN("<target>", make_seeds, max_len).
 *
 * Two build modes:
 *   1. IMPULSE_LIBFUZZER_ENABLED (-fsanitize=fuzzer): the macro defines LLVMFuzzerInitialize. If the
 *      environment variable IMPULSE_FUZZ_SEED_DIR is set, make_seeds() output is written into that
 *      directory (libFuzzer loads corpus directories after LLVMFuzzerInitialize returns).
 *   2. Standalone (default): the macro defines main(). The driver is flag-compatible with the
 *      subset of libFuzzer flags used by CI: -runs=N, -max_total_time=S, -seed=N, -max_len=N.
 *      Positional arguments are corpus files/directories. Every seed and corpus file is executed
 *      unmutated first, then random mutations (bit flips, interesting values, insert/delete,
 *      block duplicate, splice) run until the run/time budget is exhausted.
 *      `--emit-seeds=DIR` writes the generated seeds to DIR and exits.
 *      On a fatal signal/sanitizer death the current input is saved as
 *      $IMPULSE_FUZZ_ARTIFACT_DIR/crash-<target>-<fnv1a64>.
 */

#ifndef IMPULSE_FUZZ_DRIVER_HPP
#define IMPULSE_FUZZ_DRIVER_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace impfuzz {

using Bytes = std::vector<uint8_t>;

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL) : state(seed ? seed : 1ULL) {}
    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    uint32_t bounded(uint32_t n) { return n ? static_cast<uint32_t>((next() >> 16) % n) : 0; }
};

inline uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

inline bool write_file(const std::string& path, const uint8_t* data, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t w = n ? std::fwrite(data, 1, n, f) : 0;
    std::fclose(f);
    return w == n;
}

inline bool read_file(const std::string& path, Bytes& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[4096];
    size_t r;
    out.clear();
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + r);
    std::fclose(f);
    return true;
}

inline void emit_seeds(const std::string& dir, const std::vector<Bytes>& seeds, const char* target) {
#if !defined(_WIN32)
    ::mkdir(dir.c_str(), 0755);
#endif
    for (size_t i = 0; i < seeds.size(); ++i) {
        write_file(dir + "/gen_" + target + "_" + std::to_string(i) + ".bin", seeds[i].data(), seeds[i].size());
    }
}

#if !defined(IMPULSE_LIBFUZZER_ENABLED)

namespace detail {
inline const uint8_t* g_cur_data = nullptr;
inline size_t g_cur_size = 0;
inline const char* g_target = "fuzz";

inline void save_current_input() {
#if !defined(_WIN32)
    static volatile sig_atomic_t in_handler = 0;
    if (in_handler || !g_cur_data) return;
    in_handler = 1;
    const char* dir = std::getenv("IMPULSE_FUZZ_ARTIFACT_DIR");
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/crash-%s-%016llx", dir ? dir : ".", g_target,
                  static_cast<unsigned long long>(fnv1a64(g_cur_data, g_cur_size)));
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (g_cur_size) {
            ssize_t ignored = ::write(fd, g_cur_data, g_cur_size);
            (void)ignored;
        }
        ::close(fd);
        const char msg[] = "fuzz: crashing input saved to ";
        ssize_t ignored = ::write(2, msg, sizeof(msg) - 1);
        ignored = ::write(2, path, std::strlen(path));
        ignored = ::write(2, "\n", 1);
        (void)ignored;
    }
#endif
}
#if !defined(_WIN32)
inline void on_signal(int sig) {
    save_current_input();
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}
#endif
}  // namespace detail
}  // namespace impfuzz

namespace impfuzz {

inline void mutate(Bytes& b, const std::vector<Bytes>& pool, Rng& rng, size_t max_len) {
    static const uint8_t k8[] = {0x00, 0x01, 0x02, 0x3F, 0x40, 0x41, 0x7F, 0x80, 0xC0, 0xCB, 0xCC, 0xFE, 0xFF};
    static const uint32_t k32[] = {0u, 1u, 2u, 63u, 64u, 65u, 0x7FFFu, 0x8000u, 0xFFFFu, 0x10000u,
                                   0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    int rounds = 1 + static_cast<int>(rng.bounded(4));
    for (int r = 0; r < rounds; ++r) {
        switch (rng.bounded(9)) {
            case 0:
                if (!b.empty()) b[rng.bounded(b.size())] ^= static_cast<uint8_t>(1u << rng.bounded(8));
                break;
            case 1:
                if (!b.empty()) b[rng.bounded(b.size())] = static_cast<uint8_t>(rng.next());
                break;
            case 2:
                if (!b.empty()) b[rng.bounded(b.size())] = k8[rng.bounded(sizeof(k8))];
                break;
            case 3:
                if (b.size() >= 4) {
                    uint32_t v = k32[rng.bounded(sizeof(k32) / sizeof(k32[0]))];
                    std::memcpy(&b[rng.bounded(b.size() - 3)], &v, 4);
                }
                break;
            case 4:
                if (b.size() < max_len) b.insert(b.begin() + rng.bounded(b.size() + 1), static_cast<uint8_t>(rng.next()));
                break;
            case 5:
                if (b.size() > 1) b.erase(b.begin() + rng.bounded(b.size()));
                break;
            case 6:
                if (b.size() >= 2) {
                    size_t len = 1 + rng.bounded(static_cast<uint32_t>(b.size() < 64 ? b.size() : 64));
                    if (len > b.size()) len = b.size();
                    size_t from = rng.bounded(static_cast<uint32_t>(b.size() - len + 1));
                    Bytes blk(b.begin() + from, b.begin() + from + len);
                    if (b.size() + len <= max_len) b.insert(b.begin() + rng.bounded(b.size() + 1), blk.begin(), blk.end());
                }
                break;
            case 7:
                if (!pool.empty() && !b.empty()) {
                    const Bytes& o = pool[rng.bounded(pool.size())];
                    if (!o.empty()) {
                        size_t cut = rng.bounded(b.size());
                        size_t ocut = rng.bounded(o.size());
                        b.resize(cut);
                        b.insert(b.end(), o.begin() + ocut, o.end());
                    }
                }
                break;
            default:
                if (b.size() >= 8) {
                    uint64_t v = rng.next();
                    std::memcpy(&b[rng.bounded(b.size() - 7)], &v, 8);
                }
                break;
        }
    }
    if (b.size() > max_len) b.resize(max_len);
}

inline void collect_corpus(const std::string& path, std::vector<Bytes>& out) {
#if !defined(_WIN32)
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = ::opendir(path.c_str());
        if (!d) return;
        std::vector<std::string> names;
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] != '.') names.emplace_back(e->d_name);
        }
        ::closedir(d);
        for (const auto& n : names) collect_corpus(path + "/" + n, out);
    } else {
        Bytes b;
        if (read_file(path, b)) out.push_back(std::move(b));
    }
#else
    Bytes b;
    if (read_file(path, b)) out.push_back(std::move(b));
#endif
}

inline int standalone_main(int argc, char** argv, const char* target, std::vector<Bytes> (*make_seeds)(),
                           size_t default_max_len) {
    detail::g_target = target;
    uint64_t runs = 3000;
    double max_time = 0;
    uint64_t seed = 0x5eed5eed12345ULL;
    size_t max_len = default_max_len;
    std::string emit_dir;
    std::vector<std::string> corpus_paths;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("-runs=", 0) == 0) runs = std::strtoull(a.c_str() + 6, nullptr, 10);
        else if (a.rfind("-max_total_time=", 0) == 0) { max_time = std::atof(a.c_str() + 16); runs = ~0ULL; }
        else if (a.rfind("-seed=", 0) == 0) seed = std::strtoull(a.c_str() + 6, nullptr, 10);
        else if (a.rfind("-max_len=", 0) == 0) max_len = std::strtoull(a.c_str() + 9, nullptr, 10);
        else if (a.rfind("--emit-seeds=", 0) == 0) emit_dir = a.substr(13);
        else if (!a.empty() && a[0] == '-') { /* ignore other libFuzzer flags */ }
        else corpus_paths.push_back(a);
    }

    std::vector<Bytes> pool = make_seeds();
    if (!emit_dir.empty()) {
        emit_seeds(emit_dir, pool, target);
        std::printf("%s: emitted %zu generated seeds to %s\n", target, pool.size(), emit_dir.c_str());
        return 0;
    }
    size_t builtin = pool.size();
    for (const auto& p : corpus_paths) collect_corpus(p, pool);

#if !defined(_WIN32)
    using DeathCbFn = void (*)(void (*)(void));
    if (auto set_cb = reinterpret_cast<DeathCbFn>(::dlsym(RTLD_DEFAULT, "__sanitizer_set_death_callback"))) {
        // Sanitizer runtimes own SIGSEGV/SIGABRT etc. (they print the report first); hook their death callback.
        set_cb(detail::save_current_input);
    } else {
        for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) ::signal(sig, detail::on_signal);
    }
#endif

    auto now = [] {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
    };
    double t0 = now();
    uint64_t execs = 0;
    auto run_one = [&](const Bytes& b) {
        detail::g_cur_data = b.data();
        detail::g_cur_size = b.size();
        LLVMFuzzerTestOneInput(b.data(), b.size());
        detail::g_cur_data = nullptr;
        ++execs;
    };

    for (const auto& s : pool) run_one(s);
    Rng rng(seed);
    Bytes cur;
    while (!pool.empty()) {
        if (max_time > 0) {
            if ((execs & 63) == 0 && now() - t0 >= max_time) break;
        } else if (execs >= runs + pool.size()) {
            break;
        }
        cur = pool[rng.bounded(static_cast<uint32_t>(pool.size()))];
        mutate(cur, pool, rng, max_len);
        run_one(cur);
        // Feed a small fraction of mutants back so deeper structures get explored.
        if (rng.bounded(64) == 0 && pool.size() < builtin + 4096) pool.push_back(cur);
    }
    double dt = now() - t0;
    std::printf("%s: standalone driver done: seeds=%zu (generated=%zu) execs=%llu time=%.2fs (%.0f exec/s) seed=%llu\n",
                target, pool.size(), builtin, static_cast<unsigned long long>(execs), dt,
                dt > 0 ? static_cast<double>(execs) / dt : 0.0, static_cast<unsigned long long>(seed));
    return 0;
}

}  // namespace impfuzz

#define IMPULSE_FUZZ_MAIN(NAME, MAKE_SEEDS, MAX_LEN)                                              \
    int main(int argc, char** argv) {                                                             \
        return impfuzz::standalone_main(argc, argv, NAME, MAKE_SEEDS, MAX_LEN);                   \
    }

#else  // IMPULSE_LIBFUZZER_ENABLED

}  // namespace impfuzz

#define IMPULSE_FUZZ_MAIN(NAME, MAKE_SEEDS, MAX_LEN)                                              \
    extern "C" int LLVMFuzzerInitialize(int*, char***) {                                          \
        if (const char* d = std::getenv("IMPULSE_FUZZ_SEED_DIR")) {                               \
            impfuzz::emit_seeds(d, MAKE_SEEDS(), NAME);                                           \
        }                                                                                         \
        return 0;                                                                                 \
    }

#endif  // IMPULSE_LIBFUZZER_ENABLED

#endif  // IMPULSE_FUZZ_DRIVER_HPP
