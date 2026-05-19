/**
 * 
 * @file : /src/drv/disk/vfs.c
 * @brief : Virtual filesystem layer - path resolution, mount management, file/dir operations.
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

#include "vfs.h"
#include "vfs_mount.h"
#include "fatfs_ops.h"
#include "devfs.h"
#include "ata.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"
#include "../../libk/core/fd.h"
#include "../../libk/core/unix_sock.h"
#include "../../libk/core/mem.h"
#include "../../kernel/sched.h"

int vfs_init(void)
{
    vfs_mount_init();
    devfs_init();

    if (vfs_mount("/dev", devfs_get_ops(), NULL) != 0) {
        log("VFS: failed to mount devfs", 3, 0);
        return -1;
    }
    log("VFS: mounted devfs at /dev", 4, 0);

    int mounted = 0;
    for (int d = 0; d < 8; d++) {
        if (ata_drive_exists((uint8_t)d) != ATA_SUCCESS) continue;

        if (mounted > 0) {
            if (fat_mount_vol(mounted) != FAT_OK) {
                log("VFS: ATA drive %d: no FAT filesystem, skipping", 3, 0, d);
                continue;
            }
        }

        fatfs_data_t *fdata = fatfs_make_data(mounted);
        if (!fdata) continue;

        char mpath[32];
        snprintf(mpath, sizeof(mpath), "/mnt/drv%d", mounted);
        if (vfs_mount(mpath, fatfs_get_ops(), fdata) != 0) {
            kfree(fdata);
            continue;
        }
        log("VFS: /mnt/drv%d -> ATA drive %d", 4, 0, mounted, d);
        mounted++;
    }

    log("VFS: %d FAT volume(s) mounted.", 4, 0, mounted);
    return mounted;
}

static void normalize_path(const char *in, char *out, size_t outsz)
{
    char parts[32][256];
    int  depth = 0;
    int  abs   = (in[0] == '/');
    char tmp[512];
    strncpy(tmp, in, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *tok = tmp;
    while (*tok) {
        while (*tok == '/') tok++;
        if (!*tok) break;
        char *end = tok;
        while (*end && *end != '/') end++;
        int len = (int)(end - tok);
        if (len == 1 && tok[0] == '.') { tok = end; continue; }
        if (len == 2 && tok[0] == '.' && tok[1] == '.') {
            if (depth > 0) depth--;
        } else if (len > 0 && len < 256 && depth < 32) {
            memcpy(parts[depth], tok, len);
            parts[depth][len] = '\0';
            depth++;
        }
        tok = end;
    }

    size_t pos = 0;
    if (abs && pos + 1 < outsz) out[pos++] = '/';
    for (int i = 0; i < depth; i++) {
        if (i > 0 && pos + 1 < outsz) out[pos++] = '/';
        for (int j = 0; parts[i][j] && pos + 1 < outsz; j++)
            out[pos++] = parts[i][j];
    }
    if (pos == 0 && pos + 1 < outsz) out[pos++] = '/';
    out[pos] = '\0';
}

static const char *task_cwd(void)
{
    task_t *t = sched_current_task();
    if (t && t->cwd[0]) return t->cwd;
    return "/mnt/drv0";
}

static void resolve_path(const char *path, char *out, size_t outsz)
{
    char joined[512];
    if (path[0] == '/')
        strncpy(joined, path, sizeof(joined) - 1);
    else
        snprintf(joined, sizeof(joined), "%s/%s", task_cwd(), path);
    joined[sizeof(joined) - 1] = '\0';
    normalize_path(joined, out, outsz);
}

static int is_parent_of_mount(const char *path)
{
    int n = vfs_mount_count();
    for (int i = 0; i < n; i++) {
        vfs_mount_t *m = vfs_mount_get(i);
        if (!m) continue;
        size_t plen = strlen(path);
        if (plen == 1 && path[0] == '/') return 1;
        if (strncmp(m->path, path, plen) == 0 && m->path[plen] == '/')
            return 1;
    }
    return 0;
}

int vfs_open_entry(const char *path, int write, fd_entry_t *out)
{
    if (!path || !out) return -1;
    char rpath[512];
    resolve_path(path, rpath, sizeof(rpath));

    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m) return -1;
    return m->ops->open(m->fs_data, rel, write, out);
}

int vfs_read_entry(fd_entry_t *e, void *buf, uint32_t size, uint32_t *bytes_read)
{
    if (!e || !e->used) return -1;
    if (e->type == FD_DEV) {
        struct dev_entry *d = e->dev_ops;
        if (!d || !d->read) { if (bytes_read) *bytes_read = 0; return 0; }
        return d->read(buf, size, bytes_read);
    }
    return fat_read_entry(e, buf, size, bytes_read);
}

int vfs_write_entry(fd_entry_t *e, const void *buf, uint32_t size)
{
    if (!e || !e->used) return -1;
    if (e->type == FD_DEV) {
        struct dev_entry *d = e->dev_ops;
        if (!d || !d->write) return 0;
        return d->write(buf, size);
    }
    return fat_write_entry(e, buf, size);
}

int vfs_close_entry(fd_entry_t *e)
{
    if (!e || !e->used) return -1;
    if (e->type == FD_DEV) { e->used = 0; return 0; }
    return fat_close_entry(e);
}

int vfs_lseek_entry(fd_entry_t *e, int32_t offset, int whence)
{
    if (!e || !e->used) return -1;
    if (e->type == FD_DEV) return 0;
    return fat_lseek_entry(e, offset, whence);
}

uint32_t vfs_size_entry(fd_entry_t *e)
{
    if (!e || !e->used) return 0;
    if (e->type == FD_DEV) return 0;
    return fat_size_entry(e);
}

int64_t vfs_mtime_entry(fd_entry_t *e)
{
    if (!e || !e->used || e->type != FD_FILE) return 0;
    return fat_mtime_entry(e);
}

int vfs_truncate_entry(fd_entry_t *e, uint32_t size)
{
    if (!e || !e->used || e->type != FD_FILE) return -1;
    return fat_truncate_entry(e, size);
}

int vfs_sync_entry(fd_entry_t *e)
{
    if (!e || !e->used || e->type != FD_FILE) return -1;
    return fat_sync_entry(e);
}

int vfs_opendir_entry(const char *path, fd_entry_t *out)
{
    if (!path || !out) return -1;
    char rpath[512];
    resolve_path(path, rpath, sizeof(rpath));
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m) return -1;
    if (!m->ops->opendir) return -1;
    return m->ops->opendir(m->fs_data, rel, out);
}

int vfs_readdir_entry(fd_entry_t *e, char *name_out, int *is_dir_out) { return fat_readdir_entry(e, name_out, is_dir_out); }
int vfs_closedir_entry(fd_entry_t *e)                                  { return fat_closedir_entry(e); }

int vfs_chdir(const char *path)
{
    if (!path) return -1;
    char rpath[512];
    resolve_path(path, rpath, sizeof(rpath));

    if (is_parent_of_mount(rpath)) {
        task_t *t = sched_current_task();
        if (t) { strncpy(t->cwd, rpath, sizeof(t->cwd) - 1); t->cwd[sizeof(t->cwd)-1] = '\0'; }
        return 0;
    }

    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->stat) return -1;
    if (m->ops->stat(m->fs_data, rel) != 0) return -1;

    task_t *t = sched_current_task();
    if (t) { strncpy(t->cwd, rpath, sizeof(t->cwd) - 1); t->cwd[sizeof(t->cwd)-1] = '\0'; }
    return 0;
}

void vfs_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) return;
    const char *cwd = task_cwd();
    strncpy(buf, cwd, size - 1);
    buf[size - 1] = '\0';
}

int vfs_list(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return -1;

    const char *cwd = task_cwd();
    char rpath[512];
    normalize_path(cwd, rpath, sizeof(rpath));

    if (is_parent_of_mount(rpath) && vfs_mount_find(rpath, NULL) == NULL) {
        size_t pos = 0;
        const char *hdr = "Directory: ";
        for (int i = 0; hdr[i] && pos + 1 < buf_size; i++) buf[pos++] = hdr[i];
        for (int i = 0; rpath[i] && pos + 1 < buf_size; i++) buf[pos++] = rpath[i];
        if (pos + 1 < buf_size) buf[pos++] = '\n';

        int n = vfs_mount_count();
        for (int i = 0; i < n && pos + 64 < buf_size; i++) {
            vfs_mount_t *m = vfs_mount_get(i);
            if (!m) continue;
            size_t rlen = strlen(rpath);
            if (strncmp(m->path, rpath, rlen) != 0) continue;
           
            const char *after = m->path + rlen;
            if (rlen > 1) {
                if (after[0] != '/') continue;
                after++;
            }
           
            char component[256];
            const char *slash = strchr(after, '/');
            if (slash) {
                size_t clen = (size_t)(slash - after);
                if (clen == 0 || clen >= sizeof(component)) continue;
                memcpy(component, after, clen);
                component[clen] = '\0';
               
                int dup = 0;
                for (int j = 0; j < i; j++) {
                    vfs_mount_t *prev = vfs_mount_get(j);
                    if (!prev) continue;
                    const char *pa = prev->path + rlen;
                    if (rlen > 1) { if (pa[0] != '/') continue; pa++; }
                    if (strncmp(pa, component, clen) == 0 &&
                        (pa[clen] == '/' || pa[clen] == '\0')) { dup = 1; break; }
                }
                if (dup) continue;
            } else {
                strncpy(component, after, sizeof(component) - 1);
                component[sizeof(component) - 1] = '\0';
            }
            const char *pre = "[DIR]  ";
            for (int j = 0; pre[j] && pos + 1 < buf_size; j++) buf[pos++] = pre[j];
            for (int j = 0; component[j] && pos + 1 < buf_size; j++) buf[pos++] = component[j];
            if (pos + 1 < buf_size) buf[pos++] = '\n';
        }
        if (strcmp(rpath, "/") == 0) {
            char sock_paths[UNIX_SOCK_MAX][UNIX_PATH_MAX];
            uint32_t sock_count = unix_sock_list_paths(sock_paths, UNIX_SOCK_MAX);
            for (uint32_t i = 0; i < sock_count && pos + 64 < buf_size; i++) {
                const char *name = sock_paths[i][0] == '/' ? sock_paths[i] + 1 : sock_paths[i];
                const char *pre = "[SOCK] ";
                for (int j = 0; pre[j] && pos + 1 < buf_size; j++) buf[pos++] = pre[j];
                for (int j = 0; name[j] && pos + 1 < buf_size; j++) buf[pos++] = name[j];
                if (pos + 1 < buf_size) buf[pos++] = '\n';
            }
        }
        buf[pos] = '\0';
        return 0;
    }

    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->readdir) return -1;
    return m->ops->readdir(m->fs_data, rel, buf, buf_size);
}

int vfs_create(const char *path)
{
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->create) return -1;
    return m->ops->create(m->fs_data, rel);
}

int vfs_delete(const char *path)
{
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    if (unix_sock_unlink_path(rpath) == 0)
        return 0;
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->unlink) return -1;
    return m->ops->unlink(m->fs_data, rel);
}

int vfs_rename(const char *old_path, const char *new_path)
{
    char old_rpath[512];
    char new_rpath[512];
    resolve_path(old_path, old_rpath, sizeof(old_rpath));
    resolve_path(new_path, new_rpath, sizeof(new_rpath));
    if (unix_sock_path_exists(old_rpath) || unix_sock_path_exists(new_rpath))
        return -95;
    const char *old_rel = NULL;
    const char *new_rel = NULL;
    vfs_mount_t *old_m = vfs_mount_find(old_rpath, &old_rel);
    vfs_mount_t *new_m = vfs_mount_find(new_rpath, &new_rel);
    if (!old_m || !new_m)
        return -1;
    if (old_m != new_m)
        return -18;
    if (!old_m->ops->rename)
        return -95;
    return old_m->ops->rename(old_m->fs_data, old_rel, new_rel);
}

int vfs_mkdir(const char *path)
{
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->mkdir) return -1;
    return m->ops->mkdir(m->fs_data, rel);
}

int vfs_rmdir(const char *path)
{
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->rmdir) return -1;
    return m->ops->rmdir(m->fs_data, rel);
}

int vfs_stat(const char *path)
{
    if (!path) return -1;
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    if (is_parent_of_mount(rpath)) return 0;
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->stat) return -1;
    return m->ops->stat(m->fs_data, rel);
}

int vfs_statfs(const char *path, vfs_statfs_t *out)
{
    if (!path || !out) return -1;

    char rpath[512];
    resolve_path(path, rpath, sizeof(rpath));

    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m || !m->ops->statfs) return -1;
    return m->ops->statfs(m->fs_data, rel, out);
}

int vfs_open(const char *path, int write)
{
    char rpath[512]; resolve_path(path, rpath, sizeof(rpath));
    const char *rel = NULL;
    vfs_mount_t *m = vfs_mount_find(rpath, &rel);
    if (!m) return -1;
    fd_entry_t tmp;
    if (m->ops->open(m->fs_data, rel, write, &tmp) < 0) return -1;
    fatfs_data_t *fd = (fatfs_data_t *)m->fs_data;
    return fat_open_vol(rel, write, fd ? fd->vol : 0);
}

int      vfs_read (int fd, void *buf, uint32_t size, uint32_t *br) { return fat_read(fd, buf, size, br); }
int      vfs_close(int fd)                                          { return fat_close(fd); }
uint32_t vfs_size (int fd)                                          { return fat_size(fd); }
