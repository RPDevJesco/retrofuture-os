/**
 * fat12_vfs.h - FAT12 VFS Adapter
 *
 * Bridges the FAT12 filesystem implementation to the VFS layer.
 * Wraps fat12.h and fat12_write.h functionality.
 */

#ifndef FAT12_VFS_H
#define FAT12_VFS_H

#include "vfs.h"
#include "fat12.h"
#include "fat12_write.h"

/* ============================================================================
 * FAT12-VFS State
 * ============================================================================ */

typedef struct fat12_vfs_state {
    fat12_fs_t   fs;              /* FAT12 filesystem state */
    uint8_t      fat_cache[4608]; /* FAT cache (9 sectors * 512) */
    uint8_t      sector_buf[512]; /* Working sector buffer */
    blkdev_t    *device;          /* Block device */
} fat12_vfs_state_t;

/* Global state (kernel uses one instance) */
static fat12_vfs_state_t g_fat12_state;

/* ============================================================================
 * Block Device Wrappers
 * ============================================================================ */

/* Current device for block I/O (set during mount) */
static blkdev_t *g_fat12_current_dev = NULL;

static uint32_t fat12_vfs_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!g_fat12_current_dev || !g_fat12_current_dev->read) return 0;
    return g_fat12_current_dev->read(g_fat12_current_dev, lba, count, buf);
}

static uint32_t fat12_vfs_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!g_fat12_current_dev || !g_fat12_current_dev->write) return 0;
    return g_fat12_current_dev->write(g_fat12_current_dev, lba, count, buf);
}

/* ============================================================================
 * FAT12 VFS Operations Implementation
 * ============================================================================ */

/**
 * Mount FAT12 filesystem
 */
static bool fat12_vfs_mount(vfs_mount_t *mnt, blkdev_t *dev, const char *opts) {
    (void)opts;

    fat12_vfs_state_t *state = &g_fat12_state;

    /* Store device reference */
    state->device = dev;
    g_fat12_current_dev = dev;

    /* Mount FAT12 */
    if (!fat12_mount(&state->fs, fat12_vfs_read_sectors, fat12_vfs_write_sectors,
                     state->fat_cache)) {
        return false;
    }

    /* Store state in mount point */
    mnt->fs_data = state;
    mnt->sector_buf = state->sector_buf;
    mnt->cache = state->fat_cache;

    return true;
}

/**
 * Unmount FAT12 filesystem
 */
static void fat12_vfs_unmount(vfs_mount_t *mnt) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (state) {
        fat12_unmount(&state->fs);
        state->device = NULL;
        g_fat12_current_dev = NULL;
    }
}

/**
 * Sync FAT12 filesystem (flush FAT cache)
 */
static bool fat12_vfs_sync(vfs_mount_t *mnt) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (state) {
        fat12_sync(&state->fs);
    }
    return true;
}

/**
 * Lookup a file/directory by path
 */
static bool fat12_vfs_lookup(vfs_mount_t *mnt, const char *path, vfs_node_t *node) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    /* Handle root directory */
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        vfs_strcpy(node->name, "/");
        node->type = VFS_DIRECTORY;
        node->size = 0;
        node->inode = 0;
        node->cluster = 0;
        node->mount = mnt;
        return true;
    }

    /* Skip leading slash */
    if (*path == '/') path++;

    /* Open root and search */
    fat12_dir_t dir;
    fat12_dirent_t de;
    fat12_open_root(&state->fs, &dir);

    if (!fat12_find_in_dir(&dir, path, &de)) {
        return false;
    }

    /* Fill VFS node */
    fat12_name_to_string(&de, node->name);
    node->type = (de.attributes & FAT12_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    node->size = de.file_size;
    node->inode = de.cluster_low;
    node->cluster = de.cluster_low;
    node->mount = mnt;

    return true;
}

/**
 * Get file stats
 */
static bool fat12_vfs_stat(vfs_node_t *node, vfs_stat_t *stat) {
    stat->size = node->size;
    stat->type = node->type;
    stat->permissions = 0755;  /* rwxr-xr-x */
    stat->uid = 0;
    stat->gid = 0;
    stat->atime = 0;  /* TODO: Parse FAT timestamps */
    stat->mtime = 0;
    stat->ctime = 0;
    stat->blocks = (node->size + 511) / 512;
    stat->block_size = 512;
    return true;
}

/**
 * Create a file
 */
static bool fat12_vfs_create(vfs_mount_t *mnt, const char *path, uint8_t type) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    if (*path == '/') path++;

    if (type == VFS_DIRECTORY) {
        return fat12_mkdir(&state->fs, path);
    } else {
        return fat12_create_file(&state->fs, path, FAT12_ATTR_ARCHIVE);
    }
}

/**
 * Delete a file
 */
