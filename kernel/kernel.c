/**
 * kernel.c - RETROFUTURE OS Kernel
 * 
 * Refactored kernel with clean separation of:
 *   - Core kernel initialization
 *   - VFS (Virtual File System)
 *   - Shell (standalone, reusable)
 *   - Device drivers
 *
 * Target: Pentium III 600MHz, 256MB RAM
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "boot_info.h"
#include "terminal.h"
#include "idt.h"
#include "keyboard.h"
#include "ata.h"
#include "fat12.h"
#include "fat12_write.h"
#include "ramdisk.h"
#include "vfs.h"
#include "fat12_vfs.h"
#include "ata_blkdev.h"
#include "shell.h"

/* ============================================================================
 * Forward Declarations for Intrusive List
 * ============================================================================ */

typedef struct ilist_node {
    struct ilist_node *next;
    struct ilist_node *prev;
} ilist_node_t;

typedef struct ilist_head {
    ilist_node_t node;
} ilist_head_t;

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define ilist_entry(ptr, type, member) container_of(ptr, type, member)

static inline void ilist_init_head(ilist_head_t *head) {
    head->node.next = &head->node;
    head->node.prev = &head->node;
}

static inline void ilist_init_node(ilist_node_t *node) {
    node->next = NULL;
    node->prev = NULL;
}

static inline bool ilist_is_empty(const ilist_head_t *head) {
    return head->node.next == &head->node;
}

static inline void __ilist_insert(ilist_node_t *node, ilist_node_t *prev, ilist_node_t *next) {
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

static inline void ilist_push_back(ilist_head_t *head, ilist_node_t *node) {
    __ilist_insert(node, head->node.prev, &head->node);
}

static inline ilist_node_t *ilist_pop_front(ilist_head_t *head) {
    if (ilist_is_empty(head)) return NULL;
    ilist_node_t *node = head->node.next;
    node->next->prev = &head->node;
    head->node.next = node->next;
    return node;
}

/* ============================================================================
 * Memory Manager
 * ============================================================================ */

#define MM_MAX_REGIONS 64

typedef struct mem_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    ilist_node_t addr_link;
    ilist_node_t free_link;
    ilist_head_t on_alloc;
    ilist_head_t on_free;
} mem_region_t;

typedef struct mem_manager {
    ilist_head_t all_regions;
    ilist_head_t free_regions;
    ilist_head_t region_pool;

    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t kernel_bytes;

    ilist_head_t on_alloc;
    ilist_head_t on_free;
    ilist_head_t on_oom;

    mem_region_t region_storage[MM_MAX_REGIONS];
} mem_manager_t;

static mem_manager_t g_mm;

static void mm_init(mem_manager_t *mm) {
    ilist_init_head(&mm->all_regions);
    ilist_init_head(&mm->free_regions);
    ilist_init_head(&mm->region_pool);

    ilist_init_head(&mm->on_alloc);
    ilist_init_head(&mm->on_free);
    ilist_init_head(&mm->on_oom);

    mm->total_bytes = 0;
    mm->free_bytes = 0;
    mm->kernel_bytes = 0;

    for (int i = 0; i < MM_MAX_REGIONS; i++) {
        ilist_init_node(&mm->region_storage[i].addr_link);
        ilist_init_node(&mm->region_storage[i].free_link);
        ilist_init_head(&mm->region_storage[i].on_alloc);
        ilist_init_head(&mm->region_storage[i].on_free);
        ilist_push_back(&mm->region_pool, &mm->region_storage[i].addr_link);
    }
}

static void mm_parse_e820(mem_manager_t *mm, e820_map_t *map) {
    for (uint32_t i = 0; i < map->count; i++) {
        e820_entry_t *e = &map->entries[i];

        if (e->type == 1) {  /* Usable memory */
            mm->total_bytes += e->length;
            mm->free_bytes += e->length;
        }

        ilist_node_t *pool_node = ilist_pop_front(&mm->region_pool);
        if (!pool_node) continue;

        mem_region_t *region = ilist_entry(pool_node, mem_region_t, addr_link);
        region->base = e->base;
        region->length = e->length;
        region->type = e->type;

        ilist_push_back(&mm->all_regions, &region->addr_link);
        if (e->type == 1) {
            ilist_push_back(&mm->free_regions, &region->free_link);
        }
    }
}

/* ============================================================================
 * Slab Allocator
 * ============================================================================ */

#define SLAB_SIZES 8

typedef struct slab_allocator {
    uint32_t heap_start;
    uint32_t heap_size;
    uint32_t heap_used;

    struct {
        uint32_t size;
        uint32_t free_count;
        void *free_list;
    } caches[SLAB_SIZES];
} slab_allocator_t;

static slab_allocator_t g_slab;

static void slab_init(slab_allocator_t *slab, uint32_t heap_start, uint32_t heap_size) {
    slab->heap_start = heap_start;
    slab->heap_size = heap_size;
    slab->heap_used = 0;

    uint32_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    for (int i = 0; i < SLAB_SIZES; i++) {
        slab->caches[i].size = sizes[i];
        slab->caches[i].free_count = 0;
        slab->caches[i].free_list = NULL;
    }
}

/* ============================================================================
 * EventChains - Reactive Event System
 * ============================================================================ */

#define EVENT_MAX_HANDLERS 8

typedef enum {
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_TIMER_TICK,
    EVENT_FILE_CHANGE,
    EVENT_SYSTEM,
} event_type_t;

