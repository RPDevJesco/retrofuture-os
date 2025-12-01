/**
 * boot_info.h - Boot Information Structure
 * 
 * Shared between bootloader and kernel. The bootloader builds this
 * structure at physical address 0x500 before jumping to the kernel.
 */

#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

#define BOOT_MAGIC      0x4E524F43  // 'CORN'
#define BOOT_INFO_ADDR  0x500

/* E820 Memory Map Entry */
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) e820_entry_t;

#define E820_USABLE             1
#define E820_RESERVED           2
#define E820_ACPI_RECLAIMABLE   3
#define E820_ACPI_NVS           4
#define E820_BAD                5

/* E820 Map Header */
typedef struct {
    uint32_t count;
    e820_entry_t entries[];
} __attribute__((packed)) e820_map_t;

/* Boot Info Structure */
typedef struct {
    uint32_t magic;             // BOOT_MAGIC
    
    /* Memory Map */
    uint32_t e820_addr;         // Physical address of e820_map_t
    
    /* Video Mode */
    uint32_t vesa_enabled;      // 1 if VESA mode, 0 if VGA text
    uint32_t framebuffer;       // Physical framebuffer address
    uint32_t width;             // Screen width in pixels
    uint32_t height;            // Screen height in pixels
    uint32_t bpp;               // Bits per pixel
    uint32_t pitch;             // Bytes per scanline
    
} __attribute__((packed)) boot_info_t;

/* Get boot info (placed at known address by bootloader) */
static inline boot_info_t *get_boot_info(void) {
    return (boot_info_t *)BOOT_INFO_ADDR;
}

/* Get E820 map from boot info */
static inline e820_map_t *get_e820_map(boot_info_t *bi) {
    return (e820_map_t *)(uintptr_t)bi->e820_addr;
}

#endif /* BOOT_INFO_H */
