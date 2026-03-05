#include "devfs.h"
#include "../../libk/string.h"
#include "../../libk/core/mem.h"
#include "../../libk/debug/log.h"

#define DEVFS_MAX_DEVS 32

static dev_entry_t  dev_table[DEVFS_MAX_DEVS];
static int          dev_count = 0;

static int dev_null_read(void *buf, uint32_t size, uint32_t *got)
{
    (void)buf; (void)size;
    if (got) *got = 0;
    return 0;
}

static int dev_null_write(const void *buf, uint32_t size)
{
    (void)buf; (void)size;
    return 0;
}

static int dev_zero_read(void *buf, uint32_t size, uint32_t *got)
{
    for (uint32_t i = 0; i < size; i++) ((uint8_t *)buf)[i] = 0;
    if (got) *got = size;
    return 0;
}

static int dev_urandom_read(void *buf, uint32_t size, uint32_t *got)
{
    static uint64_t seed = 0xdeadbeefcafe1234ULL;
    uint8_t *b = (uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        b[i] = (uint8_t)(seed & 0xFF);
    }
    if (got) *got = size;
    return 0;
}

void devfs_init(void)
{
    dev_count = 0;
    devfs_register("null",    dev_null_read,    dev_null_write);
    devfs_register("zero",    dev_zero_read,    dev_null_write);
    devfs_register("urandom", dev_urandom_read, dev_null_write);
    devfs_register("random",  dev_urandom_read, dev_null_write);
    log("devfs: %d devices registered.", 4, 0, dev_count);
}

int devfs_register(const char *name,
                   int (*read)(void *buf, uint32_t size, uint32_t *got),
                   int (*write)(const void *buf, uint32_t size))
{
    if (!name || dev_count >= DEVFS_MAX_DEVS) return -1;
    dev_table[dev_count].name  = name;
    dev_table[dev_count].read  = read;
    dev_table[dev_count].write = write;
    dev_count++;
    return 0;
}

static dev_entry_t *devfs_find(const char *name)
{
    for (int i = 0; i < dev_count; i++)
        if (strcmp(dev_table[i].name, name) == 0)
            return &dev_table[i];
    return NULL;
}

typedef struct { dev_entry_t *dev; } devfs_fd_t;

static int devfs_open(void *fs_data, const char *path, int write, fd_entry_t *out)
{
    (void)fs_data; (void)write;
    const char *name = path;
    while (*name == '/') name++;

    dev_entry_t *d = devfs_find(name);
    if (!d) return -1;

    out->type    = FD_DEV;
    out->used    = 1;
    out->dev_ops = d;
    return 0;
}

static int devfs_readdir(void *fs_data, const char *path, char *buf, size_t bufsz)
{
    (void)fs_data; (void)path;
    size_t pos = 0;
    const char *hdr = "Directory: /dev\n";
    for (int i = 0; hdr[i] && pos + 1 < bufsz; i++) buf[pos++] = hdr[i];
    for (int i = 0; i < dev_count && pos + 64 < bufsz; i++) {
        const char *pre = "       ";
        for (int j = 0; pre[j] && pos + 1 < bufsz; j++) buf[pos++] = pre[j];
        for (int j = 0; dev_table[i].name[j] && pos + 1 < bufsz; j++) buf[pos++] = dev_table[i].name[j];
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return 0;
}

static int devfs_stat(void *fs_data, const char *path)
{
    (void)fs_data;
    const char *name = path;
    while (*name == '/') name++;
    if (*name == '\0') return 0;
    return devfs_find(name) ? 0 : -1;
}

static int devfs_notsup(void *fs_data, const char *path)
{
    (void)fs_data; (void)path; return -1;
}

static int devfs_notsup3(void *fs_data, const char *path)
{
    return devfs_notsup(fs_data, path);
}

static fs_ops_t devfs_ops = {
    .open    = devfs_open,
    .readdir = devfs_readdir,
    .mkdir   = devfs_notsup3,
    .rmdir   = devfs_notsup3,
    .unlink  = devfs_notsup3,
    .stat    = devfs_stat,
    .create  = devfs_notsup3,
};

fs_ops_t *devfs_get_ops(void) { return &devfs_ops; }