typedef struct event {
    event_type_t type;
    void *source;
    void *data;
    bool handled;
} event_t;

typedef struct {
    char ascii;
    uint8_t scancode;
    uint8_t modifiers;
} key_event_data_t;

typedef void (*event_handler_t)(event_t *event);

typedef struct event_chain {
    event_handler_t handlers[EVENT_MAX_HANDLERS];
    int count;
} event_chain_t;

static inline void event_chain_init(event_chain_t *chain) {
    chain->count = 0;
    for (int i = 0; i < EVENT_MAX_HANDLERS; i++) {
        chain->handlers[i] = NULL;
    }
}

static inline bool event_chain_subscribe(event_chain_t *chain, event_handler_t handler) {
    if (chain->count >= EVENT_MAX_HANDLERS) return false;
    chain->handlers[chain->count++] = handler;
    return true;
}

static inline bool event_chain_unsubscribe(event_chain_t *chain, event_handler_t handler) {
    for (int i = 0; i < chain->count; i++) {
        if (chain->handlers[i] == handler) {
            for (int j = i; j < chain->count - 1; j++) {
                chain->handlers[j] = chain->handlers[j + 1];
            }
            chain->handlers[--chain->count] = NULL;
            return true;
        }
    }
    return false;
}

static inline void event_chain_fire(event_chain_t *chain, event_t *event) {
    event->handled = false;
    for (int i = 0; i < chain->count && !event->handled; i++) {
        if (chain->handlers[i]) {
            chain->handlers[i](event);
        }
    }
}

/* Global event chains */
static event_chain_t g_keyboard_events;
static event_chain_t g_system_events;
static event_chain_t g_file_events;

/* ============================================================================
 * Terminal and Output (both VGA text and VESA graphics)
 * ============================================================================ */

static terminal_t g_term;
static bool g_use_vga_text = false;

/* VGA text mode support */
#define VGA_TEXT_ADDR 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint16_t *g_vga_buffer = (uint16_t *)VGA_TEXT_ADDR;
static int g_vga_x = 0, g_vga_y = 0;
static uint8_t g_vga_color = 0x0A;

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            g_vga_buffer[y * VGA_WIDTH + x] = g_vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        g_vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (g_vga_color << 8) | ' ';
    }
    g_vga_y = VGA_HEIGHT - 1;
}

static void vga_putchar(char c) {
    if (c == '\n') {
        g_vga_x = 0;
        g_vga_y++;
    } else if (c == '\r') {
        g_vga_x = 0;
    } else if (c == '\b') {
        if (g_vga_x > 0) {
            g_vga_x--;
            g_vga_buffer[g_vga_y * VGA_WIDTH + g_vga_x] = (g_vga_color << 8) | ' ';
        }
    } else {
        g_vga_buffer[g_vga_y * VGA_WIDTH + g_vga_x] = (g_vga_color << 8) | c;
        g_vga_x++;
        if (g_vga_x >= VGA_WIDTH) {
            g_vga_x = 0;
            g_vga_y++;
        }
    }
    if (g_vga_y >= VGA_HEIGHT) {
        vga_scroll();
    }
}

static void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        g_vga_buffer[i] = (g_vga_color << 8) | ' ';
    }
    g_vga_x = 0;
    g_vga_y = 0;
}

/* Universal output functions */
static void kputchar(char c) {
    if (g_use_vga_text) {
        vga_putchar(c);
    } else {
        terminal_putchar(&g_term, c);
    }
}

static void kputs(const char *s) {
    while (*s) kputchar(*s++);
}

/* Simple printf implementation */
static void kprintf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            /* Width handling */
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }

            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (s) kputs(s);
                    else kputs("(null)");
                    break;
                }
                case 'd':
                case 'i': {
                    int val = __builtin_va_arg(args, int);
                    if (val < 0) {
                        kputchar('-');
                        val = -val;
                    }
                    char buf[12];
                    int i = 0;
                    do {
                        buf[i++] = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    while (i < width) { kputchar(' '); width--; }
                    while (i--) kputchar(buf[i]);
                    break;
                }
                case 'u': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char buf[12];
                    int i = 0;
                    do {
                        buf[i++] = '0' + (val % 10);
                        val /= 10;
                    } while (val);
                    while (i < width) { kputchar(' '); width--; }
                    while (i--) kputchar(buf[i]);
                    break;
                }
                case 'x':
                case 'X': {
                    uint32_t val = __builtin_va_arg(args, uint32_t);
                    char buf[9];
                    int i = 0;
                    const char *hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    do {
                        buf[i++] = hex[val & 0xF];
                        val >>= 4;
                    } while (val);
                    while (i < width) { kputchar('0'); width--; }
                    while (i--) kputchar(buf[i]);
                    break;
                }
                case 'c':
                    kputchar((char)__builtin_va_arg(args, int));
                    break;
                case '%':
                    kputchar('%');
                    break;
                default:
                    kputchar('%');
                    kputchar(*fmt);
                    break;
            }
        } else {
            kputchar(*fmt);
        }
        fmt++;
    }

    __builtin_va_end(args);
}

/* String utilities */
static int kstrlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void kstrcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ============================================================================
 * Interrupt Handlers
 * ============================================================================ */

/* exception_names[] is defined in idt.h */

