/**
 * idt.h - Interrupt Descriptor Table and PIC Driver
 * 
 * Sets up:
 *   - IDT with 256 entries
 *   - 8259 PIC remapping (IRQ0-15 → INT 32-47)
 *   - Exception handlers (INT 0-31)
 *   - IRQ handlers (INT 32-47)
 * 
 * Target: Pentium III (i686)
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * IDT Structures
 * ============================================================================ */

/* IDT Entry (8 bytes) */
typedef struct {
    uint16_t offset_low;    // Offset bits 0-15
    uint16_t selector;      // Code segment selector
    uint8_t  zero;          // Always 0
    uint8_t  type_attr;     // Type and attributes
    uint16_t offset_high;   // Offset bits 16-31
} __attribute__((packed)) idt_entry_t;

/* IDT Pointer (for LIDT instruction) */
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

/* Interrupt Frame (pushed by CPU on interrupt) */
typedef struct {
    // Pushed by our stub
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha
    uint32_t int_no, err_code;
    // Pushed by CPU
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed)) int_frame_t;

/* ============================================================================
 * PIC Constants
 * ============================================================================ */

#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1

#define PIC_EOI         0x20    // End of Interrupt

#define ICW1_ICW4       0x01    // ICW4 needed
#define ICW1_SINGLE     0x02    // Single mode
#define ICW1_INTERVAL4  0x04    // Call address interval 4
#define ICW1_LEVEL      0x08    // Level triggered mode
#define ICW1_INIT       0x10    // Initialization

#define ICW4_8086       0x01    // 8086/88 mode
#define ICW4_AUTO       0x02    // Auto EOI
#define ICW4_BUF_SLAVE  0x08    // Buffered slave
#define ICW4_BUF_MASTER 0x0C    // Buffered master
#define ICW4_SFNM       0x10    // Special fully nested

/* IRQ numbers */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_RTC         8
#define IRQ_FREE1       9
#define IRQ_FREE2       10
#define IRQ_FREE3       11
#define IRQ_MOUSE       12
#define IRQ_FPU         13
#define IRQ_ATA_PRI     14
#define IRQ_ATA_SEC     15

/* INT numbers (after remapping) */
#define INT_IRQ_BASE    32
#define INT_IRQ(n)      (INT_IRQ_BASE + (n))

/* ============================================================================
 * IDT Type/Attribute Flags
 * ============================================================================ */

#define IDT_PRESENT     0x80
#define IDT_DPL_0       0x00    // Ring 0
#define IDT_DPL_3       0x60    // Ring 3
#define IDT_GATE_INT32  0x0E    // 32-bit interrupt gate
#define IDT_GATE_TRAP32 0x0F    // 32-bit trap gate
#define IDT_GATE_TASK   0x05    // Task gate

#define IDT_INT_GATE    (IDT_PRESENT | IDT_DPL_0 | IDT_GATE_INT32)
#define IDT_TRAP_GATE   (IDT_PRESENT | IDT_DPL_0 | IDT_GATE_TRAP32)

/* ============================================================================
 * Port I/O
 * ============================================================================ */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);  // Write to unused port for delay
}

/* ============================================================================
 * IDT Management
 * ============================================================================ */

#define IDT_ENTRIES 256

static idt_entry_t g_idt[IDT_ENTRIES];
static idt_ptr_t   g_idt_ptr;

/* Interrupt handler function type */
typedef void (*irq_handler_t)(int_frame_t *frame);

/* Handler table */
static irq_handler_t g_irq_handlers[16] = {0};

/* Set IDT entry */
static inline void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t type_attr) {
    g_idt[num].offset_low  = handler & 0xFFFF;
    g_idt[num].offset_high = (handler >> 16) & 0xFFFF;
    g_idt[num].selector    = selector;
    g_idt[num].zero        = 0;
    g_idt[num].type_attr   = type_attr;
}

/* Load IDT */
static inline void idt_load(void) {
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (uint32_t)&g_idt;
    __asm__ volatile ("lidt %0" : : "m"(g_idt_ptr));
}

/* ============================================================================
 * PIC Management
 * ============================================================================ */

/* Remap PIC to INT 32-47 */
static inline void pic_remap(void) {
    // Start initialization sequence
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // Set vector offsets
    outb(PIC1_DATA, INT_IRQ_BASE);      // Master: INT 32-39
    io_wait();
    outb(PIC2_DATA, INT_IRQ_BASE + 8);  // Slave: INT 40-47
    io_wait();

    // Tell PICs about each other
    outb(PIC1_DATA, 4);  // Slave at IRQ2
    io_wait();
    outb(PIC2_DATA, 2);  // Cascade identity
    io_wait();

    // Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Mask all interrupts initially
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/* Enable specific IRQ */
static inline void pic_enable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    mask = inb(port) & ~(1 << irq);
    outb(port, mask);
}

/* Disable specific IRQ */
static inline void pic_disable_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    mask = inb(port) | (1 << irq);
    outb(port, mask);
}

/* Send End of Interrupt */
static inline void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

/* ============================================================================
 * Exception Names (for debugging)
 * ============================================================================ */

static const char *exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved"
};

