#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#include <stddef.h>

#define FAT_MAX_FDS     32
#define FAT_MAX_PATH    256

typedef enum {
    FAT_OK = 0,
    FAT_ERR_NOT_INITIALIZED,
    FAT_ERR_NOT_FOUND,
    FAT_ERR_NO_SPACE,
    FAT_ERR_ALREADY_EXISTS,
    FAT_ERR_BAD_FD,
    FAT_ERR_IS_DIR,
    FAT_ERR_NOT_DIR,
    FAT_ERR_NOT_EMPTY,
    FAT_ERR_IO,
    FAT_ERR_INVALID_PARAM,
} fat_error_t;

fat_error_t fat_init(uint8_t drive);
fat_error_t fat_format(uint8_t drive);
int         fat_open(const char *path, int write);
int         fat_read(int fd, void *buf, uint32_t size, uint32_t *bytes_read);
int         fat_write(int fd, const void *buf, uint32_t size);
int         fat_close(int fd);
int         fat_seek(int fd, uint32_t pos);
int         fat_lseek(int fd, int32_t offset, int whence);
uint32_t    fat_size(int fd);                     
int         fat_create(const char *path);         
int         fat_delete(const char *path);
int         fat_mkdir(const char *path);
int         fat_rmdir(const char *path);
int         fat_chdir(const char *path);
void        fat_getcwd(char *buf, size_t size);
int         fat_list(char *buf, size_t buf_size); 
void        fat_print_stats(char *buf, size_t buf_size);


#include "../../libk/core/fd.h"

int         fat_open_entry(const char *path, int write, fd_entry_t *out);
int         fat_read_entry(fd_entry_t *e, void *buf, uint32_t size, uint32_t *bytes_read);
int         fat_write_entry(fd_entry_t *e, const void *buf, uint32_t size);
int         fat_close_entry(fd_entry_t *e);
int         fat_lseek_entry(fd_entry_t *e, int32_t offset, int whence);
uint32_t    fat_size_entry(fd_entry_t *e);

int         fat_opendir_entry(const char *path, fd_entry_t *out);
int         fat_readdir_entry(fd_entry_t *e, char *name_out, int *is_dir_out);
int         fat_closedir_entry(fd_entry_t *e);

#endif
