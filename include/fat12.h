/**
 * fat12.h - FAT12 File System Driver
 * 
 * FAT12 is the standard file system for 1.44MB floppy disks.
 * This driver provides read/write access to files and directories.
 * 
 * Floppy Disk Layout (1.44MB):
 *   Sector 0:       Boot sector (BPB)
 *   Sectors 1-9:    FAT1
 *   Sectors 10-18:  FAT2 (backup)
 *   Sectors 19-32:  Root directory (224 entries)
 *   Sectors 33+:    Data area
 */

#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * FAT12 Constants
 * ============================================================================ */

#define FAT12_SECTOR_SIZE       512
#define FAT12_CLUSTER_FREE      0x000
#define FAT12_CLUSTER_RESERVED  0x001
#define FAT12_CLUSTER_BAD       0xFF7
#define FAT12_CLUSTER_EOF_MIN   0xFF8
#define FAT12_CLUSTER_EOF       0xFFF

#define FAT12_ATTR_READONLY     0x01
#define FAT12_ATTR_HIDDEN       0x02
#define FAT12_ATTR_SYSTEM       0x04
#define FAT12_ATTR_VOLUME_ID    0x08
#define FAT12_ATTR_DIRECTORY    0x10
#define FAT12_ATTR_ARCHIVE      0x20
#define FAT12_ATTR_LFN          0x0F    // Long filename entry

#define FAT12_DELETED           0xE5    // First byte of deleted entry
#define FAT12_KANJI             0x05    // Escaped 0xE5 in first byte

#define FAT12_MAX_PATH          260
#define FAT12_MAX_OPEN_FILES    16

/* ============================================================================
 * BIOS Parameter Block (Boot Sector)
 * ============================================================================ */

typedef struct {
    uint8_t     jmp[3];             // Jump instruction
    char        oem_name[8];        // OEM identifier
    uint16_t    bytes_per_sector;   // Usually 512
    uint8_t     sectors_per_cluster;// Usually 1 for floppy
    uint16_t    reserved_sectors;   // Boot sectors before FAT
    uint8_t     num_fats;           // Number of FATs (usually 2)
    uint16_t    root_entry_count;   // Root directory entries (224)
    uint16_t    total_sectors_16;   // Total sectors (if < 65536)
    uint8_t     media_type;         // Media descriptor (0xF0 for floppy)
    uint16_t    fat_size_16;        // Sectors per FAT
    uint16_t    sectors_per_track;  // Sectors per track
    uint16_t    num_heads;          // Number of heads
    uint32_t    hidden_sectors;     // Hidden sectors before partition
    uint32_t    total_sectors_32;   // Total sectors (if >= 65536)
    
    // Extended boot record
    uint8_t     drive_number;
    uint8_t     reserved1;
    uint8_t     boot_signature;     // 0x29 if extended
    uint32_t    volume_id;
    char        volume_label[11];
    char        fs_type[8];         // "FAT12   "
} __attribute__((packed)) fat12_bpb_t;

/* ============================================================================
 * Directory Entry (32 bytes)
 * ============================================================================ */

typedef struct {
    char        name[8];            // Filename (space padded)
    char        ext[3];             // Extension (space padded)
    uint8_t     attributes;         // File attributes
    uint8_t     reserved;           // Reserved (NT flags)
    uint8_t     create_time_tenths; // Creation time (tenths of second)
    uint16_t    create_time;        // Creation time
    uint16_t    create_date;        // Creation date
    uint16_t    access_date;        // Last access date
    uint16_t    cluster_high;       // High 16 bits of cluster (0 for FAT12)
    uint16_t    modify_time;        // Last modification time
    uint16_t    modify_date;        // Last modification date
    uint16_t    cluster_low;        // Starting cluster
    uint32_t    file_size;          // File size in bytes
} __attribute__((packed)) fat12_dirent_t;

/* ============================================================================
 * File System State
 * ============================================================================ */

/* Block device read/write function types */
typedef uint32_t (*block_read_fn)(uint32_t lba, uint8_t count, void *buf);
typedef uint32_t (*block_write_fn)(uint32_t lba, uint8_t count, const void *buf);

