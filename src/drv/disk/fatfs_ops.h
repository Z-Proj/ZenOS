#ifndef FATFS_OPS_H
#define FATFS_OPS_H

#include "vfs_fs.h"

typedef struct {
    int vol;
} fatfs_data_t;

fs_ops_t    *fatfs_get_ops  (void);
fatfs_data_t *fatfs_make_data(int vol);

#endif
