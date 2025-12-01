; ============================================================================
; idt_asm.asm - Interrupt Service Routine Stubs
; ============================================================================
;
; Each ISR/IRQ stub:
;   1. Pushes error code (or dummy if CPU doesn't push one)
;   2. Pushes interrupt number
;   3. Saves registers
;   4. Calls C handler
;   5. Restores registers
;   6. Returns with IRET
;
; ============================================================================

[BITS 32]

; External C handlers
extern exception_handler
extern irq_handler

; ============================================================================
; Common ISR Stub (exceptions)
; ============================================================================

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push    dword 0             ; Dummy error code
    push    dword %1            ; Interrupt number
    jmp     isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    ; Error code already pushed by CPU
    push    dword %1            ; Interrupt number
    jmp     isr_common_stub
%endmacro

; ============================================================================
; Common IRQ Stub
; ============================================================================

%macro IRQ 2
global irq%1
irq%1:
    push    dword 0             ; Dummy error code
    push    dword %2            ; Interrupt number (32+irq)
    jmp     irq_common_stub
%endmacro

; ============================================================================
; ISR Stubs (Exceptions 0-31)
; ============================================================================

ISR_NOERRCODE 0     ; Division By Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; Non Maskable Interrupt
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault (has error code)
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Invalid TSS (has error code)
ISR_ERRCODE   11    ; Segment Not Present (has error code)
ISR_ERRCODE   12    ; Stack-Segment Fault (has error code)
ISR_ERRCODE   13    ; General Protection Fault (has error code)
ISR_ERRCODE   14    ; Page Fault (has error code)
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU Error
ISR_ERRCODE   17    ; Alignment Check (has error code)
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating-Point
ISR_NOERRCODE 20    ; Virtualization
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_ERRCODE   30    ; Security Exception (has error code)
ISR_NOERRCODE 31    ; Reserved

; ============================================================================
; IRQ Stubs (IRQ 0-15 → INT 32-47)
; ============================================================================

IRQ 0,  32          ; Timer
IRQ 1,  33          ; Keyboard
IRQ 2,  34          ; Cascade
IRQ 3,  35          ; COM2
IRQ 4,  36          ; COM1
IRQ 5,  37          ; LPT2
IRQ 6,  38          ; Floppy
IRQ 7,  39          ; LPT1 / Spurious
IRQ 8,  40          ; RTC
IRQ 9,  41          ; Free
IRQ 10, 42          ; Free
IRQ 11, 43          ; Free
IRQ 12, 44          ; PS/2 Mouse
IRQ 13, 45          ; FPU
IRQ 14, 46          ; Primary ATA
IRQ 15, 47          ; Secondary ATA

; ============================================================================
; Common Exception Handler Stub
; ============================================================================

isr_common_stub:
    ; Save all registers
    pusha
    
    ; Save data segment
    mov     ax, ds
    push    eax
    
    ; Load kernel data segment
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    
    ; Push pointer to stack frame as argument
    push    esp
    
    ; Call C handler
    call    exception_handler
    
    ; Remove argument
    add     esp, 4
    
    ; Restore data segment
    pop     eax
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    
    ; Restore registers
    popa
    
    ; Remove error code and interrupt number
    add     esp, 8
    
    ; Return from interrupt
    iret

; ============================================================================
; Common IRQ Handler Stub
; ============================================================================

irq_common_stub:
    ; Save all registers
    pusha
    
    ; Save data segment
    mov     ax, ds
    push    eax
    
    ; Load kernel data segment
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    
    ; Push pointer to stack frame as argument
    push    esp
    
    ; Call C handler
    call    irq_handler
    
    ; Remove argument
    add     esp, 4
    
    ; Restore data segment
    pop     eax
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    
    ; Restore registers
    popa
    
    ; Remove error code and interrupt number
    add     esp, 8
    
    ; Return from interrupt
    iret
