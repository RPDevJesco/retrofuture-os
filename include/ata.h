/**
 * ata.h - ATA/IDE PIO Mode Driver (Fixed)
 *
 * Simple PIO mode driver for IDE hard drives and CD-ROMs.
 * PIO mode is slower than DMA but works everywhere and is simpler.
 *
 * FIXES from original:
 *   - Proper wait after each sector write (wait for BSY clear, not just delay)
 *   - Better error checking with status reads
 *   - Debug output option for troubleshooting
 *
 * Target: Compaq Armada E500 (IDE interface)
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "idt.h"

/* ============================================================================
 * Debug Configuration
 * ============================================================================ */

/* Uncomment to enable debug output */
/* #define ATA_DEBUG 1 */

#ifdef ATA_DEBUG
extern void kprintf(const char *fmt, ...);
#define ATA_DBG(fmt, ...) kprintf("[ATA] " fmt, ##__VA_ARGS__)
#else
#define ATA_DBG(fmt, ...) ((void)0)
#endif

/* ============================================================================
 * ATA Port Definitions
 * ============================================================================ */

/* Primary ATA controller */
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6

/* Secondary ATA controller */
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

/* Register offsets from base I/O port */
#define ATA_REG_DATA        0x00    /* Read/Write data (16-bit) */
#define ATA_REG_ERROR       0x01    /* Read: error, Write: features */
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02    /* Sector count */
#define ATA_REG_LBA_LO      0x03    /* LBA bits 0-7 */
#define ATA_REG_LBA_MID     0x04    /* LBA bits 8-15 */
#define ATA_REG_LBA_HI      0x05    /* LBA bits 16-23 */
#define ATA_REG_DRIVE       0x06    /* Drive/head select */
#define ATA_REG_STATUS      0x07    /* Read: status */
#define ATA_REG_COMMAND     0x07    /* Write: command */

/* Control register (from ctrl base) */
#define ATA_REG_ALTSTATUS   0x00    /* Alternate status (read) */
#define ATA_REG_DEVCTRL     0x00    /* Device control (write) */

/* ============================================================================
 * ATA Status Register Bits
 * ============================================================================ */

#define ATA_SR_BSY          0x80    /* Busy */
#define ATA_SR_DRDY         0x40    /* Drive ready */
#define ATA_SR_DF           0x20    /* Drive fault */
#define ATA_SR_DSC          0x10    /* Drive seek complete */
#define ATA_SR_DRQ          0x08    /* Data request ready */
#define ATA_SR_CORR         0x04    /* Corrected data */
#define ATA_SR_IDX          0x02    /* Index */
#define ATA_SR_ERR          0x01    /* Error */

/* ============================================================================
 * ATA Error Register Bits
 * ============================================================================ */

#define ATA_ER_BBK          0x80    /* Bad block */
#define ATA_ER_UNC          0x40    /* Uncorrectable data */
#define ATA_ER_MC           0x20    /* Media changed */
#define ATA_ER_IDNF         0x10    /* ID mark not found */
#define ATA_ER_MCR          0x08    /* Media change request */
#define ATA_ER_ABRT         0x04    /* Command aborted */
#define ATA_ER_TK0NF        0x02    /* Track 0 not found */
#define ATA_ER_AMNF         0x01    /* Address mark not found */

/* ============================================================================
 * ATA Commands
 * ============================================================================ */

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24    /* 48-bit LBA */
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34    /* 48-bit LBA */
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA    /* 48-bit LBA */
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_PACKET          0xA0

/* ============================================================================
 * Drive/Head Register Bits
 * ============================================================================ */

#define ATA_DH_LBA          0x40    /* Use LBA addressing */
#define ATA_DH_DEV0         0xA0    /* Select drive 0 (master) */
#define ATA_DH_DEV1         0xB0    /* Select drive 1 (slave) */

/* ============================================================================
 * Device Control Register Bits
 * ============================================================================ */

#define ATA_CTRL_NIEN       0x02    /* Disable interrupts */
#define ATA_CTRL_SRST       0x04    /* Software reset */
#define ATA_CTRL_HOB        0x80    /* High order byte (for 48-bit LBA) */

/* ============================================================================
 * Drive Types
 * ============================================================================ */

typedef enum {
    ATA_DEV_NONE = 0,
    ATA_DEV_ATA,        /* Hard drive */
    ATA_DEV_ATAPI,      /* CD-ROM, etc. */
    ATA_DEV_UNKNOWN
} ata_dev_type_t;

