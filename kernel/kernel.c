/**
 * kernel.c - RETROFUTURE OS Kernel
 * 
 * Main kernel entry point integrating:
 *   - Memory Manager (intrusive list based)
 *   - Event Chains (reactive callbacks)
 *   - Retro-Future Terminal
 *   - PS/2 Keyboard Driver
 *   - ATA/IDE Driver
 *   - FAT12 File System
 *   - RAM Disk (in-memory filesystem)
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

static inline void ilist_remove(ilist_node_t *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

static inline ilist_node_t *ilist_pop_front(ilist_head_t *head) {
    if (ilist_is_empty(head)) return NULL;
    ilist_node_t *node = head->node.next;
    ilist_remove(node);
    return node;
}

#define ilist_foreach_entry(pos, head, member) \
    for (pos = ilist_entry((head)->node.next, typeof(*pos), member); \
         &pos->member != &(head)->node; \
         pos = ilist_entry(pos->member.next, typeof(*pos), member))

/* ============================================================================
 * Event Chain System
 * ============================================================================ */

#define EVENT_MAX_HANDLERS 8

/* Event types */
#define EVT_KEY_PRESS       0x0001
#define EVT_KEY_RELEASE     0x0002
#define EVT_MEM_ALLOC       0x0010
#define EVT_MEM_FREE        0x0011
#define EVT_MEM_OOM         0x0012
#define EVT_DISK_READ       0x0020
#define EVT_DISK_WRITE      0x0021
#define EVT_COMMAND         0x0030
#define EVT_FILE_CREATE     0x0040
#define EVT_FILE_DELETE     0x0041

/* Key event data (passed via event.data) */
typedef struct {
    char ascii;
    uint8_t scancode;
    uint8_t modifiers;
} key_event_data_t;

/* Memory event data */
typedef struct {
    uint32_t address;
    uint32_t size;
    const char *owner;
} mem_event_data_t;

typedef struct event {
    void *source;
    void *data;
    uint32_t type;
    bool handled;
} event_t;

typedef void (*event_handler_t)(event_t *event);

typedef struct event_chain {
    event_handler_t handlers[EVENT_MAX_HANDLERS];
    uint8_t count;
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
            // Shift remaining handlers down
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
static event_chain_t g_keyboard_events;   // Keyboard events
static event_chain_t g_system_events;     // System-wide events
static event_chain_t g_file_events;       // File system events
static uint32_t g_event_fire_count = 0;   // Track how many events fired

/* Helper to fire keyboard events */
static void fire_key_event(char ascii, uint8_t scancode, uint8_t modifiers, bool pressed) {
    key_event_data_t data = { ascii, scancode, modifiers };
    event_t evt = {
        .source = &g_keyboard_events,
        .data = &data,
        .type = pressed ? EVT_KEY_PRESS : EVT_KEY_RELEASE,
        .handled = false
    };
    g_event_fire_count++;
    event_chain_fire(&g_keyboard_events, &evt);
    event_chain_fire(&g_system_events, &evt);  // Also fire on system chain
}

/* Helper to fire file events */
static void fire_file_event(const char *filename, uint32_t type) {
    event_t evt = {
        .source = &g_ramdisk,
        .data = (void *)filename,
        .type = type,
        .handled = false
    };
    g_event_fire_count++;
    event_chain_fire(&g_file_events, &evt);
    event_chain_fire(&g_system_events, &evt);
}

/* ============================================================================
 * Physical Memory Region
 * ============================================================================ */

#define REGION_FREE         0x0001
#define REGION_KERNEL       0x0002
#define REGION_RESERVED     0x0004
#define REGION_DEVICE       0x0008
#define REGION_STACK        0x0010
#define REGION_HEAP         0x0020
#define REGION_RAMDISK      0x0040

typedef struct phys_region {
    ilist_node_t    addr_link;
    ilist_node_t    free_link;
    uint32_t        base;
    uint32_t        size;
    uint32_t        flags;
    const char     *owner;
    event_chain_t   on_alloc;
    event_chain_t   on_free;
} phys_region_t;

/* ============================================================================
 * Memory Manager
 * ============================================================================ */

#define MM_MAX_REGIONS 64

typedef struct mem_manager {
    ilist_head_t    all_regions;
    ilist_head_t    free_regions;
    phys_region_t   region_storage[MM_MAX_REGIONS];
    ilist_head_t    region_pool;
    event_chain_t   on_alloc;
    event_chain_t   on_free;
    event_chain_t   on_oom;
    uint32_t        total_bytes;
    uint32_t        free_bytes;
    uint32_t        kernel_bytes;
} mem_manager_t;

static mem_manager_t g_mm;

/* Slab allocator */
#define SLAB_SIZES 8
static const uint32_t slab_sizes[SLAB_SIZES] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

typedef struct slab_block {
    ilist_node_t link;
} slab_block_t;

typedef struct slab_cache {
    ilist_head_t free_list;
    uint32_t block_size;
    uint32_t total_allocs;
    uint32_t total_frees;
} slab_cache_t;

typedef struct slab_allocator {
    slab_cache_t caches[SLAB_SIZES];
    uint32_t heap_base;
    uint32_t heap_end;
    uint32_t heap_ptr;
} slab_allocator_t;

static slab_allocator_t g_slab;

/* Memory manager functions */
static void mm_init(mem_manager_t *mm);
static void mm_add_region(mem_manager_t *mm, uint32_t base, uint32_t size, uint32_t flags, const char *owner);
static void mm_parse_e820(mem_manager_t *mm, e820_map_t *map);

/* Slab allocator functions */
static void slab_init(slab_allocator_t *slab, uint32_t heap_base, uint32_t heap_size);

/* ============================================================================
 * VGA Text Mode Driver (fallback when VESA fails)
 * ============================================================================ */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static uint16_t *const g_vga_buffer = (uint16_t *)VGA_MEMORY;
static uint8_t g_vga_x = 0;
static uint8_t g_vga_y = 0;
static uint8_t g_vga_color = 0x0A;  // Light green on black
static bool g_use_vga_text = false;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            g_vga_buffer[y * VGA_WIDTH + x] = g_vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        g_vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', g_vga_color);
    }
}

