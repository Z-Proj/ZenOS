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
    int                 refcount;
    int                 domain;
    int                 type;
    int                 acceptconn;
    int                 registered;
    unix_sock_state_t   state;
    char                path[UNIX_PATH_MAX];
    char                peer_path[UNIX_PATH_MAX];
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
void        unix_sock_retain(unix_sock_t *s);
void        unix_sock_free(unix_sock_t *s);
int         unix_sock_bind(unix_sock_t *s, const char *path);
int         unix_sock_listen(unix_sock_t *s, int backlog);
int         unix_sock_connect(unix_sock_t *s, const char *path);
unix_sock_t *unix_sock_accept(unix_sock_t *s);
int         unix_sock_write(unix_sock_t *s, const void *buf, size_t len);
int         unix_sock_read(unix_sock_t *s, void *buf, size_t len);
int         unix_sock_sendto(unix_sock_t *s, const void *buf, size_t len, const char *path);
int         unix_sock_recvfrom(unix_sock_t *s, void *buf, size_t len, char *path, size_t path_len);
unix_sock_t *unix_sock_find_by_path(const char *path);
int         unix_sock_path_exists(const char *path);
int         unix_sock_unlink_path(const char *path);
uint32_t    unix_sock_list_paths(char paths[][UNIX_PATH_MAX], uint32_t max_count);
int         unix_sock_socketpair(int type, int nonblock, unix_sock_t **a, unix_sock_t **b);
int         unix_sock_getsockname(unix_sock_t *s, char *path, size_t path_len);
int         unix_sock_getpeername(unix_sock_t *s, char *path, size_t path_len);
int         unix_sock_set_nonblock(unix_sock_t *s, int nonblock);
void        unix_sock_shutdown(unix_sock_t *s);

#endif
