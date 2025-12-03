/**
 * rfasm_shell.h - RF-ASM Shell Integration
 *
 * Provides shell commands for the RetroFuture assembler:
 *   - asm <source> [output]    - Assemble source file to binary
 *   - hexdump <file> [offset]  - Hex dump of file or memory
 *   - asmhelp                  - Show instruction reference
 *
 * Usage:
 *   1. Include this file in kernel.c after shell.h and vfs.h
 *   2. Call rfasm_shell_register() after shell_init()
 *
 * Author: Jesse (with Claude)
 * License: Public Domain / MIT
 */

#ifndef RFASM_SHELL_H
#define RFASM_SHELL_H

#define RFASM_SHELL_INTEGRATION
#include "rfasm.h"
#include "vfs.h"
#include "shell.h"

/* ============================================================================
 * Configuration - Tune these for your memory constraints
 * ============================================================================ */

/* Assembly output buffer - 8KB is plenty for most programs */
#ifndef RFASM_OUTPUT_BUFFER_SIZE
#define RFASM_OUTPUT_BUFFER_SIZE    8192
#endif

/* Source buffer for reading files */
#ifndef RFASM_SOURCE_BUFFER_SIZE
#define RFASM_SOURCE_BUFFER_SIZE    8192
#endif

/* ============================================================================
 * Global Buffers (static allocation for OS use)
 * ============================================================================ */

static uint8_t  g_rfasm_output[RFASM_OUTPUT_BUFFER_SIZE];
static char     g_rfasm_source[RFASM_SOURCE_BUFFER_SIZE];
static rfasm_state_t g_rfasm_state;

/* ============================================================================
 * ASM Command - Assemble source file
 *
 * Usage: asm <source.asm> [output.bin]
 *        asm -e "MOV EAX, 42"     - Assemble expression inline
 * ============================================================================ */

static void sh_cmd_asm(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: asm <source.asm> [output.bin]\n");
        sh->io->printf("       asm -e \"instruction\"  (inline assembly)\n");
        sh->last_result = 1;
        return;
    }

    const char *source_text = NULL;
    const char *output_path = NULL;
    bool inline_mode = false;

    /* Check for inline mode */
    if (rfa_strcmp(argv[1], "-e") == 0) {
        if (argc < 3) {
            sh->io->printf("Usage: asm -e \"MOV EAX, 42\"\n");
            sh->last_result = 1;
            return;
        }
        inline_mode = true;
        source_text = argv[2];
        if (argc > 3) output_path = argv[3];
    } else {
        /* File mode */
        const char *source_path = argv[1];
        if (argc > 2) output_path = argv[2];

        /* Read source file */
        vfs_file_t *src_file = vfs_open(source_path, VFS_O_RDONLY);
        if (!src_file) {
            sh->io->printf("Error: Cannot open '%s'\n", source_path);
            sh->last_result = 1;
            return;
        }

        int32_t bytes_read = vfs_read(src_file, g_rfasm_source, RFASM_SOURCE_BUFFER_SIZE - 1);
        vfs_close(src_file);

        if (bytes_read < 0) {
            sh->io->printf("Error: Failed to read '%s'\n", source_path);
            sh->last_result = 1;
            return;
        }

        g_rfasm_source[bytes_read] = '\0';
        source_text = g_rfasm_source;

        sh->io->printf("Assembling '%s' (%d bytes)...\n", source_path, bytes_read);
    }

    /* Initialize assembler */
    rfasm_init(&g_rfasm_state, g_rfasm_output, RFASM_OUTPUT_BUFFER_SIZE);

    /* Assemble */
    rfasm_error_t err = rfasm_assemble(&g_rfasm_state, source_text);

    if (err != RFASM_OK) {
        sh->io->printf("Error on line %d: %s\n", 
                      g_rfasm_state.error_line,
                      rfasm_error_string(err));
        if (g_rfasm_state.error_msg[0]) {
            sh->io->printf("  %s\n", g_rfasm_state.error_msg);
        }
        sh->last_result = 1;
        return;
    }

    /* Success! */
    sh->io->printf("Assembly successful!\n");
    sh->io->printf("  Labels: %d\n", g_rfasm_state.num_labels);
    sh->io->printf("  Output: %u bytes\n", g_rfasm_state.output_size);

    /* Write output file if specified */
    if (output_path) {
        /* Create output file */
        if (!vfs_create(output_path, VFS_FILE)) {
            /* File might already exist, try opening anyway */
        }

        vfs_file_t *out_file = vfs_open(output_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
        if (!out_file) {
            sh->io->printf("Error: Cannot create '%s'\n", output_path);
            sh->last_result = 1;
            return;
        }

        int32_t written = vfs_write(out_file, g_rfasm_output, g_rfasm_state.output_size);
        vfs_close(out_file);

        if (written != (int32_t)g_rfasm_state.output_size) {
            sh->io->printf("Warning: Only wrote %d of %u bytes\n", 
                          written, g_rfasm_state.output_size);
        } else {
            sh->io->printf("  Wrote: %s\n", output_path);
        }
    } else if (inline_mode) {
        /* Show hex dump for inline mode */
        sh->io->printf("\nOutput (%u bytes):\n", g_rfasm_state.output_size);
        rfasm_hexdump(sh->io->printf, g_rfasm_output, g_rfasm_state.output_size, 0);
    }

    sh->last_result = 0;
}