static void vga_putchar(char c) {
    if (c == '\n') {
        g_vga_x = 0;
        g_vga_y++;
    } else if (c == '\r') {
        g_vga_x = 0;
    } else if (c == '\t') {
        g_vga_x = (g_vga_x + 8) & ~7;
    } else if (c == '\b') {
        if (g_vga_x > 0) {
            g_vga_x--;
            g_vga_buffer[g_vga_y * VGA_WIDTH + g_vga_x] = vga_entry(' ', g_vga_color);
        }
    } else {
        g_vga_buffer[g_vga_y * VGA_WIDTH + g_vga_x] = vga_entry(c, g_vga_color);
        g_vga_x++;
    }

    if (g_vga_x >= VGA_WIDTH) {
        g_vga_x = 0;
        g_vga_y++;
    }

    while (g_vga_y >= VGA_HEIGHT) {
        vga_scroll();
        g_vga_y--;
    }
}

static void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        g_vga_buffer[i] = vga_entry(' ', g_vga_color);
    }
    g_vga_x = 0;
    g_vga_y = 0;
}

/* ============================================================================
 * Terminal & Output (works with both VESA and VGA text)
 * ============================================================================ */

static terminal_t g_term;
extern const uint8_t terminal_font_8x16[];

/* String functions */
static int kstrlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static int kstrncmp(const char *a, const char *b, int n) {
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return *a - *b;
}

