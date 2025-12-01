/**
 * floppy.h - Floppy Disk Controller Driver
 * 
 * Driver for the Intel 82077AA compatible floppy disk controller.
 * Supports 1.44MB 3.5" floppy disks.
 * 
 * This is a simple polling-mode driver. For better performance,
 * interrupt-driven DMA transfers would be used.
 */

#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "idt.h"

/* ============================================================================
 * FDC Port Definitions
 * ============================================================================ */

#define FDC_BASE            0x3F0

#define FDC_STATUS_A        (FDC_BASE + 0)  // Status Register A (read)
#define FDC_STATUS_B        (FDC_BASE + 1)  // Status Register B (read)
#define FDC_DOR             (FDC_BASE + 2)  // Digital Output Register
#define FDC_MSR             (FDC_BASE + 4)  // Main Status Register (read)
#define FDC_DSR             (FDC_BASE + 4)  // Data Rate Select Register (write)
#define FDC_FIFO            (FDC_BASE + 5)  // Data FIFO
#define FDC_DIR             (FDC_BASE + 7)  // Digital Input Register (read)
#define FDC_CCR             (FDC_BASE + 7)  // Configuration Control Register (write)

/* ============================================================================
 * DOR (Digital Output Register) Bits
 * ============================================================================ */

#define DOR_DRIVE0          0x00    // Select drive 0
#define DOR_DRIVE1          0x01    // Select drive 1
#define DOR_RESET           0x04    // 0 = reset, 1 = normal
#define DOR_IRQ_DMA         0x08    // Enable IRQ and DMA
#define DOR_MOTOR0          0x10    // Motor 0 on
#define DOR_MOTOR1          0x20    // Motor 1 on
#define DOR_MOTOR2          0x40    // Motor 2 on
#define DOR_MOTOR3          0x80    // Motor 3 on

/* ============================================================================
 * MSR (Main Status Register) Bits
 * ============================================================================ */

#define MSR_DRIVE0_BUSY     0x01
#define MSR_DRIVE1_BUSY     0x02
#define MSR_DRIVE2_BUSY     0x04
#define MSR_DRIVE3_BUSY     0x08
#define MSR_CMD_BUSY        0x10    // Command in progress
#define MSR_NON_DMA         0x20    // Non-DMA mode
#define MSR_DIO             0x40    // Data direction: 1=read, 0=write
#define MSR_RQM             0x80    // Ready for data transfer

/* ============================================================================
 * FDC Commands
 * ============================================================================ */

#define CMD_SPECIFY         0x03    // Set step rate, head load/unload times
#define CMD_SENSE_DRIVE     0x04    // Get drive status
#define CMD_WRITE_DATA      0x05    // Write sectors
#define CMD_READ_DATA       0x06    // Read sectors
#define CMD_RECALIBRATE     0x07    // Move head to track 0
#define CMD_SENSE_INTERRUPT 0x08    // Get interrupt status
#define CMD_SEEK            0x0F    // Seek to track
#define CMD_VERSION         0x10    // Get controller version
#define CMD_CONFIGURE       0x13    // Configure controller
#define CMD_LOCK            0x14    // Lock configuration

/* Command modifiers */
#define CMD_MT              0x80    // Multi-track
#define CMD_MFM             0x40    // MFM mode (double density)
#define CMD_SK              0x20    // Skip deleted data

/* ============================================================================
 * 1.44MB Floppy Geometry
 * ============================================================================ */

#define FD_SECTORS_PER_TRACK    18
#define FD_HEADS                2
#define FD_TRACKS               80
#define FD_SECTOR_SIZE          512
#define FD_TOTAL_SECTORS        (FD_SECTORS_PER_TRACK * FD_HEADS * FD_TRACKS)  // 2880
#define FD_TOTAL_SIZE           (FD_TOTAL_SECTORS * FD_SECTOR_SIZE)  // 1474560

