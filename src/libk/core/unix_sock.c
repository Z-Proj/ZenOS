#include "unix_sock.h"
#include "mem.h"
#include "../../libk/string.h"
#include "../../kernel/sched.h"

#define UNIX_SOCK_STREAM 1
#define UNIX_SOCK_DGRAM  2

static unix_sock_t unix_sock_pool[UNIX_SOCK_MAX];
static spinlock_t  pool_lock;

static int unix_sock_valid_path(const char *path)
{
    if (!path || path[0] != '/' || path[1] == '\0')
        return 0;

    size_t len = strlen(path);
    if (len >= UNIX_PATH_MAX)
        return 0;

    for (size_t i = 1; i < len; i++)
        if (path[i] == '/')
            return 0;

    return 1;
}

static void unix_sock_copy_path(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static unix_sock_t *unix_sock_find_by_path_locked(const char *path)
{
    for (int i = 0; i < UNIX_SOCK_MAX; i++)
    {
        unix_sock_t *s = &unix_sock_pool[i];
        if (!s->in_use || !s->registered)
            continue;
        if (strcmp(s->path, path) == 0)
            return s;
    }

    return NULL;
}

void unix_sock_init(void)
{
    spinlock_init(&pool_lock);
    memset(unix_sock_pool, 0, sizeof(unix_sock_pool));
}

unix_sock_t *unix_sock_alloc(void)
{
    uint64_t f = spinlock_acquire_irqsave(&pool_lock);

    for (int i = 0; i < UNIX_SOCK_MAX; i++)
    {
        if (unix_sock_pool[i].in_use)
            continue;

        unix_sock_t *s = &unix_sock_pool[i];
        memset(s, 0, sizeof(*s));
        s->in_use = 1;
        s->refcount = 1;
        s->domain = 1;
        s->state = US_IDLE;
        spinlock_init(&s->lock);
        spinlock_release_irqrestore(&pool_lock, f);
        return s;
    }

    spinlock_release_irqrestore(&pool_lock, f);
    return NULL;
}

void unix_sock_retain(unix_sock_t *s)
{
    if (!s)
        return;

    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    if (s->in_use)
        s->refcount++;
    spinlock_release_irqrestore(&pool_lock, f);
}

void unix_sock_free(unix_sock_t *s)
{
    if (!s)
        return;

    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    if (s->refcount > 0)
        s->refcount--;
    if (s->refcount > 0)
    {
        spinlock_release_irqrestore(&pool_lock, f);
        return;
    }

    s->in_use = 0;
    s->refcount = 0;
    s->acceptconn = 0;
    s->registered = 0;
    s->state = US_CLOSED;
    s->peer = NULL;
    s->path[0] = '\0';
    s->peer_path[0] = '\0';
    spinlock_release_irqrestore(&pool_lock, f);
}

unix_sock_t *unix_sock_find_by_path(const char *path)
{
    if (!unix_sock_valid_path(path))
        return NULL;

    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    unix_sock_t *s = unix_sock_find_by_path_locked(path);
    spinlock_release_irqrestore(&pool_lock, f);
    return s;
}

int unix_sock_path_exists(const char *path)
{
    if (!unix_sock_valid_path(path))
        return 0;

    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    int exists = unix_sock_find_by_path_locked(path) != NULL;
    spinlock_release_irqrestore(&pool_lock, f);
    return exists;
}

int unix_sock_unlink_path(const char *path)
{
    if (!unix_sock_valid_path(path))
        return -1;

    uint64_t f = spinlock_acquire_irqsave(&pool_lock);
    unix_sock_t *s = unix_sock_find_by_path_locked(path);
    if (!s)
    {
        spinlock_release_irqrestore(&pool_lock, f);
        return -1;
    }

    s->registered = 0;
    spinlock_release_irqrestore(&pool_lock, f);
    return 0;
}

uint32_t unix_sock_list_paths(char paths[][UNIX_PATH_MAX], uint32_t max_count)
{
    uint32_t count = 0;
    uint64_t f = spinlock_acquire_irqsave(&pool_lock);

    for (int i = 0; i < UNIX_SOCK_MAX && count < max_count; i++)
    {
        unix_sock_t *s = &unix_sock_pool[i];
        if (!s->in_use || !s->registered || s->path[0] == '\0')
            continue;
        unix_sock_copy_path(paths[count], UNIX_PATH_MAX, s->path);
        count++;
    }

    spinlock_release_irqrestore(&pool_lock, f);
    return count;
}

int unix_sock_set_nonblock(unix_sock_t *s, int nonblock)
{
    if (!s)
        return -1;

    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    s->nonblock = !!nonblock;
    spinlock_release_irqrestore(&s->lock, f);
    return 0;
}

int unix_sock_bind(unix_sock_t *s, const char *path)
{
    if (!s || !unix_sock_valid_path(path))
        return -1;

    uint64_t fp = spinlock_acquire_irqsave(&pool_lock);
    if (unix_sock_find_by_path_locked(path))
    {
        spinlock_release_irqrestore(&pool_lock, fp);
        return -1;
    }

    uint64_t fs = spinlock_acquire_irqsave(&s->lock);
    if (s->state != US_IDLE || s->registered)
    {
        spinlock_release_irqrestore(&s->lock, fs);
        spinlock_release_irqrestore(&pool_lock, fp);
        return -1;
    }

    unix_sock_copy_path(s->path, sizeof(s->path), path);
    s->registered = 1;
    s->state = US_BOUND;
    spinlock_release_irqrestore(&s->lock, fs);
    spinlock_release_irqrestore(&pool_lock, fp);
    return 0;
}

int unix_sock_listen(unix_sock_t *s, int backlog)
{
    if (!s)
        return -1;

    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    if (s->type != UNIX_SOCK_STREAM || s->state != US_BOUND)
    {
        spinlock_release_irqrestore(&s->lock, f);
        return -1;
    }

    if (backlog <= 0)
        backlog = 1;
    if (backlog > UNIX_BACKLOG_MAX)
        backlog = UNIX_BACKLOG_MAX;

    s->acceptconn = 1;
    s->backlog_max = backlog;
    s->backlog_len = 0;
    s->state = US_LISTENING;
    spinlock_release_irqrestore(&s->lock, f);
    return 0;
}

int unix_sock_connect(unix_sock_t *s, const char *path)
{
    if (!s || !unix_sock_valid_path(path))
        return -1;

    unix_sock_t *server = unix_sock_find_by_path(path);
    if (!server)
        return -1;

    if (s->type != server->type)
        return -1;

    if (s->type == UNIX_SOCK_STREAM)
    {
        unix_sock_t *child = unix_sock_alloc();
        if (!child)
            return -1;

        uint64_t fs = spinlock_acquire_irqsave(&server->lock);
        if (!server->acceptconn || server->state != US_LISTENING ||
            server->backlog_len >= server->backlog_max)
        {
            spinlock_release_irqrestore(&server->lock, fs);
            unix_sock_free(child);
            return -1;
        }

        uint64_t fc = spinlock_acquire_irqsave(&s->lock);
        if (!(s->state == US_IDLE || s->state == US_BOUND))
        {
            spinlock_release_irqrestore(&s->lock, fc);
            spinlock_release_irqrestore(&server->lock, fs);
            unix_sock_free(child);
            return -1;
        }

        child->domain = s->domain;
        child->type = UNIX_SOCK_STREAM;
        child->state = US_CONNECTED;
        child->peer = s;
        child->nonblock = server->nonblock;
        unix_sock_copy_path(child->path, sizeof(child->path), server->path);
        unix_sock_copy_path(child->peer_path, sizeof(child->peer_path), s->path);

        s->peer = child;
        s->state = US_CONNECTED;
        unix_sock_copy_path(s->peer_path, sizeof(s->peer_path), server->path);

        server->backlog[server->backlog_len++] = child;
        spinlock_release_irqrestore(&s->lock, fc);
        spinlock_release_irqrestore(&server->lock, fs);
        return 0;
    }

    uint64_t fs = spinlock_acquire_irqsave(&server->lock);
    uint64_t fc = spinlock_acquire_irqsave(&s->lock);
    if (!(s->state == US_IDLE || s->state == US_BOUND) || server->acceptconn)
    {
        spinlock_release_irqrestore(&s->lock, fc);
        spinlock_release_irqrestore(&server->lock, fs);
        return -1;
    }

    s->peer = server;
    s->state = US_CONNECTED;
    unix_sock_copy_path(s->peer_path, sizeof(s->peer_path), server->path);
    spinlock_release_irqrestore(&s->lock, fc);
    spinlock_release_irqrestore(&server->lock, fs);
    return 0;
}

unix_sock_t *unix_sock_accept(unix_sock_t *s)
{
    if (!s)
        return NULL;

    for (;;)
    {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->type != UNIX_SOCK_STREAM || !s->acceptconn)
        {
            spinlock_release_irqrestore(&s->lock, f);
            return NULL;
        }

        if (s->backlog_len > 0)
        {
            unix_sock_t *conn = s->backlog[0];
            for (int i = 1; i < s->backlog_len; i++)
                s->backlog[i - 1] = s->backlog[i];
            s->backlog_len--;
            spinlock_release_irqrestore(&s->lock, f);
            return conn;
        }

        spinlock_release_irqrestore(&s->lock, f);
        if (s->nonblock)
            return NULL;
        sched_yield();
    }
}