static char *kstrcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static char *kstrncpy(char *dst, const char *src, int n) {
    char *ret = dst;
    while (n-- > 0 && *src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return ret;
}

static void kmemcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void itoa(uint32_t value, char *buf, int base) {
    static const char digits[] = "0123456789ABCDEF";
    char tmp[32];
    int i = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value) {
        tmp[i++] = digits[value % base];
        value /= base;
    }

    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

/* Unified output - uses VGA text or VESA terminal */
static void kputchar(char c) {
    if (g_use_vga_text) {
        vga_putchar(c);
    } else {
        terminal_putchar(&g_term, c);
    }
}

static void kputs(const char *s) {
    if (g_use_vga_text) {
        vga_puts(s);
    } else {
        terminal_puts(&g_term, s);
    }
}

static void kprintf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    char buf[32];

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd':
                case 'u':
                    itoa(__builtin_va_arg(args, uint32_t), buf, 10);
                    kputs(buf);
                    break;
                case 'x':
                case 'X':
                    itoa(__builtin_va_arg(args, uint32_t), buf, 16);
                    kputs(buf);
                    break;
                case 's':
                    kputs(__builtin_va_arg(args, char *));
                    break;
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

/* ============================================================================
 * Interrupt Handlers (called from idt_asm.asm)
 * ============================================================================ */

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
 * FAT12 Block Device Wrappers
 * ============================================================================ */

static ata_drive_t *g_boot_drive = NULL;

static uint32_t fat12_block_read(uint32_t lba, uint8_t count, void *buf) {
    if (!g_boot_drive) return 0;
    return ata_read_sectors(g_boot_drive, lba, count, buf);
}

static uint32_t fat12_block_write(uint32_t lba, uint8_t count, const void *buf) {
    if (!g_boot_drive) return 0;
    return ata_write_sectors(g_boot_drive, lba, count, buf);
}

/* ============================================================================
 * Shell Commands
 * ============================================================================ */

#define CMD_BUF_SIZE 256
#define MAX_ARGS 16

static fat12_fs_t g_fs;
static uint8_t g_fat_cache[4608];
static char g_cwd[FAT12_MAX_PATH] = "/";

static void shell_help(void) {
    kprintf("Available commands:\n\n");

    kprintf(" General:\n");
    kprintf("  help      - Show this help\n");
    kprintf("  clear     - Clear screen\n");
    kprintf("  info      - System information\n");
    kprintf("  mem       - Memory information\n");
    kprintf("  disk      - Disk information\n");
    kprintf("  events    - EventChains demo\n");
    kprintf("  color     - Cycle terminal colors\n");
    kprintf("  reboot    - Reboot system\n\n");

    kprintf(" FAT12 Disk (ATA drive):\n");
    kprintf("  ls [dir]  - List directory\n");
    kprintf("  cat <f>   - Display file\n");
    kprintf("  pwd       - Print working directory\n");
    kprintf("  cd <dir>  - Change directory\n");
    kprintf("  mkdir <d> - Create directory\n");
    kprintf("  touch <f> - Create empty file\n");
    kprintf("  write <f> <text> - Write to file\n");
    kprintf("  del <f>   - Delete file\n");
    kprintf("  rmdir <d> - Remove empty directory\n");
    kprintf("  mv <s> <d> - Rename/move file\n");
    kprintf("  format    - Format ATA drive (!)\n");
    kprintf("  fsinfo    - FAT12 filesystem info\n\n");

    kprintf(" RAM Disk (in-memory, read/write):\n");
    kprintf("  rls       - List RAM disk files\n");
    kprintf("  rcat <f>  - Display RAM disk file\n");
    kprintf("  rwrite <f> <text> - Write text to file\n");
    kprintf("  rappend <f> <text> - Append text to file\n");
    kprintf("  rdel <f>  - Delete file\n");
    kprintf("  rcopy <src> <dst> - Copy FAT12->RAM disk\n");
    kprintf("  rrename <old> <new> - Rename file\n");
    kprintf("  rstat     - RAM disk statistics\n");
    kprintf("  rhex <f>  - Hex dump file\n");
}

static void shell_info(void) {
    kprintf("\n");
    kprintf("  RETROFUTURE OS v0.2\n");
    kprintf("  ---------------\n");
    kprintf("  CPU: Intel Pentium III @ 600MHz\n");
    kprintf("  RAM: %u KB total, %u KB free\n",
            g_mm.total_bytes / 1024, g_mm.free_bytes / 1024);
    if (g_use_vga_text) {
        kprintf("  Display: 80x25 VGA Text Mode\n");
    } else {
        kprintf("  Display: %ux%u @ %ubpp\n", g_term.width, g_term.height, 32);
    }
    kprintf("  EventChains: %u events fired\n", g_event_fire_count);

    /* RAM disk info */
    ramdisk_stats_t stats;
    ramdisk_get_stats(&g_ramdisk, &stats);
    kprintf("  RAM Disk: %u/%u KB used, %u files\n",
            stats.used_size / 1024, stats.total_size / 1024, stats.file_count);
    kprintf("\n");
}

static void shell_ls(const char *path) {
    fat12_dir_t dir;
    fat12_dirent_t de;
    char name[13];

    (void)path;
    fat12_open_root(&g_fs, &dir);

    kprintf("\n");
    while (fat12_read_dir(&dir, &de)) {
        fat12_name_to_string(&de, name);

        if (de.attributes & FAT12_ATTR_DIRECTORY) {
            kprintf("  [DIR]  %s\n", name);
        } else {
            kprintf("  %6u  %s\n", de.file_size, name);
        }
    }
    kprintf("\n");
}

static void shell_cat(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: cat <filename>\n");
        return;
    }

    fat12_file_t file;
    char path[FAT12_MAX_PATH];

    if (filename[0] == '/') {
        kstrcpy(path, filename);
    } else {
        kstrcpy(path, g_cwd);
        if (path[kstrlen(path)-1] != '/') {
            path[kstrlen(path)] = '/';
            path[kstrlen(path)+1] = '\0';
        }
        kstrcpy(path + kstrlen(path), filename);
    }

    if (!fat12_open(&g_fs, path, &file)) {
        kprintf("File not found: %s\n", filename);
        return;
    }

    kprintf("\n");

    char buf[512];
    uint32_t bytes;
    while ((bytes = fat12_read(&file, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        kputs(buf);
    }

    kprintf("\n");
    fat12_close(&file);
}

/* ============================================================================
 * FAT12 Write Shell Commands
 * ============================================================================ */

/**
 * Create a directory on FAT12
 */
static void shell_mkdir(const char *dirname) {
    if (!dirname || *dirname == '\0') {
        kprintf("Usage: mkdir <directory>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (fat12_mkdir(&g_fs, dirname)) {
        kprintf("Created directory: %s\n", dirname);
    } else {
        kprintf("Failed to create directory: %s\n", dirname);
    }
}

/**
 * Create an empty file on FAT12
 */
static void shell_touch(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: touch <filename>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (fat12_create_file(&g_fs, filename, FAT12_ATTR_ARCHIVE)) {
        kprintf("Created: %s\n", filename);
    } else {
        kprintf("Failed to create: %s\n", filename);
    }
}

/**
 * Write text to a FAT12 file
 */
static void shell_fat_write(const char *filename, const char *content) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: write <filename> <content>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (!content) content = "";

    uint32_t len = kstrlen(content);

    /* Create a buffer with newline */
    char buf[CMD_BUF_SIZE];
    kstrcpy(buf, content);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    uint32_t written = fat12_write_file(&g_fs, filename, buf, len + 1);
    if (written > 0) {
        kprintf("Wrote %u bytes to %s\n", written, filename);
    } else {
        kprintf("Failed to write to %s\n", filename);
    }
}

/**
 * Delete a FAT12 file
 */
static void shell_fat_del(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: del <filename>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (fat12_delete(&g_fs, filename)) {
        kprintf("Deleted: %s\n", filename);
    } else {
        kprintf("Failed to delete: %s (file not found or is a directory)\n", filename);
    }
}

/**
 * Remove an empty directory from FAT12
 */
static void shell_rmdir(const char *dirname) {
    if (!dirname || *dirname == '\0') {
        kprintf("Usage: rmdir <directory>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (fat12_rmdir(&g_fs, dirname)) {
        kprintf("Removed directory: %s\n", dirname);
    } else {
        kprintf("Failed to remove: %s (not found, not empty, or not a directory)\n", dirname);
    }
}

/**
 * Rename/move a FAT12 file
 */
static void shell_fat_mv(const char *old_name, const char *new_name) {
    if (!old_name || *old_name == '\0' || !new_name || *new_name == '\0') {
        kprintf("Usage: mv <oldname> <newname>\n");
        return;
    }

    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    if (fat12_rename(&g_fs, old_name, new_name)) {
        kprintf("Renamed: %s -> %s\n", old_name, new_name);
    } else {
        kprintf("Failed to rename: %s\n", old_name);
    }
}

/**
 * Format the ATA drive with FAT12
 */
static void shell_format(const char *label) {
    if (!g_boot_drive) {
        kprintf("No ATA drive available\n");
        return;
    }

    kprintf("\n!!! WARNING: This will ERASE ALL DATA on the drive !!!\n");
    kprintf("Drive: %s (%u MB)\n", g_boot_drive->model, g_boot_drive->size / 2048);
    kprintf("\nType 'YES' to confirm: ");

    /* Read confirmation */
    char confirm[16];
    int i = 0;
    while (i < 15) {
        char c = keyboard_getchar();
        if (c == '\n') break;
        if (c == '\b' && i > 0) {
            i--;
            kputchar('\b');
        } else if (c >= 32 && c < 127) {
            confirm[i++] = c;
            kputchar(c);
        }
    }
    confirm[i] = '\0';
    kprintf("\n");

    if (kstrcmp(confirm, "YES") != 0) {
        kprintf("Format cancelled.\n");
        return;
    }

    /* Unmount if mounted */
    if (g_fs.mounted) {
        fat12_unmount(&g_fs);
    }

    /* Calculate sectors to format (limit to FAT12 max) */
    uint32_t total_sectors = g_boot_drive->size;
    if (total_sectors > FAT12_MAX_SECTORS) {
        total_sectors = FAT12_MAX_SECTORS;
        kprintf("Note: Formatting only first %u MB (FAT12 limit)\n",
                total_sectors / 2048);
    }

    kprintf("Formatting...\n");

    /* Format the drive */
    fat12_format_params_t params;
    fat12_calc_params(&params, total_sectors);

    if (label && *label) {
        fat12_copy_padded(params.volume_label, label, 11);
    } else {
        fat12_copy_padded(params.volume_label, "RETROFUTURE", 11);
    }

    if (fat12_format(fat12_block_write, fat12_block_read, &params,
                     total_sectors, NULL)) {
        kprintf("Format complete!\n");
        kprintf("  Total sectors: %u\n", total_sectors);
        kprintf("  Sectors/cluster: %u\n", params.sectors_per_cluster);
        kprintf("  FAT sectors: %u\n", params.sectors_per_fat);
        kprintf("  Root entries: %u\n", params.root_entry_count);
        kprintf("  Volume label: %.11s\n", params.volume_label);

        /* Remount */
        kprintf("\nRemounting...\n");
        if (fat12_mount(&g_fs, fat12_block_read, fat12_block_write, g_fat_cache)) {
            kprintf("Filesystem mounted successfully.\n");
        } else {
            kprintf("Warning: Failed to remount filesystem.\n");
        }
    } else {
        kprintf("Format FAILED!\n");
    }
}

/**
 * Show FAT12 filesystem information
 */
static void shell_fsinfo(void) {
    if (!g_fs.mounted) {
        kprintf("No filesystem mounted\n");
        return;
    }

    char label[12];
    fat12_get_label(&g_fs, label);

    kprintf("\nFAT12 Filesystem Information:\n");
    kprintf("------------------------------\n");
    kprintf("  Volume Label:      %.11s\n", label[0] ? label : "(none)");
    kprintf("  OEM Name:          %.8s\n", g_fs.bpb.oem_name);
    kprintf("  Bytes/Sector:      %u\n", g_fs.bpb.bytes_per_sector);
    kprintf("  Sectors/Cluster:   %u\n", g_fs.bpb.sectors_per_cluster);
    kprintf("  Reserved Sectors:  %u\n", g_fs.bpb.reserved_sectors);
    kprintf("  Number of FATs:    %u\n", g_fs.bpb.num_fats);
    kprintf("  Root Entries:      %u\n", g_fs.bpb.root_entry_count);
    kprintf("  Total Sectors:     %u\n",
            g_fs.bpb.total_sectors_16 ? g_fs.bpb.total_sectors_16 : g_fs.bpb.total_sectors_32);
    kprintf("  Sectors/FAT:       %u\n", g_fs.bpb.fat_size_16);
    kprintf("  Media Type:        0x%x\n", g_fs.bpb.media_type);
    kprintf("\n  Layout:\n");
    kprintf("    FAT Start:       Sector %u\n", g_fs.fat_start);
    kprintf("    Root Start:      Sector %u\n", g_fs.root_start);
    kprintf("    Data Start:      Sector %u\n", g_fs.data_start);
    kprintf("    Total Clusters:  %u\n", g_fs.total_clusters);
    kprintf("    Free Space:      %u KB\n", fat12_free_space(&g_fs) / 1024);
    kprintf("\n");
}

/**
 * Change directory (simplified - updates g_cwd)
 */
static void shell_cd(const char *dirname) {
    if (!dirname || *dirname == '\0' || kstrcmp(dirname, "/") == 0) {
        kstrcpy(g_cwd, "/");
        return;
    }

    if (kstrcmp(dirname, "..") == 0) {
        /* Go up one level */
        int len = kstrlen(g_cwd);
        if (len > 1) {
            /* Remove trailing slash if any */
            if (g_cwd[len-1] == '/') g_cwd[--len] = '\0';
            /* Find last slash */
            while (len > 0 && g_cwd[len-1] != '/') len--;
            if (len == 0) len = 1;
            g_cwd[len] = '\0';
        }
        return;
    }

    /* Check if directory exists */
    fat12_dir_t dir;
    fat12_dirent_t de;
    fat12_open_root(&g_fs, &dir);

    if (!fat12_find_in_dir(&dir, dirname, &de)) {
        kprintf("Directory not found: %s\n", dirname);
        return;
    }

    if (!(de.attributes & FAT12_ATTR_DIRECTORY)) {
        kprintf("Not a directory: %s\n", dirname);
        return;
    }

    /* Update cwd */
    if (g_cwd[kstrlen(g_cwd)-1] != '/') {
        int len = kstrlen(g_cwd);
        g_cwd[len] = '/';
        g_cwd[len+1] = '\0';
    }
    kstrcpy(g_cwd + kstrlen(g_cwd), dirname);
}

static void shell_mem(void) {
    kprintf("\nMemory Map:\n");
    kprintf("-----------\n");

    phys_region_t *region;
    ilist_foreach_entry(region, &g_mm.all_regions, addr_link) {
        kprintf("  0x%x - 0x%x  %s\n",
                region->base,
                region->base + region->size,
                region->owner);
    }

    kprintf("\nHeap: 0x%x - 0x%x (used: %u bytes)\n",
            g_slab.heap_base, g_slab.heap_end,
            g_slab.heap_ptr - g_slab.heap_base);

    kprintf("RAM Disk: 0x%x - 0x%x (%u KB)\n",
            RAMDISK_DATA_ADDR, RAMDISK_DATA_ADDR + RAMDISK_SIZE,
            RAMDISK_SIZE / 1024);
    kprintf("\n");
}

static void shell_disk(void) {
    kprintf("\nATA Drives:\n");
    kprintf("-----------\n");

    for (int i = 0; i < 4; i++) {
        ata_drive_t *d = &g_ata.drives[i];
        if (d->present) {
            const char *type = (d->type == ATA_DEV_ATA) ? "HDD" : "CDROM";
            kprintf("  Drive %d: %s\n", i, type);
            kprintf("    Model:  %s\n", d->model);
            kprintf("    Size:   %u MB\n", (d->size / 2048));
        }
    }

    if (g_fs.mounted) {
        char label[12];
        fat12_get_label(&g_fs, label);
        kprintf("\nMounted Volume: %s\n", label[0] ? label : "(no label)");
        kprintf("Free Space: %u KB\n", fat12_free_space(&g_fs) / 1024);
    }
    kprintf("\n");
}

static int g_color_scheme = 0;

static void shell_color(void) {
    if (g_use_vga_text) {
        // Cycle VGA text colors
        static const uint8_t vga_colors[] = {0x0A, 0x0E, 0x0B, 0x0F};  // green, yellow, cyan, white
        g_color_scheme = (g_color_scheme + 1) % 4;
        g_vga_color = vga_colors[g_color_scheme];
        vga_clear();
        kprintf("========================================\n");
        kprintf("         RETROFUTURE OS v0.2 (Text Mode)\n");
        kprintf("========================================\n\n");
    } else {
        g_color_scheme = (g_color_scheme + 1) % 4;

        switch (g_color_scheme) {
            case 0: terminal_set_scheme(&g_term, PHOSPHOR_GREEN); break;
            case 1: terminal_set_scheme(&g_term, PHOSPHOR_AMBER); break;
            case 2: terminal_set_scheme(&g_term, PHOSPHOR_CYAN); break;
            case 3: terminal_set_scheme(&g_term, PHOSPHOR_WHITE); break;
        }

        terminal_clear(&g_term);
        terminal_draw_border(&g_term, " RETROFUTURE OS v0.2 ");
        terminal_set_cursor(&g_term, 2, 2);
    }

    const char *names[] = {"GREEN", "AMBER", "CYAN", "WHITE"};
    kprintf("Color scheme: %s\n", names[g_color_scheme]);
}

static void shell_reboot(void) {
    kprintf("Rebooting...\n");

    // Use keyboard controller to pulse reset line
    while (inb(0x64) & 0x02);  // Wait for input buffer clear
    outb(0x64, 0xFE);          // Pulse CPU reset line

    // If that didn't work, triple fault
    idt_set_gate(0, 0, 0, 0);
    __asm__ volatile ("int $0");
}

/* EventChains demo state */
static volatile uint32_t demo_keypress_count = 0;
static volatile char demo_last_key = 0;

static void demo_key_handler(event_t *event) {
    if (event->type == EVT_KEY_PRESS) {
        key_event_data_t *kd = (key_event_data_t *)event->data;
        demo_keypress_count++;
        demo_last_key = kd->ascii;
    }
}

static void shell_events(void) {
    kprintf("\n  === EventChains Demo ===\n\n");
    kprintf("  EventChains is a reactive pub/sub pattern\n");
    kprintf("  that decouples event producers from consumers.\n\n");

    kprintf("  Current stats:\n");
    kprintf("    Total events fired: %u\n", g_event_fire_count);
    kprintf("    Keyboard handlers:  %u\n", g_keyboard_events.count);
    kprintf("    System handlers:    %u\n");
    kprintf("    File handlers:      %u\n\n", g_file_events.count);

    kprintf("  Subscribing demo handler...\n");
    demo_keypress_count = 0;
    event_chain_subscribe(&g_keyboard_events, demo_key_handler);
    kprintf("    Keyboard handlers: %u\n\n", g_keyboard_events.count);

    kprintf("  Press 5 keys (handler will count them):\n  ");

    while (demo_keypress_count < 5) {
        char c = keyboard_getchar();
        if (c >= 32 && c < 127) {
            kputchar(c);
        }
    }

    kprintf("\n\n  Demo handler received %u events!\n", demo_keypress_count);
    kprintf("  Last key: '%c'\n\n", demo_last_key);

    kprintf("  Unsubscribing demo handler...\n");
    event_chain_unsubscribe(&g_keyboard_events, demo_key_handler);
    kprintf("    Keyboard handlers: %u\n\n", g_keyboard_events.count);

    kprintf("  EventChains allows loose coupling:\n");
    kprintf("    - Keyboard fires events without knowing who listens\n");
    kprintf("    - Handlers subscribe/unsubscribe dynamically\n");
    kprintf("    - event.handled can stop propagation\n\n");
}

/* ============================================================================
 * RAM Disk Shell Commands
 * ============================================================================ */

/**
 * List RAM disk files
 */
static void shell_rls(void) {
    ramdisk_dir_t dir;
    ramdisk_file_t file;

    if (!g_ramdisk.initialized) {
        kprintf("RAM disk not initialized\n");
        return;
    }

    kprintf("\nRAM Disk Contents:\n");
    kprintf("------------------\n");

    ramdisk_opendir(&g_ramdisk, &dir, true);  // Show hidden files too

    int count = 0;
    while (ramdisk_readdir(&dir, &file)) {
        /* Build flags string */
        char flags[8];
        int fi = 0;
        if (file.flags & RAMDISK_FILE_DIRECTORY)  flags[fi++] = 'd';
        if (file.flags & RAMDISK_FILE_READONLY)   flags[fi++] = 'r';
        if (file.flags & RAMDISK_FILE_HIDDEN)     flags[fi++] = 'h';
        if (file.flags & RAMDISK_FILE_SYSTEM)     flags[fi++] = 's';
        if (file.flags & RAMDISK_FILE_EXECUTABLE) flags[fi++] = 'x';
        if (file.flags & RAMDISK_FILE_BINARY)     flags[fi++] = 'b';
        while (fi < 6) flags[fi++] = '-';
        flags[fi] = '\0';

        kprintf("  %s  %6u  %s\n", flags, file.size, file.name);
        count++;
    }

    if (count == 0) {
        kprintf("  (empty)\n");
    }

    /* Show statistics */
    ramdisk_stats_t stats;
    ramdisk_get_stats(&g_ramdisk, &stats);
    kprintf("\n%u file(s), %u/%u KB used\n\n",
            stats.file_count, stats.used_size / 1024, stats.total_size / 1024);
}

/**
 * Display RAM disk file contents
 */
static void shell_rcat(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rcat <filename>\n");
        return;
    }

    ramdisk_handle_t handle;
    if (!ramdisk_open(&g_ramdisk, filename, &handle, false)) {
        kprintf("File not found: %s\n", filename);
        return;
    }

    kprintf("\n");

    char buf[512];
    uint32_t bytes;
    uint32_t total = 0;
    const uint32_t max_display = 16384;  /* Limit display to 16KB */

    while ((bytes = ramdisk_read(&handle, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';

        /* Print characters, converting non-printable to dots */
        for (uint32_t i = 0; i < bytes; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r' || c == '\t') {
                kputchar(c);
            } else if (c >= 32 && c < 127) {
                kputchar(c);
            } else {
                kputchar('.');
            }
        }

        total += bytes;
        if (total >= max_display) {
            kprintf("\n... (truncated at %u bytes)\n", max_display);
            break;
        }
    }

    kprintf("\n");
    ramdisk_close(&handle);
}

/**
 * Write text to RAM disk file (creates or overwrites)
 */
static void shell_rwrite(const char *filename, const char *content) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rwrite <filename> <content>\n");
        return;
    }

    if (!content) {
        content = "";
    }

    /* Delete existing file if any */
    ramdisk_delete(&g_ramdisk, filename);

    ramdisk_handle_t handle;
    if (!ramdisk_open(&g_ramdisk, filename, &handle, true)) {
        kprintf("Error creating file: %s\n", ramdisk_error_str(g_ramdisk.last_error));
        return;
    }

    uint32_t len = kstrlen(content);
    uint32_t written = ramdisk_write(&handle, content, len);

    /* Add newline */
    ramdisk_putc(&handle, '\n');

    ramdisk_close(&handle);

    kprintf("Wrote %u bytes to %s\n", written + 1, filename);

    /* Fire event */
    fire_file_event(filename, EVT_FILE_CREATE);
}

/**
 * Append text to RAM disk file
 */
static void shell_rappend(const char *filename, const char *content) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rappend <filename> <content>\n");
        return;
    }

    if (!content) {
        content = "";
    }

    ramdisk_handle_t handle;
    if (!ramdisk_open_append(&g_ramdisk, filename, &handle)) {
        kprintf("Error opening file: %s\n", ramdisk_error_str(g_ramdisk.last_error));
        return;
    }

    uint32_t len = kstrlen(content);
    uint32_t written = ramdisk_write(&handle, content, len);
    ramdisk_putc(&handle, '\n');

    ramdisk_close(&handle);

    kprintf("Appended %u bytes to %s\n", written + 1, filename);
}

