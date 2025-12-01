/**
 * fat12_write.h - FAT12 Write Operations
 *
 * Extends fat12.h with write capabilities:
 *   - Format a drive with FAT12
 *   - Create files and directories
 *   - Write file data
 *   - Delete files and directories
 *   - Allocate/free clusters
 *
 * Supports both floppy-sized (1.44MB) and larger ATA partitions.
 */

#ifndef FAT12_WRITE_H
#define FAT12_WRITE_H

#include "fat12.h"

/* ============================================================================
 * FAT12 Geometry Presets
 * ============================================================================ */

/* 1.44MB Floppy Disk */
#define FAT12_FLOPPY_SECTORS        2880
#define FAT12_FLOPPY_SECTORS_TRACK  18
#define FAT12_FLOPPY_HEADS          2
#define FAT12_FLOPPY_MEDIA          0xF0

/* Small partition (up to 16MB - FAT12 limit) */
#define FAT12_MAX_SECTORS           32680   /* ~16MB, FAT12 limit */

/* ============================================================================
 * Format Parameters
 * ============================================================================ */

typedef struct {
    uint32_t    total_sectors;      /* Total sectors on device */
    uint16_t    bytes_per_sector;   /* Usually 512 */
    uint8_t     sectors_per_cluster;/* 1, 2, 4, 8, etc. */
    uint16_t    reserved_sectors;   /* Sectors before FAT (usually 1) */
    uint8_t     num_fats;           /* Number of FATs (usually 2) */
    uint16_t    root_entry_count;   /* Root dir entries (224 for floppy) */
    uint16_t    sectors_per_fat;    /* Sectors per FAT */
    uint16_t    sectors_per_track;  /* For CHS geometry */
    uint16_t    num_heads;          /* For CHS geometry */
    uint8_t     media_type;         /* Media descriptor */
    char        volume_label[11];   /* Volume label (space padded) */
    uint32_t    volume_id;          /* Volume serial number */
} fat12_format_params_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Simple string copy with space padding */
static void fat12_copy_padded(char *dst, const char *src, int len) {
    int i;
    for (i = 0; i < len && src[i]; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;  /* Uppercase */
        dst[i] = c;
    }
    for (; i < len; i++) {
        dst[i] = ' ';
    }
}

/* Generate simple volume ID from tick counter or other source */
static uint32_t fat12_generate_volume_id(void) {
    /* Simple pseudo-random based on port reads */
    uint32_t id = 0;
    for (int i = 0; i < 4; i++) {
        id = (id << 8) | (inb(0x40) ^ inb(0x41));  /* PIT counter */
    }
    return id ? id : 0x12345678;
}

/* ============================================================================
 * Calculate Format Parameters
 * ============================================================================ */

/**
 * Calculate optimal FAT12 parameters for given disk size
 */
