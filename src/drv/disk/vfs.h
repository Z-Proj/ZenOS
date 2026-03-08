#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "fat.h"
#include "vfs_mount.h"
#include "../../libk/core/fd.h"

int      vfs_init(void);

int      vfs_open_entry   (const char *path, int write, fd_entry_t *out);
int      vfs_read_entry   (fd_entry_t *e, void *buf, uint32_t size, uint32_t *bytes_read);
int      vfs_write_entry  (fd_entry_t *e, const void *buf, uint32_t size);
int      vfs_close_entry  (fd_entry_t *e);
int      vfs_lseek_entry  (fd_entry_t *e, int32_t offset, int whence);
uint32_t vfs_size_entry   (fd_entry_t *e);
int64_t  vfs_mtime_entry  (fd_entry_t *e);

int      vfs_opendir_entry (const char *path, fd_entry_t *out);
int      vfs_readdir_entry (fd_entry_t *e, char *name_out, int *is_dir_out);
int      vfs_closedir_entry(fd_entry_t *e);

int      vfs_create (const char *path);
int      vfs_delete (const char *path);
int      vfs_mkdir  (const char *path);
int      vfs_rmdir  (const char *path);
int      vfs_chdir  (const char *path);
void     vfs_getcwd (char *buf, size_t size);
int      vfs_list   (char *buf, size_t buf_size);
int      vfs_stat   (const char *path);

int      vfs_open (const char *path, int write);
int      vfs_read (int fd, void *buf, uint32_t size, uint32_t *bytes_read);
int      vfs_close(int fd);
uint32_t vfs_size (int fd);

#endif
