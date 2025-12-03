/**
 * program_shell.h - Shell commands for program loader
 *
 * Add these commands to your kernel shell:
 *   load <file> [addr]  - Load program into memory
 *   run [args...]       - Run loaded program
 *   exec <file> [args]  - Load and run in one step
 *   unload              - Unload current program
 *   proginfo            - Show loaded program info
 *
 * Integration:
 *   1. Include this header after program.h
 *   2. Call program_shell_init() before registering commands
 *   3. Call program_shell_register() after shell_init()
 */

#ifndef PROGRAM_SHELL_H
#define PROGRAM_SHELL_H

#include "program.h"
#include "shell.h"
#include "vfs.h"

/* ============================================================================
 * Kernel API Instance
 * ============================================================================
 *
 * This is the API structure passed to programs. It must be populated with
 * pointers to actual kernel functions before running any programs.
 */

static kernel_api_t g_kernel_api;

/* Program exit flag and return code */
static volatile bool g_program_exited = false;
static volatile int g_program_exit_code = 0;

/* Current shell reference for API callbacks */
static shell_state_t *g_current_shell = NULL;

/* ============================================================================
 * API Callback Implementations
 * ============================================================================ */

/* Console I/O - forward to shell I/O */
static void api_putchar(char c) {
    if (g_current_shell && g_current_shell->io->putchar) {
        g_current_shell->io->putchar(c);
    }
}

static void api_puts(const char *s) {
    if (g_current_shell && g_current_shell->io->puts) {
        g_current_shell->io->puts(s);
    }
}

static char api_getchar(void) {
    if (g_current_shell && g_current_shell->io->getchar) {
        return g_current_shell->io->getchar();
    }
    return 0;
}

static int api_getchar_nonblock(void) {
    if (g_current_shell && g_current_shell->io->getchar_nonblock) {
        return g_current_shell->io->getchar_nonblock();
    }
    return -1;
}

static void api_clear(void) {
    if (g_current_shell && g_current_shell->io->clear) {
        g_current_shell->io->clear();
    }
}

/* Exit - sets flag to return control to shell */
static void api_exit(int code) {
    g_program_exit_code = code;
    g_program_exited = true;
    /* 
     * Note: In a real implementation, this would need to do a longjmp
     * or similar to actually return control. For now, programs must
     * check the exit flag and return normally.
     */
}

/* Stub implementations for functions not yet available */
static void api_set_cursor(int x, int y) { (void)x; (void)y; }
static void api_get_cursor(int *x, int *y) { if(x) *x=0; if(y) *y=0; }
static void api_set_color(uint8_t fg, uint8_t bg) { (void)fg; (void)bg; }

static void *api_malloc(size_t size) { (void)size; return NULL; }
static void *api_calloc(size_t count, size_t size) { (void)count; (void)size; return NULL; }
static void *api_realloc(void *ptr, size_t size) { (void)ptr; (void)size; return NULL; }
static void api_free(void *ptr) { (void)ptr; }

static void api_sleep(uint32_t ms) { (void)ms; }
static uint32_t api_get_ticks(void) { return 0; }
static void api_reboot(void) { /* Could call kernel reboot */ }

static const char *api_get_program_name(void) {
    return g_program.loaded ? g_program.name : "";
}

static const char *api_get_cwd(void) {
    return g_current_shell ? g_current_shell->cwd : "/";
}

static int api_set_cwd(const char *path) { (void)path; return -1; }

/* File I/O wrappers */
static void *api_fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r') flags = VFS_O_RDONLY;
    else if (mode[0] == 'w') flags = VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC;
    else if (mode[0] == 'a') flags = VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND;
    return vfs_open(path, flags);
}

static int api_fclose(void *file) {
    vfs_close((vfs_file_t *)file);
    return 0;
}

static int api_fread(void *buf, size_t size, size_t count, void *file) {
    int32_t result = vfs_read((vfs_file_t *)file, buf, size * count);
    return result > 0 ? result / (int)size : result;
}

static int api_fwrite(const void *buf, size_t size, size_t count, void *file) {
    int32_t result = vfs_write((vfs_file_t *)file, buf, size * count);
    return result > 0 ? result / (int)size : result;
}

static int api_fseek(void *file, long offset, int whence) {
    (void)file; (void)offset; (void)whence;
    return -1;  /* Not implemented yet */
}

static long api_ftell(void *file) {
    (void)file;
    return -1;  /* Not implemented yet */
}

static int api_feof(void *file) {
    (void)file;
    return 0;
}

static void *api_opendir(const char *path) {
    return vfs_opendir(path);
}

