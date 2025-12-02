/**
 * ata_blkdev.h - ATA Block Device Wrapper
 *
 * Wraps the ATA driver to provide VFS-compatible block device interface.
 */

#ifndef ATA_BLKDEV_H
#define ATA_BLKDEV_H

#include "vfs.h"
#include "ata.h"

/* ============================================================================
 * ATA Block Device Implementation
 * ============================================================================ */

/**
 * ATA read implementation
 */
static uint32_t ata_blkdev_read(blkdev_t *dev, uint32_t lba, uint8_t count, void *buf) {
    ata_drive_t *drive = (ata_drive_t *)dev->driver_data;
    if (!drive) return 0;
    return ata_read_sectors(drive, lba, count, buf);
}

/**
 * ATA write implementation
 */
static uint32_t ata_blkdev_write(blkdev_t *dev, uint32_t lba, uint8_t count, const void *buf) {
    ata_drive_t *drive = (ata_drive_t *)dev->driver_data;
    if (!drive) return 0;
    return ata_write_sectors(drive, lba, count, buf);
}

/**
 * ATA sync implementation
 * Note: PIO mode writes are synchronous, so no cache flush needed
 */
static bool ata_blkdev_sync(blkdev_t *dev) {
    (void)dev;  /* PIO mode is synchronous - nothing to flush */
    return true;
}

/**
 * Initialize ATA block device from ATA drive
 */
static void ata_blkdev_init(blkdev_t *blk, ata_drive_t *drive) {
    blk->type = BLKDEV_ATA;

    /* Copy name */
    int i;
    for (i = 0; i < 31 && drive->model[i]; i++) {
        blk->name[i] = drive->model[i];
    }
    blk->name[i] = '\0';

    /* Copy model */
    for (i = 0; i < 47 && drive->model[i]; i++) {
        blk->model[i] = drive->model[i];
    }
    blk->model[i] = '\0';

    /* Geometry */
    blk->sector_size = 512;
    blk->total_sectors = drive->size;
    blk->cylinders = 0;  /* Not used for LBA */
    blk->heads = 0;
    blk->sectors_per_track = 0;

    /* State */
    blk->present = drive->present;
    blk->removable = false;
    blk->readonly = false;

    /* Operations */
    blk->read = ata_blkdev_read;
    blk->write = ata_blkdev_write;
    blk->sync = ata_blkdev_sync;
    blk->eject = NULL;

    /* Driver data */
    blk->driver_data = drive;
}

/**
 * Find first available ATA drive and wrap it
 */
static bool ata_blkdev_find_first(blkdev_t *blk) {
    ata_drive_t *drive = ata_get_first_drive();
    if (!drive) return false;

    ata_blkdev_init(blk, drive);
    return true;
}

#endif /* ATA_BLKDEV_H */