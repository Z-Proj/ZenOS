#ifndef UNIX_SOCK_H
#define UNIX_SOCK_H

#include <stdint.h>
#include <stddef.h>
#include "../spinlock.h"

#define UNIX_SOCK_MAX       64
#define UNIX_PATH_MAX       108
#define UNIX_BUF_SIZE       65536
#define UNIX_BACKLOG_MAX    8

typedef enum {
    US_IDLE = 0,
    US_BOUND,
    US_LISTENING,
    US_CONNECTING,
    US_CONNECTED,
    US_CLOSED,
} unix_sock_state_t;

typedef struct unix_sock unix_sock_t;

struct unix_sock {
    int                 in_use;
    unix_sock_state_t   state;
    char                path[UNIX_PATH_MAX];
    int                 nonblock;

    uint8_t             rbuf[UNIX_BUF_SIZE];
    uint32_t            rread;
    uint32_t            rwrite;
    uint32_t            rcount;

    unix_sock_t        *peer;

    unix_sock_t        *backlog[UNIX_BACKLOG_MAX];
    int                 backlog_len;
    int                 backlog_max;

    int                 write_closed;
    int                 read_closed;

    spinlock_t          lock;
};

void        unix_sock_init(void);
unix_sock_t *unix_sock_alloc(void);
void        unix_sock_free(unix_sock_t *s);
int         unix_sock_bind(unix_sock_t *s, const char *path);
int         unix_sock_listen(unix_sock_t *s, int backlog);
int         unix_sock_connect(unix_sock_t *s, const char *path);
unix_sock_t *unix_sock_accept(unix_sock_t *s);
int         unix_sock_write(unix_sock_t *s, const void *buf, size_t len);
int         unix_sock_read(unix_sock_t *s, void *buf, size_t len);
unix_sock_t *unix_sock_find_by_path(const char *path);
void        unix_sock_shutdown(unix_sock_t *s);

#endif