void exception_handler(int_frame_t *frame) {
    kprintf("\n!!! EXCEPTION: %s !!!\n", exception_names[frame->int_no]);
    kprintf("Error Code: 0x%x\n", frame->err_code);
    kprintf("EIP: 0x%x  CS: 0x%x  EFLAGS: 0x%x\n",
            frame->eip, frame->cs, frame->eflags);
    kprintf("EAX: 0x%x  EBX: 0x%x  ECX: 0x%x  EDX: 0x%x\n",
            frame->eax, frame->ebx, frame->ecx, frame->edx);

    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void irq_handler(int_frame_t *frame) {
    uint8_t irq = frame->int_no - INT_IRQ_BASE;

    if (g_irq_handlers[irq]) {
        g_irq_handlers[irq](frame);
    }

    pic_eoi(irq);
}

/* ============================================================================
 * Keyboard Event Integration
 * ============================================================================ */

static void fire_key_event(char ascii, uint8_t scancode, uint8_t modifiers, bool pressed) {
    key_event_data_t data = { ascii, scancode, modifiers };
    event_t evt = {
        .source = &g_keyboard_events,
        .data = &data,
        .type = pressed ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE,
        .handled = false
    };
    event_chain_fire(&g_keyboard_events, &evt);
}

/* ============================================================================
 * Direct ATA Block Device Callbacks (for format, etc.)
 * ============================================================================ */

static ata_drive_t *g_format_drive = NULL;

static uint32_t ata_block_read(uint32_t lba, uint8_t count, void *buf) {
    if (!g_format_drive) return 0;
    return ata_read_sectors(g_format_drive, lba, count, buf);
}

static uint32_t ata_block_write(uint32_t lba, uint8_t count, const void *buf) {
    if (!g_format_drive) return 0;
    return ata_write_sectors(g_format_drive, lba, count, buf);
}

/* ============================================================================
 * Shell I/O Interface
 * ============================================================================ */

/* Shell I/O callbacks */
static void shell_io_putchar(char c) {
    kputchar(c);
}

static void shell_io_puts(const char *s) {
    kputs(s);
}

static void shell_io_printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    /* Simple printf - just forward to kprintf via a buffer */
    /* For simplicity, just use kprintf directly */
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    kputs(s ? s : "(null)");
                    break;
                }
                case 'd': case 'i': {
                    kprintf("%d", __builtin_va_arg(args, int));
                    break;
                }
                case 'u': {
                    kprintf("%u", __builtin_va_arg(args, uint32_t));
                    break;
                }
                case 'x': {
                    kprintf("%x", __builtin_va_arg(args, uint32_t));
                    break;
                }
                case 'c':
                    kputchar((char)__builtin_va_arg(args, int));
                    break;
                case '%':
                    kputchar('%');
                    break;
                default:
                    kputchar('%');
                    kputchar(*fmt);
                    break;
            }
        } else {
            kputchar(*fmt);
        }
        fmt++;
    }

    __builtin_va_end(args);
}

static char shell_io_getchar(void) {
    return keyboard_getchar();
}

static void shell_io_clear(void) {
    if (g_use_vga_text) {
        vga_clear();
    } else {
        terminal_clear(&g_term);
        terminal_draw_border(&g_term, " RETROFUTURE OS v0.3 ");
        terminal_set_cursor(&g_term, 2, 2);
    }
}

static shell_io_t g_shell_io = {
    .putchar = shell_io_putchar,
    .puts = shell_io_puts,
    .printf = shell_io_printf,
    .getchar = shell_io_getchar,
    .getchar_nonblock = NULL,
    .clear = shell_io_clear,
    .set_color = NULL,
    .get_cursor = NULL,
    .set_cursor = NULL,
    .user_data = NULL
};

/* ============================================================================
 * Kernel Context (passed to shell commands)
 * ============================================================================ */

typedef struct kernel_context {
    /* Memory */
    mem_manager_t *mm;
    slab_allocator_t *slab;

    /* Events */
    event_chain_t *keyboard_events;
    event_chain_t *system_events;
    event_chain_t *file_events;

    /* VFS */
    vfs_mount_t *root_mount;
    blkdev_t boot_device;

    /* Terminal */
    terminal_t *term;
    bool use_vga_text;

    /* Boot info */
    boot_info_t *boot_info;
} kernel_context_t;

static kernel_context_t g_kernel_ctx;

/* ============================================================================
 * Shell Command Implementations
 * ============================================================================ */

static void kcmd_clear(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    if (sh->io->clear) sh->io->clear();
    sh->last_result = 0;
}

static void kcmd_info(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    kernel_context_t *ctx = (kernel_context_t *)sh->context;

    sh->io->printf("\nSystem Information:\n");
    sh->io->printf("-------------------\n");
    sh->io->printf("  CPU: Pentium III compatible\n");
    sh->io->printf("  RAM: %u KB total\n", ctx->mm->total_bytes / 1024);

    if (!ctx->use_vga_text) {
        sh->io->printf("  Display: %ux%u VESA\n",
                      ctx->term->width, ctx->term->height);
    } else {
        sh->io->printf("  Display: 80x25 VGA text mode\n");
    }

    sh->io->printf("\n");
    sh->last_result = 0;
}

