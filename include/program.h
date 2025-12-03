/**
 * program.h - RetroFuture OS Program Loader
 *
 * Simple executable format and loader for external programs.
 * Programs are loaded from disk and executed with access to kernel services.
 *
 * Usage:
 *   > load /fd1/rfasm.bin     Load program from disk
 *   > run                     Execute loaded program
 *   > exec /fd0/edit.bin      Load and run in one step
 */

#ifndef PROGRAM_H
#define PROGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Executable Format
 * ============================================================================
 *
 * RetroFuture Executable (RFEX) - Simple flat binary with header
 *
 * Offset  Size  Description
 * ------  ----  -----------
 * 0x00    4     Magic: 'RFEX' (0x58454652)
 * 0x04    4     Entry point offset from load address
 * 0x08    4     Preferred load address (0 = default 0x200000)
 * 0x0C    4     BSS size (zero-initialized data after binary)
 * 0x10    4     Flags (reserved)
 * 0x14    12    Reserved (pad to 32 bytes)
 * 0x20    ...   Program code and data
 *
 * For simplicity, programs can also be raw binaries without headers.
 * The loader detects this by checking for the magic number.
 */

#define RFEX_MAGIC          0x58454652  /* 'RFEX' */
#define RFEX_HEADER_SIZE    32

/* Program header */
typedef struct {
    uint32_t magic;         /* Must be RFEX_MAGIC */
    uint32_t entry_offset;  /* Entry point offset from load base */
    uint32_t load_addr;     /* Preferred load address (0 = default) */
    uint32_t bss_size;      /* Size of BSS section to zero */
    uint32_t flags;         /* Reserved flags */
    uint32_t reserved[3];   /* Padding to 32 bytes */
} __attribute__((packed)) rfex_header_t;

/* Program flags */
#define RFEX_FLAG_RESIDENT  0x01    /* Don't unload after exit */
#define RFEX_FLAG_SHARED    0x02    /* Can be called by other programs */

/* ============================================================================
 * Kernel API
 * ============================================================================
 *
 * This structure is passed to programs so they can access kernel services.
 * Programs should NOT call kernel functions directly - always use this API.
 */

typedef struct kernel_api {
    /* Version info */
    uint32_t version;           /* API version (0x0001_0000 = 1.0) */
    uint32_t struct_size;       /* Size of this struct for compatibility */

    /* Console I/O */
    void (*putchar)(char c);
    void (*puts)(const char *s);
    void (*printf)(const char *fmt, ...);
    char (*getchar)(void);
    int  (*getchar_nonblock)(void);     /* Returns -1 if no char available */

    /* Console control */
    void (*clear)(void);
    void (*set_cursor)(int x, int y);
    void (*get_cursor)(int *x, int *y);
    void (*set_color)(uint8_t fg, uint8_t bg);

    /* Memory management */
    void *(*malloc)(size_t size);
    void *(*calloc)(size_t count, size_t size);
    void *(*realloc)(void *ptr, size_t size);
    void  (*free)(void *ptr);

    /* File I/O */
    void *(*fopen)(const char *path, const char *mode);
    int   (*fclose)(void *file);
    int   (*fread)(void *buf, size_t size, size_t count, void *file);
    int   (*fwrite)(const void *buf, size_t size, size_t count, void *file);
    int   (*fseek)(void *file, long offset, int whence);
    long  (*ftell)(void *file);
    int   (*feof)(void *file);

    /* Directory operations */
    void *(*opendir)(const char *path);
    void *(*readdir)(void *dir);
    int   (*closedir)(void *dir);

    /* File utilities */
    int   (*stat)(const char *path, void *statbuf);
    int   (*remove)(const char *path);
    int   (*rename)(const char *old, const char *new);
    int   (*mkdir)(const char *path);

    /* System */
    void  (*exit)(int code);            /* Return to shell */
    void  (*sleep)(uint32_t ms);        /* Sleep milliseconds */
    uint32_t (*get_ticks)(void);        /* System tick counter */
    void  (*reboot)(void);              /* Reboot system */

    /* Program info */
    const char *(*get_program_name)(void);
    const char *(*get_cwd)(void);
    int   (*set_cwd)(const char *path);

    /* Extended (added in API 1.1+) */
    void *reserved[16];                 /* Room for future expansion */
} kernel_api_t;

#define KERNEL_API_VERSION  0x00010000  /* Version 1.0 */

/* ============================================================================
 * Program Entry Point
 * ============================================================================
 *
 * Programs must export a function matching this signature.
 * The kernel calls this after loading the program.
 *
 * Parameters:
 *   api  - Pointer to kernel API structure
 *   argc - Argument count (including program name)
 *   argv - Argument array (argv[0] = program name)
 *
 * Returns:
 *   Exit code (0 = success)
 */

typedef int (*program_entry_t)(kernel_api_t *api, int argc, char **argv);

/* ============================================================================
 * Program Loader State
 * ============================================================================ */

/* Default load address for programs */
#define PROGRAM_DEFAULT_LOAD_ADDR   0x200000    /* 2MB mark */
#define PROGRAM_MAX_SIZE            0x100000    /* 1MB max program size */
#define PROGRAM_MAX_ARGS            16