/**
 * Delete RAM disk file
 */
static void shell_rdel(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rdel <filename>\n");
        return;
    }

    if (!ramdisk_delete(&g_ramdisk, filename)) {
        kprintf("Error: %s\n", ramdisk_error_str(g_ramdisk.last_error));
        return;
    }

    kprintf("Deleted: %s\n", filename);

    /* Fire event */
    fire_file_event(filename, EVT_FILE_DELETE);
}

/**
 * Copy file from FAT12 to RAM disk
 */
static void shell_rcopy(const char *src, const char *dst) {
    if (!src || *src == '\0') {
        kprintf("Usage: rcopy <fat12_file> <ramdisk_file>\n");
        return;
    }

    /* Use source name as destination if not provided */
    if (!dst || *dst == '\0') {
        dst = src;
        /* Skip leading path */
        const char *p = src;
        while (*p) {
            if (*p == '/') dst = p + 1;
            p++;
        }
    }

    /* Open FAT12 file */
    fat12_file_t fat_file;
    char path[FAT12_MAX_PATH];

    if (src[0] == '/') {
        kstrcpy(path, src);
    } else {
        kstrcpy(path, g_cwd);
        if (path[kstrlen(path)-1] != '/') {
            int len = kstrlen(path);
            path[len] = '/';
            path[len+1] = '\0';
        }
        kstrcpy(path + kstrlen(path), src);
    }

    if (!fat12_open(&g_fs, path, &fat_file)) {
        kprintf("FAT12 file not found: %s\n", src);
        return;
    }

    uint32_t file_size = fat12_size(&fat_file);

    /* Check if file too large */
    if (file_size > RAMDISK_SIZE / 2) {
        kprintf("File too large for RAM disk: %u bytes\n", file_size);
        fat12_close(&fat_file);
        return;
    }

    /* Delete existing RAM disk file if any */
    ramdisk_delete(&g_ramdisk, dst);

    /* Create RAM disk file */
    ramdisk_handle_t ram_handle;
    if (!ramdisk_open(&g_ramdisk, dst, &ram_handle, true)) {
        kprintf("Error creating RAM disk file: %s\n", ramdisk_error_str(g_ramdisk.last_error));
        fat12_close(&fat_file);
        return;
    }

    /* Copy data */
    char buf[512];
    uint32_t bytes;
    uint32_t total = 0;

    while ((bytes = fat12_read(&fat_file, buf, sizeof(buf))) > 0) {
        uint32_t written = ramdisk_write(&ram_handle, buf, bytes);
        if (written != bytes) {
            kprintf("Write error after %u bytes\n", total);
            break;
        }
        total += bytes;

        /* Show progress for large files */
        if (total % 8192 == 0) {
            kprintf(".");
        }
    }

    fat12_close(&fat_file);
    ramdisk_close(&ram_handle);

    kprintf("\nCopied %u bytes: %s -> %s\n", total, src, dst);

    fire_file_event(dst, EVT_FILE_CREATE);
}

