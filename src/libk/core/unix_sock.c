#include "unix_sock.h"
#include "mem.h"
#include "../../libk/string.h"
#include "../../kernel/sched.h"

static unix_sock_t unix_sock_pool[UNIX_SOCK_MAX];
static spinlock_t  pool_lock;

void unix_sock_init(void)
{
    spinlock_init(&pool_lock);
    memset(unix_sock_pool, 0, sizeof(unix_sock_pool));
}

unix_sock_t *unix_sock_alloc(void)
{
    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (!unix_sock_pool[i].in_use) {
            unix_sock_t *s = &unix_sock_pool[i];
            memset(s, 0, sizeof(*s));
            s->in_use = 1;
            s->state  = US_IDLE;
            spinlock_init(&s->lock);
            spinlock_release_irqrestore(&pool_lock, f);
            return s;
        }
    }
    spinlock_release_irqrestore(&pool_lock, f);
    return NULL;
}

void unix_sock_free(unix_sock_t *s)
{
    if (!s) return;
    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    s->in_use = 0;
    s->state  = US_CLOSED;
    s->peer   = NULL;
    spinlock_release_irqrestore(&pool_lock, f);
}

unix_sock_t *unix_sock_find_by_path(const char *path)
{
    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (unix_sock_pool[i].in_use &&
            unix_sock_pool[i].state == US_LISTENING &&
            strcmp(unix_sock_pool[i].path, path) == 0) {
            spinlock_release_irqrestore(&pool_lock, f);
            return &unix_sock_pool[i];
        }
    }
    spinlock_release_irqrestore(&pool_lock, f);
    return NULL;
}

int unix_sock_bind(unix_sock_t *s, const char *path)
{
    if (!s || !path) return -1;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->state != US_IDLE) {
        spinlock_release_irqrestore(&s->lock, f);
        return -1;
    }
    strncpy(s->path, path, UNIX_PATH_MAX - 1);
    s->path[UNIX_PATH_MAX - 1] = '\0';
    s->state = US_BOUND;
    spinlock_release_irqrestore(&s->lock, f);
    return 0;
}

int unix_sock_listen(unix_sock_t *s, int backlog)
{
    if (!s) return -1;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->state != US_BOUND) {
        spinlock_release_irqrestore(&s->lock, f);
        return -1;
    }
    s->backlog_max = backlog > UNIX_BACKLOG_MAX ? UNIX_BACKLOG_MAX : backlog;
    s->backlog_len = 0;
    s->state = US_LISTENING;
    spinlock_release_irqrestore(&s->lock, f);
    return 0;
}

int unix_sock_connect(unix_sock_t *s, const char *path)
{
    if (!s || !path) return -1;

    unix_sock_t *server = unix_sock_find_by_path(path);
    if (!server) return -1;

    unix_sock_t *client_side = unix_sock_alloc();
    if (!client_side) return -1;

    uint64_t fs = spinlock_acquire_irqsave(&server->lock);
    if (server->backlog_len >= server->backlog_max) {
        spinlock_release_irqrestore(&server->lock, fs);
        unix_sock_free(client_side);
        return -1;
    }

    client_side->state = US_CONNECTED;
    client_side->peer  = s;

    s->peer  = client_side;
    s->state = US_CONNECTED;
    strncpy(s->path, path, UNIX_PATH_MAX - 1);

    server->backlog[server->backlog_len++] = client_side;
    spinlock_release_irqrestore(&server->lock, fs);

    return 0;
}

unix_sock_t *unix_sock_accept(unix_sock_t *s)
{
    if (!s) return NULL;

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->backlog_len > 0) {
            unix_sock_t *conn = s->backlog[0];
            for (int i = 1; i < s->backlog_len; i++)
                s->backlog[i - 1] = s->backlog[i];
            s->backlog_len--;
            spinlock_release_irqrestore(&s->lock, f);
            return conn;
        }
        spinlock_release_irqrestore(&s->lock, f);
        if (s->nonblock) return NULL;
        sched_yield();
    }
}

int unix_sock_write(unix_sock_t *s, const void *buf, size_t len)
{
    if (!s || !buf || len == 0) return -1;

    unix_sock_t *peer = s->peer;
    if (!peer) return -1;

    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (written < len) {
        uint64_t f = spinlock_acquire_irqsave(&peer->lock);
        if (peer->read_closed) {
            spinlock_release_irqrestore(&peer->lock, f);
            return written > 0 ? (int)written : -1;
        }
        while (written < len && peer->rcount < UNIX_BUF_SIZE) {
            peer->rbuf[peer->rwrite] = src[written++];
            peer->rwrite = (peer->rwrite + 1) % UNIX_BUF_SIZE;
            peer->rcount++;
        }
        spinlock_release_irqrestore(&peer->lock, f);
        if (written < len) {
            if (s->nonblock) break;
            sched_yield();
        }
    }
    return (int)written;
}

int unix_sock_read(unix_sock_t *s, void *buf, size_t len)
{
    if (!s || !buf || len == 0) return -1;

    uint8_t *dst = (uint8_t *)buf;

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->rcount > 0) {
            size_t n = 0;
            while (n < len && s->rcount > 0) {
                dst[n++] = s->rbuf[s->rread];
                s->rread  = (s->rread + 1) % UNIX_BUF_SIZE;
                s->rcount--;
            }
            spinlock_release_irqrestore(&s->lock, f);
            return (int)n;
        }
        int closed = s->write_closed || (!s->peer);
        spinlock_release_irqrestore(&s->lock, f);
        if (closed) return 0;
        if (s->nonblock) return -11;
        sched_yield();
    }
}

void unix_sock_shutdown(unix_sock_t *s)
{
    if (!s) return;
    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    s->write_closed = 1;
    s->state = US_CLOSED;
    if (s->peer) {
        unix_sock_t *p = s->peer;
        s->peer = NULL;
        uint64_t fp = spinlock_acquire_irqsave(&p->lock);
        p->write_closed = 1;
        p->peer = NULL;
        spinlock_release_irqrestore(&p->lock, fp);
    }
    spinlock_release_irqrestore(&s->lock, f);
}