/* Gap lengths for 1.44MB */
#define FD_GAP3_RW              0x1B    // Gap length for read/write
#define FD_GAP3_FORMAT          0x54    // Gap length for format

/* ============================================================================
 * Driver State
 * ============================================================================ */

typedef struct {
    bool        initialized;
    bool        motor_on;
    uint8_t     current_track;
    volatile bool irq_received;
} floppy_state_t;

static floppy_state_t g_floppy = {0};

/* DMA buffer (must be below 16MB and not cross 64KB boundary) */
#define FD_DMA_BUFFER   0x80000     // 512KB mark - safe location
static uint8_t *const fd_dma_buffer = (uint8_t *)FD_DMA_BUFFER;

/* ============================================================================
 * Low-Level I/O
 * ============================================================================ */

/* Wait for FDC to be ready for command/data */
static bool fdc_wait_ready(uint32_t timeout) {
    while (timeout--) {
        if (inb(FDC_MSR) & MSR_RQM) return true;
        for (volatile int i = 0; i < 100; i++);
    }
    return false;
}

/* Wait for data to be available */
static bool fdc_wait_data(uint32_t timeout) {
    while (timeout--) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) return true;
        for (volatile int i = 0; i < 100; i++);
    }
    return false;
}

/* Send byte to FDC FIFO */
static bool fdc_send_byte(uint8_t byte) {
    if (!fdc_wait_ready(10000)) return false;
    outb(FDC_FIFO, byte);
    return true;
}

/* Read byte from FDC FIFO */
static bool fdc_read_byte(uint8_t *byte) {
    if (!fdc_wait_data(10000)) return false;
    *byte = inb(FDC_FIFO);
    return true;
}

/* Send command and wait for result */
static bool fdc_command(uint8_t *cmd, int cmd_len, uint8_t *result, int result_len) {
    // Send command bytes
    for (int i = 0; i < cmd_len; i++) {
        if (!fdc_send_byte(cmd[i])) return false;
    }
    
    // Read result bytes (if any)
    for (int i = 0; i < result_len; i++) {
        if (!fdc_read_byte(&result[i])) return false;
    }
    
    return true;
}

/* ============================================================================
 * IRQ Handler
 * ============================================================================ */

static void fdc_irq_handler(int_frame_t *frame) {
    (void)frame;
    g_floppy.irq_received = true;
}

/* Wait for IRQ with timeout */
static bool fdc_wait_irq(uint32_t timeout_ms) {
    g_floppy.irq_received = false;
    
    // Timeout in ~ms
    uint32_t timeout = timeout_ms * 1000;
    while (!g_floppy.irq_received && timeout--) {
        for (volatile int i = 0; i < 100; i++);
    }
    
    return g_floppy.irq_received;
}

/* ============================================================================
 * DMA Setup
 * ============================================================================ */

/* DMA channel 2 is used for floppy */
#define DMA_CHANNEL     2

/* DMA ports */
#define DMA_MASK        0x0A
#define DMA_MODE        0x0B
#define DMA_FLIPFLOP    0x0C
#define DMA_ADDR(ch)    ((ch) * 2)          // Address register
#define DMA_COUNT(ch)   ((ch) * 2 + 1)      // Count register
#define DMA_PAGE(ch)    (((uint8_t[]){0x87, 0x83, 0x81, 0x82})[ch])  // Page register