/* ============================================================================
 * Interrupt Stubs (defined in idt_asm.asm)
 * ============================================================================ */

/* These are defined in assembly */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* ============================================================================
 * High-Level Handlers (called from assembly stubs)
 * ============================================================================ */

/* Exception handler - called from ISR stubs */
void exception_handler(int_frame_t *frame);

/* IRQ handler - called from IRQ stubs */
void irq_handler(int_frame_t *frame);

/* Register IRQ handler */
static inline void irq_register(uint8_t irq, irq_handler_t handler) {
    g_irq_handlers[irq] = handler;
}

/* Unregister IRQ handler */
static inline void irq_unregister(uint8_t irq) {
    g_irq_handlers[irq] = NULL;
}

/* ============================================================================
 * IDT Initialization
 * ============================================================================ */

static inline void idt_init(void) {
    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set up exception handlers (ISR 0-31)
    idt_set_gate(0,  (uint32_t)isr0,  0x08, IDT_INT_GATE);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, IDT_INT_GATE);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, IDT_INT_GATE);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, IDT_INT_GATE);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, IDT_INT_GATE);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, IDT_INT_GATE);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, IDT_INT_GATE);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, IDT_INT_GATE);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, IDT_INT_GATE);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, IDT_INT_GATE);
    idt_set_gate(10, (uint32_t)isr10, 0x08, IDT_INT_GATE);
    idt_set_gate(11, (uint32_t)isr11, 0x08, IDT_INT_GATE);
    idt_set_gate(12, (uint32_t)isr12, 0x08, IDT_INT_GATE);
    idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_INT_GATE);
    idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_INT_GATE);
    idt_set_gate(15, (uint32_t)isr15, 0x08, IDT_INT_GATE);
    idt_set_gate(16, (uint32_t)isr16, 0x08, IDT_INT_GATE);
    idt_set_gate(17, (uint32_t)isr17, 0x08, IDT_INT_GATE);
    idt_set_gate(18, (uint32_t)isr18, 0x08, IDT_INT_GATE);
    idt_set_gate(19, (uint32_t)isr19, 0x08, IDT_INT_GATE);
    idt_set_gate(20, (uint32_t)isr20, 0x08, IDT_INT_GATE);
    idt_set_gate(21, (uint32_t)isr21, 0x08, IDT_INT_GATE);
    idt_set_gate(22, (uint32_t)isr22, 0x08, IDT_INT_GATE);
    idt_set_gate(23, (uint32_t)isr23, 0x08, IDT_INT_GATE);
    idt_set_gate(24, (uint32_t)isr24, 0x08, IDT_INT_GATE);
    idt_set_gate(25, (uint32_t)isr25, 0x08, IDT_INT_GATE);
    idt_set_gate(26, (uint32_t)isr26, 0x08, IDT_INT_GATE);
    idt_set_gate(27, (uint32_t)isr27, 0x08, IDT_INT_GATE);
    idt_set_gate(28, (uint32_t)isr28, 0x08, IDT_INT_GATE);
    idt_set_gate(29, (uint32_t)isr29, 0x08, IDT_INT_GATE);
    idt_set_gate(30, (uint32_t)isr30, 0x08, IDT_INT_GATE);
    idt_set_gate(31, (uint32_t)isr31, 0x08, IDT_INT_GATE);

    // Remap PIC
    pic_remap();

    // Set up IRQ handlers (INT 32-47)
    idt_set_gate(32, (uint32_t)irq0,  0x08, IDT_INT_GATE);
    idt_set_gate(33, (uint32_t)irq1,  0x08, IDT_INT_GATE);
    idt_set_gate(34, (uint32_t)irq2,  0x08, IDT_INT_GATE);
    idt_set_gate(35, (uint32_t)irq3,  0x08, IDT_INT_GATE);
    idt_set_gate(36, (uint32_t)irq4,  0x08, IDT_INT_GATE);
    idt_set_gate(37, (uint32_t)irq5,  0x08, IDT_INT_GATE);
    idt_set_gate(38, (uint32_t)irq6,  0x08, IDT_INT_GATE);
    idt_set_gate(39, (uint32_t)irq7,  0x08, IDT_INT_GATE);
    idt_set_gate(40, (uint32_t)irq8,  0x08, IDT_INT_GATE);
    idt_set_gate(41, (uint32_t)irq9,  0x08, IDT_INT_GATE);
    idt_set_gate(42, (uint32_t)irq10, 0x08, IDT_INT_GATE);
    idt_set_gate(43, (uint32_t)irq11, 0x08, IDT_INT_GATE);
    idt_set_gate(44, (uint32_t)irq12, 0x08, IDT_INT_GATE);
    idt_set_gate(45, (uint32_t)irq13, 0x08, IDT_INT_GATE);
    idt_set_gate(46, (uint32_t)irq14, 0x08, IDT_INT_GATE);
    idt_set_gate(47, (uint32_t)irq15, 0x08, IDT_INT_GATE);

    // Load IDT
    idt_load();
}

/* Enable interrupts */
static inline void interrupts_enable(void) {
    __asm__ volatile ("sti");
}

/* Disable interrupts */
static inline void interrupts_disable(void) {
    __asm__ volatile ("cli");
}

#endif /* IDT_H */