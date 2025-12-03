/**
 * mount_cmd.h - Mount/Unmount Shell Commands
 *
 * Provides shell commands for mounting and unmounting filesystems.
 *
 * Commands:
 *   mount              - List mounted filesystems
 *   mount fd0 /fd0     - Mount floppy at /fd0
 *   mount hda /mnt     - Mount hard drive at /mnt
 *   umount /fd0        - Unmount filesystem
 *   eject              - Eject floppy (turn off motor)
 *
 * Integration:
 *   1. Include floppy_blkdev.h and this file
 *   2. Call mount_cmd_init() during kernel init (after floppy_init)
 *   3. Call mount_cmd_register() after shell_init()
 */

#ifndef MOUNT_CMD_H
#define MOUNT_CMD_H

#include "shell.h"
#include "vfs.h"
#include "fat12_vfs.h"
#include "floppy_blkdev.h"

/* ============================================================================
 * Global Device State
 * ============================================================================ */

/* Floppy block device */
static blkdev_t g_floppy_dev = {0};
static bool g_floppy_available = false;

/* Track what's mounted where */
#define MAX_MOUNT_POINTS 4

typedef struct {
    char path[32];
    blkdev_t *device;
    bool in_use;
} mount_point_t;

static mount_point_t g_mount_points[MAX_MOUNT_POINTS] = {0};

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize mount subsystem
 * Call this during kernel init, after interrupts are set up
 */
static void mount_cmd_init(void) {
    /* Try to initialize floppy */
    g_floppy_available = floppy_blkdev_init(&g_floppy_dev);
}

/**
 * Check if floppy is available
 */
static bool mount_floppy_available(void) {
    return g_floppy_available;
}

/**
 * Get floppy block device
 */
static blkdev_t *mount_get_floppy(void) {
    return g_floppy_available ? &g_floppy_dev : NULL;
}

/* ============================================================================
 * Shell Commands
 * ============================================================================ */

/**
 * mount - List mounts or mount a device
 *
 * Usage:
 *   mount              - List all mounts
 *   mount fd0 /fd0     - Mount floppy drive at /fd0
 */
static void kcmd_mount(shell_state_t *sh, int argc, char **argv) {
    /* No args - list mounts */
    if (argc == 1) {
        sh->io->printf("\nMounted filesystems:\n");
        sh->io->printf("--------------------\n");
        sh->io->printf("  /        (root)\n");
        
        /* Show additional mounts */
        for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
            if (g_mount_points[i].in_use) {
                sh->io->printf("  %-8s %s\n",
                    g_mount_points[i].path,
                    g_mount_points[i].device->name);
            }
        }
        
        sh->io->printf("\nAvailable devices:\n");
        sh->io->printf("  fd0      %s\n", g_floppy_available ? "Floppy drive" : "(not available)");
        sh->io->printf("\n");
        sh->last_result = 0;
        return;
    }

    /* Need device and mount point */
    if (argc < 3) {
        sh->io->printf("Usage: mount <device> <path>\n");
        sh->io->printf("       mount fd0 /fd0\n");
        sh->io->printf("       mount          (list mounts)\n");
        sh->last_result = 1;
        return;
    }

    const char *device = argv[1];
    const char *path = argv[2];

    /* Find device */
    blkdev_t *dev = NULL;
    
    if (sh_strcmp(device, "fd0") == 0 || sh_strcmp(device, "floppy") == 0) {
        if (!g_floppy_available) {
            sh->io->printf("Floppy drive not available.\n");
            sh->last_result = 1;
            return;
        }
        dev = &g_floppy_dev;
    } else {
        sh->io->printf("Unknown device: %s\n", device);
        sh->io->printf("Available: fd0\n");
        sh->last_result = 1;
        return;
    }

    /* Check if disk is present */
    sh->io->printf("Checking for disk in %s...\n", device);
    if (!floppy_blkdev_check_media(dev)) {
        sh->io->printf("No disk in drive or read error.\n");
        sh->io->printf("Insert a disk and try again.\n");
        sh->last_result = 1;
        return;
    }

    /* Find free mount point slot */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!g_mount_points[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        sh->io->printf("Too many mounted filesystems.\n");
        sh->last_result = 1;
        return;
    }

    /* Mount the filesystem */
    sh->io->printf("Mounting %s at %s...\n", device, path);
    
    vfs_mount_t *mnt = vfs_mount(path, dev, fat12_get_vfs_ops(), NULL, NULL);
    if (!mnt) {
        sh->io->printf("Failed to mount filesystem.\n");
        sh->io->printf("Is this a FAT12 formatted disk?\n");
        sh->last_result = 1;
        return;
    }

    /* Record mount point */
    sh_strcpy(g_mount_points[slot].path, path);
    g_mount_points[slot].device = dev;
    g_mount_points[slot].in_use = true;

    /* Show volume label if available */
    char label[12] = {0};
    if (mnt->ops->label && mnt->ops->label(mnt, label, sizeof(label)) && label[0]) {
        sh->io->printf("Mounted: %s (Volume: %s)\n", path, label);
    } else {
        sh->io->printf("Mounted: %s\n", path);
    }

    sh->last_result = 0;
}

/**
 * umount - Unmount a filesystem
 */
static void kcmd_umount(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: umount <path>\n");
        sh->io->printf("       umount /fd0\n");
        sh->last_result = 1;
        return;
    }

    const char *path = argv[1];

    /* Can't unmount root */
    if (sh_strcmp(path, "/") == 0) {
        sh->io->printf("Cannot unmount root filesystem.\n");
        sh->last_result = 1;
        return;
    }

    /* Find mount point */
    int slot = -1;
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].in_use && sh_strcmp(g_mount_points[i].path, path) == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        sh->io->printf("Not mounted: %s\n", path);
        sh->last_result = 1;
        return;
    }

    /* TODO: Actually unmount from VFS */
    /* For now just clear our tracking */
    g_mount_points[slot].in_use = false;

    sh->io->printf("Unmounted: %s\n", path);
    sh->last_result = 0;
}

/**
 * eject - Eject removable media (floppy)
 */
static void kcmd_eject(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;

    if (!g_floppy_available) {
        sh->io->printf("No floppy drive available.\n");
        sh->last_result = 1;
        return;
    }

    /* Turn off floppy motor */
    fdc_motor_off();
    sh->io->printf("Floppy motor stopped. Safe to remove disk.\n");
    sh->last_result = 0;
}

/* ============================================================================
 * Register Commands
 * ============================================================================ */

static inline void mount_cmd_register(shell_state_t *sh) {
    shell_register(sh, "mount",  "Mount filesystem",     kcmd_mount,  0);
    shell_register(sh, "umount", "Unmount filesystem",   kcmd_umount, 1);
    shell_register(sh, "eject",  "Eject floppy disk",    kcmd_eject,  0);
}

#endif /* MOUNT_CMD_H */