static void fat12_calc_params(fat12_format_params_t *params, uint32_t total_sectors) {
    params->total_sectors = total_sectors;
    params->bytes_per_sector = 512;
    params->reserved_sectors = 1;
    params->num_fats = 2;

    /* Determine sectors per cluster based on size */
    if (total_sectors <= 2880) {
        /* Floppy-sized */
        params->sectors_per_cluster = 1;
        params->root_entry_count = 224;
        params->sectors_per_track = 18;
        params->num_heads = 2;
        params->media_type = 0xF0;
    } else if (total_sectors <= 8192) {
        /* Small partition (4MB) */
        params->sectors_per_cluster = 1;
        params->root_entry_count = 512;
        params->sectors_per_track = 32;
        params->num_heads = 16;
        params->media_type = 0xF8;  /* Fixed disk */
    } else if (total_sectors <= 16384) {
        /* Medium partition (8MB) */
        params->sectors_per_cluster = 2;
        params->root_entry_count = 512;
        params->sectors_per_track = 32;
        params->num_heads = 16;
        params->media_type = 0xF8;
    } else {
        /* Large partition (up to 16MB) */
        params->sectors_per_cluster = 4;
        params->root_entry_count = 512;
        params->sectors_per_track = 63;
        params->num_heads = 16;
        params->media_type = 0xF8;
    }

    /* Calculate FAT size
     * FAT12 entries are 12 bits (1.5 bytes per cluster)
     * total_data_sectors = total - reserved - root_dir_sectors
     * data_clusters = total_data_sectors / sectors_per_cluster
     * fat_bytes = (data_clusters * 3 + 1) / 2
     * fat_sectors = (fat_bytes + 511) / 512
     */
    uint32_t root_sectors = (params->root_entry_count * 32 + 511) / 512;
    uint32_t data_sectors = total_sectors - params->reserved_sectors - root_sectors;

    /* Estimate clusters (will refine after knowing FAT size) */
    uint32_t est_clusters = data_sectors / params->sectors_per_cluster;
    uint32_t fat_bytes = (est_clusters * 3 + 1) / 2 + 3;  /* +3 for reserved entries */
    params->sectors_per_fat = (fat_bytes + 511) / 512;

    /* Ensure FAT12 limit (max 4084 clusters) */
    uint32_t final_data = data_sectors - (params->num_fats * params->sectors_per_fat);
    uint32_t final_clusters = final_data / params->sectors_per_cluster;
    if (final_clusters > 4084) {
        /* Increase cluster size */
        params->sectors_per_cluster *= 2;
        fat12_calc_params(params, total_sectors);  /* Recalculate */
        return;
    }

    /* Default volume label */
    fat12_copy_padded(params->volume_label, "NO NAME", 11);
    params->volume_id = fat12_generate_volume_id();
}

/* ============================================================================
 * Format Drive with FAT12
 * ============================================================================ */

/**
 * Format a drive with FAT12 filesystem
 *
 * @param write_fn      Block write function
 * @param read_fn       Block read function (for verification)
 * @param params        Format parameters (NULL for auto-calculate)
 * @param total_sectors Total sectors on device
 * @param label         Volume label (NULL for default)
 * @return              true on success
 */
