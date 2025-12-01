; ============================================================================
; entry.asm - Kernel Entry Point
; ============================================================================
;
; Called from stage2 bootloader in 32-bit protected mode.
; Sets up stack and calls kernel_main().
;
; ============================================================================

[BITS 32]

; Multiboot header (optional, for GRUB compatibility)
MBOOT_MAGIC     equ 0x1BADB002
MBOOT_FLAGS     equ 0x00000003  ; Align modules, provide memory map
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384              ; 16KB kernel stack
stack_top:

; Entry point MUST be in .text.entry to be linked first!
section .text.entry
global _start
global __kernel_start
global __kernel_end
extern kernel_main

__kernel_start:

_start:
    ; EAX contains boot info pointer from stage2
    ; Set up stack
    mov     esp, stack_top

    ; Clear direction flag
    cld

    ; Push boot info pointer as argument
    push    eax

    ; Call kernel main
    call    kernel_main

    ; If kernel_main returns, halt
    cli
.halt:
    hlt
    jmp     .halt

section .text
__kernel_end: