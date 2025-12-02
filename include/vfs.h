/**
 * vfs.h - Virtual File System Layer
 *
 * Provides a unified interface for different filesystems and block devices.
 * Supports mounting multiple filesystems and accessing them through paths.
 *
 * Architecture:
 *   VFS Layer        - Path resolution, mount points, file descriptors
 *     |
 *   Filesystem Ops   - FAT12, ext2, etc. (read/write/create/delete)
 *     |
 *   Block Device     - ATA, Floppy, Ramdisk (sector read/write)
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define VFS_MAX_MOUNTS      8       /* Maximum mount points */
#define VFS_MAX_PATH        256     /* Maximum path length */
#define VFS_MAX_NAME        64      /* Maximum filename length */
#define VFS_MAX_OPEN_FILES  32      /* Maximum open file handles */

/* ============================================================================
 * Block Device Interface
 *
 * All storage devices implement this interface for sector-level access.
 * ============================================================================ */

typedef enum {
    BLKDEV_NONE = 0,
    BLKDEV_ATA,         /* ATA/IDE hard drive */
    BLKDEV_FLOPPY,      /* Floppy disk */
    BLKDEV_RAMDISK,     /* RAM disk */
    BLKDEV_CDROM,       /* CD-ROM (ATAPI) */
    BLKDEV_USB,         /* USB mass storage (future) */
} blkdev_type_t;

typedef struct blkdev {
    /* Device identification */
    blkdev_type_t type;
    char name[32];          /* Device name (e.g., "hda", "fd0") */
    char model[48];         /* Model/description */

    /* Device geometry */
    uint32_t sector_size;   /* Bytes per sector (usually 512) */
    uint32_t total_sectors; /* Total sector count */
    uint32_t cylinders;     /* CHS geometry (optional) */
    uint16_t heads;
    uint16_t sectors_per_track;

    /* Device state */
    bool     present;       /* Device is present */
    bool     removable;     /* Device is removable */
    bool     readonly;      /* Write-protected */

    /* Block operations */
    uint32_t (*read)(struct blkdev *dev, uint32_t lba, uint8_t count, void *buf);
    uint32_t (*write)(struct blkdev *dev, uint32_t lba, uint8_t count, const void *buf);
    bool     (*sync)(struct blkdev *dev);       /* Flush cache */
    bool     (*eject)(struct blkdev *dev);      /* Eject media (removable) */

    /* Private driver data */
    void    *driver_data;
} blkdev_t;

/* ============================================================================
 * Filesystem Interface
 *
 * Each filesystem type implements these operations.
 * ============================================================================ */

/* Forward declarations */
struct vfs_node;
struct vfs_mount;
struct vfs_file;
struct vfs_dir;

/* File types */
typedef enum {
    VFS_FILE      = 0x01,
    VFS_DIRECTORY = 0x02,
    VFS_SYMLINK   = 0x04,
    VFS_DEVICE    = 0x08,
} vfs_node_type_t;

/* File mode flags */
#define VFS_O_RDONLY    0x0001
#define VFS_O_WRONLY    0x0002
#define VFS_O_RDWR      0x0003
#define VFS_O_CREAT     0x0100
#define VFS_O_TRUNC     0x0200
#define VFS_O_APPEND    0x0400

/* Seek modes */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* Directory entry returned by readdir */
typedef struct vfs_dirent {
    char     name[VFS_MAX_NAME];
    uint32_t inode;         /* Inode number (fs-specific) */
    uint8_t  type;          /* VFS_FILE, VFS_DIRECTORY, etc. */
    uint32_t size;          /* File size in bytes */
} vfs_dirent_t;

/* File stat information */
typedef struct vfs_stat {
    uint32_t size;          /* File size */
    uint8_t  type;          /* Node type */
    uint16_t permissions;   /* rwx bits (needs 9+ bits for full Unix perms) */
    uint16_t uid;           /* Owner user ID */
    uint16_t gid;           /* Owner group ID */
    uint32_t atime;         /* Access time */
    uint32_t mtime;         /* Modify time */
    uint32_t ctime;         /* Create time */
    uint32_t blocks;        /* Blocks allocated */
    uint32_t block_size;    /* Block size */
} vfs_stat_t;

/* Filesystem operations */
typedef struct vfs_ops {
    /* Filesystem identification */
    const char *name;       /* "fat12", "ext2", etc. */

    /* Mount/unmount */
    bool (*mount)(struct vfs_mount *mnt, blkdev_t *dev, const char *opts);
    void (*unmount)(struct vfs_mount *mnt);
    bool (*sync)(struct vfs_mount *mnt);

    /* Node operations */
    bool (*lookup)(struct vfs_mount *mnt, const char *path, struct vfs_node *node);
    bool (*stat)(struct vfs_node *node, vfs_stat_t *stat);
    bool (*create)(struct vfs_mount *mnt, const char *path, uint8_t type);
    bool (*unlink)(struct vfs_mount *mnt, const char *path);
    bool (*rename)(struct vfs_mount *mnt, const char *old_path, const char *new_path);
    bool (*mkdir)(struct vfs_mount *mnt, const char *path);
    bool (*rmdir)(struct vfs_mount *mnt, const char *path);

    /* File operations */
    bool     (*open)(struct vfs_file *file, uint32_t flags);
    void     (*close)(struct vfs_file *file);
    int32_t  (*read)(struct vfs_file *file, void *buf, uint32_t size);
    int32_t  (*write)(struct vfs_file *file, const void *buf, uint32_t size);
    int32_t  (*seek)(struct vfs_file *file, int32_t offset, int whence);
    bool     (*truncate)(struct vfs_file *file, uint32_t size);

    /* Directory operations */
    bool (*opendir)(struct vfs_dir *dir);
    void (*closedir)(struct vfs_dir *dir);
    bool (*readdir)(struct vfs_dir *dir, vfs_dirent_t *entry);
    void (*rewinddir)(struct vfs_dir *dir);

    /* Filesystem info */
    bool (*statfs)(struct vfs_mount *mnt, uint32_t *total, uint32_t *free);
    bool (*label)(struct vfs_mount *mnt, char *buf, size_t len);
} vfs_ops_t;

/* ============================================================================
 * VFS Structures
 * ============================================================================ */

/* VFS node (inode representation) */
typedef struct vfs_node {
    char          name[VFS_MAX_NAME];
    uint8_t       type;           /* VFS_FILE, VFS_DIRECTORY, etc. */
    uint32_t      size;
    uint32_t      inode;          /* Filesystem-specific identifier */
    uint16_t      cluster;        /* For FAT filesystems */
    struct vfs_mount *mount;      /* Mount point this node belongs to */
    void         *fs_data;        /* Filesystem-specific data */
} vfs_node_t;

/* Mount point */
typedef struct vfs_mount {
    char          path[VFS_MAX_PATH];   /* Mount point path */
    blkdev_t     *device;               /* Underlying block device */
    vfs_ops_t    *ops;                  /* Filesystem operations */
    bool          mounted;
    bool          readonly;
    void         *fs_data;              /* Filesystem-specific state */
    uint8_t      *sector_buf;           /* Sector buffer for fs use */
    uint8_t      *cache;                /* Cache buffer (e.g., FAT cache) */
} vfs_mount_t;

/* Open file handle */
typedef struct vfs_file {
    vfs_node_t    node;           /* Associated node */
    vfs_mount_t  *mount;          /* Mount point */
    uint32_t      flags;          /* Open flags */
    uint32_t      position;       /* Current position */
    bool          open;
    bool          dirty;          /* Has unsaved changes */
    void         *fs_data;        /* Filesystem-specific file state */

    /* For FAT: track directory entry location for updates */
    uint32_t      dir_sector;
    uint32_t      dir_offset;
} vfs_file_t;

/* Open directory handle */
typedef struct vfs_dir {
    vfs_node_t    node;           /* Associated node */
    vfs_mount_t  *mount;          /* Mount point */
    uint32_t      position;       /* Current entry index */
    bool          open;
    void         *fs_data;        /* Filesystem-specific dir state */
} vfs_dir_t;

/* ============================================================================
 * VFS State
 * ============================================================================ */

typedef struct vfs_state {
    vfs_mount_t   mounts[VFS_MAX_MOUNTS];
    int           mount_count;
    vfs_mount_t  *root_mount;     /* Root filesystem */

    /* Open file table */
    vfs_file_t    files[VFS_MAX_OPEN_FILES];
    vfs_dir_t     dirs[VFS_MAX_OPEN_FILES];
} vfs_state_t;

/* Global VFS state */
static vfs_state_t g_vfs = {0};

/* ============================================================================
 * VFS String Utilities
 * ============================================================================ */

static inline int vfs_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static inline void vfs_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static inline void vfs_strncpy(char *dst, const char *src, int n) {
    while (n-- && (*dst++ = *src++));
    if (n < 0) *(dst-1) = '\0';
}

static inline int vfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int vfs_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? ((unsigned char)*a - (unsigned char)*b) : 0;
}

/* ============================================================================
 * Path Utilities
 * ============================================================================ */

/**
 * Normalize a path (remove ., .., duplicate slashes)
 */
static void vfs_normalize_path(const char *path, char *normalized, const char *cwd) {
    char buf[VFS_MAX_PATH];
    int len = 0;

    /* Handle relative paths */
    if (path[0] != '/') {
        /* Start with cwd */
        vfs_strcpy(buf, cwd);
        len = vfs_strlen(buf);
        if (len > 0 && buf[len-1] != '/') {
            buf[len++] = '/';
        }
    }

    /* Append path */
    while (*path && len < VFS_MAX_PATH - 1) {
        if (*path == '/') {
            /* Skip duplicate slashes */
            if (len == 0 || buf[len-1] != '/') {
                buf[len++] = '/';
            }
            path++;
        } else if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
            /* Skip . */
            path++;
            if (*path == '/') path++;
        } else if (path[0] == '.' && path[1] == '.' &&
                   (path[2] == '/' || path[2] == '\0')) {
            /* Handle .. */
            path += 2;
            if (*path == '/') path++;

            /* Go up one level */
            if (len > 1) {
                len--;  /* Remove trailing slash */
                while (len > 0 && buf[len-1] != '/') len--;
            }
        } else {
            /* Copy path component */
            while (*path && *path != '/' && len < VFS_MAX_PATH - 1) {
                buf[len++] = *path++;
            }
        }
    }

    buf[len] = '\0';

    /* Ensure root path */
    if (len == 0) {
        buf[0] = '/';
        buf[1] = '\0';
    }

    vfs_strcpy(normalized, buf);
}

/**
 * Find mount point for a path
 */
static vfs_mount_t *vfs_find_mount(const char *path) {
    vfs_mount_t *best = NULL;
    int best_len = 0;

    for (int i = 0; i < g_vfs.mount_count; i++) {
        vfs_mount_t *m = &g_vfs.mounts[i];
        if (!m->mounted) continue;

        int mlen = vfs_strlen(m->path);
        if (vfs_strncmp(path, m->path, mlen) == 0) {
            /* Path starts with mount point */
            if (mlen > best_len) {
                best = m;
                best_len = mlen;
            }
        }
    }

    return best ? best : g_vfs.root_mount;
}

/**
 * Get path relative to mount point
 */
static const char *vfs_relative_path(vfs_mount_t *mnt, const char *path) {
    int mlen = vfs_strlen(mnt->path);
    if (vfs_strncmp(path, mnt->path, mlen) == 0) {
        path += mlen;
        while (*path == '/') path++;
    }
    return path;
}

/* ============================================================================
 * VFS API
 * ============================================================================ */

/**
 * Initialize VFS
 */
static void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_vfs.mounts[i].mounted = false;
    }
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        g_vfs.files[i].open = false;
        g_vfs.dirs[i].open = false;
    }
    g_vfs.mount_count = 0;
    g_vfs.root_mount = NULL;
}

/**
 * Register and mount a filesystem
 */
static vfs_mount_t *vfs_mount(const char *path, blkdev_t *dev, vfs_ops_t *ops,
                               uint8_t *sector_buf, uint8_t *cache) {
    /* Find free mount point */
    vfs_mount_t *mnt = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_vfs.mounts[i].mounted) {
            mnt = &g_vfs.mounts[i];
            break;
        }
    }

    if (!mnt) return NULL;

    /* Initialize mount point */
    vfs_strcpy(mnt->path, path);
    mnt->device = dev;
    mnt->ops = ops;
    mnt->sector_buf = sector_buf;
    mnt->cache = cache;
    mnt->readonly = dev ? dev->readonly : false;

    /* Call filesystem mount */
    if (ops->mount && !ops->mount(mnt, dev, NULL)) {
        return NULL;
    }

    mnt->mounted = true;
    g_vfs.mount_count++;

    /* Set as root if first mount at "/" */
    if (vfs_strcmp(path, "/") == 0) {
        g_vfs.root_mount = mnt;
    }

    return mnt;
}

/**
 * Unmount a filesystem
 */
static bool vfs_unmount(const char *path) {
    vfs_mount_t *mnt = vfs_find_mount(path);
    if (!mnt || !mnt->mounted) return false;

    /* Close any open files on this mount */
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_vfs.files[i].open && g_vfs.files[i].mount == mnt) {
            if (mnt->ops->close) {
                mnt->ops->close(&g_vfs.files[i]);
            }
            g_vfs.files[i].open = false;
        }
    }

    /* Call filesystem unmount */
    if (mnt->ops->unmount) {
        mnt->ops->unmount(mnt);
    }

    mnt->mounted = false;
    g_vfs.mount_count--;

    if (g_vfs.root_mount == mnt) {
        g_vfs.root_mount = NULL;
    }

    return true;
}

/**
 * Open a file
 */
static vfs_file_t *vfs_open(const char *path, uint32_t flags) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted) return NULL;

    /* Find free file slot */
    vfs_file_t *file = NULL;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_vfs.files[i].open) {
            file = &g_vfs.files[i];
            break;
        }
    }
    if (!file) return NULL;

    /* Get relative path */
    const char *rel = vfs_relative_path(mnt, normalized);

    /* Lookup node */
    if (!mnt->ops->lookup || !mnt->ops->lookup(mnt, rel, &file->node)) {
        /* File doesn't exist - create if requested */
        if (flags & VFS_O_CREAT) {
            if (!mnt->ops->create || !mnt->ops->create(mnt, rel, VFS_FILE)) {
                return NULL;
            }
            /* Try lookup again */
            if (!mnt->ops->lookup(mnt, rel, &file->node)) {
                return NULL;
            }
        } else {
            return NULL;
        }
    }

    file->mount = mnt;
    file->flags = flags;
    file->position = 0;
    file->dirty = false;

    /* Call filesystem open */
    if (mnt->ops->open && !mnt->ops->open(file, flags)) {
        return NULL;
    }

    file->open = true;
    return file;
}

/**
 * Close a file
 */
static void vfs_close(vfs_file_t *file) {
    if (!file || !file->open) return;

    if (file->mount && file->mount->ops->close) {
        file->mount->ops->close(file);
    }

    file->open = false;
}

/**
 * Read from file
 */
static int32_t vfs_read(vfs_file_t *file, void *buf, uint32_t size) {
    if (!file || !file->open) return -1;
    if (!file->mount || !file->mount->ops->read) return -1;

    return file->mount->ops->read(file, buf, size);
}

/**
 * Write to file
 */
static int32_t vfs_write(vfs_file_t *file, const void *buf, uint32_t size) {
    if (!file || !file->open) return -1;
    if (!file->mount || !file->mount->ops->write) return -1;
    if (file->mount->readonly) return -1;

    return file->mount->ops->write(file, buf, size);
}

/**
 * Seek in file
 */
static int32_t vfs_seek(vfs_file_t *file, int32_t offset, int whence) {
    if (!file || !file->open) return -1;

    if (file->mount && file->mount->ops->seek) {
        return file->mount->ops->seek(file, offset, whence);
    }

    /* Default seek implementation */
    int32_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset; break;
        case VFS_SEEK_CUR: new_pos = file->position + offset; break;
        case VFS_SEEK_END: new_pos = file->node.size + offset; break;
        default: return -1;
    }

    if (new_pos < 0) return -1;
    file->position = new_pos;
    return new_pos;
}

/**
 * Open directory
 */
static vfs_dir_t *vfs_opendir(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted) return NULL;

    /* Find free dir slot */
    vfs_dir_t *dir = NULL;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_vfs.dirs[i].open) {
            dir = &g_vfs.dirs[i];
            break;
        }
    }
    if (!dir) return NULL;

    const char *rel = vfs_relative_path(mnt, normalized);

    /* Lookup directory node */
    if (mnt->ops->lookup) {
        if (!mnt->ops->lookup(mnt, rel, &dir->node)) {
            return NULL;
        }
    }

    dir->mount = mnt;
    dir->position = 0;

    if (mnt->ops->opendir && !mnt->ops->opendir(dir)) {
        return NULL;
    }

    dir->open = true;
    return dir;
}

/**
 * Close directory
 */
static void vfs_closedir(vfs_dir_t *dir) {
    if (!dir || !dir->open) return;

    if (dir->mount && dir->mount->ops->closedir) {
        dir->mount->ops->closedir(dir);
    }

    dir->open = false;
}

/**
 * Read directory entry
 */
static bool vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *entry) {
    if (!dir || !dir->open) return false;
    if (!dir->mount || !dir->mount->ops->readdir) return false;

    return dir->mount->ops->readdir(dir, entry);
}

/**
 * Create a file
 */
static bool vfs_create(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted || mnt->readonly) return false;
    if (!mnt->ops->create) return false;

    const char *rel = vfs_relative_path(mnt, normalized);
    return mnt->ops->create(mnt, rel, VFS_FILE);
}

/**
 * Create a directory
 */
static bool vfs_mkdir(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted || mnt->readonly) return false;
    if (!mnt->ops->mkdir) return false;

    const char *rel = vfs_relative_path(mnt, normalized);
    return mnt->ops->mkdir(mnt, rel);
}

/**
 * Delete a file
 */
static bool vfs_unlink(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted || mnt->readonly) return false;
    if (!mnt->ops->unlink) return false;

    const char *rel = vfs_relative_path(mnt, normalized);
    return mnt->ops->unlink(mnt, rel);
}

/**
 * Delete a directory
 */
static bool vfs_rmdir(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted || mnt->readonly) return false;
    if (!mnt->ops->rmdir) return false;

    const char *rel = vfs_relative_path(mnt, normalized);
    return mnt->ops->rmdir(mnt, rel);
}

/**
 * Rename/move a file
 */
static bool vfs_rename(const char *old_path, const char *new_path) {
    char old_norm[VFS_MAX_PATH], new_norm[VFS_MAX_PATH];
    vfs_normalize_path(old_path, old_norm, "/");
    vfs_normalize_path(new_path, new_norm, "/");

    vfs_mount_t *mnt = vfs_find_mount(old_norm);
    if (!mnt || !mnt->mounted || mnt->readonly) return false;
    if (!mnt->ops->rename) return false;

    /* For now, both paths must be on same mount */
    vfs_mount_t *mnt2 = vfs_find_mount(new_norm);
    if (mnt != mnt2) return false;

    const char *old_rel = vfs_relative_path(mnt, old_norm);
    const char *new_rel = vfs_relative_path(mnt, new_norm);

    return mnt->ops->rename(mnt, old_rel, new_rel);
}

/**
 * Get file information
 */
static bool vfs_stat(const char *path, vfs_stat_t *stat) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(path, normalized, "/");

    vfs_mount_t *mnt = vfs_find_mount(normalized);
    if (!mnt || !mnt->mounted) return false;

    vfs_node_t node;
    const char *rel = vfs_relative_path(mnt, normalized);

    if (!mnt->ops->lookup || !mnt->ops->lookup(mnt, rel, &node)) {
        return false;
    }

    if (mnt->ops->stat) {
        return mnt->ops->stat(&node, stat);
    }

    /* Fill basic info from node */
    stat->size = node.size;
    stat->type = node.type;
    stat->permissions = 0;
    stat->uid = 0;
    stat->gid = 0;
    stat->atime = 0;
    stat->mtime = 0;
    stat->ctime = 0;
    stat->blocks = (node.size + 511) / 512;
    stat->block_size = 512;

    return true;
}

/**
 * Sync all filesystems
 */
static void vfs_sync_all(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t *mnt = &g_vfs.mounts[i];
        if (mnt->mounted && mnt->ops->sync) {
            mnt->ops->sync(mnt);
        }
    }
}

#endif /* VFS_H */