static void *api_readdir(void *dir) {
    static vfs_dirent_t entry;
    if (vfs_readdir((vfs_dir_t *)dir, &entry)) {
        return &entry;
    }
    return NULL;
}

static int api_closedir(void *dir) {
    vfs_closedir((vfs_dir_t *)dir);
    return 0;
}

static int api_stat(const char *path, void *statbuf) {
    (void)path; (void)statbuf;
    return -1;  /* Not implemented yet */
}

static int api_remove(const char *path) {
    return vfs_unlink(path) ? 0 : -1;
}

static int api_rename(const char *old, const char *new) {
    (void)old; (void)new;
    return -1;  /* Not implemented yet */
}

static int api_mkdir(const char *path) {
    return vfs_mkdir(path) ? 0 : -1;
}

/* ============================================================================
 * Initialize Kernel API
 * ============================================================================ */

static void program_shell_init(void) {
    /* Set up API version info */
    g_kernel_api.version = KERNEL_API_VERSION;
    g_kernel_api.struct_size = sizeof(kernel_api_t);

    /* Console I/O */
    g_kernel_api.putchar = api_putchar;
    g_kernel_api.puts = api_puts;
    g_kernel_api.printf = NULL;  /* Will be set from shell */
    g_kernel_api.getchar = api_getchar;
    g_kernel_api.getchar_nonblock = api_getchar_nonblock;

    /* Console control */
    g_kernel_api.clear = api_clear;
    g_kernel_api.set_cursor = api_set_cursor;
    g_kernel_api.get_cursor = api_get_cursor;
    g_kernel_api.set_color = api_set_color;

    /* Memory management */
    g_kernel_api.malloc = api_malloc;
    g_kernel_api.calloc = api_calloc;
    g_kernel_api.realloc = api_realloc;
    g_kernel_api.free = api_free;

    /* File I/O */
    g_kernel_api.fopen = api_fopen;
    g_kernel_api.fclose = api_fclose;
    g_kernel_api.fread = api_fread;
    g_kernel_api.fwrite = api_fwrite;
    g_kernel_api.fseek = api_fseek;
    g_kernel_api.ftell = api_ftell;
    g_kernel_api.feof = api_feof;

    /* Directory operations */
    g_kernel_api.opendir = api_opendir;
    g_kernel_api.readdir = api_readdir;
    g_kernel_api.closedir = api_closedir;

    /* File utilities */
    g_kernel_api.stat = api_stat;
    g_kernel_api.remove = api_remove;
    g_kernel_api.rename = api_rename;
    g_kernel_api.mkdir = api_mkdir;

    /* System */
    g_kernel_api.exit = api_exit;
    g_kernel_api.sleep = api_sleep;
    g_kernel_api.get_ticks = api_get_ticks;
    g_kernel_api.reboot = api_reboot;

    /* Program info */
    g_kernel_api.get_program_name = api_get_program_name;
    g_kernel_api.get_cwd = api_get_cwd;
    g_kernel_api.set_cwd = api_set_cwd;

    /* Clear reserved pointers */
    for (int i = 0; i < 16; i++) {
        g_kernel_api.reserved[i] = NULL;
    }
}

/* ============================================================================
 * Shell Commands
 * ============================================================================ */

/**
 * load <file> [address]
 * Load a program into memory
 */
static void sh_cmd_load(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: load <file> [address]\n");
        sh->io->printf("Load a program into memory.\n");
        sh->last_result = 1;
        return;
    }

    const char *path = argv[1];
    uint32_t load_addr = 0;

    /* Parse optional address */
    if (argc > 2) {
        const char *p = argv[2];
        /* Simple hex parser */
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
        while (*p) {
            char c = *p++;
            load_addr <<= 4;
            if (c >= '0' && c <= '9') load_addr |= c - '0';
            else if (c >= 'a' && c <= 'f') load_addr |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') load_addr |= c - 'A' + 10;
        }
    }

    sh->io->printf("Loading '%s'...\n", path);

    if (program_load(path, load_addr)) {
        const program_state_t *prog = program_get_state();
        sh->io->printf("Loaded %u bytes at 0x%08X\n", prog->size, prog->load_addr);
        sh->io->printf("Entry point: 0x%08X\n", prog->entry);
        sh->last_result = 0;
    } else {
        sh->io->printf("Failed to load '%s'\n", path);
        sh->last_result = 1;
    }
}

/**
 * run [args...]
 * Execute the loaded program
 */