static bool fat12_vfs_unlink(vfs_mount_t *mnt, const char *path) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    if (*path == '/') path++;
    return fat12_delete(&state->fs, path);
}

/**
 * Rename a file
 */
static bool fat12_vfs_rename(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    return fat12_rename(&state->fs, old_path, new_path);
}

/**
 * Create a directory
 */
static bool fat12_vfs_mkdir(vfs_mount_t *mnt, const char *path) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    if (*path == '/') path++;
    return fat12_mkdir(&state->fs, path);
}

/**
 * Remove a directory
 */
static bool fat12_vfs_rmdir(vfs_mount_t *mnt, const char *path) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    if (*path == '/') path++;
    return fat12_rmdir(&state->fs, path);
}

/* ============================================================================
 * File Operations
 * ============================================================================ */

/* File state stored in fs_data */
typedef struct {
    fat12_file_t file;
    bool opened;
} fat12_file_state_t;

/* Pool of file states */
static fat12_file_state_t g_fat12_file_states[VFS_MAX_OPEN_FILES];
static int g_fat12_file_state_next = 0;

static fat12_file_state_t *alloc_file_state(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        int idx = (g_fat12_file_state_next + i) % VFS_MAX_OPEN_FILES;
        if (!g_fat12_file_states[idx].opened) {
            g_fat12_file_state_next = (idx + 1) % VFS_MAX_OPEN_FILES;
            return &g_fat12_file_states[idx];
        }
    }
    return NULL;
}

/**
 * Open a file
 */
static bool fat12_vfs_open(vfs_file_t *file, uint32_t flags) {
    vfs_mount_t *mnt = file->mount;
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    /* Allocate file state */
    fat12_file_state_t *fstate = alloc_file_state();
    if (!fstate) return false;

    /* Build path from node name */
    char path[VFS_MAX_PATH];
    path[0] = '/';
    vfs_strcpy(path + 1, file->node.name);

    /* Open the file */
    if (!fat12_open(&state->fs, path, &fstate->file)) {
        return false;
    }

    fstate->opened = true;
    file->fs_data = fstate;
    file->position = 0;

    /* Truncate if requested */
    if (flags & VFS_O_TRUNC) {
        /* TODO: Implement truncate */
    }

    /* Seek to end if append mode */
    if (flags & VFS_O_APPEND) {
        file->position = file->node.size;
    }

    return true;
}

/**
 * Close a file
 */
static void fat12_vfs_close(vfs_file_t *file) {
    fat12_file_state_t *fstate = (fat12_file_state_t *)file->fs_data;
    if (fstate) {
        fat12_close(&fstate->file);
        fstate->opened = false;
        file->fs_data = NULL;
    }
}

/**
 * Read from file
 */
static int32_t fat12_vfs_read(vfs_file_t *file, void *buf, uint32_t size) {
    fat12_file_state_t *fstate = (fat12_file_state_t *)file->fs_data;
    if (!fstate) return -1;

    /* Seek to current position */
    fstate->file.position = file->position;

    uint32_t bytes = fat12_read(&fstate->file, buf, size);
    file->position = fstate->file.position;

    return (int32_t)bytes;
}

/**
 * Write to file
 */
static int32_t fat12_vfs_write(vfs_file_t *file, const void *buf, uint32_t size) {
    fat12_file_state_t *fstate = (fat12_file_state_t *)file->fs_data;
    if (!fstate) return -1;

    /* Seek to current position */
    fstate->file.position = file->position;

    uint32_t bytes = fat12_write(&fstate->file, buf, size);

    if (bytes > 0) {
        file->position = fstate->file.position;
        file->node.size = fstate->file.dirent.file_size;
        file->dirty = true;

        /* Flush the file entry */
        fat12_flush(&fstate->file);
    }

    return (int32_t)bytes;
}

/**
 * Seek in file
 */
static int32_t fat12_vfs_seek(vfs_file_t *file, int32_t offset, int whence) {
    int32_t new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = file->node.size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0) return -1;

    file->position = new_pos;

    fat12_file_state_t *fstate = (fat12_file_state_t *)file->fs_data;
    if (fstate) {
        fstate->file.position = new_pos;
    }

    return new_pos;
}

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

/* Directory state */
typedef struct {
    fat12_dir_t dir;
    bool opened;
} fat12_dir_state_t;

static fat12_dir_state_t g_fat12_dir_states[VFS_MAX_OPEN_FILES];
static int g_fat12_dir_state_next = 0;

static fat12_dir_state_t *alloc_dir_state(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        int idx = (g_fat12_dir_state_next + i) % VFS_MAX_OPEN_FILES;
        if (!g_fat12_dir_states[idx].opened) {
            g_fat12_dir_state_next = (idx + 1) % VFS_MAX_OPEN_FILES;
            return &g_fat12_dir_states[idx];
        }
    }
    return NULL;
}

/**
 * Open directory
 */
