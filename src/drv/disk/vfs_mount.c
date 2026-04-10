/**
 * 
 * @file : /src/drv/disk/vfs_mount.c
 * @brief : VFS mount point management - register, unregister, and lookup filesystems.
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

#include "vfs_mount.h"
#include "../../libk/string.h"
#include "../../libk/core/mem.h"
#include "../../libk/debug/log.h"

static vfs_mount_t *mount_list = NULL;

void vfs_mount_init(void)
{
    mount_list = NULL;
}

int vfs_mount(const char *path, fs_ops_t *ops, void *fs_data)
{
    if (!path || !ops) return -1;

    vfs_mount_t *m = (vfs_mount_t *)kmalloc(sizeof(vfs_mount_t));
    if (!m) return -1;

    strncpy(m->path, path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';

    int len = strlen(m->path);
    if (len > 1 && m->path[len - 1] == '/')
        m->path[--len] = '\0';

    m->ops     = ops;
    m->fs_data = fs_data;
    m->next    = NULL;

    if (!mount_list) {
        mount_list = m;
        return 0;
    }

    vfs_mount_t *cur = mount_list;
    while (cur->next) cur = cur->next;
    cur->next = m;
    return 0;
}

int vfs_umount(const char *path)
{
    if (!path || !mount_list) return -1;

    vfs_mount_t *prev = NULL;
    vfs_mount_t *cur  = mount_list;

    while (cur) {
        if (strcmp(cur->path, path) == 0) {
            if (prev) prev->next = cur->next;
            else       mount_list = cur->next;
            kfree(cur);
            return 0;
        }
        prev = cur;
        cur  = cur->next;
    }
    return -1;
}

vfs_mount_t *vfs_mount_find(const char *path, const char **rel_out)
{
    if (!path) return NULL;

    vfs_mount_t *best      = NULL;
    size_t       best_len  = 0;
    const char  *best_rel  = path;

    for (vfs_mount_t *m = mount_list; m; m = m->next) {
        size_t mlen = strlen(m->path);
        if (strncmp(path, m->path, mlen) != 0) continue;
        char next_ch = path[mlen];
        if (next_ch != '\0' && next_ch != '/') continue;
        if (mlen > best_len) {
            best     = m;
            best_len = mlen;
            best_rel = (next_ch == '/') ? path + mlen : "/";
        }
    }

    if (best && rel_out)
        *rel_out = best_rel;

    return best;
}

int vfs_mount_count(void)
{
    int n = 0;
    for (vfs_mount_t *m = mount_list; m; m = m->next) n++;
    return n;
}

vfs_mount_t *vfs_mount_get(int index)
{
    int i = 0;
    for (vfs_mount_t *m = mount_list; m; m = m->next, i++)
        if (i == index) return m;
    return NULL;
}