static bool fat12_format(block_write_fn write_fn, block_read_fn read_fn,
                         fat12_format_params_t *params, uint32_t total_sectors,
                         const char *label) {
    uint8_t sector[512];
    fat12_format_params_t local_params;

    /* Calculate parameters if not provided */
    if (!params) {
        params = &local_params;
        fat12_calc_params(params, total_sectors);
    }

    /* Set volume label if provided */
    if (label) {
        fat12_copy_padded(params->volume_label, label, 11);
    }

    /* ========== Create Boot Sector (BPB) ========== */

    /* Clear sector */
    for (int i = 0; i < 512; i++) sector[i] = 0;

    /* Jump instruction (EB 3C 90 = JMP SHORT 0x3E; NOP) */
    sector[0] = 0xEB;
    sector[1] = 0x3C;
    sector[2] = 0x90;

    /* OEM Name */
    fat12_copy_padded((char *)&sector[3], "RETROFUTUREOS", 8);

    /* BIOS Parameter Block */
    *(uint16_t *)&sector[11] = params->bytes_per_sector;
    sector[13] = params->sectors_per_cluster;
    *(uint16_t *)&sector[14] = params->reserved_sectors;
    sector[16] = params->num_fats;
    *(uint16_t *)&sector[17] = params->root_entry_count;

    if (params->total_sectors < 65536) {
        *(uint16_t *)&sector[19] = (uint16_t)params->total_sectors;
        *(uint32_t *)&sector[32] = 0;
    } else {
        *(uint16_t *)&sector[19] = 0;
        *(uint32_t *)&sector[32] = params->total_sectors;
    }

    sector[21] = params->media_type;
    *(uint16_t *)&sector[22] = params->sectors_per_fat;
    *(uint16_t *)&sector[24] = params->sectors_per_track;
    *(uint16_t *)&sector[26] = params->num_heads;
    *(uint32_t *)&sector[28] = 0;   /* Hidden sectors */

    /* Extended Boot Record */
    sector[36] = 0x00;              /* Drive number */
    sector[37] = 0x00;              /* Reserved */
    sector[38] = 0x29;              /* Extended boot signature */
    *(uint32_t *)&sector[39] = params->volume_id;

    /* Volume label */
    for (int i = 0; i < 11; i++) {
        sector[43 + i] = params->volume_label[i];
    }

    /* File system type */
    fat12_copy_padded((char *)&sector[54], "FAT12", 8);

    /* Boot code placeholder (simple halt loop) */
    sector[62] = 0xEB;  /* JMP $ (infinite loop) */
    sector[63] = 0xFE;

    /* Boot signature */
    sector[510] = 0x55;
    sector[511] = 0xAA;

    /* Write boot sector */
    if (write_fn(0, 1, sector) != 1) {
        return false;
    }

    /* ========== Create FAT Tables ========== */

    uint32_t fat_start = params->reserved_sectors;

    for (uint8_t fat_num = 0; fat_num < params->num_fats; fat_num++) {
        uint32_t fat_sector = fat_start + (fat_num * params->sectors_per_fat);

        for (uint16_t s = 0; s < params->sectors_per_fat; s++) {
            /* Clear sector */
            for (int i = 0; i < 512; i++) sector[i] = 0;

            /* First sector has reserved entries */
            if (s == 0) {
                /* FAT[0] = media type | 0xF00 */
                /* FAT[1] = 0xFFF (end of chain marker) */
                sector[0] = params->media_type;
                sector[1] = 0xFF;
                sector[2] = 0xFF;
            }

            if (write_fn(fat_sector + s, 1, sector) != 1) {
                return false;
            }
        }
    }

    /* ========== Create Root Directory ========== */

    uint32_t root_start = fat_start + (params->num_fats * params->sectors_per_fat);
    uint32_t root_sectors = (params->root_entry_count * 32 + 511) / 512;

    for (uint32_t s = 0; s < root_sectors; s++) {
        /* Clear sector */
        for (int i = 0; i < 512; i++) sector[i] = 0;

        /* First sector contains volume label entry */
        if (s == 0) {
            fat12_dirent_t *vol = (fat12_dirent_t *)sector;

            /* Copy volume label */
            for (int i = 0; i < 8; i++) {
                vol->name[i] = params->volume_label[i];
            }
            for (int i = 0; i < 3; i++) {
                vol->ext[i] = params->volume_label[8 + i];
            }

            vol->attributes = FAT12_ATTR_VOLUME_ID;
            vol->cluster_low = 0;
            vol->file_size = 0;

            /* Set creation time/date (placeholder) */
            vol->create_time = 0;
            vol->create_date = (1 << 5) | 1;  /* Jan 1, 1980 */
            vol->modify_time = 0;
            vol->modify_date = (1 << 5) | 1;
        }

        if (write_fn(root_start + s, 1, sector) != 1) {
            return false;
        }
    }

    /* ========== Clear First Data Cluster (optional) ========== */

    uint32_t data_start = root_start + root_sectors;
    for (int i = 0; i < 512; i++) sector[i] = 0;

    for (uint8_t s = 0; s < params->sectors_per_cluster; s++) {
        write_fn(data_start + s, 1, sector);
    }

    return true;
}

/* ============================================================================
 * Cluster Allocation
 * ============================================================================ */

/**
 * Allocate a free cluster
 *
 * @param fs    Mounted filesystem
 * @return      Cluster number, or 0 on failure
 */
static uint16_t fat12_alloc_cluster(fat12_fs_t *fs) {
    uint16_t cluster = fat12_find_free_cluster(fs);
    if (cluster == 0) {
        return 0;  /* No free clusters */
    }

    /* Mark as end of chain */
    fat12_write_fat(fs, cluster, FAT12_CLUSTER_EOF);

    return cluster;
}

/**
 * Extend a cluster chain
 *
 * @param fs      Mounted filesystem
 * @param last    Last cluster in chain
 * @return        New cluster number, or 0 on failure
 */
