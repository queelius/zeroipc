/**
 * ZeroIPC Table Layout - Single source of truth for binary-compatible table structs
 *
 * These structures define the on-disk/in-memory layout that must match
 * the C++, Go, and Python implementations exactly.
 *
 * Table Header: 32 bytes
 * Table Entry:  48 bytes
 */

#ifndef ZEROIPC_TABLE_LAYOUT_H
#define ZEROIPC_TABLE_LAYOUT_H

#include <stdint.h>

/* Table header - binary compatible with C++, Go, and Python (32 bytes) */
typedef struct {
    uint32_t magic;         /* 0x00: 0x5A49504D ('ZIPM') */
    uint32_t version;       /* 0x04: 1 */
    uint32_t entry_count;   /* 0x08: active entries */
    uint32_t max_entries;   /* 0x0C: max entries (C extension; C++/Go write 0 here) */
    uint64_t memory_size;   /* 0x10: total memory size */
    uint64_t next_offset;   /* 0x18: next allocation offset */
} zipc_table_header_t;      /* 32 bytes total */

/* Table entry - binary compatible with C++, Go, and Python (48 bytes) */
typedef struct {
    char     name[32];      /* 0x00: null-terminated name */
    uint64_t offset;        /* 0x20: offset from base */
    uint64_t size;          /* 0x28: allocated size */
} zipc_table_entry_t;       /* 48 bytes total */

#endif /* ZEROIPC_TABLE_LAYOUT_H */