static bool fat12_vfs_opendir(vfs_dir_t *dir) {
    vfs_mount_t *mnt = dir->mount;
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    fat12_dir_state_t *dstate = alloc_dir_state();
    if (!dstate) return false;

    /* Open directory based on cluster */
    if (dir->node.cluster == 0) {
        fat12_open_root(&state->fs, &dstate->dir);
    } else {
        fat12_open_dir(&state->fs, &dstate->dir, dir->node.cluster);
    }

    dstate->opened = true;
    dir->fs_data = dstate;
    dir->position = 0;

    return true;
}

/**
 * Close directory
 */
static void fat12_vfs_closedir(vfs_dir_t *dir) {
    fat12_dir_state_t *dstate = (fat12_dir_state_t *)dir->fs_data;
    if (dstate) {
        dstate->opened = false;
        dir->fs_data = NULL;
    }
}

/**
 * Read directory entry
 */
static bool fat12_vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *entry) {
    fat12_dir_state_t *dstate = (fat12_dir_state_t *)dir->fs_data;
    if (!dstate) return false;

    fat12_dirent_t de;

    while (fat12_read_dir(&dstate->dir, &de)) {
        /* Skip . and .. */
        if (de.name[0] == '.') {
            if (de.name[1] == ' ' ||
                (de.name[1] == '.' && de.name[2] == ' ')) {
                continue;
            }
        }

        /* Fill VFS dirent */
        fat12_name_to_string(&de, entry->name);
        entry->inode = de.cluster_low;
        entry->type = (de.attributes & FAT12_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
        entry->size = de.file_size;

        dir->position++;
        return true;
    }

    return false;
}

/**
 * Rewind directory to beginning
 */
static void fat12_vfs_rewinddir(vfs_dir_t *dir) {
    vfs_mount_t *mnt = dir->mount;
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    fat12_dir_state_t *dstate = (fat12_dir_state_t *)dir->fs_data;

    if (!state || !dstate) return;

    /* Reopen directory */
    if (dir->node.cluster == 0) {
        fat12_open_root(&state->fs, &dstate->dir);
    } else {
        fat12_open_dir(&state->fs, &dstate->dir, dir->node.cluster);
    }

    dir->position = 0;
}

/**
 * Get filesystem info
 */
static bool fat12_vfs_statfs(vfs_mount_t *mnt, uint32_t *total, uint32_t *free) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    uint32_t cluster_size = state->fs.bpb.sectors_per_cluster *
                            state->fs.bpb.bytes_per_sector;

    *total = state->fs.total_clusters * cluster_size;
    *free = fat12_free_space(&state->fs);

    return true;
}

/**
 * Get volume label
 */
static bool fat12_vfs_label(vfs_mount_t *mnt, char *buf, size_t len) {
    fat12_vfs_state_t *state = (fat12_vfs_state_t *)mnt->fs_data;
    if (!state) return false;

    char label[12];
    fat12_get_label(&state->fs, label);

    int i;
    for (i = 0; i < 11 && i < (int)len - 1 && label[i]; i++) {
        buf[i] = label[i];
    }
    buf[i] = '\0';

    return true;
}

/* ============================================================================
 * FAT12 VFS Operations Table
 * ============================================================================ */

static vfs_ops_t g_fat12_vfs_ops = {
    .name      = "fat12",

    /* Mount/unmount */
    .mount     = fat12_vfs_mount,
    .unmount   = fat12_vfs_unmount,
    .sync      = fat12_vfs_sync,

    /* Node operations */
    .lookup    = fat12_vfs_lookup,
    .stat      = fat12_vfs_stat,
    .create    = fat12_vfs_create,
    .unlink    = fat12_vfs_unlink,
    .rename    = fat12_vfs_rename,
    .mkdir     = fat12_vfs_mkdir,
    .rmdir     = fat12_vfs_rmdir,

    /* File operations */
    .open      = fat12_vfs_open,
    .close     = fat12_vfs_close,
    .read      = fat12_vfs_read,
    .write     = fat12_vfs_write,
    .seek      = fat12_vfs_seek,
    .truncate  = NULL,  /* TODO */

    /* Directory operations */
    .opendir   = fat12_vfs_opendir,
    .closedir  = fat12_vfs_closedir,
    .readdir   = fat12_vfs_readdir,
    .rewinddir = fat12_vfs_rewinddir,

    /* Filesystem info */
    .statfs    = fat12_vfs_statfs,
    .label     = fat12_vfs_label,
};

/**
 * Get FAT12 VFS operations table
 */
static inline vfs_ops_t *fat12_get_vfs_ops(void) {
    return &g_fat12_vfs_ops;
}

/**
 * Get FAT12 VFS state (for direct access if needed)
 */
static inline fat12_vfs_state_t *fat12_get_vfs_state(void) {
    return &g_fat12_state;
}

#endif /* FAT12_VFS_H */