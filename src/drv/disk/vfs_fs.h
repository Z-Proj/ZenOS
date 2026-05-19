/**
 * 
 * @file : /src/drv/disk/vfs_fs.h
 * @brief : VFS filesystem operations interface - open, readdir, mkdir, unlink, etc.
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

#ifndef VFS_FS_H
#define VFS_FS_H

#include <stdint.h>
#include <stddef.h>
#include "../../libk/core/fd.h"

typedef struct vfs_statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_fsid;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
} vfs_statfs_t;

typedef struct fs_ops {
    int      (*open)    (void *fs_data, const char *path, int write, fd_entry_t *out);
    int      (*opendir) (void *fs_data, const char *path, fd_entry_t *out);
    int      (*readdir) (void *fs_data, const char *path, char *buf, size_t bufsz);
    int      (*mkdir)   (void *fs_data, const char *path);
    int      (*rmdir)   (void *fs_data, const char *path);
    int      (*unlink)  (void *fs_data, const char *path);
    int      (*rename)  (void *fs_data, const char *old_path, const char *new_path);
    int      (*stat)    (void *fs_data, const char *path);
    int      (*statfs)  (void *fs_data, const char *path, vfs_statfs_t *out);
    int      (*create)  (void *fs_data, const char *path);
} fs_ops_t;

#endif
