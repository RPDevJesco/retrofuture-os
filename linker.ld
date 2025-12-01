/* ============================================================================
 * linker.ld - Kernel Linker Script
 * ============================================================================
 *
 * Kernel is loaded at 1MB (0x100000) by the bootloader.
 * This is above the legacy BIOS area and conventional memory.
 *
 * ============================================================================ */

ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

KERNEL_OFFSET = 0x100000;

SECTIONS
{
    . = KERNEL_OFFSET;

    /* Code section - entry point MUST be first */
    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.text.entry)       /* Entry point first */
        *(.text)
        *(.text.*)
        *(.multiboot)        /* Multiboot header after code (for GRUB compat) */
    }

    /* Read-only data */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Read-write initialized data */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data (BSS) */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Kernel end symbol */
    _kernel_end = .;

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
