; ============================================================================
; boot.asm - Stage 1 Bootloader
; ============================================================================
; 
; Fits in 512-byte boot sector. Loads stage 2 and jumps to it.
; Works with both floppy and El Torito CD-ROM boot.
;
; Memory map during boot:
;   0x0000:0x7C00  - This bootloader (512 bytes)
;   0x0000:0x7E00  - Stage 2 loaded here
;   0x0000:0x0500  - Boot info structure
;
; Assemble: nasm -f bin -o boot.bin boot.asm
; ============================================================================

[BITS 16]
[ORG 0x7C00]

; ============================================================================
; Constants
; ============================================================================

STAGE2_SEGMENT  equ 0x0000
STAGE2_OFFSET   equ 0x7E00      ; Right after boot sector
STAGE2_SECTORS  equ 32          ; 16KB for stage 2
BOOT_DRIVE      equ 0x0500      ; Store boot drive here

; ============================================================================
; Entry Point
; ============================================================================

start:
    ; Set up segments
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00          ; Stack grows down from bootloader
    sti

    ; Save boot drive (BIOS passes it in DL)
    mov     [BOOT_DRIVE], dl

    ; Clear screen and set video mode (80x25 text)
    mov     ax, 0x0003
    int     0x10

    ; Display loading message
    mov     si, msg_loading
    call    print_string

    ; Reset disk system
    xor     ax, ax
    mov     dl, [BOOT_DRIVE]
    int     0x13
    jc      disk_error

    ; Load stage 2
    mov     ax, STAGE2_SEGMENT
    mov     es, ax
    mov     bx, STAGE2_OFFSET   ; ES:BX = destination

    mov     ah, 0x02            ; BIOS read sectors
    mov     al, STAGE2_SECTORS  ; Number of sectors
    mov     ch, 0               ; Cylinder 0
    mov     cl, 2               ; Sector 2 (1-indexed, sector 1 is boot)
    mov     dh, 0               ; Head 0
    mov     dl, [BOOT_DRIVE]    ; Drive
    int     0x13
    jc      disk_error

    ; Verify magic number at start of stage 2
    cmp     word [STAGE2_OFFSET], 0x5441  ; 'AT' magic
    jne     stage2_error

    ; Jump to stage 2
    mov     dl, [BOOT_DRIVE]    ; Pass boot drive
    jmp     STAGE2_SEGMENT:STAGE2_OFFSET + 2  ; Skip magic

; ============================================================================
; Error Handlers
; ============================================================================

disk_error:
    mov     si, msg_disk_err
    call    print_string
    jmp     halt

stage2_error:
    mov     si, msg_stage2_err
    call    print_string
    jmp     halt

halt:
    mov     si, msg_halt
    call    print_string
.loop:
    cli
    hlt
    jmp     .loop

; ============================================================================
; Print String (SI = string pointer, null terminated)
; ============================================================================

print_string:
    pusha
    mov     ah, 0x0E            ; BIOS teletype
    mov     bh, 0               ; Page 0
.loop:
    lodsb
    test    al, al
    jz      .done
    int     0x10
    jmp     .loop
.done:
    popa
    ret

; ============================================================================
; Data
; ============================================================================

msg_loading:    db '[BOOT] Loading stage 2...', 13, 10, 0
msg_disk_err:   db '[BOOT] Disk read error!', 13, 10, 0
msg_stage2_err: db '[BOOT] Stage 2 corrupt!', 13, 10, 0
msg_halt:       db '[BOOT] System halted.', 13, 10, 0

; ============================================================================
; Boot Sector Padding and Signature
; ============================================================================

times 510 - ($ - $$) db 0       ; Pad to 510 bytes
dw 0xAA55                       ; Boot signature