/* ============================================================================
 * HEXDUMP Command - Display file or memory as hex
 *
 * Usage: hexdump <file>
 *        hexdump -m <addr> <len>  - Memory dump
 *        hexdump -l                - Dump last assembly output
 * ============================================================================ */

static void sh_cmd_hexdump(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: hexdump <file>\n");
        sh->io->printf("       hexdump -l          (last asm output)\n");
        sh->io->printf("       hexdump -m <addr> <len>\n");
        sh->last_result = 1;
        return;
    }

    const uint8_t *data = NULL;
    uint32_t len = 0;
    uint32_t base_addr = 0;

    if (rfa_strcmp(argv[1], "-l") == 0) {
        /* Last assembly output */
        if (g_rfasm_state.output_size == 0) {
            sh->io->printf("No assembly output available.\n");
            sh->last_result = 1;
            return;
        }
        data = g_rfasm_output;
        len = g_rfasm_state.output_size;
        base_addr = g_rfasm_state.org;
        sh->io->printf("Last assembly output (%u bytes at 0x%08X):\n\n", len, base_addr);
    } else if (rfa_strcmp(argv[1], "-m") == 0) {
        /* Memory dump */
        if (argc < 4) {
            sh->io->printf("Usage: hexdump -m <addr> <len>\n");
            sh->last_result = 1;
            return;
        }

        /* Parse address */
        const char *p = argv[2];
        int32_t addr_val;
        if (!rfasm_parse_number(&p, &addr_val)) {
            sh->io->printf("Invalid address: %s\n", argv[2]);
            sh->last_result = 1;
            return;
        }

        /* Parse length */
        p = argv[3];
        int32_t len_val;
        if (!rfasm_parse_number(&p, &len_val)) {
            sh->io->printf("Invalid length: %s\n", argv[3]);
            sh->last_result = 1;
            return;
        }

        data = (const uint8_t *)(uintptr_t)addr_val;
        len = (uint32_t)len_val;
        base_addr = (uint32_t)addr_val;

        sh->io->printf("Memory at 0x%08X (%u bytes):\n\n", base_addr, len);
    } else {
        /* File dump */
        const char *filepath = argv[1];

        vfs_file_t *file = vfs_open(filepath, VFS_O_RDONLY);
        if (!file) {
            sh->io->printf("Error: Cannot open '%s'\n", filepath);
            sh->last_result = 1;
            return;
        }

        /* Read into source buffer (reusing it) */
        int32_t bytes_read = vfs_read(file, g_rfasm_source, RFASM_SOURCE_BUFFER_SIZE);
        vfs_close(file);

        if (bytes_read < 0) {
            sh->io->printf("Error: Failed to read '%s'\n", filepath);
            sh->last_result = 1;
            return;
        }

        data = (const uint8_t *)g_rfasm_source;
        len = (uint32_t)bytes_read;

        sh->io->printf("File '%s' (%u bytes):\n\n", filepath, len);
    }

    /* Print hexdump */
    rfasm_hexdump(sh->io->printf, data, len, base_addr);

    sh->last_result = 0;
}

/* ============================================================================
 * ASMHELP Command - Show instruction reference
 * ============================================================================ */

static void sh_cmd_asmhelp(shell_state_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    rfasm_print_help(sh->io->printf);
    sh->last_result = 0;
}

/* ============================================================================
 * ASMLABELS Command - Show labels from last assembly
 * ============================================================================ */

