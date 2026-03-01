#include "fat.h"
#include "fatfs/ff.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"
#include "../../libk/core/mem.h"

static FATFS fs;
static uint8_t initialized = 0;
static uint8_t fat_drive = 0;

typedef struct
{
    FIL fil;
    int used;
    int writable;
    uint32_t total_written;
} fat_fd_entry_t;

static fat_fd_entry_t fd_table[FAT_MAX_FDS];

static int alloc_fd(void)
{

    for (int i = 3; i < FAT_MAX_FDS; i++)
        if (!fd_table[i].used)
            return i;
    return -1;
}

static void make_fatpath(const char *path, char *out, size_t outsz)
{
    if (path[0] == '/')
    {
        snprintf(out, outsz, "0:%s", path);
    }
    else
    {
        snprintf(out, outsz, "%s", path);
    }
}

// static fat_error_t fresult_to_fat(FRESULT fr)
// {
//     switch (fr)
//     {
//     case FR_OK:
//         return FAT_OK;
//     case FR_NO_FILE:
//     case FR_NO_PATH:
//         return FAT_ERR_NOT_FOUND;
//     case FR_EXIST:
//         return FAT_ERR_ALREADY_EXISTS;
//     case FR_NOT_ENOUGH_CORE:
//     case FR_TOO_MANY_OPEN_FILES:
//         return FAT_ERR_NO_SPACE;
//     case FR_INVALID_NAME:
//     case FR_INVALID_PARAMETER:
//         return FAT_ERR_INVALID_PARAM;
//     case FR_DENIED:
//         return FAT_ERR_NOT_EMPTY;
//     default:
//         return FAT_ERR_IO;
//     }
// }

fat_error_t fat_init(uint8_t drive)
{
    fat_drive = drive;
    char volpath[4] = "0:";
    FRESULT fr = f_mount(&fs, volpath, 1);
    if (fr != FR_OK)
    {
        log("f_mount failed (%d).", 3, 0, fr);
        return FAT_ERR_IO;
    }
    initialized = 1;
    log("Mounted drive %d.", 4, 0, drive);
    return FAT_OK;
}

fat_error_t fat_format(uint8_t drive)
{
    fat_drive = drive;
    log("Formatting drive %d ...", 1, 0, drive);

    uint8_t *work = (uint8_t *)kmalloc(FF_MAX_SS);
    if (!work)
        return FAT_ERR_IO;

    MKFS_PARM opt = {
        .fmt = FM_FAT32,
        .n_fat = 2,
        .align = 0,
        .n_root = 0,
        .au_size = 4096,
    };

    FRESULT fr = f_mkfs("0:", &opt, work, FF_MAX_SS);
    kfree(work);
    if (fr != FR_OK)
    {
        log("f_mkfs failed (%d).", 3, 0, fr);
        return FAT_ERR_IO;
    }

    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        log("f_mount after format failed (%d).", 3, 0, fr);
        return FAT_ERR_IO;
    }
    initialized = 1;
    log("Formatted and mounted.", 4, 0);
    return FAT_OK;
}

int fat_open(const char *path, int write)
{
    if (!initialized)
        return -1;
    if (!path)
        return -1;

    int fd = alloc_fd();
    if (fd < 0)
    {
        log("No free FDs.", 3, 0);
        return -1;
    }

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    BYTE mode;
    if (write == 2)
        mode = FA_CREATE_ALWAYS | FA_WRITE | FA_READ;
    else if (write == 1)
        mode = FA_OPEN_ALWAYS | FA_WRITE | FA_READ;
    else
        mode = FA_OPEN_EXISTING | FA_READ;

    FRESULT fr = f_open(&fd_table[fd].fil, fpath, mode);
    if (fr != FR_OK)
    {
        if (fr != FR_NO_FILE && fr != FR_NO_PATH)
            log("Opening '%s' failed (%d).", 2, 0, path, fr);
        fd_table[fd].used = 0;
        return -1;
    }

    fd_table[fd].used = 1;
    fd_table[fd].writable = write;
    return fd;
}