static void kcmd_mem(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    kernel_context_t *ctx = (kernel_context_t *)sh->context;

    e820_map_t *map = get_e820_map(ctx->boot_info);

    sh->io->printf("\nMemory Map (E820):\n");
    sh->io->printf("------------------\n");

    for (uint32_t i = 0; i < map->count; i++) {
        e820_entry_t *e = &map->entries[i];
        const char *type_str;

        switch (e->type) {
            case 1: type_str = "Usable"; break;
            case 2: type_str = "Reserved"; break;
            case 3: type_str = "ACPI Reclaim"; break;
            case 4: type_str = "ACPI NVS"; break;
            default: type_str = "Unknown"; break;
        }

        sh->io->printf("  0x%x - 0x%x  %s\n",
                      (uint32_t)e->base,
                      (uint32_t)(e->base + e->length - 1),
                      type_str);
    }

    sh->io->printf("\n  Total usable: %u KB\n\n", ctx->mm->total_bytes / 1024);
    sh->last_result = 0;
}

static void kcmd_disk(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    kernel_context_t *ctx = (kernel_context_t *)sh->context;

    sh->io->printf("\nDisk Information:\n");
    sh->io->printf("-----------------\n");

    if (ctx->boot_device.present) {
        sh->io->printf("  Type: %s\n",
                      ctx->boot_device.type == BLKDEV_ATA ? "ATA/IDE" : "Unknown");
        sh->io->printf("  Model: %s\n", ctx->boot_device.model);
        sh->io->printf("  Size: %u MB\n", ctx->boot_device.total_sectors / 2048);
        sh->io->printf("  Sectors: %u\n", ctx->boot_device.total_sectors);
    } else {
        sh->io->printf("  No disk detected\n");
    }

    sh->io->printf("\n");
    sh->last_result = 0;
}

static void kcmd_ls(shell_state_t *sh, int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : sh->cwd;

    vfs_dir_t *dir = vfs_opendir(path);
    if (!dir) {
        sh->io->printf("Cannot open directory: %s\n", path);
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\nDirectory: %s\n", path);
    sh->io->printf("----------------\n");

    vfs_dirent_t entry;
    while (vfs_readdir(dir, &entry)) {
        if (entry.type == VFS_DIRECTORY) {
            sh->io->printf("  [DIR]  %s\n", entry.name);
        } else {
            sh->io->printf("  %6u  %s\n", entry.size, entry.name);
        }
    }

    sh->io->printf("\n");
    vfs_closedir(dir);
    sh->last_result = 0;
}

static void kcmd_cat(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: cat <filename>\n");
        sh->last_result = 1;
        return;
    }

    /* Build full path */
    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        vfs_strcpy(path, argv[1]);
    } else {
        vfs_strcpy(path, sh->cwd);
        int len = vfs_strlen(path);
        if (len > 0 && path[len-1] != '/') {
            path[len++] = '/';
        }
        vfs_strcpy(path + len, argv[1]);
    }

    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        sh->io->printf("File not found: %s\n", argv[1]);
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\n");

    char buf[512];
    int32_t bytes;
    while ((bytes = vfs_read(file, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        sh->io->puts(buf);
    }

    sh->io->printf("\n\n");
    vfs_close(file);
    sh->last_result = 0;
}

static void kcmd_mkdir(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: mkdir <directory>\n");
        sh->last_result = 1;
        return;
    }

    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        vfs_strcpy(path, argv[1]);
    } else {
        vfs_strcpy(path, sh->cwd);
        int len = vfs_strlen(path);
        if (len > 0 && path[len-1] != '/') path[len++] = '/';
        vfs_strcpy(path + len, argv[1]);
    }

    if (vfs_mkdir(path)) {
        sh->io->printf("Created directory: %s\n", argv[1]);
        sh->last_result = 0;
    } else {
        sh->io->printf("Failed to create directory: %s\n", argv[1]);
        sh->last_result = 1;
    }
}

static void kcmd_touch(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: touch <filename>\n");
        sh->last_result = 1;
        return;
    }

    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        vfs_strcpy(path, argv[1]);
    } else {
        vfs_strcpy(path, sh->cwd);
        int len = vfs_strlen(path);
        if (len > 0 && path[len-1] != '/') path[len++] = '/';
        vfs_strcpy(path + len, argv[1]);
    }

    if (vfs_create(path)) {
        sh->io->printf("Created: %s\n", argv[1]);
        sh->last_result = 0;
    } else {
        sh->io->printf("Failed to create: %s\n", argv[1]);
        sh->last_result = 1;
    }
}

static void kcmd_del(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: del <filename>\n");
        sh->last_result = 1;
        return;
    }

    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        vfs_strcpy(path, argv[1]);
    } else {
        vfs_strcpy(path, sh->cwd);
        int len = vfs_strlen(path);
        if (len > 0 && path[len-1] != '/') path[len++] = '/';
        vfs_strcpy(path + len, argv[1]);
    }

    if (vfs_unlink(path)) {
        sh->io->printf("Deleted: %s\n", argv[1]);
        sh->last_result = 0;
    } else {
        sh->io->printf("Failed to delete: %s\n", argv[1]);
        sh->last_result = 1;
    }
}