typedef struct {
    /* Block device callbacks */
    block_read_fn   read_sectors;
    block_write_fn  write_sectors;
    
    /* BPB cache */
    fat12_bpb_t     bpb;
    
    /* Computed values */
    uint32_t        fat_start;          // First FAT sector
    uint32_t        root_start;         // Root directory sector
    uint32_t        data_start;         // First data sector
    uint32_t        root_sectors;       // Sectors in root directory
    uint32_t        total_clusters;     // Total data clusters
    
    /* FAT cache (for small FATs, cache entire FAT) */
    uint8_t        *fat_cache;
    bool            fat_dirty;
    
    /* Sector buffer */
    uint8_t         sector_buf[FAT12_SECTOR_SIZE];
    
    bool            mounted;
} fat12_fs_t;

/* ============================================================================
 * File Handle
 * ============================================================================ */

typedef struct {
    fat12_fs_t     *fs;
    fat12_dirent_t  dirent;         // Copy of directory entry
    uint32_t        dir_sector;     // Sector containing dirent
    uint32_t        dir_offset;     // Offset within sector
    uint32_t        position;       // Current read/write position
    uint32_t        cluster;        // Current cluster
    uint32_t        cluster_offset; // Offset within current cluster
    bool            open;
    bool            dirty;
} fat12_file_t;

/* ============================================================================
 * Directory Iterator
 * ============================================================================ */

typedef struct {
    fat12_fs_t     *fs;
    uint32_t        sector;         // Current sector
    uint32_t        entry;          // Entry index within sector
    uint32_t        cluster;        // Current cluster (0 for root)
    bool            is_root;        // In root directory
} fat12_dir_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Get cluster from directory entry */
static inline uint16_t fat12_get_cluster(fat12_dirent_t *de) {
    return de->cluster_low;
}

/* Convert cluster number to sector number */
static inline uint32_t fat12_cluster_to_sector(fat12_fs_t *fs, uint16_t cluster) {
    return fs->data_start + (cluster - 2) * fs->bpb.sectors_per_cluster;
}

/* Read FAT entry for cluster */
static uint16_t fat12_read_fat(fat12_fs_t *fs, uint16_t cluster) {
    uint32_t fat_offset = cluster + (cluster / 2);  // cluster * 1.5
    uint16_t value;
    
    if (fs->fat_cache) {
        value = *(uint16_t *)&fs->fat_cache[fat_offset];
    } else {
        // Read from disk
        uint32_t sector = fs->fat_start + (fat_offset / FAT12_SECTOR_SIZE);
        uint32_t offset = fat_offset % FAT12_SECTOR_SIZE;
        
        fs->read_sectors(sector, 1, fs->sector_buf);
        
        if (offset == FAT12_SECTOR_SIZE - 1) {
            // Spans sector boundary
            value = fs->sector_buf[offset];
            fs->read_sectors(sector + 1, 1, fs->sector_buf);
            value |= fs->sector_buf[0] << 8;
        } else {
            value = *(uint16_t *)&fs->sector_buf[offset];
        }
    }
    
    // FAT12 entries are 12 bits
    if (cluster & 1) {
        return value >> 4;
    } else {
        return value & 0x0FFF;
    }
}

/* Write FAT entry for cluster */
static void fat12_write_fat(fat12_fs_t *fs, uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster + (cluster / 2);
    
    if (fs->fat_cache) {
        uint16_t *entry = (uint16_t *)&fs->fat_cache[fat_offset];
        if (cluster & 1) {
            *entry = (*entry & 0x000F) | (value << 4);
        } else {
            *entry = (*entry & 0xF000) | (value & 0x0FFF);
        }
        fs->fat_dirty = true;
    }
    // TODO: Direct disk write if no cache
}

/* Check if cluster is end-of-chain */
static inline bool fat12_is_eof(uint16_t cluster) {
    return cluster >= FAT12_CLUSTER_EOF_MIN;
}

/* Find free cluster */
static uint16_t fat12_find_free_cluster(fat12_fs_t *fs) {
    for (uint16_t i = 2; i < fs->total_clusters + 2; i++) {
        if (fat12_read_fat(fs, i) == FAT12_CLUSTER_FREE) {
            return i;
        }
    }
    return 0;  // No free clusters
}

/* ============================================================================
 * Filename Conversion
 * ============================================================================ */