static int unix_sock_write_locked(unix_sock_t *dst, const uint8_t *src, size_t len, int nonblock)
{
    size_t written = 0;

    while (written < len)
    {
        uint64_t f = spinlock_acquire_irqsave(&dst->lock);
        if (dst->read_closed)
        {
            spinlock_release_irqrestore(&dst->lock, f);
            return written > 0 ? (int)written : -1;
        }

        while (written < len && dst->rcount < UNIX_BUF_SIZE)
        {
            dst->rbuf[dst->rwrite] = src[written++];
            dst->rwrite = (dst->rwrite + 1) % UNIX_BUF_SIZE;
            dst->rcount++;
        }

        spinlock_release_irqrestore(&dst->lock, f);
        if (written < len)
        {
            if (nonblock)
                break;
            sched_yield();
        }
    }

    return (int)written;
}

int unix_sock_sendto(unix_sock_t *s, const void *buf, size_t len, const char *path)
{
    if (!s || !buf || len == 0)
        return -1;

    if (s->type == UNIX_SOCK_STREAM)
    {
        unix_sock_t *peer = s->peer;
        if (!peer)
            return -1;
        return unix_sock_write_locked(peer, (const uint8_t *)buf, len, s->nonblock);
    }

    unix_sock_t *dst = NULL;
    if (path && path[0])
        dst = unix_sock_find_by_path(path);
    else
        dst = s->peer;
    if (!dst)
        return -1;

    if (dst->type != UNIX_SOCK_DGRAM)
        return -1;

    unix_sock_copy_path(dst->peer_path, sizeof(dst->peer_path), s->path);
    return unix_sock_write_locked(dst, (const uint8_t *)buf, len, s->nonblock);
}