static uint16_t fat12_extend_chain(fat12_fs_t *fs, uint16_t last) {
    uint16_t new_cluster = fat12_alloc_cluster(fs);
    if (new_cluster == 0) {
        return 0;
    }

    /* Link to chain */
    fat12_write_fat(fs, last, new_cluster);

    return new_cluster;
}

/**
 * Free a cluster chain
 *
 * @param fs       Mounted filesystem
 * @param cluster  First cluster of chain
 */
static void fat12_free_chain(fat12_fs_t *fs, uint16_t cluster) {
    while (cluster >= 2 && cluster < FAT12_CLUSTER_EOF_MIN) {
        uint16_t next = fat12_read_fat(fs, cluster);
        fat12_write_fat(fs, cluster, FAT12_CLUSTER_FREE);
        cluster = next;
    }
}

/* ============================================================================
 * Directory Entry Operations
 * ============================================================================ */

/**
 * Find a free directory entry
 *
 * @param fs         Mounted filesystem
 * @param dir_cluster Directory cluster (0 for root)
 * @param out_sector Output: sector containing entry
 * @param out_offset Output: offset within sector
 * @return           true if found
 */
static bool fat12_find_free_entry(fat12_fs_t *fs, uint16_t dir_cluster,
                                   uint32_t *out_sector, uint32_t *out_offset) {
    uint32_t sector;
    uint32_t max_entries;

    if (dir_cluster == 0) {
        /* Root directory */
        sector = fs->root_start;
        max_entries = fs->bpb.root_entry_count;
    } else {
        /* Subdirectory - TODO: extend cluster chain if needed */
        sector = fat12_cluster_to_sector(fs, dir_cluster);
        max_entries = 16 * fs->bpb.sectors_per_cluster;  /* Approx */
    }

    uint32_t entries_per_sector = 512 / 32;
    uint32_t entries_checked = 0;

    while (entries_checked < max_entries) {
        fs->read_sectors(sector, 1, fs->sector_buf);

        for (uint32_t i = 0; i < entries_per_sector && entries_checked < max_entries; i++) {
            fat12_dirent_t *de = (fat12_dirent_t *)&fs->sector_buf[i * 32];

            /* Check for free entry */
            if (de->name[0] == 0x00 || (uint8_t)de->name[0] == FAT12_DELETED) {
                *out_sector = sector;
                *out_offset = i * 32;
                return true;
            }

            entries_checked++;
        }

        /* Move to next sector */
        if (dir_cluster == 0) {
            sector++;
            if (sector >= fs->root_start + fs->root_sectors) {
                return false;  /* Root full */
            }
        } else {
            /* TODO: Handle cluster chain for subdirectories */
            return false;
        }
    }

    return false;
}

/**
 * Write a directory entry
 */
static bool fat12_write_entry(fat12_fs_t *fs, uint32_t sector, uint32_t offset,
                               fat12_dirent_t *entry) {
    /* Read sector */
    fs->read_sectors(sector, 1, fs->sector_buf);

    /* Copy entry */
    fat12_dirent_t *de = (fat12_dirent_t *)&fs->sector_buf[offset];
    *de = *entry;

    /* Write back */
    return fs->write_sectors(sector, 1, fs->sector_buf) == 1;
}

/* ============================================================================
 * File Creation
 * ============================================================================ */

/**
 * Create a new file
 *
 * @param fs       Mounted filesystem
 * @param path     Path to file (e.g., "/FOLDER/FILE.TXT")
 * @param attr     File attributes
 * @return         true on success
 */