int fat_read(int fd, void *buf, uint32_t size, uint32_t *bytes_read)
{
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return -1;

    UINT br = 0;
    FRESULT fr = f_read(&fd_table[fd].fil, buf, size, &br);
    if (bytes_read)
        *bytes_read = br;
    return (fr == FR_OK) ? 0 : -1;
}

int fat_write(int fd, const void *buf, uint32_t size)
{
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return -1;
    if (!fd_table[fd].writable)
        return -1;

    UINT bw = 0;
    uint32_t fpos_before = (uint32_t)f_tell(&fd_table[fd].fil);
    FRESULT fr = f_write(&fd_table[fd].fil, buf, size, &bw);
    if (fr != FR_OK || bw != size)
    {
        log("Writing fd=%d size=%d bw=%d fr=%d failed.", 2, 0, fd, size, bw, fr);
        return -1;
    }
    fd_table[fd].total_written += bw;
    return 0;
}

int fat_close(int fd)
{
    if (fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return -1;
    f_close(&fd_table[fd].fil);
    fd_table[fd].used = 0;
    fd_table[fd].total_written = 0;
    return 0;
}

int fat_seek(int fd, uint32_t pos)
{
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return -1;
    FRESULT fr = f_lseek(&fd_table[fd].fil, pos);
    return (fr == FR_OK) ? 0 : -1;
}

int fat_lseek(int fd, int32_t offset, int whence)
{
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return -1;
    uint32_t newpos;
    if (whence == 0)
        newpos = (uint32_t)offset;
    else if (whence == 1)
        newpos = (uint32_t)((int32_t)f_tell(&fd_table[fd].fil) + offset);
    else
        newpos = (uint32_t)((int32_t)f_size(&fd_table[fd].fil) + offset);
    FRESULT fr = f_lseek(&fd_table[fd].fil, newpos);
    return (fr == FR_OK) ? (int)newpos : -1;
}

uint32_t fat_size(int fd)
{
    if (fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used)
        return 0;
    return (uint32_t)f_size(&fd_table[fd].fil);
}

int fat_create(const char *path)
{
    if (!initialized)
        return -1;

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    FIL fil;
    FRESULT fr = f_open(&fil, fpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
        return -1;
    f_close(&fil);
    return 0;
}

int fat_delete(const char *path)
{
    if (!initialized)
        return -1;

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    FRESULT fr = f_unlink(fpath);
    return (fr == FR_OK) ? 0 : -1;
}

int fat_mkdir(const char *path)
{
    if (!initialized)
        return -1;

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    FRESULT fr = f_mkdir(fpath);

    return (fr == FR_OK || fr == FR_EXIST) ? 0 : -1;
}

int fat_rmdir(const char *path)
{
    if (!initialized)
        return -1;

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    FRESULT fr = f_unlink(fpath);
    return (fr == FR_OK) ? 0 : -1;
}

int fat_chdir(const char *path)
{
    if (!initialized)
        return -1;

    char fpath[FAT_MAX_PATH];
    make_fatpath(path, fpath, sizeof(fpath));

    FRESULT fr = f_chdir(fpath);
    return (fr == FR_OK) ? 0 : -1;
}

void fat_getcwd(char *buf, size_t size)
{
    if (!initialized || !buf || size == 0)
    {
        if (buf && size > 0)
            buf[0] = '\0';
        return;
    }
    char tmp[FAT_MAX_PATH];
    FRESULT fr = f_getcwd(tmp, sizeof(tmp));
    if (fr != FR_OK)
    {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }

    char *start = tmp;
    if (tmp[0] == '0' && tmp[1] == ':')
        start = tmp + 2;
    if (start[0] == '\0')
    {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }
    strncpy(buf, start, size - 1);
    buf[size - 1] = '\0';
}

int fat_list(char *buf, size_t buf_size)
{
    if (!initialized || !buf || buf_size == 0)
        return -1;

    char cwd[FAT_MAX_PATH];
    fat_getcwd(cwd, sizeof(cwd));

    size_t pos = 0;

    const char *hdr = "Directory: ";
    for (int i = 0; hdr[i] && pos + 1 < buf_size; i++)
        buf[pos++] = hdr[i];
    for (int i = 0; cwd[i] && pos + 1 < buf_size; i++)
        buf[pos++] = cwd[i];
    if (pos + 1 < buf_size)
        buf[pos++] = '\n';

    DIR dir;
    char dirpath[FAT_MAX_PATH];

    char tmp[FAT_MAX_PATH];
    f_getcwd(tmp, sizeof(tmp));
    FRESULT fr = f_opendir(&dir, tmp);
    if (fr != FR_OK)
    {
        const char *err = "(error opening dir)\n";
        for (int i = 0; err[i] && pos + 1 < buf_size; i++)
            buf[pos++] = err[i];
        buf[pos] = '\0';
        return -1;
    }

    int count = 0;
    FILINFO fno;
    while (1)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0')
            break;

        int is_dir = (fno.fattrib & AM_DIR) != 0;
        const char *prefix = is_dir ? "[DIR]  " : "       ";
        for (int i = 0; prefix[i] && pos + 1 < buf_size; i++)
            buf[pos++] = prefix[i];
        for (int i = 0; fno.fname[i] && pos + 1 < buf_size; i++)
            buf[pos++] = fno.fname[i];

        if (!is_dir)
        {

            char szbuf[16];
            uint32_t sz = (uint32_t)fno.fsize;
            int si = 0;
            if (sz == 0)
            {
                szbuf[si++] = '0';
            }
            else
            {
                char tmp2[12];
                int ti = 0;
                while (sz)
                {
                    tmp2[ti++] = '0' + sz % 10;
                    sz /= 10;
                }
                for (int x = ti - 1; x >= 0; x--)
                    szbuf[si++] = tmp2[x];
            }
            szbuf[si] = '\0';
            const char *pre2 = "  (";
            for (int i = 0; pre2[i] && pos + 1 < buf_size; i++)
                buf[pos++] = pre2[i];
            for (int i = 0; szbuf[i] && pos + 1 < buf_size; i++)
                buf[pos++] = szbuf[i];
            const char *suf = "B)";
            for (int i = 0; suf[i] && pos + 1 < buf_size; i++)
                buf[pos++] = suf[i];
        }
        if (pos + 1 < buf_size)
            buf[pos++] = '\n';
        count++;
    }
    f_closedir(&dir);
    (void)dirpath;

    if (count == 0 && pos + 8 < buf_size)
    {
        const char *empty = "(empty)\n";
        for (int i = 0; empty[i] && pos + 1 < buf_size; i++)
            buf[pos++] = empty[i];
    }

    buf[pos] = '\0';
    return 0;
}

void fat_print_stats(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return;
    if (!initialized)
    {
        strncpy(buf, "FAT: Not initialized.\n", buf_size - 1);
        buf[buf_size - 1] = '\0';
        return;
    }
    DWORD free_clust;
    FATFS *fsp;
    FRESULT fr = f_getfree("0:", &free_clust, &fsp);
    if (fr != FR_OK)
    {
        strncpy(buf, "FAT: Stat error.\n", buf_size - 1);
        buf[buf_size - 1] = '\0';
        return;
    }
    DWORD total = (fsp->n_fatent - 2) * fsp->csize / 2;
    DWORD free_kb = free_clust * fsp->csize / 2;
    snprintf(buf, buf_size,
             "FAT32 stats:\n"
             "  Total: %lu KB\n"
             "  Free:  %lu KB\n"
             "  Used:  %lu KB\n",
             (unsigned long)total,
             (unsigned long)free_kb,
             (unsigned long)(total - free_kb));
}
