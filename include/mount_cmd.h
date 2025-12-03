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
 * LAZY INITIALIZATION: Floppy hardware is not touched until
 * user actually tries to mount or access it.
 *
 * Integration:
 *   1. Include floppy_blkdev.h and this file
 *   2. Call mount_cmd_init() during kernel init (safe before interrupts)
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
static bool g_floppy_struct_ready = false;

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
 *
 * SAFE TO CALL BEFORE INTERRUPTS ARE ENABLED!
 * This only sets up data structures, does not touch hardware.
 * Hardware init happens lazily on first actual floppy access.
 */
static void mount_cmd_init(void) {
    /* Set up floppy block device structure (no hardware access) */
    g_floppy_struct_ready = floppy_blkdev_init(&g_floppy_dev);
}

/**
 * Check if floppy structure is set up
 * (Does NOT indicate hardware availability)
 */
static bool mount_floppy_available(void) {
    return g_floppy_struct_ready;
}

/**
 * Get floppy block device
 */
static blkdev_t *mount_get_floppy(void) {
    return g_floppy_struct_ready ? &g_floppy_dev : NULL;
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

        /* Show floppy status */
        if (g_floppy_struct_ready) {
            if (floppy_blkdev_init_attempted()) {
                /* We've tried to use it before */
                sh->io->printf("  fd0      %s\n",
                    floppy_blkdev_hw_ok() ? "1.44MB Floppy (ready)" : "Floppy (not responding)");
            } else {
                /* Haven't tried yet */
                sh->io->printf("  fd0      1.44MB Floppy (not probed)\n");
            }
        } else {
            sh->io->printf("  fd0      (not configured)\n");
        }

        sh->io->printf("\n");
        sh->last_result = 0;
        return;
    }

    /* Need device and mount point */
    if (argc < 3) {
        sh->io->printf("Usage: mount <device> <path>\n");
        sh->io->printf("  mount fd0 /fd0   - Mount floppy at /fd0\n");
        sh->last_result = 1;
        return;
    }

    const char *device = argv[1];
    const char *path = argv[2];

    /* Find a free mount point slot */
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

    /* Check for floppy */
    if (sh_strcmp(device, "fd0") == 0) {
        if (!g_floppy_struct_ready) {
            sh->io->printf("Floppy drive not configured.\n");
            sh->last_result = 1;
            return;
        }

        sh->io->printf("Checking floppy drive...\n");

        /* This triggers lazy hardware init */
        if (!floppy_blkdev_check_media(&g_floppy_dev)) {
            sh->io->printf("No disk in drive or drive not responding.\n");
            sh->last_result = 1;
            return;
        }

        sh->io->printf("Mounting %s at %s...\n", device, path);

        vfs_mount_t *mnt = vfs_mount(path, &g_floppy_dev, fat12_get_vfs_ops(), NULL, NULL);
        if (mnt) {
            /* Record mount */
            sh_strcpy(g_mount_points[slot].path, path);
            g_mount_points[slot].device = &g_floppy_dev;
            g_mount_points[slot].in_use = true;

            sh->io->printf("Mounted successfully.\n");
            sh->last_result = 0;
        } else {
            sh->io->printf("Mount failed. Is the disk formatted?\n");
            sh->last_result = 1;
        }
        return;
    }

    sh->io->printf("Unknown device: %s\n", device);
    sh->io->printf("Available: fd0\n");
    sh->last_result = 1;
}

/**
 * umount - Unmount a filesystem
 */
static void kcmd_umount(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        sh->io->printf("Usage: umount <path>\n");
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

    /* TODO: Actually unmount via VFS */
    sh->io->printf("Unmounting %s...\n", path);

    /* Clear mount record */
    g_mount_points[slot].in_use = false;
    g_mount_points[slot].path[0] = '\0';
    g_mount_points[slot].device = NULL;

    sh->io->printf("Unmounted.\n");
    sh->last_result = 0;
}

/**
 * eject - Eject removable media (turn off floppy motor)
 */
static void kcmd_eject(shell_state_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;

    if (!g_floppy_struct_ready) {
        sh->io->printf("No floppy drive configured.\n");
        sh->last_result = 1;
        return;
    }

    /* Check if floppy is mounted anywhere */
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].in_use && g_mount_points[i].device == &g_floppy_dev) {
            sh->io->printf("Warning: Floppy is mounted at %s\n", g_mount_points[i].path);
            sh->io->printf("Unmount first with: umount %s\n", g_mount_points[i].path);
            sh->last_result = 1;
            return;
        }
    }

    floppy_blkdev_eject(&g_floppy_dev);
    sh->io->printf("Floppy motor stopped. Safe to remove disk.\n");
    sh->last_result = 0;
}

/* ============================================================================
 * Register Commands
 * ============================================================================ */

/**
 * Register mount-related commands with shell
 */
static void mount_cmd_register(shell_state_t *sh) {
    shell_register(sh, "mount",  "Mount filesystem",      kcmd_mount,  0);
    shell_register(sh, "umount", "Unmount filesystem",    kcmd_umount, 1);
    shell_register(sh, "eject",  "Eject floppy disk",     kcmd_eject,  0);
}

#endif /* MOUNT_CMD_H */