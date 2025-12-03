/**
 * floppy_blkdev.h - Floppy Block Device Adapter
 *
 * Bridges the floppy driver (floppy.h) to the VFS block device interface.
 * Allows mounting floppy disks through the standard VFS layer.
 *
 * Usage:
 *   blkdev_t fd0;
 *   if (floppy_blkdev_init(&fd0)) {
 *       vfs_mount("/fd0", &fd0, fat12_get_vfs_ops(), NULL, NULL);
 *   }
 */

#ifndef FLOPPY_BLKDEV_H
#define FLOPPY_BLKDEV_H

#include "vfs.h"
#include "floppy.h"

/* ============================================================================
 * Block Device Callbacks
 * ============================================================================ */

/**
 * Read sectors from floppy
 */
static uint32_t floppy_blkdev_read(blkdev_t *dev, uint32_t lba, uint8_t count, void *buf) {
    (void)dev;
    return fdc_read_sectors(lba, count, buf);
}

/**
 * Write sectors to floppy
 */
static uint32_t floppy_blkdev_write(blkdev_t *dev, uint32_t lba, uint8_t count, const void *buf) {
    (void)dev;
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
    fdc_motor_off();
    return true;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize floppy as a block device
 *
 * @param dev   Block device structure to fill in
 * @return      true if floppy is available
 */
static bool floppy_blkdev_init(blkdev_t *dev) {
    /* Initialize floppy controller */
    if (!floppy_init()) {
        return false;
    }

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

    /* State */
    dev->present = true;
    dev->removable = true;
    dev->readonly = false;

    /* Operations */
    dev->read = floppy_blkdev_read;
    dev->write = floppy_blkdev_write;
    dev->sync = floppy_blkdev_sync;
    dev->eject = floppy_blkdev_eject;

    dev->driver_data = NULL;

    return true;
}

/**
 * Check if floppy is ready (disk inserted, motor can spin)
 * This does a quick test read of sector 0
 */
static bool floppy_blkdev_check_media(blkdev_t *dev) {
    if (!dev || dev->type != BLKDEV_FLOPPY) return false;
    
    uint8_t buf[512];
    return fdc_read_sectors(0, 1, buf) == 1;
}

#endif /* FLOPPY_BLKDEV_H */