/* Convert 8.3 name to string */
static void fat12_name_to_string(fat12_dirent_t *de, char *out) {
    int i, j = 0;
    
    // Copy name (trim trailing spaces)
    for (i = 0; i < 8 && de->name[i] != ' '; i++) {
        out[j++] = de->name[i];
    }
    
    // Add extension if present
    if (de->ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && de->ext[i] != ' '; i++) {
            out[j++] = de->ext[i];
        }
    }
    
    out[j] = '\0';
}

/* Convert string to 8.3 name */
static void fat12_string_to_name(const char *str, char *name, char *ext) {
    int i, j;
    
    // Initialize with spaces
    for (i = 0; i < 8; i++) name[i] = ' ';
    for (i = 0; i < 3; i++) ext[i] = ' ';
    
    // Copy name part
    for (i = 0, j = 0; str[i] && str[i] != '.' && j < 8; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') c -= 32;  // Uppercase
        name[j++] = c;
    }
    
    // Find and copy extension
    while (str[i] && str[i] != '.') i++;
    if (str[i] == '.') {
        i++;
        for (j = 0; str[i] && j < 3; i++) {
            char c = str[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            ext[j++] = c;
        }
    }
}

/* Compare 8.3 names */
static bool fat12_name_match(fat12_dirent_t *de, const char *name, const char *ext) {
    for (int i = 0; i < 8; i++) {
        if (de->name[i] != name[i]) return false;
    }
    for (int i = 0; i < 3; i++) {
        if (de->ext[i] != ext[i]) return false;
    }
    return true;
}

/* ============================================================================
 * File System Operations
 * ============================================================================ */

/**
 * Mount FAT12 file system
 */
static bool fat12_mount(fat12_fs_t *fs, block_read_fn read_fn, block_write_fn write_fn, 
                        uint8_t *fat_cache_buf) {
    fs->read_sectors = read_fn;
    fs->write_sectors = write_fn;
    fs->fat_cache = fat_cache_buf;
    fs->fat_dirty = false;
    fs->mounted = false;
    
    // Read boot sector
    if (fs->read_sectors(0, 1, &fs->bpb) != 1) {
        return false;
    }
    
    // Validate BPB
    if (fs->bpb.bytes_per_sector != 512) return false;
    if (fs->bpb.num_fats == 0) return false;
    
    // Calculate layout
    fs->fat_start = fs->bpb.reserved_sectors;
    fs->root_sectors = (fs->bpb.root_entry_count * 32 + 511) / 512;
    fs->root_start = fs->fat_start + (fs->bpb.num_fats * fs->bpb.fat_size_16);
    fs->data_start = fs->root_start + fs->root_sectors;
    
    uint32_t total_sectors = fs->bpb.total_sectors_16 ? 
                             fs->bpb.total_sectors_16 : fs->bpb.total_sectors_32;
    uint32_t data_sectors = total_sectors - fs->data_start;
    fs->total_clusters = data_sectors / fs->bpb.sectors_per_cluster;
    
    // Load FAT into cache if buffer provided
    if (fs->fat_cache) {
        for (uint16_t i = 0; i < fs->bpb.fat_size_16; i++) {
            fs->read_sectors(fs->fat_start + i, 1, 
                            &fs->fat_cache[i * FAT12_SECTOR_SIZE]);
        }
    }
    
    fs->mounted = true;
    return true;
}

/**
 * Sync FAT to disk
 */
static void fat12_sync(fat12_fs_t *fs) {
    if (!fs->fat_cache || !fs->fat_dirty) return;
    
    // Write to both FATs
    for (uint8_t f = 0; f < fs->bpb.num_fats; f++) {
        uint32_t fat_sector = fs->fat_start + f * fs->bpb.fat_size_16;
        for (uint16_t i = 0; i < fs->bpb.fat_size_16; i++) {
            fs->write_sectors(fat_sector + i, 1,
                             &fs->fat_cache[i * FAT12_SECTOR_SIZE]);
        }
    }
    
    fs->fat_dirty = false;
}

/**
 * Unmount file system
 */
static void fat12_unmount(fat12_fs_t *fs) {
    fat12_sync(fs);
    fs->mounted = false;
}

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

/**
 * Open root directory for iteration
 */
static void fat12_open_root(fat12_fs_t *fs, fat12_dir_t *dir) {
    dir->fs = fs;
    dir->sector = fs->root_start;
    dir->entry = 0;
    dir->cluster = 0;
    dir->is_root = true;
}

/**
 * Open subdirectory for iteration
 */
static void fat12_open_dir(fat12_fs_t *fs, fat12_dir_t *dir, uint16_t cluster) {
    dir->fs = fs;
    dir->cluster = cluster;
    dir->sector = fat12_cluster_to_sector(fs, cluster);
    dir->entry = 0;
    dir->is_root = false;
}

/**
 * Read next directory entry
 */
static bool fat12_read_dir(fat12_dir_t *dir, fat12_dirent_t *de) {
    fat12_fs_t *fs = dir->fs;
    
    while (1) {
        // Check bounds
        if (dir->is_root) {
            if (dir->sector >= fs->root_start + fs->root_sectors) {
                return false;  // End of root directory
            }
        } else {
            // Check if we need to follow cluster chain
            uint32_t cluster_end = fat12_cluster_to_sector(fs, dir->cluster) + 
                                   fs->bpb.sectors_per_cluster;
            if (dir->sector >= cluster_end) {
                // Move to next cluster
                uint16_t next = fat12_read_fat(fs, dir->cluster);
                if (fat12_is_eof(next)) return false;
                dir->cluster = next;
                dir->sector = fat12_cluster_to_sector(fs, next);
            }
        }
        
        // Read sector if needed
        if (dir->entry == 0) {
            fs->read_sectors(dir->sector, 1, fs->sector_buf);
        }
        
        // Get entry
        fat12_dirent_t *entry = (fat12_dirent_t *)&fs->sector_buf[dir->entry * 32];
        
        // Advance to next entry
        dir->entry++;
        if (dir->entry >= 16) {  // 16 entries per sector
            dir->entry = 0;
            dir->sector++;
        }
        
        // Check entry validity
        if (entry->name[0] == 0x00) {
            return false;  // End of directory
        }
        if ((uint8_t)entry->name[0] == FAT12_DELETED) {
            continue;  // Deleted entry
        }
        if (entry->attributes == FAT12_ATTR_LFN) {
            continue;  // Long filename entry (skip)
        }

        *de = *entry;
        return true;
    }
}

/**
 * Find file in directory
 */
static bool fat12_find_in_dir(fat12_dir_t *dir, const char *filename, fat12_dirent_t *de) {
    char name[8], ext[3];
    fat12_string_to_name(filename, name, ext);

    while (fat12_read_dir(dir, de)) {
        if (fat12_name_match(de, name, ext)) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

/**
 * Open file by path (e.g., "/FOLDER/FILE.TXT")
 */
static bool fat12_open(fat12_fs_t *fs, const char *path, fat12_file_t *file) {
    fat12_dir_t dir;
    fat12_dirent_t de;

    // Skip leading slash
    if (*path == '/') path++;
    if (*path == '\0') return false;

    // Start at root
    fat12_open_root(fs, &dir);

    // Parse path components
    while (*path) {
        // Extract component
        char component[13] = {0};
        int i = 0;
        while (*path && *path != '/' && i < 12) {
            component[i++] = *path++;
        }
        if (*path == '/') path++;

        // Find in current directory
        if (!fat12_find_in_dir(&dir, component, &de)) {
            return false;
        }

        // If more path components, must be directory
        if (*path) {
            if (!(de.attributes & FAT12_ATTR_DIRECTORY)) {
                return false;
            }
            fat12_open_dir(fs, &dir, fat12_get_cluster(&de));
        }
    }

    // Set up file handle
    file->fs = fs;
    file->dirent = de;
    file->position = 0;
    file->cluster = fat12_get_cluster(&de);
    file->cluster_offset = 0;
    file->open = true;
    file->dirty = false;

    return true;
}

/**
 * Read from file
 */
static uint32_t fat12_read(fat12_file_t *file, void *buf, uint32_t size) {
    if (!file->open) return 0;

    fat12_fs_t *fs = file->fs;
    uint8_t *out = (uint8_t *)buf;
    uint32_t bytes_read = 0;
    uint32_t cluster_size = fs->bpb.sectors_per_cluster * FAT12_SECTOR_SIZE;

    while (size > 0 && file->position < file->dirent.file_size) {
        // Clamp to file size
        uint32_t remaining = file->dirent.file_size - file->position;
        if (size > remaining) size = remaining;

        // Handle cluster boundary
        if (file->cluster_offset >= cluster_size) {
            uint16_t next = fat12_read_fat(fs, file->cluster);
            if (fat12_is_eof(next)) break;
            file->cluster = next;
            file->cluster_offset = 0;
        }

        // Calculate how much to read from current cluster
        uint32_t to_read = cluster_size - file->cluster_offset;
        if (to_read > size) to_read = size;

        // Read sector(s)
        uint32_t sector = fat12_cluster_to_sector(fs, file->cluster) +
                          (file->cluster_offset / FAT12_SECTOR_SIZE);
        uint32_t offset = file->cluster_offset % FAT12_SECTOR_SIZE;

        // Simple: read one sector at a time
        while (to_read > 0) {
            fs->read_sectors(sector, 1, fs->sector_buf);

            uint32_t chunk = FAT12_SECTOR_SIZE - offset;
            if (chunk > to_read) chunk = to_read;

            for (uint32_t i = 0; i < chunk; i++) {
                out[bytes_read + i] = fs->sector_buf[offset + i];
            }

            bytes_read += chunk;
            file->position += chunk;
            file->cluster_offset += chunk;
            to_read -= chunk;
            size -= chunk;

            offset = 0;
            sector++;
        }
    }

    return bytes_read;
}

/**
 * Get file size
 */
static inline uint32_t fat12_size(fat12_file_t *file) {
    return file->open ? file->dirent.file_size : 0;
}

/**
 * Seek within file
 */
static bool fat12_seek(fat12_file_t *file, uint32_t position) {
    if (!file->open) return false;
    if (position > file->dirent.file_size) return false;

    fat12_fs_t *fs = file->fs;
    uint32_t cluster_size = fs->bpb.sectors_per_cluster * FAT12_SECTOR_SIZE;

    // Restart from beginning
    file->cluster = fat12_get_cluster(&file->dirent);
    file->position = 0;
    file->cluster_offset = 0;

    // Follow cluster chain
    while (file->position + cluster_size <= position) {
        uint16_t next = fat12_read_fat(fs, file->cluster);
        if (fat12_is_eof(next)) break;
        file->cluster = next;
        file->position += cluster_size;
    }

    file->cluster_offset = position - file->position;
    file->position = position;

    return true;
}

/**
 * Close file
 */
static void fat12_close(fat12_file_t *file) {
    if (file->dirty) {
        // Update directory entry if modified
        // TODO: Write back modified dirent
    }
    file->open = false;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Check if entry is a directory
 */
static inline bool fat12_is_dir(fat12_dirent_t *de) {
    return (de->attributes & FAT12_ATTR_DIRECTORY) != 0;
}

/**
 * Get volume label
 */
static bool fat12_get_label(fat12_fs_t *fs, char *label) {
    // First check BPB
    if (fs->bpb.boot_signature == 0x29) {
        for (int i = 0; i < 11; i++) {
            label[i] = fs->bpb.volume_label[i];
        }
        label[11] = '\0';
        // Trim trailing spaces
        for (int i = 10; i >= 0 && label[i] == ' '; i--) {
            label[i] = '\0';
        }
        return true;
    }

    // Otherwise look in root directory
    fat12_dir_t dir;
    fat12_dirent_t de;
    fat12_open_root(fs, &dir);

    while (fat12_read_dir(&dir, &de)) {
        if (de.attributes == FAT12_ATTR_VOLUME_ID) {
            for (int i = 0; i < 8; i++) label[i] = de.name[i];
            for (int i = 0; i < 3; i++) label[8 + i] = de.ext[i];
            label[11] = '\0';
            return true;
        }
    }

    label[0] = '\0';
    return false;
}

/**
 * Get free space in bytes
 */
static uint32_t fat12_free_space(fat12_fs_t *fs) {
    uint32_t free_clusters = 0;

    for (uint16_t i = 2; i < fs->total_clusters + 2; i++) {
        if (fat12_read_fat(fs, i) == FAT12_CLUSTER_FREE) {
            free_clusters++;
        }
    }

    return free_clusters * fs->bpb.sectors_per_cluster * FAT12_SECTOR_SIZE;
}

#endif /* FAT12_H */