static void kcmd_write(shell_state_t *sh, int argc, char **argv) {
    if (argc < 3) {
        sh->io->printf("Usage: write <filename> <content>\n");
        sh->last_result = 1;
        return;
    }

    /* Build path */
    char path[VFS_MAX_PATH];
    if (argv[1][0] == '/') {
        vfs_strcpy(path, argv[1]);
    } else {
        vfs_strcpy(path, sh->cwd);
        int len = vfs_strlen(path);
        if (len > 0 && path[len-1] != '/') path[len++] = '/';
        vfs_strcpy(path + len, argv[1]);
    }

    /* Reconstruct content from remaining args */
    char content[SHELL_CMD_BUF_SIZE];
    content[0] = '\0';
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            int len = sh_strlen(content);
            content[len] = ' ';
            content[len+1] = '\0';
        }
        sh_strcpy(content + sh_strlen(content), argv[i]);
    }

    /* Add newline */
    int len = sh_strlen(content);
    content[len] = '\n';
    content[len+1] = '\0';

    /* Open file with create and truncate */
    vfs_file_t *file = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (!file) {
        sh->io->printf("Failed to open: %s\n", argv[1]);
        sh->last_result = 1;
        return;
    }

    int32_t written = vfs_write(file, content, len + 1);
    vfs_close(file);

    if (written > 0) {
        sh->io->printf("Wrote %d bytes to %s\n", written, argv[1]);
        sh->last_result = 0;
    } else {
        sh->io->printf("Failed to write to %s\n", argv[1]);
        sh->last_result = 1;
    }
}