static bool fat12_create_file(fat12_fs_t *fs, const char *path, uint8_t attr) {
    /* Skip leading slash */
    if (*path == '/') path++;
    if (*path == '\0') return false;

    /* For now, only support root directory */
    /* TODO: Parse path for subdirectories */

    /* Check if file already exists */
    fat12_dir_t dir;
    fat12_dirent_t existing;
    fat12_open_root(fs, &dir);
    if (fat12_find_in_dir(&dir, path, &existing)) {
        return false;  /* Already exists */
    }

    /* Find free entry */
    uint32_t entry_sector, entry_offset;
    if (!fat12_find_free_entry(fs, 0, &entry_sector, &entry_offset)) {
        return false;  /* Directory full */
    }

    /* Create directory entry */
    fat12_dirent_t de;
    for (int i = 0; i < 32; i++) ((uint8_t *)&de)[i] = 0;

    /* Convert filename to 8.3 format */
    fat12_string_to_name(path, de.name, de.ext);

    de.attributes = attr ? attr : FAT12_ATTR_ARCHIVE;
    de.cluster_low = 0;     /* No data yet */
    de.file_size = 0;

    /* Set timestamps (placeholder - no RTC driver yet) */
    de.create_time = 0;
    de.create_date = (1 << 5) | 1;  /* Jan 1, 1980 */
    de.modify_time = 0;
    de.modify_date = (1 << 5) | 1;
    de.access_date = (1 << 5) | 1;

    /* Write entry */
    if (!fat12_write_entry(fs, entry_sector, entry_offset, &de)) {
        return false;
    }

    /* Sync FAT if using cache */
    fat12_sync(fs);

    return true;
}

/**
 * Create a new directory
 *
 * @param fs    Mounted filesystem
 * @param path  Path to directory
 * @return      true on success
 */
static bool fat12_mkdir(fat12_fs_t *fs, const char *path) {
    /* Skip leading slash */
    if (*path == '/') path++;
    if (*path == '\0') return false;

    /* Check if already exists */
    fat12_dir_t dir;
    fat12_dirent_t existing;
    fat12_open_root(fs, &dir);
    if (fat12_find_in_dir(&dir, path, &existing)) {
        return false;  /* Already exists */
    }

    /* Allocate a cluster for the directory */
    uint16_t cluster = fat12_alloc_cluster(fs);
    if (cluster == 0) {
        return false;  /* No space */
    }

    /* Find free entry in parent (root for now) */
    uint32_t entry_sector, entry_offset;
    if (!fat12_find_free_entry(fs, 0, &entry_sector, &entry_offset)) {
        fat12_write_fat(fs, cluster, FAT12_CLUSTER_FREE);
        return false;
    }

    /* Create directory entry */
    fat12_dirent_t de;
    for (int i = 0; i < 32; i++) ((uint8_t *)&de)[i] = 0;

    fat12_string_to_name(path, de.name, de.ext);
    de.attributes = FAT12_ATTR_DIRECTORY;
    de.cluster_low = cluster;
    de.file_size = 0;  /* Directories have size 0 */

    de.create_time = 0;
    de.create_date = (1 << 5) | 1;
    de.modify_time = 0;
    de.modify_date = (1 << 5) | 1;

    /* Write parent entry */
    if (!fat12_write_entry(fs, entry_sector, entry_offset, &de)) {
        fat12_write_fat(fs, cluster, FAT12_CLUSTER_FREE);
        return false;
    }

    /* Initialize the new directory with . and .. entries */
    uint8_t dir_sector[512];
    for (int i = 0; i < 512; i++) dir_sector[i] = 0;

    /* "." entry */
    fat12_dirent_t *dot = (fat12_dirent_t *)&dir_sector[0];
    for (int i = 0; i < 8; i++) dot->name[i] = ' ';
    dot->name[0] = '.';
    for (int i = 0; i < 3; i++) dot->ext[i] = ' ';
    dot->attributes = FAT12_ATTR_DIRECTORY;
    dot->cluster_low = cluster;

    /* ".." entry */
    fat12_dirent_t *dotdot = (fat12_dirent_t *)&dir_sector[32];
    for (int i = 0; i < 8; i++) dotdot->name[i] = ' ';
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    for (int i = 0; i < 3; i++) dotdot->ext[i] = ' ';
    dotdot->attributes = FAT12_ATTR_DIRECTORY;
    dotdot->cluster_low = 0;  /* Root directory */

    /* Write directory cluster */
    uint32_t dir_lba = fat12_cluster_to_sector(fs, cluster);

    /* Write all sectors in cluster */
    for (uint8_t s = 0; s < fs->bpb.sectors_per_cluster; s++) {
        if (fs->write_sectors(dir_lba + s, 1, dir_sector) != 1) {
            return false;
        }
        /* Clear buffer for subsequent sectors */
        for (int i = 0; i < 512; i++) dir_sector[i] = 0;
    }

    fat12_sync(fs);
    return true;
}

