#include "fd.h"
#include "mem.h"
#include "../../libk/string.h"

fd_table_t *fd_table_alloc(void)
{
    fd_table_t *t = (fd_table_t *)kmalloc(sizeof(fd_table_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(fd_table_t));
    return t;
}

void fd_table_free(fd_table_t *t)
{
    if (!t) return;
    fd_table_close_all(t);
    kfree(t);
}

int fd_alloc(fd_table_t *t)
{
    for (int i = 3; i < TASK_MAX_FDS; i++)
        if (!t->entries[i].used)
            return i;
    return -1;
}

static void fd_close_entry(fd_entry_t *e)
{
    if (!e->used) return;

    if (e->type == FD_FILE) {
        f_close(&e->file.fil);
    } else if (e->type == FD_DIR) {
        f_closedir(&e->dir.dir);
    } else if (e->type == FD_PIPE_READ) {
        e->pipe->readers--;
        if (e->pipe->readers <= 0) e->pipe->read_closed = 1;
        e->pipe->refcount--;
        if (e->pipe->refcount <= 0)
            kfree(e->pipe);
    } else if (e->type == FD_PIPE_WRITE) {
        e->pipe->writers--;
        if (e->pipe->writers <= 0) e->pipe->write_closed = 1;
        e->pipe->refcount--;
        if (e->pipe->refcount <= 0)
            kfree(e->pipe);
    }

    memset(e, 0, sizeof(fd_entry_t));
}

fd_table_t *fd_table_clone(fd_table_t *src)
{
    if (!src) return NULL;
    fd_table_t *dst = fd_table_alloc();
    if (!dst) return NULL;

    for (int i = 0; i < TASK_MAX_FDS; i++) {
        fd_entry_t *se = &src->entries[i];
        fd_entry_t *de = &dst->entries[i];
        if (!se->used) continue;

        de->used    = se->used;
        de->type    = se->type;
        de->cloexec = se->cloexec;

        if (se->type == FD_FILE) {
            FRESULT fr = f_open(&de->file.fil, NULL, 0);
            (void)fr;
            de->file = se->file;
            FSIZE_t pos = f_tell(&se->file.fil);
            f_rewind(&de->file.fil);
            f_lseek(&de->file.fil, pos);
        } else if (se->type == FD_PIPE_READ || se->type == FD_PIPE_WRITE) {
            de->pipe = se->pipe;
            de->pipe->refcount++;
            if (se->type == FD_PIPE_READ) de->pipe->readers++;
            if (se->type == FD_PIPE_WRITE) de->pipe->writers++;
        }
    }

    return dst;
}

void fd_table_close_cloexec(fd_table_t *t)
{
    if (!t) return;
    for (int i = 3; i < TASK_MAX_FDS; i++)
        if (t->entries[i].used && t->entries[i].cloexec)
            fd_close_entry(&t->entries[i]);
}

void fd_table_close_all(fd_table_t *t)
{
    if (!t) return;
    for (int i = 3; i < TASK_MAX_FDS; i++)
        if (t->entries[i].used)
            fd_close_entry(&t->entries[i]);
}

int fd_close(fd_table_t *t, int fd)
{
    if (!t || fd < 0 || fd >= TASK_MAX_FDS) return -1;
    fd_close_entry(&t->entries[fd]);
    return 0;
}
