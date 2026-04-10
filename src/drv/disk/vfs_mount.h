/**
 * 
 * @file : /src/drv/disk/vfs_mount.h
 * @brief : VFS mount point management - register, unregister, and lookup filesystems.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#ifndef VFS_MOUNT_H
#define VFS_MOUNT_H

#include <stddef.h>
#include "vfs_fs.h"

typedef struct vfs_mount {
    char             path[256];
    fs_ops_t        *ops;
    void            *fs_data;
    struct vfs_mount *next;
} vfs_mount_t;

void          vfs_mount_init  (void);
int           vfs_mount       (const char *path, fs_ops_t *ops, void *fs_data);
int           vfs_umount      (const char *path);
vfs_mount_t  *vfs_mount_find  (const char *path, const char **rel_out);
int           vfs_mount_count (void);
vfs_mount_t  *vfs_mount_get   (int index);

#endif
