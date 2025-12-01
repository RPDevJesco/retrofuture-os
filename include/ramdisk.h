/**
 * ramdisk.h - In-Memory File System for RETROFUTURE OS
 *
 * A simple RAM-based filesystem for development and experimentation.
 * Files persist only until reboot.
 *
 * Features:
 *   - Create, read, write, append, delete files
 *   - Simple directory listing
 *   - File flags (readonly, hidden, executable, etc.)
 *   - Works with editor and assembler (future)
 *   - Copy between FAT12 and RAM disk
 */

#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define RAMDISK_MAX_FILES       128
#define RAMDISK_MAX_FILENAME    32
#define RAMDISK_SIZE            (1024 * 1024)   /* 1MB RAM disk */
#define RAMDISK_BLOCK_SIZE      512

/* Data area location - must be in usable memory above kernel */
#define RAMDISK_DATA_ADDR       0x400000        /* 4MB mark */

/* ============================================================================
 * File Flags
 * ============================================================================ */

#define RAMDISK_FILE_FREE       0x00
#define RAMDISK_FILE_USED       0x01
#define RAMDISK_FILE_DIRECTORY  0x02
#define RAMDISK_FILE_READONLY   0x04
#define RAMDISK_FILE_HIDDEN     0x08
#define RAMDISK_FILE_SYSTEM     0x10
#define RAMDISK_FILE_EXECUTABLE 0x20
#define RAMDISK_FILE_BINARY     0x40

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    RAMDISK_OK = 0,
    RAMDISK_ERR_NOT_INIT,
    RAMDISK_ERR_NOT_FOUND,
    RAMDISK_ERR_EXISTS,
    RAMDISK_ERR_NO_SPACE,
    RAMDISK_ERR_NO_SLOTS,
    RAMDISK_ERR_READONLY,
    RAMDISK_ERR_INVALID,
    RAMDISK_ERR_NOT_OPEN,
    RAMDISK_ERR_TOO_LARGE
} ramdisk_error_t;

/* ============================================================================
 * File Entry Structure
 * ============================================================================ */

typedef struct {
    char        name[RAMDISK_MAX_FILENAME];
    uint32_t    size;           /* File size in bytes */
    uint32_t    offset;         /* Offset into data area */
    uint32_t    allocated;      /* Allocated size (may be > size) */
    uint32_t    flags;
    uint32_t    created;        /* Timestamp (ticks since boot) */
    uint32_t    modified;       /* Last modified timestamp */
} ramdisk_file_t;

/* ============================================================================
 * RAM Disk Structure
 * ============================================================================ */

typedef struct {
    /* File table */
    ramdisk_file_t  files[RAMDISK_MAX_FILES];
    uint32_t        file_count;

    /* Data storage */
    uint8_t        *data;           /* Points to data area */
    uint32_t        data_size;      /* Total data area size */
    uint32_t        data_used;      /* Bytes allocated (includes fragmentation) */
    uint32_t        data_ptr;       /* Next free offset (simple bump allocator) */

    /* Statistics */
    uint32_t        total_reads;
    uint32_t        total_writes;
    uint32_t        total_created;
    uint32_t        total_deleted;
    uint32_t        bytes_read;
    uint32_t        bytes_written;

    /* State */
    bool            initialized;
    ramdisk_error_t last_error;
} ramdisk_t;

/* ============================================================================
 * File Handle for Read/Write Operations
 * ============================================================================ */

typedef struct {
    ramdisk_t      *disk;
    ramdisk_file_t *file;
    uint32_t        position;       /* Current read/write position */
    uint32_t        file_index;     /* Index in file table */
    bool            open;
    bool            write_mode;
    bool            append_mode;
} ramdisk_handle_t;

/* ============================================================================
 * Directory Iterator
 * ============================================================================ */

typedef struct {
    ramdisk_t      *disk;
    uint32_t        index;
    bool            show_hidden;
} ramdisk_dir_t;