static void dma_setup(bool write, uint32_t addr, uint16_t count) {
    uint8_t mode = write ? 0x4A : 0x46;  // Single mode, increment, read/write
    
    // Mask channel
    outb(DMA_MASK, 0x04 | DMA_CHANNEL);
    
    // Reset flip-flop
    outb(DMA_FLIPFLOP, 0xFF);
    
    // Set mode
    outb(DMA_MODE, mode);
    
    // Set address (low, high)
    outb(DMA_ADDR(DMA_CHANNEL), addr & 0xFF);
    outb(DMA_ADDR(DMA_CHANNEL), (addr >> 8) & 0xFF);
    
    // Set page
    outb(DMA_PAGE(DMA_CHANNEL), (addr >> 16) & 0xFF);
    
    // Reset flip-flop
    outb(DMA_FLIPFLOP, 0xFF);
    
    // Set count (length - 1)
    outb(DMA_COUNT(DMA_CHANNEL), (count - 1) & 0xFF);
    outb(DMA_COUNT(DMA_CHANNEL), ((count - 1) >> 8) & 0xFF);
    
    // Unmask channel
    outb(DMA_MASK, DMA_CHANNEL);
}

/* ============================================================================
 * Motor Control
 * ============================================================================ */

static void fdc_motor_on(void) {
    if (!g_floppy.motor_on) {
        outb(FDC_DOR, DOR_DRIVE0 | DOR_RESET | DOR_IRQ_DMA | DOR_MOTOR0);
        g_floppy.motor_on = true;
        
        // Wait for motor to spin up (~300ms)
        for (volatile int i = 0; i < 300000; i++);
    }
}

static void fdc_motor_off(void) {
    outb(FDC_DOR, DOR_DRIVE0 | DOR_RESET | DOR_IRQ_DMA);
    g_floppy.motor_on = false;
}

/* ============================================================================
 * FDC Operations
 * ============================================================================ */

/* Reset the FDC */
static bool fdc_reset(void) {
    // Disable controller
    outb(FDC_DOR, 0x00);
    for (volatile int i = 0; i < 10000; i++);
    
    // Enable controller
    outb(FDC_DOR, DOR_RESET | DOR_IRQ_DMA);
    
    // Wait for interrupt
    if (!fdc_wait_irq(500)) return false;
    
    // Sense interrupt for each drive
    for (int i = 0; i < 4; i++) {
        uint8_t cmd = CMD_SENSE_INTERRUPT;
        uint8_t result[2];
        fdc_command(&cmd, 1, result, 2);
    }
    
    // Set data rate for 1.44MB (500kbps)
    outb(FDC_CCR, 0x00);
    
    // Configure: specify step rate, head load/unload times
    uint8_t specify[] = {CMD_SPECIFY, 0xDF, 0x02};  // Step rate, head unload, head load
    return fdc_command(specify, 3, NULL, 0);
}

/* Recalibrate (seek to track 0) */
static bool fdc_recalibrate(void) {
    fdc_motor_on();
    
    uint8_t cmd[] = {CMD_RECALIBRATE, 0x00};  // Drive 0
    if (!fdc_command(cmd, 2, NULL, 0)) return false;
    
    // Wait for seek to complete
    if (!fdc_wait_irq(2000)) return false;
    
    // Check result
    uint8_t sense_cmd = CMD_SENSE_INTERRUPT;
    uint8_t result[2];
    if (!fdc_command(&sense_cmd, 1, result, 2)) return false;
    
    g_floppy.current_track = 0;
    return (result[0] & 0xC0) == 0;  // No error
}

/* Seek to track */
static bool fdc_seek(uint8_t track) {
    if (track == g_floppy.current_track) return true;
    
    fdc_motor_on();
    
    uint8_t cmd[] = {CMD_SEEK, 0x00, track};  // Drive 0, head 0, track
    if (!fdc_command(cmd, 3, NULL, 0)) return false;
    
    // Wait for seek to complete
    if (!fdc_wait_irq(2000)) return false;
    
    // Check result
    uint8_t sense_cmd = CMD_SENSE_INTERRUPT;
    uint8_t result[2];
    if (!fdc_command(&sense_cmd, 1, result, 2)) return false;
    
    if ((result[0] & 0xC0) != 0) return false;
    
    g_floppy.current_track = track;
    return true;
}

