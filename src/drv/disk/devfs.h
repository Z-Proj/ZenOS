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
} dev_entry_t;

void      devfs_init       (void);
int       devfs_register   (const char *name,
                            int (*read)(void *buf, uint32_t size, uint32_t *got),
                            int (*write)(const void *buf, uint32_t size));
fs_ops_t *devfs_get_ops    (void);

#endif
