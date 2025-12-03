/*
 * program.ld - Linker script for RetroFuture OS programs
 *
 * Use with: i686-elf-ld -T program.ld -o program.bin program.o
 *
 * This produces a flat binary with:
 *   - RFEX header at start (32 bytes)
 *   - Code and data following
 *   - BSS at end (zeroed by loader)
 */

OUTPUT_FORMAT(binary)
OUTPUT_ARCH(i386)
ENTRY(_start)

/* Default load address - loader may override */
PROGRAM_LOAD = 0x200000;

SECTIONS
{
    . = PROGRAM_LOAD;

    /* RFEX header must come first */
    .header : {
        *(.header)
    }

    /* Code */
    .text : {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data : {
        *(.data)
        *(.data.*)
    }

    /* Uninitialized data (BSS) */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Calculate BSS size for header */
    __bss_size = __bss_end - __bss_start;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