static void kcmd_reboot(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;

    sh->io->printf("Syncing filesystems...\n");
    vfs_sync_all();

    sh->io->printf("Rebooting...\n");

    /* Triple fault to reboot */
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    /* If that didn't work, halt */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void kcmd_color(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    kernel_context_t *ctx = (kernel_context_t *)sh->context;

    if (ctx->use_vga_text) {
        sh->io->printf("Color cycling not available in text mode\n");
        sh->last_result = 1;
        return;
    }

    static int scheme = 0;
    const char *names[] = {"Green", "Amber", "White", "Cyan"};
    phosphor_scheme_t schemes[] = {PHOSPHOR_GREEN, PHOSPHOR_AMBER, PHOSPHOR_WHITE, PHOSPHOR_CYAN};

    scheme = (scheme + 1) % 4;
    terminal_set_scheme(ctx->term, schemes[scheme]);

    sh->io->printf("Color scheme: %s\n", names[scheme]);
    sh->last_result = 0;
}

/* Test ATA write capability */
static void kcmd_atatest(shell_state_t *sh, int argc, char **argv) {
    kernel_context_t *ctx = (kernel_context_t *)sh->context;

    if (!ctx->boot_device.present) {
        sh->io->printf("No ATA drive present\n");
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\nATA Write Test\n");
    sh->io->printf("--------------\n");
    sh->io->printf("Drive: %s\n", ctx->boot_device.model);
    sh->io->printf("Size: %u sectors\n\n", ctx->boot_device.total_sectors);

    /* Use LBA from argument, or default to 100 */
    uint32_t test_lba = 100;
    if (argc > 1) {
        test_lba = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++) {
            test_lba = test_lba * 10 + (*p - '0');
        }
    }

    sh->io->printf("Testing write at LBA %u...\n", test_lba);

    if (test_lba == 0) {
        sh->io->printf("WARNING: LBA 0 is the boot sector!\n");
    }

    /* Get the underlying ATA drive */
    ata_drive_t *drive = (ata_drive_t *)ctx->boot_device.driver_data;
    if (!drive) {
        sh->io->printf("Error: No ATA driver data\n");
        sh->last_result = 1;
        return;
    }

    /* Create test pattern */
    uint8_t write_buf[512];
    uint8_t read_buf[512];

    for (int i = 0; i < 512; i++) {
        write_buf[i] = (uint8_t)(i ^ 0xAA ^ (test_lba & 0xFF));
    }

    /* Write test sector */
    sh->io->printf("  Writing test pattern...\n");
    uint32_t written = ata_write_sectors(drive, test_lba, 1, write_buf);
    if (written != 1) {
        sh->io->printf("  FAILED: Write returned %u (expected 1)\n", written);
        sh->last_result = 1;
        return;
    }
    sh->io->printf("  Write OK\n");

    /* Read it back */
    sh->io->printf("  Reading back...\n");
    uint32_t read = ata_read_sectors(drive, test_lba, 1, read_buf);
    if (read != 1) {
        sh->io->printf("  FAILED: Read returned %u (expected 1)\n", read);
        sh->last_result = 1;
        return;
    }
    sh->io->printf("  Read OK\n");

    /* Verify */
    sh->io->printf("  Verifying...\n");
    int errors = 0;
    for (int i = 0; i < 512; i++) {
        if (read_buf[i] != write_buf[i]) {
            if (errors < 5) {
                sh->io->printf("    Mismatch at %d: wrote 0x%x, read 0x%x\n",
                              i, write_buf[i], read_buf[i]);
            }
            errors++;
        }
    }

    if (errors == 0) {
        sh->io->printf("  Verify OK - All 512 bytes match!\n");
        sh->io->printf("\nATA write test PASSED!\n");
        sh->last_result = 0;
    } else {
        sh->io->printf("  FAILED: %d bytes mismatched\n", errors);
        sh->last_result = 1;
    }
}

/* Format drive with FAT12 - with progress output */
static void kcmd_format(shell_state_t *sh, int argc, char **argv) {
    kernel_context_t *ctx = (kernel_context_t *)sh->context;
    const char *label = (argc > 1) ? argv[1] : NULL;

    if (!ctx->boot_device.present) {
        sh->io->printf("No drive to format\n");
        sh->last_result = 1;
        return;
    }

    sh->io->printf("\n!!! WARNING: This will ERASE ALL DATA !!!\n");
    sh->io->printf("Drive: %s\n", ctx->boot_device.model);
    sh->io->printf("Size: %u MB\n", ctx->boot_device.total_sectors / 2048);
    sh->io->printf("\nType 'YES' to confirm: ");

    /* Read confirmation */
    char confirm[16];
    int i = 0;
    while (i < 15) {
        char c = sh->io->getchar();
        if (c == '\n' || c == '\r') {
            sh->io->putchar('\n');
            break;
        }
        if (c == '\b' && i > 0) {
            i--;
            sh->io->putchar('\b');
            sh->io->putchar(' ');
            sh->io->putchar('\b');
        } else if (c >= 32 && c < 127) {
            confirm[i++] = c;
            sh->io->putchar(c);
        }
    }
    confirm[i] = '\0';

    if (sh_strcmp(confirm, "YES") != 0) {
        sh->io->printf("Format cancelled.\n");
        sh->last_result = 1;
        return;
    }

    /* Unmount current filesystem */
    if (ctx->root_mount) {
        sh->io->printf("Unmounting filesystem...\n");
        vfs_unmount("/");
        ctx->root_mount = NULL;
    }

    /* Calculate format parameters */
    uint32_t total_sectors = ctx->boot_device.total_sectors;

    /* Limit to FAT12 maximum (~32MB) */
    #define FAT12_MAX_SECTORS 65536
    if (total_sectors > FAT12_MAX_SECTORS) {
        total_sectors = FAT12_MAX_SECTORS;
        sh->io->printf("Note: Formatting first %u MB only (FAT12 limit)\n",
                      total_sectors / 2048);
    }

    /* Get underlying ATA drive for direct writes */
    ata_drive_t *drive = (ata_drive_t *)ctx->boot_device.driver_data;
    if (!drive) {
        sh->io->printf("Error: No ATA driver data\n");
        sh->last_result = 1;
        return;
    }

    /* Set global drive pointer for block callbacks */
    g_format_drive = drive;

    /* Calculate format parameters MANUALLY to avoid buggy fat12_calc_params */
    /* For 65536 sectors (32MB), we need sectors_per_cluster that gives < 4085 clusters */
    uint8_t sectors_per_cluster = 1;
    uint16_t reserved_sectors = 1;
    uint8_t num_fats = 2;
    uint16_t root_entry_count = 512;  /* 512 entries = 16KB = 32 sectors */
    uint32_t root_sectors = (root_entry_count * 32 + 511) / 512;

    /* Iteratively find correct cluster size */
    while (sectors_per_cluster < 128) {
        uint32_t fat_sectors_est = ((total_sectors / sectors_per_cluster) * 3 / 2 + 511) / 512;
        uint32_t data_start = reserved_sectors + (num_fats * fat_sectors_est) + root_sectors;
        uint32_t data_sectors = total_sectors - data_start;
        uint32_t clusters = data_sectors / sectors_per_cluster;

        if (clusters <= 4084) break;
        sectors_per_cluster *= 2;
    }

    /* Recalculate with final cluster size */
    uint32_t data_sectors_approx = total_sectors - reserved_sectors - root_sectors;
    uint32_t clusters_approx = data_sectors_approx / sectors_per_cluster;
    uint16_t sectors_per_fat = ((clusters_approx * 3 / 2) + 511) / 512;
    if (sectors_per_fat < 1) sectors_per_fat = 1;

    sh->io->printf("\nFormat parameters:\n");
    sh->io->printf("  Sectors/cluster: %u\n", sectors_per_cluster);
    sh->io->printf("  FAT sectors: %u (x%u FATs)\n", sectors_per_fat, num_fats);
    sh->io->printf("  Root entries: %u (%u sectors)\n", root_entry_count, root_sectors);

    /* Manual format with progress output */
    uint8_t sector[512];
    bool success = true;
    uint32_t written = 0;

    /* ========== Write Boot Sector ========== */
    sh->io->printf("\nWriting boot sector (LBA 0)...");

    for (int j = 0; j < 512; j++) sector[j] = 0;

    /* Jump instruction */
    sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;

    /* OEM Name */
    sector[3] = 'R'; sector[4] = 'E'; sector[5] = 'T'; sector[6] = 'R';
    sector[7] = 'O'; sector[8] = 'F'; sector[9] = 'U'; sector[10] = 'T';

    /* BPB */
    *(uint16_t *)&sector[11] = 512;  /* bytes per sector */
    sector[13] = sectors_per_cluster;
    *(uint16_t *)&sector[14] = reserved_sectors;
    sector[16] = num_fats;
    *(uint16_t *)&sector[17] = root_entry_count;
    *(uint16_t *)&sector[19] = (total_sectors < 65536) ? total_sectors : 0;
    sector[21] = 0xF8;  /* media type: fixed disk */
    *(uint16_t *)&sector[22] = sectors_per_fat;
    *(uint16_t *)&sector[24] = 63;   /* sectors per track */
    *(uint16_t *)&sector[26] = 16;   /* number of heads */
    *(uint32_t *)&sector[28] = 0;    /* hidden sectors */
    *(uint32_t *)&sector[32] = (total_sectors >= 65536) ? total_sectors : 0;

    /* Extended boot record */
    sector[36] = 0x80;  /* Drive number */
    sector[37] = 0x00;
    sector[38] = 0x29;  /* Extended boot signature */
    *(uint32_t *)&sector[39] = 0x12345678;  /* Volume ID */

    /* Volume label */
    const char *vol_label = (label && *label) ? label : "RETROFUTURE";
    for (int j = 0; j < 11; j++) {
        if (vol_label[j] && vol_label[j] != '\0') {
            char c = vol_label[j];
            if (c >= 'a' && c <= 'z') c -= 32;
            sector[43 + j] = c;
        } else {
            sector[43 + j] = ' ';
        }
    }

    /* FS Type */
    sector[54] = 'F'; sector[55] = 'A'; sector[56] = 'T';
    sector[57] = '1'; sector[58] = '2'; sector[59] = ' ';
    sector[60] = ' '; sector[61] = ' ';

    /* Boot signature */
    sector[510] = 0x55;
    sector[511] = 0xAA;

    if (ata_write_sectors(drive, 0, 1, sector) != 1) {
        sh->io->printf(" FAILED!\n");
        success = false;
        goto format_done;
    }
    sh->io->printf(" OK\n");
    written++;

    /* ========== Write FAT Tables ========== */
    uint32_t fat_start = reserved_sectors;

    for (uint8_t fat_num = 0; fat_num < num_fats && success; fat_num++) {
        sh->io->printf("Writing FAT %u (%u sectors)...", fat_num + 1, sectors_per_fat);

        uint32_t fat_sector = fat_start + (fat_num * sectors_per_fat);

        for (uint16_t s = 0; s < sectors_per_fat; s++) {
            for (int j = 0; j < 512; j++) sector[j] = 0;

            /* First sector has reserved entries */
            if (s == 0) {
                sector[0] = 0xF8;  /* media type */
                sector[1] = 0xFF;
                sector[2] = 0xFF;
            }

            if (ata_write_sectors(drive, fat_sector + s, 1, sector) != 1) {
                sh->io->printf(" FAILED at sector %u!\n", s);
                success = false;
                break;
            }
            written++;

            /* Progress dot every 10 sectors */
            if ((s % 10) == 9) sh->io->putchar('.');
        }

        if (success) sh->io->printf(" OK\n");
    }

    /* ========== Write Root Directory ========== */
    if (success) {
        uint32_t root_start = fat_start + (num_fats * sectors_per_fat);

        sh->io->printf("Writing root directory (%u sectors)...", root_sectors);

        for (uint32_t s = 0; s < root_sectors && success; s++) {
            for (int j = 0; j < 512; j++) sector[j] = 0;

            /* First sector contains volume label */
            if (s == 0) {
                /* Volume label entry */
                for (int j = 0; j < 11; j++) {
                    if (vol_label[j] && vol_label[j] != '\0') {
                        char c = vol_label[j];
                        if (c >= 'a' && c <= 'z') c -= 32;
                        sector[j] = c;
                    } else {
                        sector[j] = ' ';
                    }
                }
                sector[11] = 0x08;  /* Volume label attribute */
            }

            if (ata_write_sectors(drive, root_start + s, 1, sector) != 1) {
                sh->io->printf(" FAILED at sector %u!\n", s);
                success = false;
                break;
            }
            written++;
        }

        if (success) sh->io->printf(" OK\n");
    }

format_done:
    g_format_drive = NULL;

    if (success) {
        sh->io->printf("\nFormat complete! Wrote %u sectors.\n", written);

        /* Remount */
        sh->io->printf("Remounting...\n");
        vfs_mount_t *mnt = vfs_mount("/", &ctx->boot_device, fat12_get_vfs_ops(), NULL, NULL);
        if (mnt) {
            ctx->root_mount = mnt;
            sh->io->printf("Filesystem mounted successfully.\n");
            sh->last_result = 0;
        } else {
            sh->io->printf("Warning: Failed to remount.\n");
            sh->last_result = 1;
        }
    } else {
        sh->io->printf("\nFormat FAILED after %u sectors.\n", written);
        sh->last_result = 1;
    }
}

/* ============================================================================
 * Initialize Shell with Kernel Commands
 * ============================================================================ */

static shell_state_t g_shell;

static void setup_kernel_shell(kernel_context_t *ctx) {
    shell_init(&g_shell, &g_shell_io, ctx);

    /* Register kernel-specific commands */
    shell_register(&g_shell, "clear",   "Clear screen",              kcmd_clear,   0);
    shell_register(&g_shell, "info",    "System information",        kcmd_info,    0);
    shell_register(&g_shell, "mem",     "Memory information",        kcmd_mem,     0);
    shell_register(&g_shell, "disk",    "Disk information",          kcmd_disk,    0);
    shell_register(&g_shell, "color",   "Cycle terminal colors",     kcmd_color,   0);
    shell_register(&g_shell, "reboot",  "Reboot system",             kcmd_reboot,  0);
    shell_register(&g_shell, "atatest", "Test ATA write [lba]",       kcmd_atatest, 0);
    shell_register(&g_shell, "format",  "Format drive (FAT12)",      kcmd_format,  0);

    /* File commands */
    shell_register(&g_shell, "ls",     "List directory",            kcmd_ls,     0);
    shell_register(&g_shell, "cat",    "Display file contents",     kcmd_cat,    1);
    shell_register(&g_shell, "mkdir",  "Create directory",          kcmd_mkdir,  1);
    shell_register(&g_shell, "touch",  "Create empty file",         kcmd_touch,  1);
    shell_register(&g_shell, "del",    "Delete file",               kcmd_del,    1);
    shell_register(&g_shell, "write",  "Write text to file",        kcmd_write,  2);
}

/* ============================================================================
 * Kernel Entry Point
 * ============================================================================ */

/* External font data (defined in drivers/font_8x16.c, declared in terminal.h) */
extern const uint8_t terminal_font_8x16[];

void kernel_main(boot_info_t *bi) {
    /* Verify boot info */
    if (bi->magic != BOOT_MAGIC) {
        /* Can't even verify boot info, just halt */
        while (1) { __asm__ volatile ("hlt"); }
    }

    /* Initialize terminal based on video mode */
    if (bi->vesa_enabled && bi->framebuffer) {
        /* VESA graphics mode */
        terminal_config_t term_cfg = {
            .scheme = PHOSPHOR_GREEN,
            .colors = COLORS_GREEN,
            .scanlines = true,
            .cursor_blink = true,
            .cursor_rate = 15
        };
        terminal_init(&g_term, bi, &term_cfg);
        terminal_clear(&g_term);
        terminal_draw_border(&g_term, " RETROFUTURE OS v0.3 ");
        terminal_set_cursor(&g_term, 2, 2);
        g_use_vga_text = false;
    } else {
        /* VGA text mode fallback */
        vga_clear();
        g_use_vga_text = true;
    }

    kprintf("========================================\n");
    kprintf("         RETROFUTURE OS v0.3\n");
    kprintf("========================================\n\n");
    kprintf("  Initializing system...\n\n");

    /* Initialize EventChains */
    kprintf("  [....] EventChains");
    event_chain_init(&g_keyboard_events);
    event_chain_init(&g_system_events);
    event_chain_init(&g_file_events);
    kprintf("\r  [ OK ] EventChains\n");

    /* Initialize IDT and interrupts */
    kprintf("  [....] Interrupt system");
    idt_init();
    kprintf("\r  [ OK ] Interrupt system\n");

    /* Initialize memory manager */
    kprintf("  [....] Memory manager");
    mm_init(&g_mm);
    e820_map_t *e820 = get_e820_map(bi);
    mm_parse_e820(&g_mm, e820);
    kprintf("\r  [ OK ] Memory manager\n");
    kprintf("         Total: %u KB\n", g_mm.total_bytes / 1024);

    /* Initialize slab allocator */
    kprintf("  [....] Slab allocator");
    slab_init(&g_slab, 0x200000, 0x200000);
    kprintf("\r  [ OK ] Slab allocator\n");

    /* Initialize RAM disk */
    kprintf("  [....] RAM disk");
    if (ramdisk_init_global()) {
        kprintf("\r  [ OK ] RAM disk\n");
        kprintf("         Size: %u KB at 0x%x\n", RAMDISK_SIZE / 1024, RAMDISK_DATA_ADDR);
    } else {
        kprintf("\r  [FAIL] RAM disk\n");
    }

    /* Initialize keyboard */
    kprintf("  [....] PS/2 keyboard");
    keyboard_init();
    keyboard_set_event_callback(fire_key_event);
    kprintf("\r  [ OK ] PS/2 keyboard\n");

    /* Initialize ATA */
    kprintf("  [....] ATA controller");
    ata_init();
    kprintf("\r  [ OK ] ATA controller\n");

    /* Initialize VFS */
    kprintf("  [....] Virtual File System");
    vfs_init();
    kprintf("\r  [ OK ] Virtual File System\n");

    /* Find boot device and mount filesystem */
    blkdev_t boot_dev;
    bool have_disk = ata_blkdev_find_first(&boot_dev);

    if (have_disk) {
        kprintf("         Found: %s\n", boot_dev.model);

        kprintf("  [....] Mounting FAT12");
        vfs_mount_t *mnt = vfs_mount("/", &boot_dev, fat12_get_vfs_ops(), NULL, NULL);

        if (mnt) {
            char label[12];
            if (mnt->ops->label && mnt->ops->label(mnt, label, sizeof(label))) {
                kprintf("\r  [ OK ] Mounting FAT12\n");
                if (label[0]) {
                    kprintf("         Volume: %s\n", label);
                }
            } else {
                kprintf("\r  [ OK ] Mounting FAT12\n");
            }
            g_kernel_ctx.root_mount = mnt;
        } else {
            kprintf("\r  [FAIL] Mounting FAT12\n");
        }

        g_kernel_ctx.boot_device = boot_dev;
    } else {
        kprintf("         No drives found\n");
    }

    /* Setup kernel context */
    g_kernel_ctx.mm = &g_mm;
    g_kernel_ctx.slab = &g_slab;
    g_kernel_ctx.keyboard_events = &g_keyboard_events;
    g_kernel_ctx.system_events = &g_system_events;
    g_kernel_ctx.file_events = &g_file_events;
    g_kernel_ctx.term = &g_term;
    g_kernel_ctx.use_vga_text = g_use_vga_text;
    g_kernel_ctx.boot_info = bi;

    /* Enable interrupts */
    kprintf("\n  Enabling interrupts...\n");
    interrupts_enable();

    kprintf("  System ready.\n");

    /* Setup and run shell */
    setup_kernel_shell(&g_kernel_ctx);
    shell_run(&g_shell);
}