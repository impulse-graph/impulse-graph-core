/**
 * @file impulse_hexio.h
 * @brief Binary Snapshot Hex I/O and Corruption Patching Utilities
 *        (modeled on SQLite's src/test_hexio.c).
 *
 * Enables tests to build a valid .imps snapshot via official Impulse Graph APIs
 * and apply targeted byte-level hex reads, writes, and little-endian integer
 * patches in memory or on disk without committing binary blobs to Git (Rule 3.6 & 3.17).
 */

#ifndef IMPULSE_HEXIO_H
#define IMPULSE_HEXIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int impulse_hexio_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Reads `count` bytes from `buf` (of size `buf_len`) at `offset` and formats as
 * uppercase hexadecimal into `out_hex` (which must have capacity >= 2 * count + 1).
 * Returns the number of bytes read, or -1 on out-of-bounds.
 */
static inline int impulse_hexio_read_buf(const uint8_t* buf, size_t buf_len,
                                         size_t offset, size_t count,
                                         char* out_hex, size_t out_hex_cap) {
    static const char kHexDigits[] = "0123456789ABCDEF";
    if (!buf || !out_hex || offset + count > buf_len || out_hex_cap < count * 2 + 1) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        uint8_t b = buf[offset + i];
        out_hex[i * 2]     = kHexDigits[(b >> 4) & 0x0F];
        out_hex[i * 2 + 1] = kHexDigits[b & 0x0F];
    }
    out_hex[count * 2] = '\0';
    return (int)count;
}

/**
 * Decodes `hex_str` (ignoring ASCII whitespace) and writes the resulting bytes
 * into `buf` (of size `buf_len`) starting at `offset`.
 * Returns the number of bytes written, or -1 on invalid hex or out-of-bounds.
 */
static inline int impulse_hexio_write_buf(uint8_t* buf, size_t buf_len,
                                          size_t offset, const char* hex_str) {
    if (!buf || !hex_str || offset > buf_len) return -1;
    size_t written = 0;
    const char* p = hex_str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == '\0') break;
        int hi = impulse_hexio_nibble(p[0]);
        int lo = (p[0] != '\0') ? impulse_hexio_nibble(p[1]) : -1;
        if (hi < 0 || lo < 0) return -1;
        if (offset + written >= buf_len) return -1;
        buf[offset + written] = (uint8_t)((hi << 4) | lo);
        ++written;
        p += 2;
    }
    return (int)written;
}

/**
 * Reads `count` bytes from file `path` at `offset` and writes uppercase hex into `out_hex`.
 */
static inline int impulse_hexio_read_file(const char* path, size_t offset, size_t count,
                                          char* out_hex, size_t out_hex_cap) {
    if (!path || !out_hex || out_hex_cap < count * 2 + 1) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    static const char kHexDigits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < count; ++i) {
        int c = fgetc(f);
        if (c == EOF) {
            fclose(f);
            return -1;
        }
        uint8_t b = (uint8_t)c;
        out_hex[i * 2]     = kHexDigits[(b >> 4) & 0x0F];
        out_hex[i * 2 + 1] = kHexDigits[b & 0x0F];
    }
    out_hex[count * 2] = '\0';
    fclose(f);
    return (int)count;
}

/**
 * Patches file `path` in-place at byte `offset` with the decoded bytes of `hex_str`.
 */
static inline int impulse_hexio_write_file(const char* path, size_t offset, const char* hex_str) {
    if (!path || !hex_str) return -1;
    FILE* f = fopen(path, "r+b");
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    size_t written = 0;
    const char* p = hex_str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (*p == '\0') break;
        int hi = impulse_hexio_nibble(p[0]);
        int lo = (p[0] != '\0') ? impulse_hexio_nibble(p[1]) : -1;
        if (hi < 0 || lo < 0) {
            fclose(f);
            return -1;
        }
        uint8_t b = (uint8_t)((hi << 4) | lo);
        if (fputc(b, f) == EOF) {
            fclose(f);
            return -1;
        }
        ++written;
        p += 2;
    }
    fflush(f);
    fclose(f);
    return (int)written;
}

/** Renders a 16-bit integer as 4-char uppercase Little-Endian hex (e.g., 0x0009 -> "0900"). */
static inline void impulse_hexio_render_u16_le(uint16_t val, char out_hex[5]) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(val & 0xFFu);
    bytes[1] = (uint8_t)((val >> 8) & 0xFFu);
    impulse_hexio_read_buf(bytes, 2, 0, 2, out_hex, 5);
}

/** Renders a 32-bit integer as 8-char uppercase Little-Endian hex. */
static inline void impulse_hexio_render_u32_le(uint32_t val, char out_hex[9]) {
    uint8_t bytes[4];
    for (int i = 0; i < 4; ++i) bytes[i] = (uint8_t)((val >> (i * 8)) & 0xFFu);
    impulse_hexio_read_buf(bytes, 4, 0, 4, out_hex, 9);
}

/** Renders a 64-bit integer as 16-char uppercase Little-Endian hex. */
static inline void impulse_hexio_render_u64_le(uint64_t val, char out_hex[17]) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = (uint8_t)((val >> (i * 8)) & 0xFFu);
    impulse_hexio_read_buf(bytes, 8, 0, 8, out_hex, 17);
}

#ifdef __cplusplus
}
#endif

#endif /* IMPULSE_HEXIO_H */
