#ifndef VFS_FS_H
#define VFS_FS_H

#include <stdint.h>
#include <stddef.h>
#include "../../libk/core/fd.h"

typedef struct fs_ops {
    int      (*open)    (void *fs_data, const char *path, int write, fd_entry_t *out);
    int      (*readdir) (void *fs_data, const char *path, char *buf, size_t bufsz);
    int      (*mkdir)   (void *fs_data, const char *path);
    int      (*rmdir)   (void *fs_data, const char *path);
    int      (*unlink)  (void *fs_data, const char *path);
    int      (*stat)    (void *fs_data, const char *path);
    int      (*create)  (void *fs_data, const char *path);
} fs_ops_t;

#endif