/* ============================================================================
 * Drive Structure
 * ============================================================================ */

typedef struct {
    bool            present;
    ata_dev_type_t  type;
    uint16_t        io_base;
    uint16_t        ctrl_base;
    uint8_t         drive;          /* 0 = master, 1 = slave */
    bool            lba48;          /* Supports 48-bit LBA */
    uint32_t        size;           /* Size in sectors (28-bit) */
    uint64_t        size48;         /* Size in sectors (48-bit) */
    char            model[41];      /* Model string */
    char            serial[21];     /* Serial number */
    uint16_t        identify[256];  /* Raw identify data */
} ata_drive_t;

/* ============================================================================
 * ATA Controller
 * ============================================================================ */

typedef struct {
    ata_drive_t drives[4];  /* Primary master/slave, Secondary master/slave */
    bool initialized;
} ata_controller_t;

/* Global controller */
static ata_controller_t g_ata;

/* ============================================================================
 * Low-Level I/O
 * ============================================================================ */

static inline void ata_outb(ata_drive_t *drive, uint8_t reg, uint8_t val) {
    outb(drive->io_base + reg, val);
}

static inline uint8_t ata_inb(ata_drive_t *drive, uint8_t reg) {
    return inb(drive->io_base + reg);
}

static inline void ata_outw(ata_drive_t *drive, uint8_t reg, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"((uint16_t)(drive->io_base + reg)));
}

static inline uint16_t ata_inw(ata_drive_t *drive, uint8_t reg) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"((uint16_t)(drive->io_base + reg)));
    return ret;
}

/* 400ns delay (read alternate status 4 times) */
static inline void ata_delay(ata_drive_t *drive) {
    inb(drive->ctrl_base + ATA_REG_ALTSTATUS);
    inb(drive->ctrl_base + ATA_REG_ALTSTATUS);
    inb(drive->ctrl_base + ATA_REG_ALTSTATUS);
    inb(drive->ctrl_base + ATA_REG_ALTSTATUS);
}

/* Read status without clearing interrupt */
static inline uint8_t ata_alt_status(ata_drive_t *drive) {
    return inb(drive->ctrl_base + ATA_REG_ALTSTATUS);
}

/* ============================================================================
 * Wait Functions (with proper timeout handling)
 * ============================================================================ */

/**
 * Wait for BSY to clear
 * @return true if ready, false on timeout
 */
static inline bool ata_wait_ready(ata_drive_t *drive, uint32_t timeout) {
    while (timeout--) {
        uint8_t status = ata_alt_status(drive);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        /* Small delay between polls */
        for (volatile int i = 0; i < 10; i++);
    }
    ATA_DBG("wait_ready timeout\n");
    return false;
}

/**
 * Wait for DRQ (data request)
 * @return true if DRQ set, false on timeout or error
 */
static inline bool ata_wait_drq(ata_drive_t *drive, uint32_t timeout) {
    while (timeout--) {
        uint8_t status = ata_alt_status(drive);
        if (status & ATA_SR_ERR) {
            ATA_DBG("wait_drq: ERR set, error=0x%x\n", ata_inb(drive, ATA_REG_ERROR));
            return false;
        }
        if (status & ATA_SR_DF) {
            ATA_DBG("wait_drq: DF (drive fault)\n");
            return false;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return true;
        }
        /* Small delay between polls */
        for (volatile int i = 0; i < 10; i++);
    }
    ATA_DBG("wait_drq timeout\n");
    return false;
}

/**
 * Wait for BSY to clear and check for errors
 * Used after write operations
 * @return true if successful, false on error or timeout
 */
static inline bool ata_wait_write_complete(ata_drive_t *drive, uint32_t timeout) {
    while (timeout--) {
        uint8_t status = ata_alt_status(drive);

        if (status & ATA_SR_ERR) {
            ATA_DBG("write_complete: ERR, error=0x%x\n", ata_inb(drive, ATA_REG_ERROR));
            return false;
        }
        if (status & ATA_SR_DF) {
            ATA_DBG("write_complete: DF (drive fault)\n");
            return false;
        }
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        /* Small delay between polls */
        for (volatile int i = 0; i < 10; i++);
    }
    ATA_DBG("write_complete timeout\n");
    return false;
}

/* Select drive */
static inline void ata_select_drive(ata_drive_t *drive) {
    uint8_t val = drive->drive ? ATA_DH_DEV1 : ATA_DH_DEV0;
    ata_outb(drive, ATA_REG_DRIVE, val);
    ata_delay(drive);  /* Allow 400ns for drive select */
}

