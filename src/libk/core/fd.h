#ifndef FD_H
#define FD_H

#include <stdint.h>
#include <stddef.h>
#include "../../drv/disk/fatfs/ff.h"
#include "../spinlock.h"

#define TASK_MAX_FDS 32
#define PTY_NCCS 19

typedef enum
{
    FD_NONE = 0,
    FD_FILE,
    FD_PIPE_READ,
    FD_PIPE_WRITE,
    FD_DIR,
    FD_DEV,
    FD_PTY_MASTER,
    FD_PTY_SLAVE,
} fd_type_t;

#define PTY_BUF_SIZE 8192

typedef struct pty_buf
{

    uint8_t m2s_data[PTY_BUF_SIZE];
    uint32_t m2s_read;
    uint32_t m2s_write;
    uint32_t m2s_count;
    uint32_t m2s_ready;

    uint8_t s2m_data[PTY_BUF_SIZE];
    uint32_t s2m_read;
    uint32_t s2m_write;
    uint32_t s2m_count;

    uint16_t ws_rows;
    uint16_t ws_cols;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;

    int master_open;
    int master_refs;
    int slave_open;
    int slave_refs;
    int refcount;

    uint32_t iflag;
    uint32_t oflag;
    uint32_t cflag;
    uint32_t lflag;
    uint8_t cc[PTY_NCCS];
    uint32_t ispeed;
    uint32_t ospeed;
    int32_t pgrp;
    int eof_pending;
    uint8_t esc_state;
    spinlock_t lock;
} pty_buf_t;

typedef struct
{
    FIL fil;
    int writable;
    uint32_t total_written;
    uint16_t fdate;
    uint16_t ftime;
} fd_file_t;

typedef struct pipe_buf
{
    uint8_t data[4096];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    int write_closed;
    int read_closed;
    int readers;
    int writers;
    int refcount;
    spinlock_t lock;
} pipe_buf_t;

typedef struct
{
    DIR dir;
    FILINFO fno;
    int first_read;
} fd_dir_t;

struct dev_entry;

typedef struct
{
    fd_type_t type;
    int used;
    int cloexec;
    union
    {
        fd_file_t file;
        pipe_buf_t *pipe;
        fd_dir_t dir;
        struct dev_entry *dev_ops;
        pty_buf_t *pty;
    };
} fd_entry_t;

typedef struct
{
    fd_entry_t entries[TASK_MAX_FDS];
} fd_table_t;

fd_table_t *fd_table_alloc(void);
void fd_table_free(fd_table_t *t);
fd_table_t *fd_table_clone(fd_table_t *src);
void fd_table_close_cloexec(fd_table_t *t);
void fd_table_close_all(fd_table_t *t);
int fd_alloc(fd_table_t *t);
int fd_close(fd_table_t *t, int fd);

#endif
