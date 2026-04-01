#include "devfs.h"
#include "../keyboard.h"
#include "../vga.h"
#include "../../libk/core/syscall.h"
#include "../../libk/string.h"
#include "../../libk/core/mem.h"
#include "../../libk/debug/log.h"
#include "../../kernel/sched.h"

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

static int dev_console_read(void *buf, uint32_t size, uint32_t *got)
{
    if (!buf)
        return -1;
    if (!size)
    {
        if (got) *got = 0;
        return 0;
    }

    char *out = (char *)buf;
    uint32_t total = 0;
    while (total < size)
    {
        char c = 0;
        while (!c)
        {
            c = get_key();
            if (!c)
                sched_yield();
        }
        out[total++] = c;
        if (kbd_pending_chars() == 0)
            break;
    }

    if (got) *got = total;
    return 0;
}

static int dev_console_write(const void *buf, uint32_t size)
{
    const char *str = (const char *)buf;
    if (!str)
        return -1;
    for (uint32_t i = 0; i < size; i++)
        printc(str[i]);
    return 0;
}

static int dev_console_ioctl(unsigned long req, void *argp)
{
    if (req == ZEN_TCGETS)
    {
        if (!argp)
            return -1;
        zen_termios_t *tio = (zen_termios_t *)argp;
        memset(tio, 0, sizeof(*tio));
        tio->c_iflag = ZEN_ICRNL | ZEN_IXON;
        tio->c_oflag = ZEN_OPOST | ZEN_ONLCR;
        tio->c_cflag = ZEN_CS8 | ZEN_CREAD;
        tio->c_lflag = ZEN_ISIG | ZEN_ICANON | ZEN_ECHO | ZEN_ECHOE | ZEN_ECHOK | ZEN_IEXTEN;
        tio->c_cc[ZEN_VINTR] = 3;
        tio->c_cc[ZEN_VQUIT] = 28;
        tio->c_cc[ZEN_VERASE] = 127;
        tio->c_cc[ZEN_VKILL] = 21;
        tio->c_cc[ZEN_VEOF] = 4;
        tio->c_cc[ZEN_VTIME] = 0;
        tio->c_cc[ZEN_VMIN] = 1;
        tio->c_cc[ZEN_VSTART] = 17;
        tio->c_cc[ZEN_VSTOP] = 19;
        tio->c_cc[ZEN_VSUSP] = 26;
        return 0;
    }
    if (req == ZEN_TCSETS || req == ZEN_TCSETSW || req == ZEN_TCSETSF)
        return 0;
    if (req == ZEN_TIOCGWINSZ)
    {
        if (!argp)
            return -1;
        zen_winsize_t *ws = (zen_winsize_t *)argp;
        ws->ws_col = framebuffer_width ? (uint16_t)(framebuffer_width / 8) : 80;
        ws->ws_row = framebuffer_height ? (uint16_t)(framebuffer_height / 16) : 25;
        ws->ws_xpixel = (uint16_t)framebuffer_width;
        ws->ws_ypixel = (uint16_t)framebuffer_height;
        return 0;
    }
    if (req == ZEN_FIONREAD)
    {
        if (!argp)
            return -1;
        *(int *)argp = (int)kbd_pending_chars();
        return 0;
    }
    return -1;
}

void devfs_init(void)
{
    dev_count = 0;
    devfs_register("null",    dev_null_read,    dev_null_write, NULL);
    devfs_register("zero",    dev_zero_read,    dev_null_write, NULL);
    devfs_register("urandom", dev_urandom_read, dev_null_write, NULL);
    devfs_register("random",  dev_urandom_read, dev_null_write, NULL);
    devfs_register("tty",     dev_console_read, dev_console_write, dev_console_ioctl);
    devfs_register("console", dev_console_read, dev_console_write, dev_console_ioctl);
    log("devfs: %d devices registered.", 4, 0, dev_count);
}

int devfs_register(const char *name,
                   int (*read)(void *buf, uint32_t size, uint32_t *got),
                   int (*write)(const void *buf, uint32_t size),
                   int (*ioctl)(unsigned long req, void *argp))
{
    if (!name || dev_count >= DEVFS_MAX_DEVS) return -1;
    dev_table[dev_count].name  = name;
    dev_table[dev_count].read  = read;
    dev_table[dev_count].write = write;
    dev_table[dev_count].ioctl = ioctl;
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

static const char *devfs_skip_slashes(const char *path)
{
    while (*path == '/')
        path++;
    return path;
}

static int devfs_is_dir(const char *path)
{
    path = devfs_skip_slashes(path);
    if (*path == '\0')
        return 1;

    size_t plen = strlen(path);
    for (int i = 0; i < dev_count; i++)
    {
        const char *name = dev_table[i].name;
        if (strncmp(name, path, plen) == 0 && name[plen] == '/')
            return 1;
    }
    return 0;
}

static int devfs_open(void *fs_data, const char *path, int write, fd_entry_t *out)
{
    (void)fs_data; (void)write;
    const char *name = devfs_skip_slashes(path);
    if (*name == '\0' || devfs_is_dir(name))
        return -1;

    dev_entry_t *d = devfs_find(name);
    if (!d) return -1;

    out->type    = FD_DEV;
    out->used    = 1;
    out->dev_ops = d;
    return 0;
}

static int devfs_readdir(void *fs_data, const char *path, char *buf, size_t bufsz)
{
    (void)fs_data;
    const char *dir = devfs_skip_slashes(path);
    if (!devfs_is_dir(dir))
        return -1;

    size_t pos = 0;
    const char *hdr = "Directory: /dev";
    for (int i = 0; hdr[i] && pos + 1 < bufsz; i++) buf[pos++] = hdr[i];
    if (*dir)
    {
        if (pos + 1 < bufsz) buf[pos++] = '/';
        for (int i = 0; dir[i] && pos + 1 < bufsz; i++) buf[pos++] = dir[i];
    }
    if (pos + 1 < bufsz) buf[pos++] = '\n';

    size_t dlen = strlen(dir);
    for (int i = 0; i < dev_count && pos + 64 < bufsz; i++) {
        const char *name = dev_table[i].name;
        const char *child = name;
        if (dlen)
        {
            if (strncmp(name, dir, dlen) != 0 || name[dlen] != '/')
                continue;
            child = name + dlen + 1;
        }

        size_t clen = 0;
        while (child[clen] && child[clen] != '/')
            clen++;
        if (clen == 0)
            continue;

        int is_dir = child[clen] == '/';
        int duplicate = 0;
        for (int j = 0; j < i; j++)
        {
            const char *prev = dev_table[j].name;
            const char *prev_child = prev;
            if (dlen)
            {
                if (strncmp(prev, dir, dlen) != 0 || prev[dlen] != '/')
                    continue;
                prev_child = prev + dlen + 1;
            }

            if (strncmp(prev_child, child, clen) == 0 &&
                (prev_child[clen] == '\0' || prev_child[clen] == '/'))
            {
                duplicate = 1;
                break;
            }
        }
        if (duplicate)
            continue;

        const char *pre = is_dir ? "[DIR]  " : "       ";
        for (int j = 0; pre[j] && pos + 1 < bufsz; j++) buf[pos++] = pre[j];
        for (size_t j = 0; j < clen && pos + 1 < bufsz; j++) buf[pos++] = child[j];
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return 0;
}

static int devfs_stat(void *fs_data, const char *path)
{
    (void)fs_data;
    const char *name = devfs_skip_slashes(path);
    if (*name == '\0') return 0;
    if (devfs_is_dir(name)) return 0;
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
