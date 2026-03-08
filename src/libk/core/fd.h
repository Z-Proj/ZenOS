#ifndef FD_H
#define FD_H

#include <stdint.h>
#include <stddef.h>
#include "../../drv/disk/fatfs/ff.h"

#define TASK_MAX_FDS 32

typedef enum {
    FD_NONE = 0,
    FD_FILE,
    FD_PIPE_READ,
    FD_PIPE_WRITE,
    FD_DIR,
    FD_DEV,
} fd_type_t;

typedef struct {
    FIL         fil;
    int         writable;
    uint32_t    total_written;
    uint16_t    fdate;   /* FAT modification date (packed) */
    uint16_t    ftime;   /* FAT modification time (packed) */
} fd_file_t;

typedef struct pipe_buf {
    uint8_t     data[4096];
    uint32_t    read_pos;
    uint32_t    write_pos;
    uint32_t    count;
    int         write_closed;
    int         read_closed;
    int         readers;
    int         writers;
    int         refcount;
} pipe_buf_t;

typedef struct {
    DIR         dir;
    FILINFO     fno;
    int         first_read;
} fd_dir_t;

struct dev_entry;

typedef struct {
    fd_type_t        type;
    int              used;
    int              cloexec;
    union {
        fd_file_t        file;
        pipe_buf_t      *pipe;
        fd_dir_t         dir;
        struct dev_entry *dev_ops;
    };
} fd_entry_t;

typedef struct {
    fd_entry_t  entries[TASK_MAX_FDS];
} fd_table_t;

fd_table_t *fd_table_alloc(void);
void        fd_table_free(fd_table_t *t);
fd_table_t *fd_table_clone(fd_table_t *src);
void        fd_table_close_cloexec(fd_table_t *t);
void        fd_table_close_all(fd_table_t *t);
int         fd_alloc(fd_table_t *t);
int         fd_close(fd_table_t *t, int fd);

#endif