static void sh_cmd_run(shell_state_t *sh, int argc, char **argv) {
    const program_state_t *prog = program_get_state();

    if (!prog->loaded) {
        sh->io->printf("No program loaded. Use 'load <file>' first.\n");
        sh->last_result = 1;
        return;
    }

    /* Set up shell reference for API callbacks */
    g_current_shell = sh;
    g_kernel_api.printf = sh->io->printf;

    /* Build argument list */
    char *prog_argv[PROGRAM_MAX_ARGS];
    int prog_argc = 0;

    /* First arg is program name */
    prog_argv[prog_argc++] = (char *)prog->name;

    /* Copy remaining args from command line */
    for (int i = 1; i < argc && prog_argc < PROGRAM_MAX_ARGS; i++) {
        prog_argv[prog_argc++] = argv[i];
    }

    sh->io->printf("Running '%s'...\n\n", prog->name);

    /* Reset exit state */
    g_program_exited = false;
    g_program_exit_code = 0;

    /* Run the program */
    int result = program_run(&g_kernel_api, prog_argc, prog_argv);

    sh->io->printf("\nProgram exited with code %d\n", result);
    sh->last_result = result;

    /* Clear shell reference */
    g_current_shell = NULL;
}

/**
 * exec <file> [args...]
 * Load and execute a program in one step
 */
static void sh_cmd_exec(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: exec <file> [args...]\n");
        sh->io->printf("Load and execute a program.\n");
        sh->last_result = 1;
        return;
    }

    const char *path = argv[1];

    sh->io->printf("Loading '%s'...\n", path);

    if (!program_load(path, 0)) {
        sh->io->printf("Failed to load '%s'\n", path);
        sh->last_result = 1;
        return;
    }

    const program_state_t *prog = program_get_state();
    sh->io->printf("Loaded %u bytes at 0x%08X\n", prog->size, prog->load_addr);

    /* Set up shell reference for API callbacks */
    g_current_shell = sh;
    g_kernel_api.printf = sh->io->printf;

    /* Build argument list (shift argv to skip 'exec') */
    char *prog_argv[PROGRAM_MAX_ARGS];
    int prog_argc = 0;

    /* First arg is program name */
    prog_argv[prog_argc++] = (char *)prog->name;

    /* Copy args after filename */
    for (int i = 2; i < argc && prog_argc < PROGRAM_MAX_ARGS; i++) {
        prog_argv[prog_argc++] = argv[i];
    }

    sh->io->printf("Running '%s'...\n\n", prog->name);

    /* Reset exit state */
    g_program_exited = false;
    g_program_exit_code = 0;

    /* Run the program */
    int result = program_run(&g_kernel_api, prog_argc, prog_argv);

    sh->io->printf("\nProgram exited with code %d\n", result);
    sh->last_result = result;

    /* Clear shell reference */
    g_current_shell = NULL;
}

/**
 * unload
 * Unload the current program
 */
static void sh_cmd_unload(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;

    const program_state_t *prog = program_get_state();

    if (!prog->loaded) {
        sh->io->printf("No program loaded.\n");
        sh->last_result = 1;
        return;
    }

    sh->io->printf("Unloading '%s'...\n", prog->name);
    program_unload();
    sh->io->printf("Program unloaded.\n");
    sh->last_result = 0;
}

/**
 * proginfo
 * Show information about loaded program
 */
static void sh_cmd_proginfo(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;

    const program_state_t *prog = program_get_state();

    if (!prog->loaded) {
        sh->io->printf("No program loaded.\n");
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\nProgram Information:\n");
    sh->io->printf("--------------------\n");
    sh->io->printf("  Name:        %s\n", prog->name);
    sh->io->printf("  Load addr:   0x%08X\n", prog->load_addr);
    sh->io->printf("  Size:        %u bytes\n", prog->size);
    sh->io->printf("  Entry point: 0x%08X\n", prog->entry);
    sh->io->printf("  Flags:       0x%08X\n", prog->flags);
    if (prog->flags & RFEX_FLAG_RESIDENT) sh->io->printf("               - Resident\n");
    if (prog->flags & RFEX_FLAG_SHARED)   sh->io->printf("               - Shared\n");
    sh->io->printf("\n");

    sh->last_result = 0;
}

/* ============================================================================
 * Register Shell Commands
 * ============================================================================ */

static inline void program_shell_register(shell_state_t *sh) {
    shell_register(sh, "load",     "Load program into memory",    sh_cmd_load,     1);
    shell_register(sh, "run",      "Run loaded program",          sh_cmd_run,      0);
    shell_register(sh, "exec",     "Load and run program",        sh_cmd_exec,     1);
    shell_register(sh, "unload",   "Unload current program",      sh_cmd_unload,   0);
    shell_register(sh, "proginfo", "Show loaded program info",    sh_cmd_proginfo, 0);
}

#endif /* PROGRAM_SHELL_H */
