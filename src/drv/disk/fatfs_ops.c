/**
 * 
 * @file : /src/drv/disk/fatfs_ops.c
 * @brief : VFS glue layer mapping FAT filesystem operations to VFS.
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

#include "fatfs_ops.h"
#include "fat.h"
#include "../../libk/string.h"
#include "../../libk/core/mem.h"

static int fatfs_open(void *fs_data, const char *path, int write, fd_entry_t *out)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_open_entry_vol(path, write, out, d->vol);
}

static int fatfs_opendir(void *fs_data, const char *path, fd_entry_t *out)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_opendir_entry_vol(path, out, d->vol);
}

static int fatfs_readdir(void *fs_data, const char *path, char *buf, size_t bufsz)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    if (fat_chdir_vol(path, d->vol) != 0) return -1;
    return fat_list_vol(buf, bufsz, d->vol);
}

static int fatfs_mkdir(void *fs_data, const char *path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_mkdir_vol(path, d->vol);
}

static int fatfs_rmdir(void *fs_data, const char *path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_rmdir_vol(path, d->vol);
}

static int fatfs_unlink(void *fs_data, const char *path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_delete_vol(path, d->vol);
}

static int fatfs_rename(void *fs_data, const char *old_path, const char *new_path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_rename_vol(old_path, new_path, d->vol);
}

static int fatfs_stat(void *fs_data, const char *path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    fd_entry_t tmp;
    if (fat_open_entry_vol(path, 0, &tmp, d->vol) < 0) {
        if (fat_opendir_entry_vol(path, &tmp, d->vol) < 0) return -1;
        fat_closedir_entry(&tmp);
        return 0;
    }
    fat_close_entry(&tmp);
    return 0;
}

static int fatfs_statfs(void *fs_data, const char *path, vfs_statfs_t *out)
{
    (void)path;
    if (!fs_data || !out)
        return -1;

    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    uint64_t bsize = 0;
    uint64_t blocks = 0;
    uint64_t free_blocks = 0;
    if (fat_statfs_vol(d->vol, &bsize, &blocks, &free_blocks) < 0)
        return -1;

    memset(out, 0, sizeof(*out));
    out->f_type = 0x4D44;
    out->f_bsize = bsize;
    out->f_frsize = bsize;
    out->f_blocks = blocks;
    out->f_bfree = free_blocks;
    out->f_bavail = free_blocks;
    out->f_files = 0;
    out->f_ffree = 0;
    out->f_fsid = (uint64_t)d->vol;
    out->f_namelen = 255;
    return 0;
}

static int fatfs_create(void *fs_data, const char *path)
{
    fatfs_data_t *d = (fatfs_data_t *)fs_data;
    return fat_create_vol(path, d->vol);
}

static fs_ops_t fatfs_ops = {
    .open    = fatfs_open,
    .opendir = fatfs_opendir,
    .readdir = fatfs_readdir,
    .mkdir   = fatfs_mkdir,
    .rmdir   = fatfs_rmdir,
    .unlink  = fatfs_unlink,
    .rename  = fatfs_rename,
    .stat    = fatfs_stat,
    .statfs  = fatfs_statfs,
    .create  = fatfs_create,
};

fs_ops_t *fatfs_get_ops(void) { return &fatfs_ops; }

fatfs_data_t *fatfs_make_data(int vol)
{
    fatfs_data_t *d = (fatfs_data_t *)kmalloc(sizeof(fatfs_data_t));
    if (d) d->vol = vol;
    return d;
}
