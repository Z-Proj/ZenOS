/**
 * 
 * @file : /src/libk/core/fd.c
 * @brief : File descriptor table management - alloc, clone, close, and refcounting.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#include "fd.h"
#include "mem.h"
#include "../../drv/disk/fat.h"
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
    return fd_alloc_from(t, 3);
}

int fd_alloc_from(fd_table_t *t, int minfd)
{
    if (!t)
        return -1;
    if (minfd < 0)
        return -1;
    if (minfd < 3)
        minfd = 3;
    for (int i = minfd; i < TASK_MAX_FDS; i++)
        if (!t->entries[i].used)
            return i;
    return -1;
}

static void fd_close_entry(fd_entry_t *e)
{
    if (!e->used) return;

    if (e->type == FD_FILE) {
        if (e->file) {
            e->file->refcount--;
            if (e->file->refcount <= 0) {
                fat_lock();
                f_close(&e->file->fil);
                fat_unlock();
                kfree(e->file);
            }
        }
    } else if (e->type == FD_DIR) {
        if (e->dir) {
            e->dir->refcount--;
            if (e->dir->refcount <= 0) {
                fat_lock();
                f_closedir(&e->dir->dir);
                fat_unlock();
                kfree(e->dir);
            }
        }
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
    } else if (e->type == FD_PTY_MASTER) {
        e->pty->master_refs--;
        if (e->pty->master_refs <= 0) e->pty->master_open = 0;
        e->pty->refcount--;
        if (e->pty->refcount <= 0)
            kfree(e->pty);
    } else if (e->type == FD_PTY_SLAVE) {
        e->pty->slave_refs--;
        if (e->pty->slave_refs <= 0) e->pty->slave_open = 0;
        e->pty->refcount--;
        if (e->pty->refcount <= 0)
            kfree(e->pty);
    } else if (e->type == FD_UNIX_SOCK) {
        if (e->usock && e->usock->refcount <= 1)
            unix_sock_shutdown(e->usock);
        unix_sock_free(e->usock);
    }

    memset(e, 0, sizeof(fd_entry_t));
}

int fd_entry_clone(fd_entry_t *dst, const fd_entry_t *src, int inherit_cloexec)
{
    if (!dst || !src || !src->used)
        return -1;

    memset(dst, 0, sizeof(fd_entry_t));
    dst->used = src->used;
    dst->type = src->type;
    dst->cloexec = inherit_cloexec ? src->cloexec : 0;

    if (src->type == FD_FILE) {
        if (!src->file)
            return -1;
        dst->file = src->file;
        dst->file->refcount++;
    } else if (src->type == FD_DIR) {
        if (!src->dir)
            return -1;
        dst->dir = src->dir;
        dst->dir->refcount++;
    } else if (src->type == FD_PIPE_READ || src->type == FD_PIPE_WRITE) {
        dst->pipe = src->pipe;
        dst->pipe->refcount++;
        if (src->type == FD_PIPE_READ) dst->pipe->readers++;
        if (src->type == FD_PIPE_WRITE) dst->pipe->writers++;
    } else if (src->type == FD_PTY_MASTER) {
        dst->pty = src->pty;
        dst->pty->refcount++;
        dst->pty->master_refs++;
        dst->pty->master_open = 1;
    } else if (src->type == FD_PTY_SLAVE) {
        dst->pty = src->pty;
        dst->pty->refcount++;
        dst->pty->slave_refs++;
        dst->pty->slave_open = 1;
    } else if (src->type == FD_UNIX_SOCK) {
        dst->usock = src->usock;
        unix_sock_retain(dst->usock);
    } else if (src->type == FD_DEV) {
        dst->dev_ops = src->dev_ops;
    } else if (src->type == FD_STDIO) {
        dst->stdio_fd = src->stdio_fd;
    }

    return 0;
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
        if (fd_entry_clone(de, se, 1) < 0) {
            fd_table_free(dst);
            return NULL;
        }
    }

    return dst;
}

void fd_table_close_cloexec(fd_table_t *t)
{
    if (!t) return;
    for (int i = 0; i < TASK_MAX_FDS; i++)
        if (t->entries[i].used && t->entries[i].cloexec)
            fd_close_entry(&t->entries[i]);
}

void fd_table_close_all(fd_table_t *t)
{
    if (!t) return;
    for (int i = 0; i < TASK_MAX_FDS; i++)
        if (t->entries[i].used)
            fd_close_entry(&t->entries[i]);
}

int fd_close(fd_table_t *t, int fd)
{
    if (!t || fd < 0 || fd >= TASK_MAX_FDS) return -1;
    fd_close_entry(&t->entries[fd]);
    return 0;
}
