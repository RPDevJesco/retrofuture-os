; ============================================================================
; hello.asm - Simple test program for RetroFuture OS
; ============================================================================
;
; This is a minimal program to test the program loader.
; It prints a message using the kernel API and returns.
;
; Build: nasm -f bin -o hello.bin hello.asm
;
; Entry point receives (cdecl calling convention):
;   [esp+4]  = kernel_api_t *api
;   [esp+8]  = int argc  
;   [esp+12] = char **argv
;
; kernel_api_t structure offsets:
;   +0:  version (uint32)
;   +4:  struct_size (uint32)
;   +8:  putchar (function pointer)
;   +12: puts (function pointer)
;   +16: printf (function pointer)
;   +20: getchar (function pointer)
;   ...
; ============================================================================

[BITS 32]
[ORG 0x200000]          ; Default load address

; ============================================================================
; RFEX Header (32 bytes)
; ============================================================================

header_start:
    dd 0x58454652           ; Magic: 'RFEX'
    dd 0                    ; Entry offset (0 = code starts right after header)
    dd 0                    ; Load address (0 = default 0x200000)
    dd 0                    ; BSS size
    dd 0                    ; Flags
    dd 0, 0, 0              ; Reserved (pad to 32 bytes)

; ============================================================================
; Program Code
; ============================================================================

_start:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx

    ; Get kernel API pointer
    mov     esi, [ebp+8]        ; ESI = kernel_api_t *api

    ; =========================================================================
    ; Print banner using api->puts
    ; =========================================================================
    push    msg_banner
    mov     eax, [esi+12]       ; api->puts
    call    eax
    add     esp, 4

    ; =========================================================================
    ; Print argc using api->printf
    ; =========================================================================
    mov     eax, [ebp+12]       ; argc
    push    eax
    push    msg_argc
    mov     eax, [esi+16]       ; api->printf
    call    eax
    add     esp, 8

    ; =========================================================================
    ; Print each argument
    ; =========================================================================
    mov     ecx, [ebp+12]       ; ECX = argc
    mov     edi, [ebp+16]       ; EDI = argv
    xor     ebx, ebx            ; EBX = index (i = 0)

.arg_loop:
    cmp     ebx, ecx
    jge     .arg_done

    ; Print "  argv[i] = "value""
    push    dword [edi+ebx*4]   ; argv[i]
    push    ebx                 ; i
    push    msg_argv
    mov     eax, [esi+16]       ; api->printf
    call    eax
    add     esp, 12

    inc     ebx
    jmp     .arg_loop

.arg_done:

    ; =========================================================================
    ; Print goodbye message
    ; =========================================================================
    push    msg_done
    mov     eax, [esi+12]       ; api->puts
    call    eax
    add     esp, 4

    ; =========================================================================
    ; Return 0 (success)
    ; =========================================================================
    xor     eax, eax

    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

; ============================================================================
; Data
; ============================================================================

msg_banner:
    db '========================================', 13, 10
    db '  Hello from RetroFuture OS!', 13, 10
    db '  This program was loaded from disk.', 13, 10  
    db '========================================', 13, 10
    db 0

msg_argc:
    db 'Received %d argument(s):', 13, 10, 0

msg_argv:
    db '  argv[%d] = "%s"', 13, 10, 0

msg_done:
    db 13, 10
    db 'Program completed successfully!', 13, 10
    db 0
