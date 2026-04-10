/**
 * 
 * @file : /src/drv/disk/fatfs_ops.h
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

#ifndef FATFS_OPS_H
#define FATFS_OPS_H

#include "vfs_fs.h"

typedef struct {
    int vol;
} fatfs_data_t;

fs_ops_t    *fatfs_get_ops  (void);
fatfs_data_t *fatfs_make_data(int vol);

#endif