/* ============================================================================
 * IDENTIFY Command
 * ============================================================================ */

static void ata_parse_string(uint16_t *src, char *dst, int words) {
    for (int i = 0; i < words; i++) {
        dst[i*2]     = (src[i] >> 8) & 0xFF;
        dst[i*2 + 1] = src[i] & 0xFF;
    }
    dst[words*2] = '\0';

    /* Trim trailing spaces */
    for (int i = words*2 - 1; i >= 0 && dst[i] == ' '; i--) {
        dst[i] = '\0';
    }
}

static bool ata_identify(ata_drive_t *drive) {
    ata_select_drive(drive);

    /* Send IDENTIFY command */
    ata_outb(drive, ATA_REG_SECCOUNT, 0);
    ata_outb(drive, ATA_REG_LBA_LO, 0);
    ata_outb(drive, ATA_REG_LBA_MID, 0);
    ata_outb(drive, ATA_REG_LBA_HI, 0);
    ata_outb(drive, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    ata_delay(drive);

    /* Check if drive exists */
    uint8_t status = ata_inb(drive, ATA_REG_STATUS);
    if (status == 0) {
        drive->present = false;
        drive->type = ATA_DEV_NONE;
        return false;
    }

    /* Wait for BSY to clear */
    if (!ata_wait_ready(drive, 100000)) {
        drive->present = false;
        return false;
    }

    /* Check for ATAPI */
    uint8_t lba_mid = ata_inb(drive, ATA_REG_LBA_MID);
    uint8_t lba_hi = ata_inb(drive, ATA_REG_LBA_HI);

    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        /* ATAPI device */
        drive->type = ATA_DEV_ATAPI;
        drive->present = true;

        /* Send IDENTIFY PACKET DEVICE */
        ata_outb(drive, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        ata_delay(drive);
    } else if (lba_mid == 0 && lba_hi == 0) {
        /* ATA device */
        drive->type = ATA_DEV_ATA;
        drive->present = true;
    } else {
        /* Unknown */
        drive->type = ATA_DEV_UNKNOWN;
        drive->present = false;
        return false;
    }

    /* Wait for data */
    if (!ata_wait_drq(drive, 100000)) {
        return false;
    }

    /* Read identify data */
    for (int i = 0; i < 256; i++) {
        drive->identify[i] = ata_inw(drive, ATA_REG_DATA);
    }

    /* Parse identify data */
    ata_parse_string(&drive->identify[27], drive->model, 20);
    ata_parse_string(&drive->identify[10], drive->serial, 10);

    /* Check for LBA48 support */
    drive->lba48 = (drive->identify[83] & (1 << 10)) != 0;

    /* Get size */
    if (drive->lba48) {
        drive->size48 = ((uint64_t)drive->identify[103] << 48) |
                        ((uint64_t)drive->identify[102] << 32) |
                        ((uint64_t)drive->identify[101] << 16) |
                        ((uint64_t)drive->identify[100]);
        drive->size = (drive->size48 > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)drive->size48;
    } else {
        drive->size = ((uint32_t)drive->identify[61] << 16) | drive->identify[60];
        drive->size48 = drive->size;
    }

    ATA_DBG("Found %s: %s, %u MB\n",
            drive->type == ATA_DEV_ATA ? "ATA" : "ATAPI",
            drive->model, drive->size / 2048);

    return true;
}

/* ============================================================================
 * Read Sectors (28-bit LBA)
 * ============================================================================ */

/**
 * Read sectors using 28-bit LBA
 * @param drive  Drive to read from
 * @param lba    Starting LBA (logical block address)
 * @param count  Number of sectors to read (1-255)
 * @param buf    Buffer to read into
 * @return       Number of sectors read, or 0 on error
 */
static uint32_t ata_read_sectors(ata_drive_t *drive, uint32_t lba, uint8_t count, void *buf) {
    if (!drive->present || drive->type != ATA_DEV_ATA) return 0;
    if (count == 0) count = 1;

    ATA_DBG("read: lba=%u count=%u\n", lba, count);

    ata_select_drive(drive);

    /* Wait for drive ready */
    if (!ata_wait_ready(drive, 100000)) {
        ATA_DBG("read: drive not ready\n");
        return 0;
    }

    /* Select drive with LBA mode and high LBA bits */
    uint8_t drive_head = drive->drive ? ATA_DH_DEV1 : ATA_DH_DEV0;
    drive_head |= ATA_DH_LBA;
    drive_head |= (lba >> 24) & 0x0F;
    ata_outb(drive, ATA_REG_DRIVE, drive_head);
    ata_delay(drive);

    /* Send sector count and LBA */
    ata_outb(drive, ATA_REG_SECCOUNT, count);
    ata_outb(drive, ATA_REG_LBA_LO, lba & 0xFF);
    ata_outb(drive, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    ata_outb(drive, ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

    /* Send read command */
    ata_outb(drive, ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    /* Read sectors */
    uint16_t *buf16 = (uint16_t *)buf;
    uint32_t sectors_read = 0;

    for (int s = 0; s < count; s++) {
        /* Wait for data */
        if (!ata_wait_drq(drive, 100000)) {
            ATA_DBG("read: DRQ timeout at sector %d\n", s);
            break;
        }

        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf16[s * 256 + i] = ata_inw(drive, ATA_REG_DATA);
        }

        sectors_read++;
    }

    ATA_DBG("read: %u sectors completed\n", sectors_read);
    return sectors_read;
}

/* ============================================================================
 * Write Sectors (28-bit LBA) - FIXED VERSION
 * ============================================================================ */

/**
 * Write sectors using 28-bit LBA
 *
 * IMPORTANT: For PIO writes, after sending data for each sector,
 * we must wait for BSY to clear before writing the next sector.
 * The drive needs time to actually write the data to the platter.
 *
 * @param drive  Drive to write to
 * @param lba    Starting LBA
 * @param count  Number of sectors to write (1-255)
 * @param buf    Buffer to write from
 * @return       Number of sectors written, or 0 on error
 */
static uint32_t ata_write_sectors(ata_drive_t *drive, uint32_t lba, uint8_t count, const void *buf) {
    if (!drive->present || drive->type != ATA_DEV_ATA) return 0;
    if (count == 0) count = 1;

    ATA_DBG("write: lba=%u count=%u\n", lba, count);

    ata_select_drive(drive);

    /* Wait for drive ready */
    if (!ata_wait_ready(drive, 100000)) {
        ATA_DBG("write: drive not ready\n");
        return 0;
    }

    /* Select drive with LBA mode and high LBA bits */
    uint8_t drive_head = drive->drive ? ATA_DH_DEV1 : ATA_DH_DEV0;
    drive_head |= ATA_DH_LBA;
    drive_head |= (lba >> 24) & 0x0F;
    ata_outb(drive, ATA_REG_DRIVE, drive_head);
    ata_delay(drive);

    /* Send sector count and LBA */
    ata_outb(drive, ATA_REG_SECCOUNT, count);
    ata_outb(drive, ATA_REG_LBA_LO, lba & 0xFF);
    ata_outb(drive, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    ata_outb(drive, ATA_REG_LBA_HI, (lba >> 16) & 0xFF);

    /* Send write command */
    ata_outb(drive, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *buf16 = (const uint16_t *)buf;
    uint32_t sectors_written = 0;

    for (int s = 0; s < count; s++) {
        /* Wait for DRQ - drive is ready to receive data */
        if (!ata_wait_drq(drive, 100000)) {
            ATA_DBG("write: DRQ timeout at sector %d\n", s);
            break;
        }

        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            ata_outw(drive, ATA_REG_DATA, buf16[s * 256 + i]);
        }

        /*
         * CRITICAL: After writing a sector's data, we MUST wait for the
         * drive to finish processing before:
         * - Writing the next sector
         * - Sending another command
         *
         * The drive will assert BSY while writing to the platter.
         * Just doing ata_delay() (400ns) is NOT enough!
         */
        if (!ata_wait_write_complete(drive, 500000)) {
            ATA_DBG("write: completion timeout at sector %d\n", s);
            break;
        }

        sectors_written++;
    }

    /* Flush the drive's write cache */
    if (sectors_written > 0) {
        ata_outb(drive, ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (!ata_wait_ready(drive, 500000)) {
            ATA_DBG("write: cache flush timeout\n");
            /* Don't fail - data is probably written, just not flushed */
        }
    }

    ATA_DBG("write: %u sectors completed\n", sectors_written);
    return sectors_written;
}

/* ============================================================================
 * Multi-sector Write (optimized for large writes)
 * ============================================================================ */

/**
 * Write multiple sectors efficiently
 * Handles writes larger than 255 sectors by splitting
 */
static uint32_t ata_write_sectors_multi(ata_drive_t *drive, uint32_t lba,
                                         uint32_t count, const void *buf) {
    const uint8_t *ptr = (const uint8_t *)buf;
    uint32_t total_written = 0;

    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : count;

        uint32_t written = ata_write_sectors(drive, lba, chunk, ptr);
        if (written == 0) break;

        total_written += written;
        lba += written;
        ptr += written * 512;
        count -= written;

        if (written != chunk) break;  /* Partial write - stop */
    }

    return total_written;
}

/* ============================================================================
 * Software Reset
 * ============================================================================ */

/**
 * Perform a software reset on the ATA controller
 * This resets both master and slave on the channel
 */
static void ata_software_reset(ata_drive_t *drive) {
    ATA_DBG("Performing software reset\n");

    /* Assert SRST */
    outb(drive->ctrl_base + ATA_REG_DEVCTRL, ATA_CTRL_SRST | ATA_CTRL_NIEN);

    /* Wait at least 5us */
    for (volatile int i = 0; i < 1000; i++);

    /* De-assert SRST, keep interrupts disabled */
    outb(drive->ctrl_base + ATA_REG_DEVCTRL, ATA_CTRL_NIEN);

    /* Wait for drive to become ready (up to 30 seconds for some drives) */
    for (int i = 0; i < 300000; i++) {
        uint8_t status = ata_alt_status(drive);
        if (!(status & ATA_SR_BSY)) {
            ATA_DBG("Reset complete\n");
            return;
        }
        for (volatile int j = 0; j < 100; j++);
    }

    ATA_DBG("Reset timeout!\n");
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

static void ata_init_drive(int index, uint16_t io_base, uint16_t ctrl_base, uint8_t drive_num) {
    ata_drive_t *drive = &g_ata.drives[index];

    drive->io_base = io_base;
    drive->ctrl_base = ctrl_base;
    drive->drive = drive_num;
    drive->present = false;
    drive->type = ATA_DEV_NONE;

    ata_identify(drive);
}

static inline void ata_init(void) {
    ATA_DBG("Initializing ATA controller\n");

    /* Disable interrupts for now (PIO polling mode) */
    outb(ATA_PRIMARY_CTRL, ATA_CTRL_NIEN);
    outb(ATA_SECONDARY_CTRL, ATA_CTRL_NIEN);

    /* Initialize all 4 possible drives */
    ata_init_drive(0, ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0);     /* Primary Master */
    ata_init_drive(1, ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 1);     /* Primary Slave */
    ata_init_drive(2, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 0); /* Secondary Master */
    ata_init_drive(3, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 1); /* Secondary Slave */

    g_ata.initialized = true;
    ATA_DBG("ATA initialization complete\n");
}

/* Get drive by index */
static inline ata_drive_t *ata_get_drive(int index) {
    if (index < 0 || index >= 4) return NULL;
    if (!g_ata.drives[index].present) return NULL;
    return &g_ata.drives[index];
}

/* Find first ATA drive */
static inline ata_drive_t *ata_get_first_drive(void) {
    for (int i = 0; i < 4; i++) {
        if (g_ata.drives[i].present && g_ata.drives[i].type == ATA_DEV_ATA) {
            return &g_ata.drives[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Diagnostic Functions
 * ============================================================================ */

/**
 * Test write capability - writes and reads back a test pattern
 * @param drive  Drive to test
 * @param lba    LBA to use for test (WILL BE OVERWRITTEN!)
 * @return       true if write/read successful
 */
static bool ata_test_write(ata_drive_t *drive, uint32_t lba) {
    uint8_t write_buf[512];
    uint8_t read_buf[512];

    /* Create test pattern */
    for (int i = 0; i < 512; i++) {
        write_buf[i] = (uint8_t)(i ^ 0xAA);
    }

    /* Write test sector */
    if (ata_write_sectors(drive, lba, 1, write_buf) != 1) {
        return false;
    }

    /* Read it back */
    if (ata_read_sectors(drive, lba, 1, read_buf) != 1) {
        return false;
    }

    /* Verify */
    for (int i = 0; i < 512; i++) {
        if (read_buf[i] != write_buf[i]) {
            ATA_DBG("Verify failed at offset %d: wrote 0x%x, read 0x%x\n",
                    i, write_buf[i], read_buf[i]);
            return false;
        }
    }

    return true;
}

#endif /* ATA_H */