/* Convert LBA to CHS */
static void lba_to_chs(uint32_t lba, uint8_t *track, uint8_t *head, uint8_t *sector) {
    *track = lba / (FD_HEADS * FD_SECTORS_PER_TRACK);
    *head = (lba / FD_SECTORS_PER_TRACK) % FD_HEADS;
    *sector = (lba % FD_SECTORS_PER_TRACK) + 1;  // Sectors are 1-indexed
}

/* ============================================================================
 * Read/Write Sectors
 * ============================================================================ */

/**
 * Read sectors from floppy
 */
static uint32_t fdc_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (lba + count > FD_TOTAL_SECTORS) return 0;
    
    uint8_t track, head, sector;
    lba_to_chs(lba, &track, &head, &sector);
    
    // Seek to track
    if (!fdc_seek(track)) return 0;
    
    // Set up DMA for read
    uint32_t dma_addr = FD_DMA_BUFFER;
    uint16_t dma_len = count * FD_SECTOR_SIZE;
    dma_setup(false, dma_addr, dma_len);
    
    // Send read command
    uint8_t cmd[] = {
        CMD_READ_DATA | CMD_MFM | CMD_MT,
        (head << 2) | 0,                    // Head, drive
        track,
        head,
        sector,
        2,                                  // Sector size (2 = 512 bytes)
        FD_SECTORS_PER_TRACK,               // End of track
        FD_GAP3_RW,
        0xFF                                // Data length (ignored for sector size != 0)
    };
    
    if (!fdc_command(cmd, 9, NULL, 0)) return 0;
    
    // Wait for completion
    if (!fdc_wait_irq(2000)) return 0;
    
    // Read result
    uint8_t result[7];
    for (int i = 0; i < 7; i++) {
        if (!fdc_read_byte(&result[i])) return 0;
    }
    
    // Check for errors
    if ((result[0] & 0xC0) != 0) return 0;
    
    // Copy from DMA buffer
    uint8_t *src = fd_dma_buffer;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < dma_len; i++) {
        dst[i] = src[i];
    }
    
    return count;
}

/**
 * Write sectors to floppy
 */
static uint32_t fdc_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (lba + count > FD_TOTAL_SECTORS) return 0;
    
    uint8_t track, head, sector;
    lba_to_chs(lba, &track, &head, &sector);
    
    // Seek to track
    if (!fdc_seek(track)) return 0;
    
    // Copy to DMA buffer
    uint32_t dma_len = count * FD_SECTOR_SIZE;
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t *dst = fd_dma_buffer;
    for (uint32_t i = 0; i < dma_len; i++) {
        dst[i] = src[i];
    }
    
    // Set up DMA for write
    dma_setup(true, FD_DMA_BUFFER, dma_len);
    
    // Send write command
    uint8_t cmd[] = {
        CMD_WRITE_DATA | CMD_MFM | CMD_MT,
        (head << 2) | 0,
        track,
        head,
        sector,
        2,
        FD_SECTORS_PER_TRACK,
        FD_GAP3_RW,
        0xFF
    };
    
    if (!fdc_command(cmd, 9, NULL, 0)) return 0;
    
    // Wait for completion
    if (!fdc_wait_irq(2000)) return 0;
    
    // Read result
    uint8_t result[7];
    for (int i = 0; i < 7; i++) {
        if (!fdc_read_byte(&result[i])) return 0;
    }
    
    // Check for errors
    if ((result[0] & 0xC0) != 0) return 0;
    
    return count;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

static bool floppy_init(void) {
    // Register IRQ handler
    irq_register(IRQ_FLOPPY, fdc_irq_handler);
    pic_enable_irq(IRQ_FLOPPY);
    
    // Reset controller
    if (!fdc_reset()) return false;
    
    // Recalibrate
    if (!fdc_recalibrate()) return false;
    
    g_floppy.initialized = true;
    return true;
}

/* Check if floppy is initialized */
static inline bool floppy_ready(void) {
    return g_floppy.initialized;
}

#endif /* FLOPPY_H */