int unix_sock_write(unix_sock_t *s, const void *buf, size_t len)
{
    return unix_sock_sendto(s, buf, len, NULL);
}

int unix_sock_recvfrom(unix_sock_t *s, void *buf, size_t len, char *path, size_t path_len)
{
    if (!s || !buf || len == 0)
        return -1;

    uint8_t *dst = (uint8_t *)buf;

    for (;;)
    {
        uint64_t f = spinlock_acquire_irqsave(&s->lock);
        if (s->rcount > 0)
        {
            size_t n = 0;
            while (n < len && s->rcount > 0)
            {
                dst[n++] = s->rbuf[s->rread];
                s->rread = (s->rread + 1) % UNIX_BUF_SIZE;
                s->rcount--;
            }

            if (path && path_len)
                unix_sock_copy_path(path, path_len, s->peer_path);
            spinlock_release_irqrestore(&s->lock, f);
            return (int)n;
        }

        int closed = s->write_closed || (!s->peer && s->type == UNIX_SOCK_STREAM);
        spinlock_release_irqrestore(&s->lock, f);
        if (closed)
            return 0;
        if (s->nonblock)
            return -11;
        sched_yield();
    }
}

int unix_sock_read(unix_sock_t *s, void *buf, size_t len)
{
    return unix_sock_recvfrom(s, buf, len, NULL, 0);
}

int unix_sock_socketpair(int type, int nonblock, unix_sock_t **a, unix_sock_t **b)
{
    if (!a || !b)
        return -1;
    if (type != UNIX_SOCK_STREAM && type != UNIX_SOCK_DGRAM)
        return -1;

    unix_sock_t *left = unix_sock_alloc();
    if (!left)
        return -1;
    unix_sock_t *right = unix_sock_alloc();
    if (!right)
    {
        unix_sock_free(left);
        return -1;
    }

    left->type = type;
    left->nonblock = !!nonblock;
    left->state = US_CONNECTED;
    left->peer = right;

    right->type = type;
    right->nonblock = !!nonblock;
    right->state = US_CONNECTED;
    right->peer = left;

    *a = left;
    *b = right;
    return 0;
}

int unix_sock_getsockname(unix_sock_t *s, char *path, size_t path_len)
{
    if (!s || !path || path_len == 0)
        return -1;
    unix_sock_copy_path(path, path_len, s->path);
    return 0;
}

int unix_sock_getpeername(unix_sock_t *s, char *path, size_t path_len)
{
    if (!s || !path || path_len == 0)
        return -1;
    unix_sock_copy_path(path, path_len, s->peer_path);
    return 0;
}

void unix_sock_shutdown(unix_sock_t *s)
{
    if (!s)
        return;

    uint64_t f = spinlock_acquire_irqsave(&s->lock);
    s->write_closed = 1;
    s->state = US_CLOSED;
    s->acceptconn = 0;
    s->registered = 0;

    for (int i = 0; i < s->backlog_len; i++)
    {
        unix_sock_t *p = s->backlog[i];
        if (!p)
            continue;
        uint64_t fp = spinlock_acquire_irqsave(&p->lock);
        p->write_closed = 1;
        p->state = US_CLOSED;
        if (p->peer)
        {
            unix_sock_t *q = p->peer;
            uint64_t fq = spinlock_acquire_irqsave(&q->lock);
            q->write_closed = 1;
            q->peer = NULL;
            spinlock_release_irqrestore(&q->lock, fq);
        }
        p->peer = NULL;
        spinlock_release_irqrestore(&p->lock, fp);
    }
    s->backlog_len = 0;

    if (s->peer)
    {
        unix_sock_t *p = s->peer;
        s->peer = NULL;
        uint64_t fp = spinlock_acquire_irqsave(&p->lock);
        p->write_closed = 1;
        p->peer = NULL;
        spinlock_release_irqrestore(&p->lock, fp);
    }

    spinlock_release_irqrestore(&s->lock, f);
}