/**
 * Rename RAM disk file
 */
static void shell_rrename(const char *oldname, const char *newname) {
    if (!oldname || *oldname == '\0' || !newname || *newname == '\0') {
        kprintf("Usage: rrename <oldname> <newname>\n");
        return;
    }

    if (!ramdisk_rename(&g_ramdisk, oldname, newname)) {
        kprintf("Error: %s\n", ramdisk_error_str(g_ramdisk.last_error));
        return;
    }

    kprintf("Renamed: %s -> %s\n", oldname, newname);
}

/**
 * Show RAM disk statistics
 */
static void shell_rstat(void) {
    if (!g_ramdisk.initialized) {
        kprintf("RAM disk not initialized\n");
        return;
    }

    ramdisk_stats_t stats;
    ramdisk_get_stats(&g_ramdisk, &stats);

    kprintf("\nRAM Disk Statistics:\n");
    kprintf("--------------------\n");
    kprintf("  Total size:      %u KB\n", stats.total_size / 1024);
    kprintf("  Used:            %u KB\n", stats.used_size / 1024);
    kprintf("  Free:            %u KB\n", stats.free_size / 1024);
    kprintf("  Fragmented:      %u bytes\n", stats.fragmented_bytes);
    kprintf("  Files:           %u / %u\n", stats.file_count, stats.max_files);
    kprintf("  Read operations: %u (%u KB)\n", stats.total_reads, stats.bytes_read / 1024);
    kprintf("  Write operations:%u (%u KB)\n", stats.total_writes, stats.bytes_written / 1024);
    kprintf("  Location:        0x%x - 0x%x\n",
            RAMDISK_DATA_ADDR, RAMDISK_DATA_ADDR + RAMDISK_SIZE);
    kprintf("\n");
}

