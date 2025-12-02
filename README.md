# RetroFuture OS

A retro-future operating system designed for vintage x86 hardware (Pentium III era).

## Features

- **Custom bootloader** - Two-stage bootloader supporting floppy and CD-ROM boot
- **Retro terminal** - CRT-style phosphor display with scanlines (green/amber/cyan/white)
- **EventChains** - Reactive event system for kernel subsystems
- **Intrusive list memory manager** - Cache-friendly, zero-allocation design
- **Slab allocator** - Fast fixed-size allocation
- **PS/2 keyboard driver** - Full scancode translation with modifier support
- **ATA/IDE driver** - PIO mode disk access for hard drives
- **Floppy driver** - DMA-based floppy disk controller driver
- **FAT12 file system** - Read floppy disk contents
- **Interactive shell** - Command-line interface with built-in commands

## Target Hardware

- **CPU**: Intel Pentium III (or compatible)
- **RAM**: 256MB
- **Boot**: 3.5" floppy disk or CD-ROM
- **Video**: VESA-compatible graphics (800x600x32 or 640x480x32) text mode fallback

## Building

### Requirements

- **Cross compiler**: `i686-elf-gcc` toolchain
- **Assembler**: NASM
- **ISO tools**: xorriso, genisoimage, or mkisofs

#### Installing Cross Compiler (Debian/Ubuntu)

```bash
# Install dependencies
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev \
                 libmpfr-dev texinfo nasm xorriso qemu-system-x86

# Build cross compiler (see osdev.org for detailed instructions)
# Or use a pre-built toolchain
```

### Build Commands

```bash
# Build everything (floppy + ISO)
make all

# Build floppy image only
make floppy

# Build ISO image only  
make iso

# Test in QEMU
make run

# Test with debugger
make debug
```

### Docker

```bash
- docker build -t retrofuture-os .
- docker create --name temp retrofuture-os
- docker cp temp:/src/output ./output
- docker rm temp
```

### Output Files

- `build/RetroFuture.img` - 1.44MB bootable floppy image
- `build/RetroFuture.iso` - Bootable CD-ROM image

## Writing to Physical Media

### Floppy Disk

```bash
# Linux
sudo dd if=build/RetroFuture.img of=/dev/fd0 bs=512

# Windows (use Powershell)
$img = [System.IO.File]::ReadAllBytes("C:\path\retrofuture-os\output\RetroFuture-text.img")
$floppy = [System.IO.File]::Open("\\.\A:", [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write)
$floppy.Write($img, 0, $img.Length)
$floppy.Close()
```

### CD-ROM

Burn `build/RetroFuture.iso` using any CD burning software.

## Shell Commands

```
help     - Show available commands
clear    - Clear screen
info     - System information (CPU, RAM, display)
ls       - List root directory contents
cat <f>  - Display file contents
pwd      - Print working directory
mem      - Show memory map
disk     - Show disk information
events   - demo of event chains with keyboard events
color    - Cycle through terminal color schemes
reboot   - Reboot system
```

## Project Structure

```
retrofuture-os/
├── boot/
│   ├── boot.asm               # Stage 1 bootloader (512 bytes)
│   └── stage2.asm             # Stage 2 (E820, A20, VESA, protected mode)
├── kernel/
│   ├── entry.asm              # Kernel entry point
│   ├── idt_asm.asm            # Interrupt service routine stubs
│   └── kernel.c               # Main kernel code
├── drivers/
│   └── font_8x16.c            # Bitmap font
├── include/
│   ├── boot_info.h            # Boot info structure
│   ├── terminal.h             # Terminal driver (phosphor effects)
│   ├── idt.h                  # Interrupt descriptor table
│   ├── keyboard.h             # PS/2 keyboard driver
│   ├── ata.h                  # ATA/IDE disk driver
│   ├── floppy.h               # Floppy disk controller driver
│   ├── fat12.h                # FAT12 file system
│   ├── intrusive_list.h       # (unused)
│   ├── intrusive_list_fast.h
│   └── intrusive_list_pool.h  # (unused)
├── linker.ld                  # Linker script
└── Makefile
```

## Memory Map

```
0x00000000 - 0x000004FF  Real Mode IVT, BDA
0x00000500 - 0x00000FFF  Boot info structure
0x00001000 - 0x00001FFF  E820 memory map
0x00002000 - 0x00002FFF  VESA info
0x00007C00 - 0x00007DFF  Stage 1 bootloader
0x00007E00 - 0x0000BFFF  Stage 2 bootloader
0x00090000 - 0x0009FFFF  Kernel stack
0x000A0000 - 0x000BFFFF  VGA memory
0x000C0000 - 0x000FFFFF  BIOS ROMs
0x00100000 - 0x001FFFFF  Kernel code/data (1MB)
0x00200000 - 0x003FFFFF  Kernel heap (2MB)
0x00400000+              Free memory
```

## Color Schemes

The terminal supports multiple phosphor emulation modes:

- **Green** (default) - Classic green screen
- **Amber** - Warm amber monochrome  
- **White** - Standard white on black
- **Cyan** - Sci-fi blue/cyan

Change in `kernel.c`:
```c
terminal_config_t term_cfg = {
    .scheme = PHOSPHOR_GREEN,  // or PHOSPHOR_AMBER, etc.
    .scanlines = true,
    // ...
};
```

## Architecture

### Event System

The kernel uses EventChains for reactive programming:

```c
// Subscribe to memory allocation events
event_chain_subscribe(&g_mm.on_alloc, my_handler);

// Handler receives event with context
void my_handler(event_t *e) {
    // Process allocation
}
```

### Memory Manager

Based on intrusive linked lists for zero-overhead tracking:

```c
typedef struct phys_region {
    ilist_node_t addr_link;    // In address-sorted list
    ilist_node_t free_link;    // In free list (if free)
    uint32_t base, size, flags;
    event_chain_t on_alloc;    // Per-region events
} phys_region_t;
```

### Patterns
```
INTRUSIVE LISTS (ilist_*)
├── Memory Manager
│   ├── all_regions    - Track all physical memory regions
│   ├── free_regions   - Fast free-list for allocation
│   └── region_pool    - Pre-allocated region structs
├── Slab Allocator
│   └── free_list      - Per-size-class free blocks
└── Zero allocations for list operations!

EVENT CHAINS (event_chain_*)
├── g_keyboard_events  - Keyboard press/release
├── g_system_events    - System-wide bus
├── Memory Manager
│   ├── on_alloc       - Fired on allocation
│   ├── on_free        - Fired on free
│   └── on_oom         - Fired on out-of-memory
└── Per-Region hooks
    ├── on_alloc       - Region-specific events
    └── on_free
```

## License

Public Domain / MIT - Use freely for any purpose.

## Credits
- Jesco      - Architecture and implementation
- OSDev Wiki - Invaluable reference
- Intel      - For the glorious Pentium III

https://github.com/user-attachments/assets/1e393e59-ffd3-40c9-856d-fac87c6acf9b