/* ============================================================================
 * File Writing
 * ============================================================================ */

/**
 * Write data to an open file (overwrites from current position)
 *
 * @param file  Open file handle
 * @param buf   Data to write
 * @param size  Bytes to write
 * @return      Bytes written
 */
static uint32_t fat12_write(fat12_file_t *file, const void *buf, uint32_t size) {
    if (!file->open) return 0;

    fat12_fs_t *fs = file->fs;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t bytes_written = 0;
    uint32_t cluster_size = fs->bpb.sectors_per_cluster * FAT12_SECTOR_SIZE;

    while (size > 0) {
        /* Need to allocate first cluster? */
        if (file->cluster == 0) {
            file->cluster = fat12_alloc_cluster(fs);
            if (file->cluster == 0) {
                break;  /* Out of space */
            }
            file->dirent.cluster_low = file->cluster;
            file->cluster_offset = 0;
            file->dirty = true;
        }

        /* Need to allocate next cluster? */
        if (file->cluster_offset >= cluster_size) {
            uint16_t next = fat12_read_fat(fs, file->cluster);
            if (fat12_is_eof(next)) {
                /* Extend chain */
                next = fat12_extend_chain(fs, file->cluster);
                if (next == 0) {
                    break;  /* Out of space */
                }
            }
            file->cluster = next;
            file->cluster_offset = 0;
        }

        /* Calculate sector and offset */
        uint32_t sector = fat12_cluster_to_sector(fs, file->cluster) +
                          (file->cluster_offset / FAT12_SECTOR_SIZE);
        uint32_t offset = file->cluster_offset % FAT12_SECTOR_SIZE;

        /* Read sector if partial write */
        if (offset != 0 || size < FAT12_SECTOR_SIZE) {
            fs->read_sectors(sector, 1, fs->sector_buf);
        }

        /* Copy data */
        uint32_t to_write = FAT12_SECTOR_SIZE - offset;
        if (to_write > size) to_write = size;

        for (uint32_t i = 0; i < to_write; i++) {
            fs->sector_buf[offset + i] = in[bytes_written + i];
        }

        /* Write sector */
        if (fs->write_sectors(sector, 1, fs->sector_buf) != 1) {
            break;
        }

        bytes_written += to_write;
        size -= to_write;
        file->position += to_write;
        file->cluster_offset += to_write;

        /* Update file size */
        if (file->position > file->dirent.file_size) {
            file->dirent.file_size = file->position;
            file->dirty = true;
        }
    }

    return bytes_written;
}

/**
 * Flush file changes to disk (update directory entry)
 */
static bool fat12_flush(fat12_file_t *file) {
    if (!file->open || !file->dirty) return true;

    fat12_fs_t *fs = file->fs;

    /* Read directory sector */
    fs->read_sectors(file->dir_sector, 1, fs->sector_buf);

    /* Update entry */
    fat12_dirent_t *de = (fat12_dirent_t *)&fs->sector_buf[file->dir_offset];
    de->file_size = file->dirent.file_size;
    de->cluster_low = file->dirent.cluster_low;
    de->cluster_high = file->dirent.cluster_high;

    /* Write back */
    if (fs->write_sectors(file->dir_sector, 1, fs->sector_buf) != 1) {
        return false;
    }

    file->dirty = false;
    fat12_sync(fs);

    return true;
}

/* ============================================================================
 * File Deletion
 * ============================================================================ */

/**
 * Delete a file
 *
 * @param fs    Mounted filesystem
 * @param path  Path to file
 * @return      true on success
 */
