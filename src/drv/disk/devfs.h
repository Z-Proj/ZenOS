/**
 * 
 * @file : /src/drv/disk/devfs.h
 * @brief : Simple device filesystem for /dev with null, zero, random, and console.
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

#ifndef DEVFS_H
#define DEVFS_H

#include <stdint.h>
#include <stddef.h>
#include "vfs_fs.h"
#include "../../libk/core/fd.h"

typedef struct dev_entry {
    const char *name;
    int (*read) (void *buf, uint32_t size, uint32_t *got);
    int (*write)(const void *buf, uint32_t size);
    int (*ioctl)(unsigned long req, void *argp);
} dev_entry_t;

void      devfs_init       (void);
int       devfs_register   (const char *name,
                            int (*read)(void *buf, uint32_t size, uint32_t *got),
                            int (*write)(const void *buf, uint32_t size),
                            int (*ioctl)(unsigned long req, void *argp));
fs_ops_t *devfs_get_ops    (void);

#endif