/* Loader state */
typedef struct {
    bool loaded;                /* Program is loaded */
    uint32_t load_addr;         /* Where program is loaded */
    uint32_t size;              /* Size in bytes */
    uint32_t entry;             /* Entry point address */
    char name[64];              /* Program name */
    uint32_t flags;             /* Program flags */
} program_state_t;

/* Global program state */
static program_state_t g_program = {0};

/* ============================================================================
 * Loader Functions
 * ============================================================================ */

/**
 * Load a program from disk into memory
 *
 * @param path      Path to program file
 * @param load_addr Load address (0 = auto)
 * @return          true on success
 */
static bool program_load(const char *path, uint32_t load_addr);

/**
 * Execute the currently loaded program
 *
 * @param api       Kernel API to pass to program
 * @param argc      Argument count
 * @param argv      Argument array
 * @return          Program exit code, or -1 on error
 */
static int program_run(kernel_api_t *api, int argc, char **argv);

/**
 * Load and execute a program in one step
 *
 * @param path      Path to program file
 * @param api       Kernel API to pass to program
 * @param argc      Argument count
 * @param argv      Argument array
 * @return          Program exit code, or -1 on error
 */
static int program_exec(const char *path, kernel_api_t *api, int argc, char **argv);

/**
 * Unload the current program (free memory)
 */
static void program_unload(void);

/**
 * Get information about loaded program
 */
static const program_state_t *program_get_state(void);

/* ============================================================================
 * Implementation
 * ============================================================================ */

/* Simple string copy */
static void prog_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Extract filename from path */
static const char *prog_basename(const char *path) {
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') name = p + 1;
        p++;
    }
    return name;
}

/* Zero memory */
static void prog_memset(void *dst, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = val;
}

/* Copy memory */
static void prog_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

/**
 * Load a program from disk
 */
static bool program_load(const char *path, uint32_t load_addr) {
    /* Unload any existing program */
    program_unload();

    /* Open the file */
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        return false;
    }

    /* Get file size by reading to end */
    uint8_t temp[512];
    uint32_t file_size = 0;
    int32_t bytes;

    /* First pass: get size */
    while ((bytes = vfs_read(file, temp, sizeof(temp))) > 0) {
        file_size += bytes;
    }

    if (file_size == 0 || file_size > PROGRAM_MAX_SIZE) {
        vfs_close(file);
        return false;
    }

    /* Reopen to read from beginning */
    vfs_close(file);
    file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        return false;
    }

    /* Determine load address */
    uint32_t addr = load_addr ? load_addr : PROGRAM_DEFAULT_LOAD_ADDR;

    /* Read entire file into memory */
    uint8_t *dest = (uint8_t *)(uintptr_t)addr;
    uint32_t total_read = 0;

    while ((bytes = vfs_read(file, dest + total_read, 4096)) > 0) {
        total_read += bytes;
    }

    vfs_close(file);

    /* Check for RFEX header */
    rfex_header_t *header = (rfex_header_t *)dest;
    uint32_t entry_offset = 0;
    uint32_t bss_size = 0;
    uint32_t code_start = 0;

    if (header->magic == RFEX_MAGIC) {
        /* Valid RFEX executable */
        entry_offset = header->entry_offset;
        bss_size = header->bss_size;
        g_program.flags = header->flags;
        code_start = RFEX_HEADER_SIZE;

        /* If header specifies load address, use it */
        if (header->load_addr && !load_addr) {
            /* Would need to relocate - for now just use where we loaded */
        }
    } else {
        /* Raw binary - assume entry at start */
        entry_offset = 0;
        bss_size = 0;
        g_program.flags = 0;
        code_start = 0;
    }

    /* Zero BSS section if present */
    if (bss_size > 0) {
        prog_memset(dest + total_read, 0, bss_size);
    }

    /* Update program state */
    g_program.loaded = true;
    g_program.load_addr = addr + code_start;
    g_program.size = total_read - code_start;
    g_program.entry = addr + code_start + entry_offset;
    prog_strcpy(g_program.name, prog_basename(path), sizeof(g_program.name));

    return true;
}

/**
 * Execute the loaded program
 */
static int program_run(kernel_api_t *api, int argc, char **argv) {
    if (!g_program.loaded) {
        return -1;
    }

    /* Get entry point */
    program_entry_t entry = (program_entry_t)(uintptr_t)g_program.entry;

    /* Call the program */
    int result = entry(api, argc, argv);

    /* Unload unless marked resident */
    if (!(g_program.flags & RFEX_FLAG_RESIDENT)) {
        program_unload();
    }

    return result;
}

/**
 * Load and execute in one step
 */
static int program_exec(const char *path, kernel_api_t *api, int argc, char **argv) {
    if (!program_load(path, 0)) {
        return -1;
    }
    return program_run(api, argc, argv);
}

/**
 * Unload current program
 */
static void program_unload(void) {
    if (g_program.loaded) {
        /* Could zero memory for security, but not required */
        g_program.loaded = false;
        g_program.load_addr = 0;
        g_program.size = 0;
        g_program.entry = 0;
        g_program.name[0] = '\0';
        g_program.flags = 0;
    }
}

/**
 * Get program state
 */
static const program_state_t *program_get_state(void) {
    return &g_program;
}

#endif /* PROGRAM_H */