static bool fat12_delete(fat12_fs_t *fs, const char *path) {
    /* Skip leading slash */
    if (*path == '/') path++;
    if (*path == '\0') return false;

    /* Find the file */
    fat12_dir_t dir;
    fat12_dirent_t de;
    fat12_open_root(fs, &dir);

    char name[8], ext[3];
    fat12_string_to_name(path, name, ext);

    /* Manually search so we can track position */
    uint32_t sector = fs->root_start;
    uint32_t entry_idx = 0;
    bool found = false;

    for (uint32_t s = 0; s < fs->root_sectors && !found; s++) {
        fs->read_sectors(sector + s, 1, fs->sector_buf);

        for (uint32_t i = 0; i < 16 && !found; i++) {
            fat12_dirent_t *e = (fat12_dirent_t *)&fs->sector_buf[i * 32];

            if (e->name[0] == 0x00) break;  /* End of directory */
            if ((uint8_t)e->name[0] == FAT12_DELETED) continue;
            if (e->attributes == FAT12_ATTR_LFN) continue;

            if (fat12_name_match(e, name, ext)) {
                de = *e;
                sector = sector + s;
                entry_idx = i;
                found = true;
            }
        }
    }

    if (!found) return false;

    /* Don't delete directories with this function */
    if (de.attributes & FAT12_ATTR_DIRECTORY) {
        return false;
    }

    /* Free cluster chain */
    if (de.cluster_low >= 2) {
        fat12_free_chain(fs, de.cluster_low);
    }

    /* Mark entry as deleted */
    fs->read_sectors(sector, 1, fs->sector_buf);
    fs->sector_buf[entry_idx * 32] = FAT12_DELETED;
    fs->write_sectors(sector, 1, fs->sector_buf);

    fat12_sync(fs);
    return true;
}

/**
 * Delete a directory (must be empty)
 */
static bool fat12_rmdir(fat12_fs_t *fs, const char *path) {
    /* Skip leading slash */
    if (*path == '/') path++;
    if (*path == '\0') return false;

    /* Find the directory */
    char name[8], ext[3];
    fat12_string_to_name(path, name, ext);

    uint32_t sector = fs->root_start;
    uint32_t entry_idx = 0;
    fat12_dirent_t de;
    bool found = false;

    for (uint32_t s = 0; s < fs->root_sectors && !found; s++) {
        fs->read_sectors(sector + s, 1, fs->sector_buf);

        for (uint32_t i = 0; i < 16 && !found; i++) {
            fat12_dirent_t *e = (fat12_dirent_t *)&fs->sector_buf[i * 32];

            if (e->name[0] == 0x00) break;
            if ((uint8_t)e->name[0] == FAT12_DELETED) continue;

            if (fat12_name_match(e, name, ext) &&
                (e->attributes & FAT12_ATTR_DIRECTORY)) {
                de = *e;
                sector = sector + s;
                entry_idx = i;
                found = true;
            }
        }
    }

    if (!found) return false;

    /* Check if directory is empty (only . and ..) */
    fat12_dir_t subdir;
    fat12_dirent_t child;
    fat12_open_dir(fs, &subdir, de.cluster_low);

    int entry_count = 0;
    while (fat12_read_dir(&subdir, &child)) {
        /* Skip . and .. */
        if (child.name[0] == '.' &&
            (child.name[1] == ' ' || child.name[1] == '.')) {
            continue;
        }
        entry_count++;
    }

    if (entry_count > 0) {
        return false;  /* Directory not empty */
    }

    /* Free cluster chain */
    fat12_free_chain(fs, de.cluster_low);

    /* Mark entry as deleted */
    fs->read_sectors(sector, 1, fs->sector_buf);
    fs->sector_buf[entry_idx * 32] = FAT12_DELETED;
    fs->write_sectors(sector, 1, fs->sector_buf);

    fat12_sync(fs);
    return true;
}

/* ============================================================================
 * High-Level Convenience Functions
 * ============================================================================ */

/**
 * Write entire buffer to file (creates/overwrites)
 */