static void sh_cmd_asmlabels(shell_state_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (g_rfasm_state.num_labels == 0) {
        sh->io->printf("No labels defined. Run 'asm' first.\n");
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\nSymbol Table (%d labels):\n", g_rfasm_state.num_labels);
    sh->io->printf("%-32s  %s\n", "Label", "Address");
    sh->io->printf("%-32s  %s\n", "-----", "-------");

    for (int i = 0; i < g_rfasm_state.num_labels; i++) {
        rfasm_label_t *label = &g_rfasm_state.labels[i];
        sh->io->printf("%-32s  0x%08X\n", label->name, label->address);
    }

    sh->io->printf("\n");
    sh->last_result = 0;
}

/* ============================================================================
 * RUN Command - Execute code at address
 *
 * Usage: run <address>
 *        run -l          (run last assembled code)
 *
 * WARNING: This jumps to arbitrary memory - use carefully!
 * ============================================================================ */

typedef void (*run_func_t)(void);

static void sh_cmd_run(shell_state_t *sh, int argc, char **argv) {
    uint32_t addr = 0;

    if (argc < 2) {
        sh->io->printf("Usage: run <address>\n");
        sh->io->printf("       run -l          (run last assembly output)\n");
        sh->last_result = 1;
        return;
    }

    if (rfa_strcmp(argv[1], "-l") == 0) {
        /* Run last assembly output */
        if (g_rfasm_state.output_size == 0) {
            sh->io->printf("Error: No assembly output available.\n");
            sh->io->printf("Use 'asm' to assemble code first.\n");
            sh->last_result = 1;
            return;
        }
        addr = (uint32_t)(uintptr_t)g_rfasm_output;
        sh->io->printf("Executing %u bytes at 0x%08X...\n", 
                      g_rfasm_state.output_size, addr);
    } else {
        /* Parse address */
        const char *p = argv[1];
        int32_t addr_val;
        if (!rfasm_parse_number(&p, &addr_val)) {
            sh->io->printf("Invalid address: %s\n", argv[1]);
            sh->last_result = 1;
            return;
        }
        addr = (uint32_t)addr_val;
        sh->io->printf("Executing at 0x%08X...\n", addr);
    }

    /* Jump to the code */
    run_func_t func = (run_func_t)(uintptr_t)addr;
    func();

    sh->io->printf("Returned from execution.\n");
    sh->last_result = 0;
}

/* ============================================================================
 * POKE Command - Write bytes to memory
 *
 * Usage: poke <address> <byte1> [byte2] [byte3] ...
 * ============================================================================ */

static void sh_cmd_poke(shell_state_t *sh, int argc, char **argv) {
    if (argc < 3) {
        sh->io->printf("Usage: poke <address> <byte1> [byte2] ...\n");
        sh->io->printf("Example: poke 0x100000 0x90 0x90 0xC3\n");
        sh->last_result = 1;
        return;
    }

    /* Parse address */
    const char *p = argv[1];
    int32_t addr_val;
    if (!rfasm_parse_number(&p, &addr_val)) {
        sh->io->printf("Invalid address: %s\n", argv[1]);
        sh->last_result = 1;
        return;
    }

    uint8_t *dest = (uint8_t *)(uintptr_t)addr_val;

    /* Write bytes */
    int count = 0;
    for (int i = 2; i < argc; i++) {
        p = argv[i];
        int32_t byte_val;
        if (rfasm_parse_number(&p, &byte_val)) {
            dest[count++] = (uint8_t)byte_val;
        } else {
            sh->io->printf("Invalid byte value: %s\n", argv[i]);
        }
    }

    sh->io->printf("Wrote %d bytes to 0x%08X\n", count, addr_val);
    sh->last_result = 0;
}

/* ============================================================================
 * PEEK Command - Read bytes from memory
 *
 * Usage: peek <address> [count]
 * ============================================================================ */

static void sh_cmd_peek(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: peek <address> [count]\n");
        sh->last_result = 1;
        return;
    }

    const char *p = argv[1];
    int32_t addr_val;
    if (!rfasm_parse_number(&p, &addr_val)) {
        sh->io->printf("Invalid address: %s\n", argv[1]);
        sh->last_result = 1;
        return;
    }

    int32_t count = 16;  /* Default */
    if (argc > 2) {
        p = argv[2];
        rfasm_parse_number(&p, &count);
    }

    const uint8_t *src = (const uint8_t *)(uintptr_t)addr_val;
    rfasm_hexdump(sh->io->printf, src, count, addr_val);

    sh->last_result = 0;
}

/* ============================================================================
 * MONITOR Command - Interactive Machine Language Monitor
 *
 * Usage: monitor [address]
 *
 * Enters interactive mode where you can:
 *   - Type assembly instructions (assembled and placed at current address)
 *   - Use . to exit
 *   - Use = to show current address
 *   - Use @ <addr> to change address
 *   - Use # to run from start address
 *
 * This is like the C64's built-in assembler!
 * ============================================================================ */

