/**
 * floppy_blkdev.h - Floppy Disk Block Device Adapter
 *
 * Wraps the floppy driver as a VFS-compatible block device.
 * Allows mounting floppy disks through the standard VFS layer.
 *
 * LAZY INITIALIZATION: Hardware is not touched until first actual use.
 * This avoids IRQ timing issues during early boot when interrupts
 * may not yet be enabled.
 *
 * Usage:
 *   blkdev_t fd0;
 *   if (floppy_blkdev_init(&fd0)) {
 *       // Device structure ready, but hardware not yet initialized
 *       // Hardware init happens automatically on first read/write
 *       vfs_mount("/fd0", &fd0, fat12_get_vfs_ops(), NULL, NULL);
 *   }
 */

#ifndef FLOPPY_BLKDEV_H
#define FLOPPY_BLKDEV_H

#include "vfs.h"
#include "floppy.h"

/* ============================================================================
 * Lazy Initialization State
 * ============================================================================ */

static bool g_floppy_hw_initialized = false;
static bool g_floppy_hw_init_attempted = false;
static bool g_floppy_hw_available = false;

/**
 * Ensure floppy hardware is initialized (lazy init)
 * Called automatically before any hardware access
 * Safe to call multiple times - only initializes once
 */
static bool floppy_ensure_init(void) {
    /* Already know the result? */
    if (g_floppy_hw_init_attempted) {
        return g_floppy_hw_available;
    }

    /* Mark that we've attempted init */
    g_floppy_hw_init_attempted = true;

    /* Now actually initialize the hardware */
    if (floppy_init()) {
        g_floppy_hw_initialized = true;
        g_floppy_hw_available = true;
        return true;
    }

    /* Init failed */
    g_floppy_hw_available = false;
    return false;
}

/* ============================================================================
 * Block Device Callbacks
 * ============================================================================ */

/**
 * Read sectors from floppy
 */
static uint32_t floppy_blkdev_read(blkdev_t *dev, uint32_t lba, uint8_t count, void *buf) {
    (void)dev;

    /* Lazy init */
    if (!floppy_ensure_init()) {
        return 0;
    }

    return fdc_read_sectors(lba, count, buf);
}

/**
 * Write sectors to floppy
 */
static uint32_t floppy_blkdev_write(blkdev_t *dev, uint32_t lba, uint8_t count, const void *buf) {
    (void)dev;

    /* Lazy init */
    if (!floppy_ensure_init()) {
        return 0;
    }

    return fdc_write_sectors(lba, count, buf);
}

/**
 * Sync (flush) - floppy has no cache, so this is a no-op
 */
static bool floppy_blkdev_sync(blkdev_t *dev) {
    (void)dev;
    return true;
}

/**
 * Eject floppy (turn off motor)
 */
static bool floppy_blkdev_eject(blkdev_t *dev) {
    (void)dev;

    /* Only turn off motor if we actually initialized */
    if (g_floppy_hw_initialized) {
        fdc_motor_off();
    }

    return true;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize floppy as a block device
 *
 * NOTE: This does NOT touch the hardware! It only sets up the
 * block device structure. Actual hardware initialization is
 * deferred until first read/write/check_media call.
 *
 * @param dev   Block device structure to fill in
 * @return      true (structure is always valid, hw checked on first use)
 */
static bool floppy_blkdev_init(blkdev_t *dev) {
    /* Fill in block device structure */
    dev->type = BLKDEV_FLOPPY;

    /* Copy name */
    const char *name = "fd0";
    for (int i = 0; name[i] && i < 31; i++) {
        dev->name[i] = name[i];
        dev->name[i+1] = '\0';
    }

    /* Copy model */
    const char *model = "3.5\" Floppy Drive";
    for (int i = 0; model[i] && i < 47; i++) {
        dev->model[i] = model[i];
        dev->model[i+1] = '\0';
    }

    /* Geometry for 1.44MB floppy */
    dev->sector_size = FD_SECTOR_SIZE;          /* 512 */
    dev->total_sectors = FD_TOTAL_SECTORS;      /* 2880 */
    dev->cylinders = FD_TRACKS;                 /* 80 */
    dev->heads = FD_HEADS;                      /* 2 */
    dev->sectors_per_track = FD_SECTORS_PER_TRACK;  /* 18 */

    /* State - assume present, actual check on first use */
    dev->present = true;
    dev->removable = true;
    dev->readonly = false;

    /* Operations */
    dev->read = floppy_blkdev_read;
    dev->write = floppy_blkdev_write;
    dev->sync = floppy_blkdev_sync;
    dev->eject = floppy_blkdev_eject;

    dev->driver_data = NULL;

    /*
     * Always return true - we're just setting up the structure.
     * Actual hardware availability is determined on first use.
     */
    return true;
}

/**
 * Check if floppy hardware is actually available
 * This WILL trigger hardware initialization if not yet done
 */
static bool floppy_blkdev_available(void) {
    return floppy_ensure_init();
}

/**
 * Check if floppy is ready (disk inserted, motor can spin)
 * This does a quick test read of sector 0
 */
static bool floppy_blkdev_check_media(blkdev_t *dev) {
    if (!dev || dev->type != BLKDEV_FLOPPY) return false;

    /* Lazy init */
    if (!floppy_ensure_init()) {
        return false;
    }

    uint8_t buf[512];
    return fdc_read_sectors(0, 1, buf) == 1;
}

/**
 * Check if hardware init has been attempted yet
 * (useful for status display)
 */
static inline bool floppy_blkdev_init_attempted(void) {
    return g_floppy_hw_init_attempted;
}

/**
 * Check if hardware is known to be working
 * (only valid after init has been attempted)
 */
static inline bool floppy_blkdev_hw_ok(void) {
    return g_floppy_hw_available;
}

#endif /* FLOPPY_BLKDEV_H */