/**
 * Hex dump RAM disk file
 */
static void shell_rhex(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rhex <filename>\n");
        return;
    }

    ramdisk_handle_t handle;
    if (!ramdisk_open(&g_ramdisk, filename, &handle, false)) {
        kprintf("File not found: %s\n", filename);
        return;
    }

    kprintf("\nHex dump of %s (%u bytes):\n\n", filename, ramdisk_size(&handle));

    uint8_t buf[16];
    uint32_t offset = 0;
    uint32_t bytes;
    const uint32_t max_dump = 512;  /* Limit dump size */

    while ((bytes = ramdisk_read(&handle, buf, 16)) > 0 && offset < max_dump) {
        /* Address */
        kprintf("%x: ", offset);

        /* Hex bytes */
        for (uint32_t i = 0; i < 16; i++) {
            if (i < bytes) {
                char hex[3];
                hex[0] = "0123456789ABCDEF"[(buf[i] >> 4) & 0xF];
                hex[1] = "0123456789ABCDEF"[buf[i] & 0xF];
                hex[2] = '\0';
                kprintf("%s ", hex);
            } else {
                kprintf("   ");
            }
            if (i == 7) kprintf(" ");
        }

        kprintf(" |");

        /* ASCII */
        for (uint32_t i = 0; i < bytes; i++) {
            char c = buf[i];
            kputchar((c >= 32 && c < 127) ? c : '.');
        }

        kprintf("|\n");
        offset += bytes;
    }

    if (offset >= max_dump && !ramdisk_eof(&handle)) {
        kprintf("... (truncated)\n");
    }

    kprintf("\n");
    ramdisk_close(&handle);
}

/**
 * Create an executable (set x flag)
 */
static void shell_rexec(const char *filename) {
    if (!filename || *filename == '\0') {
        kprintf("Usage: rexec <filename>\n");
        return;
    }

    uint32_t flags;
    if (!ramdisk_stat(&g_ramdisk, filename, NULL, &flags)) {
        kprintf("File not found: %s\n", filename);
        return;
    }

    ramdisk_set_flags(&g_ramdisk, filename, flags | RAMDISK_FILE_EXECUTABLE);
    kprintf("Marked as executable: %s\n", filename);
}

