/**
 *
 * @file : /fat_man.c
 * @brief : Host-side FAT32 disk image manager with raw and VHD support.
 *
 * Build: clang fat_man.c src/drv/disk/fatfs/ff.c src/drv/disk/fatfs/ffunicode.c \
 *        -Isrc/drv/disk/fatfs -o fat_man
 *
 * This file is a part of the Zen (ZenOS)
 * Operating System build process, and is
 * released under the terms of the MIT
 * Licensing : Read LICENSE at the root of
 * the repository.
 *
 * @copyright (c) 2026
 * @author : Rishies2010
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "src/drv/disk/fatfs/ff.h"
#include "src/drv/disk/fatfs/diskio.h"

#include <time.h>
DWORD get_fattime(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    return ((DWORD)(t->tm_year - 80) << 25) | ((DWORD)(t->tm_mon + 1) << 21) | ((DWORD)t->tm_mday << 16) | ((DWORD)t->tm_hour << 11) | ((DWORD)t->tm_min << 5) | ((DWORD)(t->tm_sec / 2));
}

#define VHD_COOKIE_FOOTER 0x636f6e6563746978ULL
#define VHD_TYPE_FIXED 2
#define VHD_TYPE_DYNAMIC 3
#define VHD_BAT_FREE 0xFFFFFFFF

static int img_fd = -1;
static int is_vhd = 0;
static int vhd_type = 0;
static off_t fixed_data_start = 0;

static uint64_t dyn_bat_file_off;
static uint32_t dyn_max_bat;
static uint32_t dyn_block_size;
static uint32_t dyn_sectors_per_block;
static uint32_t dyn_bitmap_sectors;
static uint64_t dyn_virtual_size;
static uint32_t *bat = NULL;
static uint8_t saved_footer[512];

static inline uint32_t be32r(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t be64r(const uint8_t *p)
{
    return ((uint64_t)be32r(p) << 32) | be32r(p + 4);
}

static void bat_flush(void)
{
    lseek(img_fd, (off_t)dyn_bat_file_off, SEEK_SET);
    for (uint32_t i = 0; i < dyn_max_bat; i++)
    {

        uint32_t v = bat[i];
        uint8_t b[4] = {v >> 24, v >> 16, v >> 8, v};
        write(img_fd, b, 4);
    }
    fsync(img_fd);
}

static int bat_alloc_block(uint32_t block_idx)
{
    if (block_idx >= dyn_max_bat || bat[block_idx] != VHD_BAT_FREE)
        return 0;

    struct stat st;
    fstat(img_fd, &st);
    off_t footer_off = st.st_size - 512;

    uint32_t new_sector = (uint32_t)(footer_off / 512);

    uint32_t bitmap_bytes = dyn_bitmap_sectors * 512;
    uint32_t data_bytes = dyn_block_size;
    uint8_t *buf = (uint8_t *)calloc(1, bitmap_bytes + data_bytes);
    if (!buf)
    {
        fprintf(stderr, "OOM allocating block\n");
        return -1;
    }
    memset(buf, 0xFF, bitmap_bytes);

    lseek(img_fd, footer_off, SEEK_SET);
    write(img_fd, buf, bitmap_bytes + data_bytes);
    free(buf);

    lseek(img_fd, footer_off + bitmap_bytes + data_bytes, SEEK_SET);
    write(img_fd, saved_footer, 512);
    fsync(img_fd);

    bat[block_idx] = new_sector;
    bat_flush();
    return 0;
}

static off_t lba_to_off(LBA_t lba, int alloc)
{
    if (!is_vhd || vhd_type == VHD_TYPE_FIXED)
        return fixed_data_start + (off_t)lba * 512;

    uint32_t block_idx = (uint32_t)((uint64_t)lba / dyn_sectors_per_block);
    uint32_t sector_in_blk = (uint32_t)((uint64_t)lba % dyn_sectors_per_block);

    if (bat[block_idx] == VHD_BAT_FREE)
    {
        if (!alloc)
            return -1;
        if (bat_alloc_block(block_idx) != 0)
            return -1;
    }

    return (off_t)bat[block_idx] * 512 + (off_t)dyn_bitmap_sectors * 512 + (off_t)sector_in_blk * 512;
}

static int open_image(const char *path)
{
    img_fd = open(path, O_RDWR);
    if (img_fd < 0)
    {
        perror(path);
        return -1;
    }

    uint8_t hdr[8];
    lseek(img_fd, 0, SEEK_SET);
    if (read(img_fd, hdr, 8) != 8)
        return -1;

    uint64_t cookie = be64r(hdr);
    if (cookie != VHD_COOKIE_FOOTER)
    {

        is_vhd = 0;
        fixed_data_start = 0;
        printf("[img] Raw image\n");
        return 0;
    }

    is_vhd = 1;

    uint8_t ftr_buf[512];
    lseek(img_fd, 0, SEEK_SET);
    read(img_fd, ftr_buf, 512);

    uint32_t dtype = be32r(ftr_buf + 0x3c);
    vhd_type = (int)dtype;

    struct stat st;
    fstat(img_fd, &st);
    lseek(img_fd, st.st_size - 512, SEEK_SET);
    read(img_fd, saved_footer, 512);

    if (vhd_type == VHD_TYPE_FIXED)
    {
        fixed_data_start = 512;
        printf("[vhd] Fixed VHD\n");
    }
    else if (vhd_type == VHD_TYPE_DYNAMIC)
    {

        uint64_t dyn_off = be64r(ftr_buf + 0x10);
        uint8_t dyn_buf[1024];
        lseek(img_fd, (off_t)dyn_off, SEEK_SET);
        read(img_fd, dyn_buf, sizeof(dyn_buf));

        dyn_bat_file_off = be64r(dyn_buf + 16);
        dyn_max_bat = be32r(dyn_buf + 28);
        dyn_block_size = be32r(dyn_buf + 32);
        dyn_sectors_per_block = dyn_block_size / 512;
        dyn_bitmap_sectors = (dyn_sectors_per_block / 8 + 511) / 512;
        dyn_virtual_size = be64r(ftr_buf + 0x28);

        printf("[vhd] Dynamic VHD — %llu MB virtual, %u KB blocks, %u max blocks\n",
               (unsigned long long)dyn_virtual_size / 1024 / 1024,
               dyn_block_size / 1024, dyn_max_bat);

        bat = (uint32_t *)malloc(dyn_max_bat * 4);
        if (!bat)
        {
            fprintf(stderr, "OOM\n");
            return -1;
        }

        lseek(img_fd, (off_t)dyn_bat_file_off, SEEK_SET);
        int allocated = 0;
        for (uint32_t i = 0; i < dyn_max_bat; i++)
        {
            uint8_t b[4];
            read(img_fd, b, 4);
            bat[i] = be32r(b);
            if (bat[i] != VHD_BAT_FREE)
                allocated++;
        }
        printf("[vhd] %d / %d blocks allocated\n", allocated, dyn_max_bat);
    }
    else
    {
        fprintf(stderr, "[vhd] Unsupported type %d\n", vhd_type);
        return -1;
    }

    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return img_fd >= 0 ? 0 : STA_NOINIT;
}
DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return img_fd >= 0 ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (UINT i = 0; i < count; i++)
    {
        off_t off = lba_to_off(sector + i, 0);
        if (off < 0)
        {
            memset(buff + i * 512, 0, 512);
            continue;
        }
        lseek(img_fd, off, SEEK_SET);
        if (read(img_fd, buff + i * 512, 512) != 512)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    for (UINT i = 0; i < count; i++)
    {
        off_t off = lba_to_off(sector + i, 1);
        if (off < 0)
            return RES_ERROR;
        lseek(img_fd, off, SEEK_SET);
        if (write(img_fd, buff + i * 512, 512) != 512)
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    switch (cmd)
    {
    case CTRL_SYNC:
        fsync(img_fd);
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (is_vhd && vhd_type == VHD_TYPE_DYNAMIC)
            *(LBA_t *)buff = (LBA_t)(dyn_virtual_size / 512);
        else
        {
            struct stat st;
            fstat(img_fd, &st);
            *(LBA_t *)buff = (LBA_t)((st.st_size - fixed_data_start) / 512);
        }
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static FATFS fs;

static int do_mount(void)
{
    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK)
    {
        fprintf(stderr, "f_mount failed (%d)\n", fr);
        return -1;
    }
    return 0;
}

static void mkpath(const char *in, char *out, size_t sz)
{
    if (in[0] == '/')
        snprintf(out, sz, "0:%s", in);
    else
        snprintf(out, sz, "0:/%s", in);
}

static const char *fr_str(FRESULT fr)
{
    static const char *t[] = {
        "OK", "DISK_ERR", "INT_ERR", "NOT_READY", "NO_FILE", "NO_PATH",
        "INVALID_NAME", "DENIED", "EXIST", "INVALID_OBJECT", "WRITE_PROTECTED",
        "INVALID_DRIVE", "NOT_ENABLED", "NO_FILESYSTEM", "MKFS_ABORTED",
        "TIMEOUT", "LOCKED", "NOT_ENOUGH_CORE", "TOO_MANY_OPEN_FILES", "INVALID_PARAMETER"};
    return (unsigned)fr < sizeof(t) / sizeof(*t) ? t[fr] : "?";
}
#define CHECKFR(op, fr)                                  \
    do                                                   \
    {                                                    \
        if ((fr) != FR_OK)                               \
        {                                                \
            fprintf(stderr, "%s: %s\n", op, fr_str(fr)); \
            return;                                      \
        }                                                \
    } while (0)

static void cmd_format(void)
{
    printf("Formatting as FAT32...\n");
    uint8_t work[FF_MAX_SS];
    MKFS_PARM opt = {.fmt = FM_FAT32 | FM_SFD, .n_fat = 2, .align = 0, .n_root = 0, .au_size = 4096};
    FRESULT fr = f_mkfs("0:", &opt, work, sizeof(work));
    CHECKFR("f_mkfs", fr);
    fr = f_mount(&fs, "0:", 1);
    CHECKFR("f_mount", fr);
    printf("Format complete.\n");
}

static void cmd_info(void)
{
    DWORD fc;
    FATFS *fsp;
    FRESULT fr = f_getfree("0:", &fc, &fsp);
    CHECKFR("f_getfree", fr);
    DWORD tot = (DWORD)((fsp->n_fatent - 2) * (uint64_t)fsp->csize / 2);
    DWORD fre = (DWORD)(fc * (uint64_t)fsp->csize / 2);
    printf("\nFAT32:\n");
    printf("  Total: %lu KB (%lu MB)\n", (unsigned long)tot, (unsigned long)tot / 1024);
    printf("  Free:  %lu KB (%lu MB)\n", (unsigned long)fre, (unsigned long)fre / 1024);
    printf("  Used:  %lu KB (%lu MB)\n", (unsigned long)(tot - fre), (unsigned long)(tot - fre) / 1024);
    printf("  Cluster: %u sectors\n\n", fsp->csize);
}

static void cmd_list(const char *path)
{
    char fp[256];
    mkpath(path, fp, sizeof(fp));
    DIR dir;
    FRESULT fr = f_opendir(&dir, fp);
    CHECKFR("f_opendir", fr);
    printf("\n%s\n%-6s %-40s %12s\n", path, "Type", "Name", "Size");
    printf("%-6s %-40s %12s\n", "----", "----", "----");
    int files = 0, dirs = 0;
    FILINFO fno;
    while ((fr = f_readdir(&dir, &fno)) == FR_OK && fno.fname[0])
    {
        if (fno.fattrib & AM_DIR)
        {
            printf("%-6s %-40s %12s\n", "[DIR]", fno.fname, "-");
            dirs++;
        }
        else
        {
            printf("%-6s %-40s %12lu\n", "", fno.fname, (unsigned long)fno.fsize);
            files++;
        }
    }
    f_closedir(&dir);
    if (!files && !dirs)
        printf("(empty)\n");
    printf("\n%d dirs, %d files\n\n", dirs, files);
}

static void cmd_mkdir(const char *path)
{
    char fp[256];
    mkpath(path, fp, sizeof(fp));
    FRESULT fr = f_mkdir(fp);
    if (fr == FR_EXIST)
    {
        printf("Already exists: %s\n", path);
        return;
    }
    CHECKFR("f_mkdir", fr);
    printf("Created: %s\n", path);
}

static void cmd_rmdir(const char *path)
{
    char fp[256];
    mkpath(path, fp, sizeof(fp));
    FRESULT fr = f_unlink(fp);
    CHECKFR("f_unlink", fr);
    printf("Removed: %s\n", path);
}

static void cmd_import(const char *host, const char *dst)
{
    FILE *src = fopen(host, "rb");
    if (!src)
    {
        perror(host);
        return;
    }
    char fp[256];
    mkpath(dst, fp, sizeof(fp));
    FIL fil;
    FRESULT fr = f_open(&fil, fp, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        fprintf(stderr, "f_open: %s\n", fr_str(fr));
        fclose(src);
        return;
    }
    uint8_t buf[4096];
    size_t total = 0, n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
    {
        UINT bw;
        fr = f_write(&fil, buf, (UINT)n, &bw);
        if (fr != FR_OK || bw != n)
        {
            fprintf(stderr, "f_write: %s\n", fr_str(fr));
            break;
        }
        total += n;
    }
    f_close(&fil);
    fclose(src);
    printf("Imported: %s -> %s (%zu bytes)\n", host, dst, total);
}

static void cmd_export(const char *src, const char *host)
{
    char fp[256];
    mkpath(src, fp, sizeof(fp));
    FIL fil;
    FRESULT fr = f_open(&fil, fp, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK)
    {
        fprintf(stderr, "f_open: %s\n", fr_str(fr));
        return;
    }
    FILE *dst = fopen(host, "wb");
    if (!dst)
    {
        perror(host);
        f_close(&fil);
        return;
    }
    uint8_t buf[4096];
    size_t total = 0;
    UINT br;
    while ((fr = f_read(&fil, buf, sizeof(buf), &br)) == FR_OK && br > 0)
    {
        fwrite(buf, 1, br, dst);
        total += br;
    }
    f_close(&fil);
    fclose(dst);
    printf("Exported: %s -> %s (%zu bytes)\n", src, host, total);
}

static void cmd_delete(const char *path)
{
    char fp[256];
    mkpath(path, fp, sizeof(fp));
    FRESULT fr = f_unlink(fp);
    CHECKFR("f_unlink", fr);
    printf("Deleted: %s\n", path);
}

static void show_help(void)
{
    printf("fat_man - ZenOS FAT32 disk image manager\n");
    printf("Supports raw images and fixed/dynamic VHD files.\n\n");
    printf("Usage: fat_man <image> <command> [args]\n\n");
    printf("Commands:\n");
    printf("  format                     Format as FAT32\n");
    printf("  info                       Show filesystem stats\n");
    printf("  list [path]                List directory (default: /)\n");
    printf("  mkdir <path>               Create directory\n");
    printf("  rmdir <path>               Remove directory\n");
    printf("  import <host_file> <path>  Copy file from host into image\n");
    printf("  export <path> <host_file>  Copy file from image to host\n");
    printf("  delete <path>              Delete file\n\n");
    printf("Examples:\n");
    printf("  fat_man ZenOS.vhd format\n");
    printf("  fat_man ZenOS.vhd import build/init /init\n");
    printf("  fat_man ZenOS.vhd list /\n");
    printf("  fat_man ZenOS.vhd export /init init_backup\n");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        show_help();
        return 1;
    }
    const char *img = argv[1];
    const char *cmd = argv[2];
    if (!strcmp(cmd, "help"))
    {
        show_help();
        return 0;
    }

    if (open_image(img) != 0)
        return 1;

    if (!strcmp(cmd, "format"))
    {
        cmd_format();
    }
    else
    {
        if (do_mount() != 0)
        {
            free(bat);
            close(img_fd);
            return 1;
        }
        if (!strcmp(cmd, "info"))
            cmd_info();
        else if (!strcmp(cmd, "list"))
            cmd_list(argc >= 4 ? argv[3] : "/");
        else if (!strcmp(cmd, "mkdir"))
        {
            if (argc != 4)
                fputs("Usage: mkdir <path>\n", stderr);
            else
                cmd_mkdir(argv[3]);
        }
        else if (!strcmp(cmd, "rmdir"))
        {
            if (argc != 4)
                fputs("Usage: rmdir <path>\n", stderr);
            else
                cmd_rmdir(argv[3]);
        }
        else if (!strcmp(cmd, "import"))
        {
            if (argc != 5)
                fputs("Usage: import <src> <dst>\n", stderr);
            else
                cmd_import(argv[3], argv[4]);
        }
        else if (!strcmp(cmd, "export"))
        {
            if (argc != 5)
                fputs("Usage: export <src> <dst>\n", stderr);
            else
                cmd_export(argv[3], argv[4]);
        }
        else if (!strcmp(cmd, "delete"))
        {
            if (argc != 4)
                fputs("Usage: delete <path>\n", stderr);
            else
                cmd_delete(argv[3]);
        }
        else
        {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            show_help();
        }
    }

    free(bat);
    close(img_fd);
    return 0;
}