static uint32_t fat12_write_file(fat12_fs_t *fs, const char *path,
                                  const void *buf, uint32_t size) {
    /* Delete existing file */
    fat12_delete(fs, path);

    /* Create new file */
    if (!fat12_create_file(fs, path, FAT12_ATTR_ARCHIVE)) {
        return 0;
    }

    /* Open for writing */
    fat12_file_t file;
    if (!fat12_open(fs, path, &file)) {
        return 0;
    }

    /* Track directory entry location for flush */
    /* Re-find entry to get sector/offset */
    fat12_dir_t dir;
    fat12_open_root(fs, &dir);

    char name[8], ext[3];
    fat12_string_to_name(path, name, ext);

    uint32_t sector = fs->root_start;
    for (uint32_t s = 0; s < fs->root_sectors; s++) {
        fs->read_sectors(sector + s, 1, fs->sector_buf);

        for (uint32_t i = 0; i < 16; i++) {
            fat12_dirent_t *e = (fat12_dirent_t *)&fs->sector_buf[i * 32];
            if (fat12_name_match(e, name, ext)) {
                file.dir_sector = sector + s;
                file.dir_offset = i * 32;
                goto found;
            }
        }
    }
found:

    /* Write data */
    uint32_t written = fat12_write(&file, buf, size);

    /* Flush and close */
    fat12_flush(&file);
    fat12_close(&file);

    return written;
}

/**
 * Append data to existing file
 */
static uint32_t fat12_append_file(fat12_fs_t *fs, const char *path,
                                   const void *buf, uint32_t size) {
    fat12_file_t file;
    if (!fat12_open(fs, path, &file)) {
        return 0;
    }

    /* Seek to end */
    fat12_seek(&file, file.dirent.file_size);

    /* Find dir entry location */
    char name[8], ext[3];
    fat12_string_to_name(path, name, ext);

    uint32_t sector = fs->root_start;
    for (uint32_t s = 0; s < fs->root_sectors; s++) {
        fs->read_sectors(sector + s, 1, fs->sector_buf);

        for (uint32_t i = 0; i < 16; i++) {
            fat12_dirent_t *e = (fat12_dirent_t *)&fs->sector_buf[i * 32];
            if (fat12_name_match(e, name, ext)) {
                file.dir_sector = sector + s;
                file.dir_offset = i * 32;
                goto found2;
            }
        }
    }
found2:

    uint32_t written = fat12_write(&file, buf, size);
    fat12_flush(&file);
    fat12_close(&file);

    return written;
}

/* ============================================================================
 * Rename/Move File
 * ============================================================================ */

/**
 * Rename a file
 */
static bool fat12_rename(fat12_fs_t *fs, const char *old_path, const char *new_path) {
    if (*old_path == '/') old_path++;
    if (*new_path == '/') new_path++;

    /* Check new name doesn't exist */
    fat12_dir_t dir;
    fat12_dirent_t de;
    fat12_open_root(fs, &dir);
    if (fat12_find_in_dir(&dir, new_path, &de)) {
        return false;  /* New name already exists */
    }

    /* Find old entry */
    char old_name[8], old_ext[3];
    fat12_string_to_name(old_path, old_name, old_ext);

    uint32_t sector = fs->root_start;
    for (uint32_t s = 0; s < fs->root_sectors; s++) {
        fs->read_sectors(sector + s, 1, fs->sector_buf);

        for (uint32_t i = 0; i < 16; i++) {
            fat12_dirent_t *e = (fat12_dirent_t *)&fs->sector_buf[i * 32];

            if (e->name[0] == 0x00) return false;
            if ((uint8_t)e->name[0] == FAT12_DELETED) continue;

            if (fat12_name_match(e, old_name, old_ext)) {
                /* Update name */
                fat12_string_to_name(new_path, e->name, e->ext);

                /* Write back */
                fs->write_sectors(sector + s, 1, fs->sector_buf);
                fat12_sync(fs);
                return true;
            }
        }
    }

    return false;
}

#endif /* FAT12_WRITE_H */