/* Monitor state */
static struct {
    uint32_t start_addr;    /* Where we started assembling */
    uint32_t current_addr;  /* Current assembly address */
    uint8_t *buffer;        /* Output buffer */
    uint32_t buffer_size;   /* Bytes assembled */
    bool active;
} g_monitor;

static void sh_cmd_monitor(shell_state_t *sh, int argc, char **argv) {
    /* Initialize monitor */
    uint32_t start = 0x100000;  /* Default: 1MB mark (safe area) */

    if (argc > 1) {
        const char *p = argv[1];
        int32_t addr_val;
        if (rfasm_parse_number(&p, &addr_val)) {
            start = (uint32_t)addr_val;
        }
    }

    g_monitor.start_addr = start;
    g_monitor.current_addr = start;
    g_monitor.buffer = (uint8_t *)(uintptr_t)start;
    g_monitor.buffer_size = 0;
    g_monitor.active = true;

    sh->io->printf("\n");
    sh->io->printf("RF-ASM Monitor at 0x%08X\n", start);
    sh->io->printf("Commands: . = exit, = = show addr, @ addr = set addr, # = run\n");
    sh->io->printf("Type assembly instructions to assemble them in place.\n\n");

    char line_buf[128];
    int line_len = 0;

    while (g_monitor.active) {
        /* Print address prompt */
        sh->io->printf("%08X: ", g_monitor.current_addr);

        /* Read line */
        line_len = 0;
        while (1) {
            char c = sh->io->getchar();

            if (c == '\n' || c == '\r') {
                sh->io->putchar('\n');
                break;
            } else if (c == '\b' || c == 0x7F) {
                if (line_len > 0) {
                    line_len--;
                    sh->io->putchar('\b');
                    sh->io->putchar(' ');
                    sh->io->putchar('\b');
                }
            } else if (c >= 32 && c < 127 && line_len < 126) {
                line_buf[line_len++] = c;
                sh->io->putchar(c);
            }
        }
        line_buf[line_len] = '\0';

        /* Skip empty lines */
        if (line_len == 0) continue;

        /* Check for monitor commands */
        if (line_buf[0] == '.') {
            /* Exit monitor */
            g_monitor.active = false;
            sh->io->printf("Exiting monitor. %u bytes assembled.\n", g_monitor.buffer_size);
            break;
        }

        if (line_buf[0] == '=') {
            /* Show current address */
            sh->io->printf("Current: 0x%08X, Start: 0x%08X, Size: %u bytes\n",
                          g_monitor.current_addr, g_monitor.start_addr, g_monitor.buffer_size);
            continue;
        }

        if (line_buf[0] == '@') {
            /* Change address */
            const char *p = line_buf + 1;
            while (*p == ' ') p++;
            int32_t new_addr;
            if (rfasm_parse_number(&p, &new_addr)) {
                g_monitor.current_addr = (uint32_t)new_addr;
                g_monitor.buffer = (uint8_t *)(uintptr_t)new_addr;
                sh->io->printf("Address set to 0x%08X\n", new_addr);
            }
            continue;
        }

        if (line_buf[0] == '#') {
            /* Run from start address */
            sh->io->printf("Executing from 0x%08X...\n", g_monitor.start_addr);
            run_func_t func = (run_func_t)(uintptr_t)g_monitor.start_addr;
            func();
            sh->io->printf("Returned.\n");
            continue;
        }

        if (line_buf[0] == '?') {
            /* Help */
            sh->io->printf("Monitor commands:\n");
            sh->io->printf("  .           Exit monitor\n");
            sh->io->printf("  =           Show current address\n");
            sh->io->printf("  @ <addr>    Set address\n");
            sh->io->printf("  #           Run from start address\n");
            sh->io->printf("  ?           This help\n");
            sh->io->printf("  <instr>     Assemble instruction\n");
            continue;
        }

        /* Try to assemble the line as an instruction */
        rfasm_state_t asm_state;
        uint8_t temp_output[32];  /* Single instruction won't be more than 15 bytes */
        rfasm_init(&asm_state, temp_output, sizeof(temp_output));
        asm_state.org = g_monitor.current_addr;
        asm_state.pc = g_monitor.current_addr;

        /* Assemble the instruction */
        rfasm_error_t err = rfasm_assemble(&asm_state, line_buf);

        if (err != RFASM_OK) {
            sh->io->printf("Error: %s\n", rfasm_error_string(err));
            if (asm_state.error_msg[0]) {
                sh->io->printf("  %s\n", asm_state.error_msg);
            }
            continue;
        }

        if (asm_state.output_size == 0) {
            /* Label or comment only */
            continue;
        }

        /* Copy assembled bytes to memory */
        for (uint32_t i = 0; i < asm_state.output_size; i++) {
            g_monitor.buffer[i] = temp_output[i];
        }

        /* Show the assembled bytes */
        sh->io->printf("          ");
        for (uint32_t i = 0; i < asm_state.output_size; i++) {
            sh->io->printf("%02X ", temp_output[i]);
        }
        sh->io->printf("\n");

        /* Advance */
        g_monitor.buffer += asm_state.output_size;
        g_monitor.current_addr += asm_state.output_size;
        g_monitor.buffer_size += asm_state.output_size;
    }

    sh->last_result = 0;
}