/* ============================================================================
 * Shell Execute - Command Dispatcher
 * ============================================================================ */

static void shell_execute(char *cmd) {
    char *args[MAX_ARGS];
    int argc = 0;

    char *p = cmd;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;

        args[argc++] = p;

        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }

    if (argc == 0) return;

    /* General commands */
    if (kstrcmp(args[0], "help") == 0) {
        shell_help();
    } else if (kstrcmp(args[0], "clear") == 0) {
        if (g_use_vga_text) {
            vga_clear();
            kprintf("========================================\n");
            kprintf("         RETROFUTURE OS v0.2 (Text Mode)\n");
            kprintf("========================================\n\n");
        } else {
            terminal_clear(&g_term);
            terminal_draw_border(&g_term, " RETROFUTURE OS v0.2 ");
            terminal_set_cursor(&g_term, 2, 2);
        }
    } else if (kstrcmp(args[0], "info") == 0) {
        shell_info();
    } else if (kstrcmp(args[0], "mem") == 0) {
        shell_mem();
    } else if (kstrcmp(args[0], "disk") == 0) {
        shell_disk();
    } else if (kstrcmp(args[0], "events") == 0) {
        shell_events();
    } else if (kstrcmp(args[0], "color") == 0) {
        shell_color();
    } else if (kstrcmp(args[0], "reboot") == 0) {
        shell_reboot();
    }
    /* FAT12 commands */
    else if (kstrcmp(args[0], "ls") == 0) {
        shell_ls(argc > 1 ? args[1] : NULL);
    } else if (kstrcmp(args[0], "cat") == 0) {
        shell_cat(argc > 1 ? args[1] : NULL);
    } else if (kstrcmp(args[0], "pwd") == 0) {
        kprintf("%s\n", g_cwd);
    }
    /* RAM disk commands */
    else if (kstrcmp(args[0], "rls") == 0) {
        shell_rls();
    } else if (kstrcmp(args[0], "rcat") == 0) {
        shell_rcat(argc > 1 ? args[1] : NULL);
    } else if (kstrcmp(args[0], "rwrite") == 0) {
        /* Join remaining args as content */
        if (argc < 3) {
            kprintf("Usage: rwrite <filename> <content>\n");
        } else {
            /* Reconstruct content from args */
            char content[CMD_BUF_SIZE];
            content[0] = '\0';
            for (int i = 2; i < argc; i++) {
                if (i > 2) {
                    int len = kstrlen(content);
                    content[len] = ' ';
                    content[len+1] = '\0';
                }
                kstrcpy(content + kstrlen(content), args[i]);
            }
            shell_rwrite(args[1], content);
        }
    } else if (kstrcmp(args[0], "rappend") == 0) {
        if (argc < 3) {
            kprintf("Usage: rappend <filename> <content>\n");
        } else {
            char content[CMD_BUF_SIZE];
            content[0] = '\0';
            for (int i = 2; i < argc; i++) {
                if (i > 2) {
                    int len = kstrlen(content);
                    content[len] = ' ';
                    content[len+1] = '\0';
                }
                kstrcpy(content + kstrlen(content), args[i]);
            }
            shell_rappend(args[1], content);
        }
    } else if (kstrcmp(args[0], "rdel") == 0) {
        shell_rdel(argc > 1 ? args[1] : NULL);
    } else if (kstrcmp(args[0], "rcopy") == 0) {
        shell_rcopy(argc > 1 ? args[1] : NULL, argc > 2 ? args[2] : NULL);
    } else if (kstrcmp(args[0], "rrename") == 0) {
        shell_rrename(argc > 1 ? args[1] : NULL, argc > 2 ? args[2] : NULL);
    } else if (kstrcmp(args[0], "rstat") == 0) {
        shell_rstat();
    } else if (kstrcmp(args[0], "rhex") == 0) {
        shell_rhex(argc > 1 ? args[1] : NULL);
    } else if (kstrcmp(args[0], "rexec") == 0) {
        shell_rexec(argc > 1 ? args[1] : NULL);
    }
    /* Unknown command */
    else {
        kprintf("Unknown command: %s\n", args[0]);
        kprintf("Type 'help' for available commands.\n");
    }
}

/* ============================================================================
 * Shell Main Loop
 * ============================================================================ */

static void shell_run(void) {
    char cmd_buf[CMD_BUF_SIZE];
    int cmd_len = 0;

    kprintf("\nType 'help' for available commands.\n\n");
    kprintf("> ");

    while (1) {
        char c = keyboard_getchar();

        switch (c) {
            case '\n':
                kputchar('\n');
                cmd_buf[cmd_len] = '\0';
                if (cmd_len > 0) {
                    shell_execute(cmd_buf);
                }
                cmd_len = 0;
                kprintf("> ");
                break;

            case '\b':
                if (cmd_len > 0) {
                    cmd_len--;
                    kputchar('\b');
                }
                break;

            default:
                if (cmd_len < CMD_BUF_SIZE - 1 && c >= 32 && c < 127) {
                    cmd_buf[cmd_len++] = c;
                    kputchar(c);
                }
                break;
        }
    }
}

/* ============================================================================
 * Memory Manager Implementation
 * ============================================================================ */

static void mm_init(mem_manager_t *mm) {
    ilist_init_head(&mm->all_regions);
    ilist_init_head(&mm->free_regions);
    ilist_init_head(&mm->region_pool);

    event_chain_init(&mm->on_alloc);
    event_chain_init(&mm->on_free);
    event_chain_init(&mm->on_oom);

    mm->total_bytes = 0;
    mm->free_bytes = 0;
    mm->kernel_bytes = 0;

    for (int i = 0; i < MM_MAX_REGIONS; i++) {
        ilist_init_node(&mm->region_storage[i].addr_link);
        ilist_init_node(&mm->region_storage[i].free_link);
        event_chain_init(&mm->region_storage[i].on_alloc);
        event_chain_init(&mm->region_storage[i].on_free);
        ilist_push_back(&mm->region_pool, &mm->region_storage[i].addr_link);
    }
}

static phys_region_t *mm_alloc_region_struct(mem_manager_t *mm) {
    ilist_node_t *node = ilist_pop_front(&mm->region_pool);
    if (!node) return NULL;
    return ilist_entry(node, phys_region_t, addr_link);
}