/* ============================================================================
 * Statistics Structure
 * ============================================================================ */

typedef struct {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t file_count;
    uint32_t max_files;
    uint32_t total_reads;
    uint32_t total_writes;
    uint32_t bytes_read;
    uint32_t bytes_written;
    uint32_t fragmented_bytes;  /* Space lost to deleted files */
} ramdisk_stats_t;

/* ============================================================================
 * Global RAM Disk Instance
 * ============================================================================ */

static ramdisk_t g_ramdisk;

/* Tick counter - should be updated by timer interrupt */
static volatile uint32_t g_ramdisk_ticks = 0;

/* ============================================================================
 * String/Memory Helpers (kernel doesn't have string.h)
 * ============================================================================ */

static inline int rd_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static inline int rd_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int rd_strncmp(const char *a, const char *b, int n) {
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int rd_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline void rd_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static inline void rd_strncpy(char *dst, const char *src, int n) {
    while (n-- > 0 && *src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

static inline void rd_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static inline void rd_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = val;
}

static inline int rd_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/**
 * Initialize the RAM disk
 */
static inline bool ramdisk_init(ramdisk_t *rd, void *data_area, uint32_t size) {
    if (!rd || !data_area || size < RAMDISK_BLOCK_SIZE) {
        return false;
    }

    /* Clear file table */
    rd_memset(rd->files, 0, sizeof(rd->files));
    rd->file_count = 0;

    /* Set up data area */
    rd->data = (uint8_t *)data_area;
    rd->data_size = size;
    rd->data_used = 0;
    rd->data_ptr = 0;

    /* Clear data area */
    rd_memset(rd->data, 0, size);

    /* Reset statistics */
    rd->total_reads = 0;
    rd->total_writes = 0;
    rd->total_created = 0;
    rd->total_deleted = 0;
    rd->bytes_read = 0;
    rd->bytes_written = 0;

    rd->last_error = RAMDISK_OK;
    rd->initialized = true;

    return true;
}

/**
 * Initialize global RAM disk at default location
 */
static inline bool ramdisk_init_global(void) {
    return ramdisk_init(&g_ramdisk, (void *)RAMDISK_DATA_ADDR, RAMDISK_SIZE);
}

/**
 * Get last error
 */
static inline ramdisk_error_t ramdisk_get_error(ramdisk_t *rd) {
    return rd ? rd->last_error : RAMDISK_ERR_INVALID;
}

/**
 * Get error string
 */
static inline const char *ramdisk_error_str(ramdisk_error_t err) {
    switch (err) {
        case RAMDISK_OK:            return "OK";
        case RAMDISK_ERR_NOT_INIT:  return "Not initialized";
        case RAMDISK_ERR_NOT_FOUND: return "File not found";
        case RAMDISK_ERR_EXISTS:    return "File exists";
        case RAMDISK_ERR_NO_SPACE:  return "No space left";
        case RAMDISK_ERR_NO_SLOTS:  return "No free file slots";
        case RAMDISK_ERR_READONLY:  return "File is read-only";
        case RAMDISK_ERR_INVALID:   return "Invalid argument";
        case RAMDISK_ERR_NOT_OPEN:  return "File not open";
        case RAMDISK_ERR_TOO_LARGE: return "File too large";
        default:                    return "Unknown error";
    }
}

/**
 * Find a file by name (case-insensitive)
 * Returns file index or -1 if not found
 */
static inline int ramdisk_find(ramdisk_t *rd, const char *name) {
    if (!rd || !name || !rd->initialized) {
        if (rd) rd->last_error = RAMDISK_ERR_NOT_INIT;
        return -1;
    }

    for (uint32_t i = 0; i < RAMDISK_MAX_FILES; i++) {
        if ((rd->files[i].flags & RAMDISK_FILE_USED) &&
            rd_strcasecmp(rd->files[i].name, name) == 0) {
            return (int)i;
        }
    }

    rd->last_error = RAMDISK_ERR_NOT_FOUND;
    return -1;
}

/**
 * Find a free file slot
 */
static inline int ramdisk_find_free_slot(ramdisk_t *rd) {
    for (uint32_t i = 0; i < RAMDISK_MAX_FILES; i++) {
        if (!(rd->files[i].flags & RAMDISK_FILE_USED)) {
            return (int)i;
        }
    }
    rd->last_error = RAMDISK_ERR_NO_SLOTS;
    return -1;
}

/**
 * Check if file exists
 */
static inline bool ramdisk_exists(ramdisk_t *rd, const char *name) {
    int idx = ramdisk_find(rd, name);
    if (idx >= 0) {
        rd->last_error = RAMDISK_OK;
        return true;
    }
    return false;
}

/**
 * Create a new file
 */
static inline bool ramdisk_create(ramdisk_t *rd, const char *name, uint32_t flags) {
    if (!rd || !name || !rd->initialized) {
        if (rd) rd->last_error = RAMDISK_ERR_NOT_INIT;
        return false;
    }

    /* Validate name */
    int len = rd_strlen(name);
    if (len == 0 || len >= RAMDISK_MAX_FILENAME) {
        rd->last_error = RAMDISK_ERR_INVALID;
        return false;
    }

    /* Check if already exists */
    if (ramdisk_exists(rd, name)) {
        rd->last_error = RAMDISK_ERR_EXISTS;
        return false;
    }

    /* Find free slot */
    int slot = ramdisk_find_free_slot(rd);
    if (slot < 0) return false;

    /* Initialize file entry */
    ramdisk_file_t *f = &rd->files[slot];
    rd_memset(f, 0, sizeof(*f));
    rd_strncpy(f->name, name, RAMDISK_MAX_FILENAME - 1);
    f->size = 0;
    f->offset = 0;          /* Will be set on first write */
    f->allocated = 0;
    f->flags = RAMDISK_FILE_USED | flags;
    f->created = g_ramdisk_ticks;
    f->modified = g_ramdisk_ticks;

    rd->file_count++;
    rd->total_created++;
    rd->last_error = RAMDISK_OK;

    return true;
}

/**
 * Delete a file
 */
static inline bool ramdisk_delete(ramdisk_t *rd, const char *name) {
    if (!rd || !name || !rd->initialized) {
        if (rd) rd->last_error = RAMDISK_ERR_NOT_INIT;
        return false;
    }

    int idx = ramdisk_find(rd, name);
    if (idx < 0) return false;

    ramdisk_file_t *f = &rd->files[idx];

    /* Check if readonly */
    if (f->flags & RAMDISK_FILE_READONLY) {
        rd->last_error = RAMDISK_ERR_READONLY;
        return false;
    }

    /* Mark as free
     * Note: data space is not reclaimed in this simple implementation.
     * A compaction function could be added later.
     */
    f->flags = RAMDISK_FILE_FREE;
    f->name[0] = '\0';

    rd->file_count--;
    rd->total_deleted++;
    rd->last_error = RAMDISK_OK;

    return true;
}

/**
 * Open a file for reading or writing
 */
static inline bool ramdisk_open(ramdisk_t *rd, const char *name,
                                 ramdisk_handle_t *handle, bool write_mode) {
    if (!rd || !name || !handle || !rd->initialized) {
        if (rd) rd->last_error = RAMDISK_ERR_INVALID;
        return false;
    }

    /* Clear handle */
    rd_memset(handle, 0, sizeof(*handle));

    int idx = ramdisk_find(rd, name);

    if (write_mode && idx < 0) {
        /* Create file if opening for write and doesn't exist */
        if (!ramdisk_create(rd, name, 0)) return false;
        idx = ramdisk_find(rd, name);
        if (idx < 0) return false;
    }

    if (idx < 0) return false;

    ramdisk_file_t *f = &rd->files[idx];

    /* Check permissions */
    if (write_mode && (f->flags & RAMDISK_FILE_READONLY)) {
        rd->last_error = RAMDISK_ERR_READONLY;
        return false;
    }

    /* Set up handle */
    handle->disk = rd;
    handle->file = f;
    handle->file_index = idx;
    handle->position = 0;
    handle->open = true;
    handle->write_mode = write_mode;
    handle->append_mode = false;

    rd->last_error = RAMDISK_OK;
    return true;
}

/**
 * Open a file for appending
 */
static inline bool ramdisk_open_append(ramdisk_t *rd, const char *name,
                                        ramdisk_handle_t *handle) {
    if (!ramdisk_open(rd, name, handle, true)) {
        return false;
    }

    /* Seek to end */
    handle->position = handle->file->size;
    handle->append_mode = true;

    return true;
}

/**
 * Close a file handle
 */
static inline void ramdisk_close(ramdisk_handle_t *handle) {
    if (handle) {
        handle->open = false;
        handle->file = NULL;
        handle->disk = NULL;
    }
}

/**
 * Read from a file
 */
static inline uint32_t ramdisk_read(ramdisk_handle_t *handle, void *buf, uint32_t size) {
    if (!handle || !handle->open || !buf || size == 0) {
        return 0;
    }

    ramdisk_file_t *f = handle->file;
    ramdisk_t *rd = handle->disk;

    /* Check bounds */
    if (handle->position >= f->size) {
        return 0;
    }

    /* Clamp read size */
    uint32_t available = f->size - handle->position;
    if (size > available) size = available;

    /* Copy data */
    rd_memcpy(buf, &rd->data[f->offset + handle->position], size);
    handle->position += size;

    rd->total_reads++;
    rd->bytes_read += size;
    rd->last_error = RAMDISK_OK;

    return size;
}

/**
 * Write to a file
 */
static inline uint32_t ramdisk_write(ramdisk_handle_t *handle, const void *buf, uint32_t size) {
    if (!handle || !handle->open || !handle->write_mode || !buf || size == 0) {
        return 0;
    }

    ramdisk_file_t *f = handle->file;
    ramdisk_t *rd = handle->disk;

    uint32_t new_end = handle->position + size;

    /* Need to allocate/extend space? */
    if (f->allocated == 0 || new_end > f->allocated) {
        /* Calculate needed space (round up to block size) */
        uint32_t needed = (new_end + RAMDISK_BLOCK_SIZE - 1) & ~(RAMDISK_BLOCK_SIZE - 1);

        /* Add some extra space for future growth */
        if (needed < 4096) needed = 4096;

        /* Check if we have space */
        if (rd->data_ptr + needed > rd->data_size) {
            rd->last_error = RAMDISK_ERR_NO_SPACE;
            return 0;
        }

        /* If file already has data, copy it to new location */
        if (f->size > 0 && f->allocated > 0) {
            rd_memcpy(&rd->data[rd->data_ptr], &rd->data[f->offset], f->size);
        }

        f->offset = rd->data_ptr;
        f->allocated = needed;
        rd->data_ptr += needed;
        rd->data_used += needed;
    }

    /* Write data */
    rd_memcpy(&rd->data[f->offset + handle->position], buf, size);
    handle->position += size;

    /* Update file size if we extended it */
    if (handle->position > f->size) {
        f->size = handle->position;
    }

    f->modified = g_ramdisk_ticks;
    rd->total_writes++;
    rd->bytes_written += size;
    rd->last_error = RAMDISK_OK;

    return size;
}

/**
 * Write a single byte
 */
static inline bool ramdisk_putc(ramdisk_handle_t *handle, char c) {
    return ramdisk_write(handle, &c, 1) == 1;
}

/**
 * Write a string (without null terminator)
 */
static inline uint32_t ramdisk_puts(ramdisk_handle_t *handle, const char *s) {
    return ramdisk_write(handle, s, rd_strlen(s));
}

/**
 * Write a line (string + newline)
 */
static inline uint32_t ramdisk_putline(ramdisk_handle_t *handle, const char *s) {
    uint32_t written = ramdisk_puts(handle, s);
    written += ramdisk_write(handle, "\n", 1);
    return written;
}

/**
 * Read a single byte
 */
static inline int ramdisk_getc(ramdisk_handle_t *handle) {
    char c;
    if (ramdisk_read(handle, &c, 1) == 1) {
        return (unsigned char)c;
    }
    return -1;
}

/**
 * Read a line (up to newline or max bytes)
 * Returns length of line (excluding newline), or -1 on EOF
 */
static inline int ramdisk_getline(ramdisk_handle_t *handle, char *buf, uint32_t max) {
    uint32_t i = 0;
    int c;

    while (i < max - 1) {
        c = ramdisk_getc(handle);
        if (c < 0) {
            /* EOF */
            if (i == 0) return -1;
            break;
        }
        if (c == '\n') break;
        if (c == '\r') continue;  /* Skip CR */
        buf[i++] = (char)c;
    }

    buf[i] = '\0';
    return (int)i;
}

/**
 * Seek to position in file
 */
static inline bool ramdisk_seek(ramdisk_handle_t *handle, uint32_t position) {
    if (!handle || !handle->open) {
        return false;
    }

    /* For read mode, can't seek past end */
    if (!handle->write_mode && position > handle->file->size) {
        return false;
    }

    handle->position = position;
    return true;
}

/**
 * Seek relative to current position
 */
static inline bool ramdisk_seek_rel(ramdisk_handle_t *handle, int32_t offset) {
    if (!handle || !handle->open) return false;

    int64_t new_pos = (int64_t)handle->position + offset;
    if (new_pos < 0) new_pos = 0;

    return ramdisk_seek(handle, (uint32_t)new_pos);
}

/**
 * Seek to end of file
 */
static inline bool ramdisk_seek_end(ramdisk_handle_t *handle) {
    if (!handle || !handle->open) return false;
    handle->position = handle->file->size;
    return true;
}

/**
 * Get current position
 */
static inline uint32_t ramdisk_tell(ramdisk_handle_t *handle) {
    if (!handle || !handle->open) return 0;
    return handle->position;
}

/**
 * Get file size (from handle)
 */
static inline uint32_t ramdisk_size(ramdisk_handle_t *handle) {
    if (!handle || !handle->open) return 0;
    return handle->file->size;
}

/**
 * Check if at end of file
 */
static inline bool ramdisk_eof(ramdisk_handle_t *handle) {
    if (!handle || !handle->open) return true;
    return handle->position >= handle->file->size;
}

/**
 * Truncate file at current position
 */
static inline bool ramdisk_truncate(ramdisk_handle_t *handle) {
    if (!handle || !handle->open || !handle->write_mode) {
        return false;
    }

    handle->file->size = handle->position;
    handle->file->modified = g_ramdisk_ticks;
    return true;
}

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * Read entire file into buffer (caller allocates)
 * Returns bytes read, or 0 on error
 */
static inline uint32_t ramdisk_read_file(ramdisk_t *rd, const char *name,
                                          void *buf, uint32_t buf_size) {
    ramdisk_handle_t handle;

    if (!ramdisk_open(rd, name, &handle, false)) return 0;

    uint32_t file_size = handle.file->size;
    if (file_size > buf_size) file_size = buf_size;

    uint32_t bytes_read = ramdisk_read(&handle, buf, file_size);
    ramdisk_close(&handle);

    return bytes_read;
}

/**
 * Write entire buffer to file (creates/overwrites)
 * Returns bytes written, or 0 on error
 */
static inline uint32_t ramdisk_write_file(ramdisk_t *rd, const char *name,
                                           const void *buf, uint32_t size) {
    /* Delete existing file if any */
    ramdisk_delete(rd, name);

    ramdisk_handle_t handle;
    if (!ramdisk_open(rd, name, &handle, true)) return 0;

    uint32_t written = ramdisk_write(&handle, buf, size);
    ramdisk_close(&handle);

    return written;
}

/**
 * Append buffer to file
 */
static inline uint32_t ramdisk_append_file(ramdisk_t *rd, const char *name,
                                            const void *buf, uint32_t size) {
    ramdisk_handle_t handle;
    if (!ramdisk_open_append(rd, name, &handle)) return 0;

    uint32_t written = ramdisk_write(&handle, buf, size);
    ramdisk_close(&handle);

    return written;
}

/**
 * Get file info without opening
 */
static inline bool ramdisk_stat(ramdisk_t *rd, const char *name,
                                 uint32_t *size, uint32_t *flags) {
    int idx = ramdisk_find(rd, name);
    if (idx < 0) return false;

    ramdisk_file_t *f = &rd->files[idx];
    if (size) *size = f->size;
    if (flags) *flags = f->flags;

    rd->last_error = RAMDISK_OK;
    return true;
}

/**
 * Get detailed file info
 */
static inline bool ramdisk_stat_full(ramdisk_t *rd, const char *name, ramdisk_file_t *out) {
    int idx = ramdisk_find(rd, name);
    if (idx < 0) return false;

    *out = rd->files[idx];
    rd->last_error = RAMDISK_OK;
    return true;
}

/**
 * Rename a file
 */
static inline bool ramdisk_rename(ramdisk_t *rd, const char *old_name, const char *new_name) {
    if (!rd || !old_name || !new_name || !rd->initialized) {
        if (rd) rd->last_error = RAMDISK_ERR_INVALID;
        return false;
    }

    /* Check new name doesn't exist */
    if (ramdisk_exists(rd, new_name)) {
        rd->last_error = RAMDISK_ERR_EXISTS;
        return false;
    }

    int idx = ramdisk_find(rd, old_name);
    if (idx < 0) return false;

    rd_strncpy(rd->files[idx].name, new_name, RAMDISK_MAX_FILENAME - 1);
    rd->files[idx].modified = g_ramdisk_ticks;
    rd->last_error = RAMDISK_OK;

    return true;
}

/**
 * Copy a file within the RAM disk
 */
static inline bool ramdisk_copy(ramdisk_t *rd, const char *src_name, const char *dst_name) {
    if (!rd || !src_name || !dst_name) return false;

    /* Get source file info */
    uint32_t size, flags;
    if (!ramdisk_stat(rd, src_name, &size, &flags)) return false;

    /* Check destination doesn't exist */
    if (ramdisk_exists(rd, dst_name)) {
        rd->last_error = RAMDISK_ERR_EXISTS;
        return false;
    }

    /* Open source */
    ramdisk_handle_t src_handle;
    if (!ramdisk_open(rd, src_name, &src_handle, false)) return false;

    /* Create destination */
    ramdisk_handle_t dst_handle;
    if (!ramdisk_open(rd, dst_name, &dst_handle, true)) {
        ramdisk_close(&src_handle);
        return false;
    }

    /* Copy in chunks */
    uint8_t buf[512];
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        uint32_t bytes_read = ramdisk_read(&src_handle, buf, chunk);
        if (bytes_read == 0) break;

        uint32_t written = ramdisk_write(&dst_handle, buf, bytes_read);
        if (written != bytes_read) break;

        remaining -= bytes_read;
    }

    ramdisk_close(&src_handle);
    ramdisk_close(&dst_handle);

    /* Copy flags (but keep USED flag) */
    int dst_idx = ramdisk_find(rd, dst_name);
    if (dst_idx >= 0) {
        rd->files[dst_idx].flags = RAMDISK_FILE_USED | (flags & ~RAMDISK_FILE_USED);
    }

    return remaining == 0;
}

/**
 * Set file flags
 */
static inline bool ramdisk_set_flags(ramdisk_t *rd, const char *name, uint32_t flags) {
    int idx = ramdisk_find(rd, name);
    if (idx < 0) return false;

    /* Keep USED flag, update rest */
    rd->files[idx].flags = RAMDISK_FILE_USED | (flags & ~RAMDISK_FILE_USED);
    return true;
}

/**
 * Clear specific flags
 */
static inline bool ramdisk_clear_flags(ramdisk_t *rd, const char *name, uint32_t flags) {
    int idx = ramdisk_find(rd, name);
    if (idx < 0) return false;

    rd->files[idx].flags &= ~flags;
    rd->files[idx].flags |= RAMDISK_FILE_USED;  /* Always keep USED */
    return true;
}

/* ============================================================================
 * Directory Listing
 * ============================================================================ */

static inline void ramdisk_opendir(ramdisk_t *rd, ramdisk_dir_t *dir, bool show_hidden) {
    dir->disk = rd;
    dir->index = 0;
    dir->show_hidden = show_hidden;
}

static inline bool ramdisk_readdir(ramdisk_dir_t *dir, ramdisk_file_t *out) {
    ramdisk_t *rd = dir->disk;

    while (dir->index < RAMDISK_MAX_FILES) {
        ramdisk_file_t *f = &rd->files[dir->index++];

        if (!(f->flags & RAMDISK_FILE_USED)) continue;
        if (!dir->show_hidden && (f->flags & RAMDISK_FILE_HIDDEN)) continue;

        *out = *f;
        return true;
    }
    return false;
}

/**
 * Count files in RAM disk
 */
static inline uint32_t ramdisk_count_files(ramdisk_t *rd) {
    return rd->file_count;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

static inline void ramdisk_get_stats(ramdisk_t *rd, ramdisk_stats_t *stats) {
    if (!rd || !stats) return;

    /* Calculate actual data used by files */
    uint32_t actual_used = 0;
    for (uint32_t i = 0; i < RAMDISK_MAX_FILES; i++) {
        if (rd->files[i].flags & RAMDISK_FILE_USED) {
            actual_used += rd->files[i].size;
        }
    }

    stats->total_size = rd->data_size;
    stats->used_size = rd->data_used;
    stats->free_size = rd->data_size - rd->data_used;
    stats->file_count = rd->file_count;
    stats->max_files = RAMDISK_MAX_FILES;
    stats->total_reads = rd->total_reads;
    stats->total_writes = rd->total_writes;
    stats->bytes_read = rd->bytes_read;
    stats->bytes_written = rd->bytes_written;
    stats->fragmented_bytes = rd->data_used - actual_used;
}

/* ============================================================================
 * Extension Helpers
 * ============================================================================ */

/**
 * Get file extension (pointer into filename)
 */
static inline const char *ramdisk_get_ext(const char *filename) {
    const char *dot = NULL;
    while (*filename) {
        if (*filename == '.') dot = filename + 1;
        filename++;
    }
    return dot ? dot : "";
}

/**
 * Check if file has specific extension (case-insensitive)
 */
static inline bool ramdisk_has_ext(const char *filename, const char *ext) {
    const char *file_ext = ramdisk_get_ext(filename);
    return rd_strcasecmp(file_ext, ext) == 0;
}

/**
 * Check if file is executable (by extension or flag)
 */
static inline bool ramdisk_is_executable(ramdisk_t *rd, const char *name) {
    uint32_t flags;
    if (!ramdisk_stat(rd, name, NULL, &flags)) return false;

    if (flags & RAMDISK_FILE_EXECUTABLE) return true;

    /* Check common executable extensions */
    return ramdisk_has_ext(name, "exe") ||
           ramdisk_has_ext(name, "elf") ||
           ramdisk_has_ext(name, "com") ||
           ramdisk_has_ext(name, "bin");
}

#endif /* RAMDISK_H */