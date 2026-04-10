/**
 * 
 * @file : /src/drv/disk/fat.c
 * @brief : FAT32 filesystem integration to FatFs with VFS integration and caching.
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

#include "fat.h"
#include "fatfs/ff.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"
#include "../../libk/core/mem.h"
#include "../../libk/spinlock.h"
#include "../../drv/rtc.h"

DWORD get_fattime(void) {
    rtc_time_t t = rtc_get_time();
    int year = 2000 + t.year;
    return ((DWORD)(year - 1980) << 25)
         | ((DWORD)t.month       << 21)
         | ((DWORD)t.day         << 16)
         | ((DWORD)t.hours       << 11)
         | ((DWORD)t.minutes     <<  5)
         | ((DWORD)(t.seconds / 2));
}

#define FAT_MAX_VOLS 4

static FATFS   fs_table[FAT_MAX_VOLS];
static uint8_t vol_mounted[FAT_MAX_VOLS];
static uint8_t initialized = 0;
static uint8_t fat_drive   = 0;
static spinlock_t fat_fs_lock = {0};

typedef struct {
    FIL      fil;
    int      used;
    int      writable;
    uint32_t total_written;
} fat_fd_entry_t;

static fat_fd_entry_t fd_table[FAT_MAX_FDS];

static int alloc_fd(void) {
    for (int i = 3; i < FAT_MAX_FDS; i++)
        if (!fd_table[i].used) return i;
    return -1;
}

void make_fatpath_vol(const char *path, int vol, char *out, size_t outsz) {
    if (path[0] == '/')
        snprintf(out, outsz, "%d:%s", vol, path);
    else
        snprintf(out, outsz, "%s", path);
}

uint8_t fat_get_drive(void)    { return fat_drive; }
uint8_t fat_is_initialized(void) { return initialized; }
void fat_lock(void) { spinlock_acquire_raw(&fat_fs_lock); }
void fat_unlock(void) { spinlock_release_raw(&fat_fs_lock); }

fat_error_t fat_init(uint8_t drive) {
    spinlock_init(&fat_fs_lock);
    fat_drive = drive;
    char volpath[4];
    snprintf(volpath, sizeof(volpath), "%d:", 0);
    fat_lock();
    FRESULT fr = f_mount(&fs_table[0], volpath, 1);
    fat_unlock();
    if (fr != FR_OK) {
        log("f_mount failed (%d).", 3, 0, fr);
        return FAT_ERR_IO;
    }
    vol_mounted[0] = 1;
    initialized = 1;
    log("Mounted drive %d.", 4, 0, drive);
    return FAT_OK;
}

fat_error_t fat_mount_vol(int slot) {
    if (slot <= 0 || slot >= FAT_MAX_VOLS) return FAT_ERR_INVALID_PARAM;
    if (vol_mounted[slot]) return FAT_OK;
    char volpath[4];
    snprintf(volpath, sizeof(volpath), "%d:", slot);
    fat_lock();
    FRESULT fr = f_mount(&fs_table[slot], volpath, 1);
    fat_unlock();
    if (fr != FR_OK) {
        log("f_mount vol %d failed (%d).", 3, 0, slot, fr);
        return FAT_ERR_IO;
    }
    vol_mounted[slot] = 1;
    log("Mounted volume %d.", 4, 0, slot);
    return FAT_OK;
}

fat_error_t fat_format(uint8_t drive) {
    spinlock_init(&fat_fs_lock);
    fat_drive = drive;
    log("Formatting drive %d ...", 1, 0, drive);
    uint8_t *work = (uint8_t *)kmalloc(FF_MAX_SS);
    if (!work) return FAT_ERR_IO;
    MKFS_PARM opt = { .fmt = FM_FAT32, .n_fat = 2, .align = 0, .n_root = 0, .au_size = 4096 };
    fat_lock();
    FRESULT fr = f_mkfs("0:", &opt, work, FF_MAX_SS);
    fat_unlock();
    kfree(work);
    if (fr != FR_OK) { log("f_mkfs failed (%d).", 3, 0, fr); return FAT_ERR_IO; }
    fat_lock();
    fr = f_mount(&fs_table[0], "0:", 1);
    fat_unlock();
    if (fr != FR_OK) { log("f_mount after format failed (%d).", 3, 0, fr); return FAT_ERR_IO; }
    vol_mounted[0] = 1;
    initialized = 1;
    log("Formatted and mounted.", 4, 0);
    return FAT_OK;
}

int fat_open_vol(const char *path, int write, int vol) {
    if (!initialized || !path) return -1;
    fat_lock();
    int fd = alloc_fd();
    if (fd < 0) { fat_unlock(); log("No free FDs.", 3, 0); return -1; }
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    BYTE mode;
    if (write == 2)      mode = FA_CREATE_ALWAYS | FA_WRITE | FA_READ;
    else if (write == 1) mode = FA_OPEN_ALWAYS   | FA_WRITE | FA_READ;
    else                 mode = FA_OPEN_EXISTING | FA_READ;
    FRESULT fr = f_open(&fd_table[fd].fil, fpath, mode);
    if (fr != FR_OK) {
        if (fr != FR_NO_FILE && fr != FR_NO_PATH)
            log("Opening '%s' vol %d failed (%d).", 2, 0, path, vol, fr);
        fd_table[fd].used = 0;
        fat_unlock();
        return -1;
    }
    fd_table[fd].used = 1;
    fd_table[fd].writable = write;
    fat_unlock();
    return fd;
}

int fat_open(const char *path, int write) { return fat_open_vol(path, write, 0); }

int fat_read(int fd, void *buf, uint32_t size, uint32_t *bytes_read) {
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return -1;
    UINT br = 0;
    fat_lock();
    FRESULT fr = f_read(&fd_table[fd].fil, buf, size, &br);
    fat_unlock();
    if (bytes_read) *bytes_read = br;
    return (fr == FR_OK) ? 0 : -1;
}

int fat_write(int fd, const void *buf, uint32_t size) {
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return -1;
    if (!fd_table[fd].writable) return -1;
    UINT bw = 0;
    fat_lock();
    FRESULT fr = f_write(&fd_table[fd].fil, buf, size, &bw);
    fat_unlock();
    if (fr != FR_OK || bw != size) { log("Write fd=%d failed.", 2, 0, fd); return -1; }
    fd_table[fd].total_written += bw;
    return 0;
}

int fat_close(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return -1;
    fat_lock();
    f_close(&fd_table[fd].fil);
    fat_unlock();
    fd_table[fd].used = 0;
    fd_table[fd].total_written = 0;
    return 0;
}

int fat_seek(int fd, uint32_t pos) {
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return -1;
    fat_lock();
    FRESULT fr = f_lseek(&fd_table[fd].fil, pos);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_lseek(int fd, int32_t offset, int whence) {
    if (!initialized || fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return -1;
    uint32_t newpos;
    fat_lock();
    if (whence == 0)      newpos = (uint32_t)offset;
    else if (whence == 1) newpos = (uint32_t)((int32_t)f_tell(&fd_table[fd].fil) + offset);
    else                  newpos = (uint32_t)((int32_t)f_size(&fd_table[fd].fil) + offset);
    FRESULT fr = f_lseek(&fd_table[fd].fil, newpos);
    fat_unlock();
    return (fr == FR_OK) ? (int)newpos : -1;
}

uint32_t fat_size(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FDS || !fd_table[fd].used) return 0;
    fat_lock();
    uint32_t size = (uint32_t)f_size(&fd_table[fd].fil);
    fat_unlock();
    return size;
}

int fat_create_vol(const char *path, int vol) {
    if (!initialized) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    FIL fil;
    fat_lock();
    FRESULT fr = f_open(&fil, fpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        fat_unlock();
        return -1;
    }
    f_close(&fil);
    fat_unlock();
    return 0;
}

int fat_create(const char *path) { return fat_create_vol(path, 0); }

int fat_delete_vol(const char *path, int vol) {
    if (!initialized) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    fat_lock();
    FRESULT fr = f_unlink(fpath);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_delete(const char *path) { return fat_delete_vol(path, 0); }

int fat_rename_vol(const char *old_path, const char *new_path, int vol) {
    if (!initialized) return -1;
    char old_fpath[FAT_MAX_PATH];
    char new_fpath[FAT_MAX_PATH];
    make_fatpath_vol(old_path, vol, old_fpath, sizeof(old_fpath));
    make_fatpath_vol(new_path, vol, new_fpath, sizeof(new_fpath));
    fat_lock();
    FRESULT fr = f_rename(old_fpath, new_fpath);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_rename(const char *old_path, const char *new_path) { return fat_rename_vol(old_path, new_path, 0); }

int fat_mkdir_vol(const char *path, int vol) {
    if (!initialized) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    fat_lock();
    FRESULT fr = f_mkdir(fpath);
    fat_unlock();
    return (fr == FR_OK || fr == FR_EXIST) ? 0 : -1;
}

int fat_mkdir(const char *path) { return fat_mkdir_vol(path, 0); }

int fat_rmdir_vol(const char *path, int vol) {
    if (!initialized) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    fat_lock();
    FRESULT fr = f_unlink(fpath);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_rmdir(const char *path) { return fat_rmdir_vol(path, 0); }

int fat_chdir_vol(const char *path, int vol) {
    if (!initialized) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    fat_lock();
    FRESULT fr = f_chdir(fpath);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_chdir(const char *path) { return fat_chdir_vol(path, 0); }

void fat_getcwd_vol(char *buf, size_t size, int vol) {
    if (!initialized || !buf || size == 0) { if (buf && size > 0) buf[0] = '\0'; return; }
    char tmp[FAT_MAX_PATH];
    char volpath[4];
    snprintf(volpath, sizeof(volpath), "%d:", vol);
    fat_lock();
    if (f_chdrive(volpath) != FR_OK) { fat_unlock(); buf[0] = '/'; buf[1] = '\0'; return; }
    FRESULT fr = f_getcwd(tmp, sizeof(tmp));
    fat_unlock();
    if (fr != FR_OK) { buf[0] = '/'; buf[1] = '\0'; return; }
    char *start = tmp;
    if (tmp[1] == ':') start = tmp + 2;
    if (start[0] == '\0') { buf[0] = '/'; buf[1] = '\0'; return; }
    strncpy(buf, start, size - 1);
    buf[size - 1] = '\0';
}

void fat_getcwd(char *buf, size_t size) { fat_getcwd_vol(buf, size, 0); }

int fat_list_vol(char *buf, size_t buf_size, int vol) {
    if (!initialized || !buf || buf_size == 0) return -1;
    char tmp[FAT_MAX_PATH];
    char volpath[4];
    snprintf(volpath, sizeof(volpath), "%d:", vol);
    fat_lock();
    if (f_chdrive(volpath) != FR_OK) {
        fat_unlock();
        return -1;
    }
    f_getcwd(tmp, sizeof(tmp));

    size_t pos = 0;
    char *start = tmp;
    if (tmp[1] == ':') start = tmp + 2;
    if (start[0] == '\0') { tmp[0] = '/'; tmp[1] = '\0'; start = tmp; }

    const char *hdr = "Directory: ";
    for (int i = 0; hdr[i] && pos + 1 < buf_size; i++) buf[pos++] = hdr[i];
    for (int i = 0; start[i] && pos + 1 < buf_size; i++) buf[pos++] = start[i];
    if (pos + 1 < buf_size) buf[pos++] = '\n';

    DIR dir;
    FRESULT fr = f_opendir(&dir, tmp);
    if (fr != FR_OK) { fat_unlock(); buf[pos] = '\0'; return -1; }

    int count = 0;
    FILINFO fno;
    while (1) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;
        int is_dir = (fno.fattrib & AM_DIR) != 0;
        const char *prefix = is_dir ? "[DIR]  " : "       ";
        for (int i = 0; prefix[i] && pos + 1 < buf_size; i++) buf[pos++] = prefix[i];
        for (int i = 0; fno.fname[i] && pos + 1 < buf_size; i++) buf[pos++] = fno.fname[i];
        if (!is_dir) {
            char szbuf[16]; uint32_t sz = (uint32_t)fno.fsize; int si = 0;
            if (sz == 0) { szbuf[si++] = '0'; }
            else { char t2[12]; int ti = 0; while (sz) { t2[ti++] = '0' + sz % 10; sz /= 10; } for (int x = ti-1; x >= 0; x--) szbuf[si++] = t2[x]; }
            szbuf[si] = '\0';
            const char *pre2 = "  (";
            for (int i = 0; pre2[i] && pos + 1 < buf_size; i++) buf[pos++] = pre2[i];
            for (int i = 0; szbuf[i] && pos + 1 < buf_size; i++) buf[pos++] = szbuf[i];
            const char *suf = "B)";
            for (int i = 0; suf[i] && pos + 1 < buf_size; i++) buf[pos++] = suf[i];
        }
        if (pos + 1 < buf_size) buf[pos++] = '\n';
        count++;
    }
    f_closedir(&dir);
    fat_unlock();
    if (count == 0 && pos + 8 < buf_size) {
        const char *empty = "(empty)\n";
        for (int i = 0; empty[i] && pos + 1 < buf_size; i++) buf[pos++] = empty[i];
    }
    buf[pos] = '\0';
    return 0;
}

int fat_list(char *buf, size_t buf_size) { return fat_list_vol(buf, buf_size, 0); }

void fat_print_stats(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    if (!initialized) { strncpy(buf, "FAT: Not initialized.\n", buf_size - 1); buf[buf_size-1] = '\0'; return; }
    DWORD free_clust; FATFS *fsp;
    fat_lock();
    FRESULT fr = f_getfree("0:", &free_clust, &fsp);
    fat_unlock();
    if (fr != FR_OK) { strncpy(buf, "FAT: Stat error.\n", buf_size - 1); buf[buf_size-1] = '\0'; return; }
    DWORD total = (fsp->n_fatent - 2) * fsp->csize / 2;
    DWORD free_kb = free_clust * fsp->csize / 2;
    snprintf(buf, buf_size, "FAT32 stats:\n  Total: %lu KB\n  Free:  %lu KB\n  Used:  %lu KB\n",
             (unsigned long)total, (unsigned long)free_kb, (unsigned long)(total - free_kb));
}

int fat_open_entry_vol(const char *path, int write, fd_entry_t *out, int vol) {
    if (!initialized || !path || !out) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    BYTE mode;
    if (write == 2)      mode = FA_CREATE_ALWAYS | FA_WRITE | FA_READ;
    else if (write == 1) mode = FA_OPEN_ALWAYS   | FA_WRITE | FA_READ;
    else                 mode = FA_OPEN_EXISTING | FA_READ;
    fd_file_t *file = (fd_file_t *)kmalloc(sizeof(fd_file_t));
    if (!file) return -1;
    memset(file, 0, sizeof(*file));
    fat_lock();
    FRESULT fr = f_open(&file->fil, fpath, mode);
    if (fr != FR_OK) {
        fat_unlock();
        kfree(file);
        return -1;
    }
    out->type = FD_FILE; out->used = 1;
    out->file = file;
    out->file->writable = write; out->file->total_written = 0; out->file->refcount = 1;
   
    FILINFO fno;
    out->file->fdate = (f_stat(fpath, &fno) == FR_OK) ? fno.fdate : 0;
    out->file->ftime = (f_stat(fpath, &fno) == FR_OK) ? fno.ftime : 0;
    fat_unlock();
    return 0;
}

int fat_open_entry(const char *path, int write, fd_entry_t *out) { return fat_open_entry_vol(path, write, out, 0); }

int fat_read_entry(fd_entry_t *e, void *buf, uint32_t size, uint32_t *bytes_read) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    UINT br = 0;
    fat_lock();
    FRESULT fr = f_read(&e->file->fil, buf, size, &br);
    fat_unlock();
    if (bytes_read) *bytes_read = br;
    return (fr == FR_OK) ? 0 : -1;
}

int fat_write_entry(fd_entry_t *e, const void *buf, uint32_t size) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    if (!e->file->writable) return -1;
    UINT bw = 0;
    fat_lock();
    FRESULT fr = f_write(&e->file->fil, buf, size, &bw);
    fat_unlock();
    if (fr != FR_OK || bw != size) return -1;
    e->file->total_written += bw;
    return 0;
}

int fat_close_entry(fd_entry_t *e) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    e->file->refcount--;
    if (e->file->refcount <= 0) {
        fat_lock();
        f_close(&e->file->fil);
        fat_unlock();
        kfree(e->file);
    }
    e->used = 0;
    e->file = NULL;
    return 0;
}

int fat_lseek_entry(fd_entry_t *e, int32_t offset, int whence) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    uint32_t newpos;
    fat_lock();
    if (whence == 0)      newpos = (uint32_t)offset;
    else if (whence == 1) newpos = (uint32_t)((int32_t)f_tell(&e->file->fil) + offset);
    else                  newpos = (uint32_t)((int32_t)f_size(&e->file->fil) + offset);
    FRESULT fr = f_lseek(&e->file->fil, newpos);
    fat_unlock();
    return (fr == FR_OK) ? (int)newpos : -1;
}

uint32_t fat_size_entry(fd_entry_t *e) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return 0;
    fat_lock();
    uint32_t size = (uint32_t)f_size(&e->file->fil);
    fat_unlock();
    return size;
}

int64_t fat_mtime_entry(fd_entry_t *e) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return 0;
    WORD d = e->file->fdate;
    WORD t = e->file->ftime;
    if (d == 0) return 0;
    int year  = 1980 + ((d >> 9) & 0x7F);
    int month = (d >> 5) & 0x0F;
    int day   = (d >> 0) & 0x1F;
    int hour  = (t >> 11) & 0x1F;
    int min   = (t >> 5)  & 0x3F;
    int sec   = ((t >> 0) & 0x1F) * 2;
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int64_t days = 0;
    for (int y = 1970; y < year; y++)
        days += ((y%4==0 && (y%100!=0 || y%400==0)) ? 366 : 365);
    int leap = (year%4==0 && (year%100!=0 || year%400==0));
    for (int m = 1; m < month; m++)
        days += (m == 2 && leap) ? 29 : mdays[m-1];
    days += day - 1;
    return days * 86400 + hour * 3600 + min * 60 + sec;
}

int fat_truncate_entry(fd_entry_t *e, uint32_t size) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    if (!e->file->writable) return -1;
    fat_lock();
    uint32_t oldpos = (uint32_t)f_tell(&e->file->fil);
    FRESULT fr = f_lseek(&e->file->fil, size);
    if (fr == FR_OK)
        fr = f_truncate(&e->file->fil);
    if (fr == FR_OK)
        fr = f_sync(&e->file->fil);
    uint32_t restore = oldpos < size ? oldpos : size;
    if (fr == FR_OK)
        fr = f_lseek(&e->file->fil, restore);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_sync_entry(fd_entry_t *e) {
    if (!e || !e->used || e->type != FD_FILE || !e->file) return -1;
    if (!e->file->writable) return 0;
    fat_lock();
    FRESULT fr = f_sync(&e->file->fil);
    fat_unlock();
    return (fr == FR_OK) ? 0 : -1;
}

int fat_opendir_entry_vol(const char *path, fd_entry_t *out, int vol) {
    if (!initialized || !path || !out) return -1;
    char fpath[FAT_MAX_PATH];
    make_fatpath_vol(path, vol, fpath, sizeof(fpath));
    fd_dir_t *dir = (fd_dir_t *)kmalloc(sizeof(fd_dir_t));
    if (!dir) return -1;
    memset(dir, 0, sizeof(*dir));
    fat_lock();
    FRESULT fr = f_opendir(&dir->dir, fpath);
    fat_unlock();
    if (fr != FR_OK) {
        kfree(dir);
        return -1;
    }
    out->type = FD_DIR; out->used = 1; out->dir = dir; out->dir->first_read = 1; out->dir->refcount = 1;
    return 0;
}

int fat_opendir_entry(const char *path, fd_entry_t *out) { return fat_opendir_entry_vol(path, out, 0); }

int fat_readdir_entry(fd_entry_t *e, char *name_out, int *is_dir_out) {
    if (!e || !e->used || e->type != FD_DIR || !e->dir) return -1;
    fat_lock();
    FRESULT fr = f_readdir(&e->dir->dir, &e->dir->fno);
    fat_unlock();
    if (fr != FR_OK) return -1;
    if (e->dir->fno.fname[0] == '\0') return 0;
    if (name_out) { int i = 0; while (e->dir->fno.fname[i] && i < 255) { name_out[i] = e->dir->fno.fname[i]; i++; } name_out[i] = '\0'; }
    if (is_dir_out) *is_dir_out = (e->dir->fno.fattrib & AM_DIR) ? 1 : 0;
    return 1;
}

int fat_closedir_entry(fd_entry_t *e) {
    if (!e || !e->used || e->type != FD_DIR || !e->dir) return -1;
    e->dir->refcount--;
    if (e->dir->refcount <= 0) {
        fat_lock();
        f_closedir(&e->dir->dir);
        fat_unlock();
        kfree(e->dir);
    }
    e->used = 0;
    e->dir = NULL;
    return 0;
}