static void mm_add_region(mem_manager_t *mm, uint32_t base, uint32_t size,
                          uint32_t flags, const char *owner) {
    phys_region_t *r = mm_alloc_region_struct(mm);
    if (!r) return;

    r->base = base;
    r->size = size;
    r->flags = flags;
    r->owner = owner;

    phys_region_t *cur;
    bool inserted = false;
    ilist_foreach_entry(cur, &mm->all_regions, addr_link) {
        if (cur->base > base) {
            __ilist_insert(&r->addr_link, cur->addr_link.prev, &cur->addr_link);
            inserted = true;
            break;
        }
    }
    if (!inserted) {
        ilist_push_back(&mm->all_regions, &r->addr_link);
    }

    if (flags & REGION_FREE) {
        ilist_push_back(&mm->free_regions, &r->free_link);
        mm->free_bytes += size;
    }

    mm->total_bytes += size;
}

static void mm_parse_e820(mem_manager_t *mm, e820_map_t *map) {
    for (uint32_t i = 0; i < map->count; i++) {
        e820_entry_t *e = &map->entries[i];

        if (e->base >= 0x100000000ULL) continue;

        uint32_t base = (uint32_t)e->base;
        uint32_t size = (uint32_t)e->length;

        if (base + size < base) {
            size = 0xFFFFFFFF - base;
        }

        uint32_t flags = 0;
        const char *owner = "unknown";

        switch (e->type) {
            case E820_USABLE:
                flags = REGION_FREE;
                owner = "usable";
                break;
            case E820_RESERVED:
                flags = REGION_RESERVED;
                owner = "reserved";
                break;
            case E820_ACPI_RECLAIMABLE:
                flags = REGION_RESERVED;
                owner = "acpi-reclaim";
                break;
            case E820_ACPI_NVS:
                flags = REGION_RESERVED;
                owner = "acpi-nvs";
                break;
            default:
                flags = REGION_RESERVED;
                owner = "bad";
                break;
        }

        mm_add_region(mm, base, size, flags, owner);
    }
}

/* ============================================================================
 * Slab Allocator Implementation
 * ============================================================================ */

static void slab_init(slab_allocator_t *slab, uint32_t heap_base, uint32_t heap_size) {
    slab->heap_base = heap_base;
    slab->heap_end = heap_base + heap_size;
    slab->heap_ptr = heap_base;

    for (int i = 0; i < SLAB_SIZES; i++) {
        ilist_init_head(&slab->caches[i].free_list);
        slab->caches[i].block_size = slab_sizes[i];
        slab->caches[i].total_allocs = 0;
        slab->caches[i].total_frees = 0;
    }
}

/* ============================================================================
 * Kernel Entry Point
 * ============================================================================ */

extern uint32_t __kernel_start;
extern uint32_t __kernel_end;

void kernel_main(uint32_t boot_info_addr) {
    boot_info_t *bi = (boot_info_t *)boot_info_addr;

    if (bi->magic != BOOT_MAGIC) {
        // If we can't even verify boot info, just halt
        while (1) { __asm__ volatile ("hlt"); }
    }

    // Check if VESA is enabled or we're in VGA text mode
    if (bi->vesa_enabled) {
        // Initialize graphical terminal
        g_use_vga_text = false;
        terminal_config_t term_cfg = {
            .scheme = PHOSPHOR_GREEN,
            .colors = COLORS_GREEN,
            .scanlines = true,
            .cursor_blink = true,
            .cursor_rate = 15
        };
        terminal_init(&g_term, bi, &term_cfg);
        terminal_clear(&g_term);
        terminal_draw_border(&g_term, " RETROFUTURE OS v0.2 ");
        terminal_set_cursor(&g_term, 2, 2);
    } else {
        // Use VGA text mode
        g_use_vga_text = true;
        vga_clear();
        kprintf("========================================\n");
        kprintf("         RETROFUTURE OS v0.2 (Text Mode)\n");
        kprintf("========================================\n\n");
    }

    kprintf("System Initializing...\n\n");

    // Initialize EventChains system
    kprintf("  [....] EventChains");
    event_chain_init(&g_keyboard_events);
    event_chain_init(&g_system_events);
    event_chain_init(&g_file_events);
    kprintf("\r  [ OK ] EventChains\n");

    // Initialize IDT and interrupts
    kprintf("  [....] Interrupt system");
    idt_init();
    kprintf("\r  [ OK ] Interrupt system\n");

    // Initialize memory manager
    kprintf("  [....] Memory manager");
    mm_init(&g_mm);
    e820_map_t *e820 = get_e820_map(bi);
    mm_parse_e820(&g_mm, e820);
    kprintf("\r  [ OK ] Memory manager\n");
    kprintf("         Total: %u KB\n", g_mm.total_bytes / 1024);

    // Initialize slab allocator
    kprintf("  [....] Slab allocator");
    slab_init(&g_slab, 0x200000, 0x200000);
    kprintf("\r  [ OK ] Slab allocator\n");

    // Initialize RAM disk
    kprintf("  [....] RAM disk");
    if (ramdisk_init_global()) {
        kprintf("\r  [ OK ] RAM disk\n");
        kprintf("         Size: %u KB at 0x%x\n", RAMDISK_SIZE / 1024, RAMDISK_DATA_ADDR);
    } else {
        kprintf("\r  [FAIL] RAM disk\n");
    }

    // Initialize keyboard with EventChains integration
    kprintf("  [....] PS/2 keyboard");
    keyboard_init();
    keyboard_set_event_callback(fire_key_event);
    kprintf("\r  [ OK ] PS/2 keyboard\n");

    // Initialize ATA
    kprintf("  [....] ATA controller");
    ata_init();
    kprintf("\r  [ OK ] ATA controller\n");

    // Find boot drive and mount file system
    g_boot_drive = ata_get_first_drive();
    if (g_boot_drive) {
        kprintf("         Found: %s\n", g_boot_drive->model);

        kprintf("  [....] FAT12 file system");
        if (fat12_mount(&g_fs, fat12_block_read, fat12_block_write, g_fat_cache)) {
            char label[12];
            fat12_get_label(&g_fs, label);
            kprintf("\r  [ OK ] FAT12 file system\n");
            if (label[0]) {
                kprintf("         Volume: %s\n", label);
            }
        } else {
            kprintf("\r  [FAIL] FAT12 file system\n");
        }
    } else {
        kprintf("         No drives found\n");
    }

    // Enable interrupts
    kprintf("\n  Enabling interrupts...\n");
    interrupts_enable();

    kprintf("  System ready.\n");

    // Run shell
    shell_run();
}