/* ============================================================================
 * ASM2MEM Command - Assemble directly to memory address
 *
 * Usage: asm2mem <address> <source_file>
 * ============================================================================ */

static void sh_cmd_asm2mem(shell_state_t *sh, int argc, char **argv) {
    if (argc < 3) {
        sh->io->printf("Usage: asm2mem <address> <source_file>\n");
        sh->io->printf("Assembles file directly to memory address.\n");
        sh->last_result = 1;
        return;
    }

    /* Parse address */
    const char *p = argv[1];
    int32_t addr_val;
    if (!rfasm_parse_number(&p, &addr_val)) {
        sh->io->printf("Invalid address: %s\n", argv[1]);
        sh->last_result = 1;
        return;
    }

    /* Read source file */
    vfs_file_t *src_file = vfs_open(argv[2], VFS_O_RDONLY);
    if (!src_file) {
        sh->io->printf("Error: Cannot open '%s'\n", argv[2]);
        sh->last_result = 1;
        return;
    }

    int32_t bytes_read = vfs_read(src_file, g_rfasm_source, RFASM_SOURCE_BUFFER_SIZE - 1);
    vfs_close(src_file);

    if (bytes_read < 0) {
        sh->io->printf("Error: Failed to read '%s'\n", argv[2]);
        sh->last_result = 1;
        return;
    }

    g_rfasm_source[bytes_read] = '\0';

    /* Assemble directly to target memory */
    uint8_t *target = (uint8_t *)(uintptr_t)addr_val;
    rfasm_init(&g_rfasm_state, target, 65536);  /* Assume 64K max */
    g_rfasm_state.org = (uint32_t)addr_val;

    rfasm_error_t err = rfasm_assemble(&g_rfasm_state, g_rfasm_source);

    if (err != RFASM_OK) {
        sh->io->printf("Error on line %d: %s\n",
                      g_rfasm_state.error_line, rfasm_error_string(err));
        if (g_rfasm_state.error_msg[0]) {
            sh->io->printf("  %s\n", g_rfasm_state.error_msg);
        }
        sh->last_result = 1;
        return;
    }

    sh->io->printf("Assembled %u bytes to 0x%08X\n", g_rfasm_state.output_size, addr_val);
    sh->io->printf("Use 'run 0x%X' to execute.\n", addr_val);
    sh->last_result = 0;
}

/* ============================================================================
 * Register RF-ASM Shell Commands
 *
 * Call this function after shell_init() to add assembler commands.
 * ============================================================================ */

static inline void rfasm_shell_register(shell_state_t *sh) {
    /* Assembly commands */
    shell_register(sh, "asm",       "Assemble source file",       sh_cmd_asm,       1);
    shell_register(sh, "asm2mem",   "Assemble to memory address", sh_cmd_asm2mem,   2);
    shell_register(sh, "monitor",   "Interactive ML monitor",     sh_cmd_monitor,   0);

    /* Memory commands */
    shell_register(sh, "hexdump",   "Hex dump file or memory",    sh_cmd_hexdump,   1);
    shell_register(sh, "peek",      "Read memory bytes",          sh_cmd_peek,      1);
    shell_register(sh, "poke",      "Write memory bytes",         sh_cmd_poke,      2);
    shell_register(sh, "run",       "Execute code at address",    sh_cmd_run,       1);

    /* Help commands */
    shell_register(sh, "asmhelp",   "RF-ASM instruction ref",     sh_cmd_asmhelp,   0);
    shell_register(sh, "asmlabels", "Show assembly labels",       sh_cmd_asmlabels, 0);
}

#endif /* RFASM_SHELL_H */
