#include "zfs.h"
#include "../disk/ata.h"
#include "../../libk/core/mem.h"
#include "../../libk/string.h"
#include "../../libk/debug/log.h"

static zfs_superblock_t superblock;
static zfs_entry_t entry_table[ZFS_MAX_ENTRIES];
static uint8_t initialized = 0;
static uint8_t current_dir = ZFS_ROOT_DIR_INDEX;

static uint32_t block_to_lba(uint32_t block)
{
    return ZFS_DATA_START_LBA + (block * 8);
}

static zfs_error_t write_superblock(void)
{
    if (ata_write_sectors(superblock.drive_number, ZFS_SUPERBLOCK_LBA, 1,
                          &superblock) != ATA_SUCCESS)
    {
        return ZFS_ERR_WRITE_FAILED;
    }
    return ZFS_OK;
}

static zfs_error_t write_entry_table(void)
{
    if (ata_write_sectors(superblock.drive_number, ZFS_FILETABLE_LBA,
                          ZFS_FILETABLE_SIZE, entry_table) != ATA_SUCCESS)
    {
        return ZFS_ERR_WRITE_FAILED;
    }
    return ZFS_OK;
}

static int find_entry(const char *name, uint8_t parent, uint8_t type)
{
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
    {
        if (entry_table[i].type == type &&
            entry_table[i].parent_index == parent &&
            strcmp(entry_table[i].name, name) == 0)
        return i;
    }
    return -1;
}

static int find_free_entry(void)
{
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
    {
        if (entry_table[i].type == ZFS_TYPE_UNUSED)
        {
            return i;
        }
    }
    return -1;
}

static int resolve_path(const char *path, uint8_t *out_parent, char *out_name)
{
    if (!path || !out_parent || !out_name)
        return -1;

    if (path[0] == '/')
    {
        *out_parent = ZFS_ROOT_DIR_INDEX;
        path++;
    }
    else
    {
        *out_parent = current_dir;
    }

    if (strlen(path) == 0)
    {
        out_name[0] = '\0';
        return 0;
    }

    char temp[ZFS_MAX_FILENAME];
    int idx = 0;
    uint8_t current = *out_parent;

    for (size_t i = 0; i <= strlen(path); i++)
    {
        if (path[i] == '/' || path[i] == '\0')
        {
            if (idx > 0)
            {
                temp[idx] = '\0';

                if (strcmp(temp, "..") == 0)
                {
                    if (current != ZFS_ROOT_DIR_INDEX)
                    {
                        current = entry_table[current].parent_index;
                    }
                }
                else if (strcmp(temp, ".") != 0)
                {
                    if (path[i] == '\0')
                    {
                        strcpy(out_name, temp);
                        *out_parent = current;
                        return 0;
                    }
                    else
                    {
                        int dir_idx = find_entry(temp, current, ZFS_TYPE_DIRECTORY);
                        if (dir_idx < 0)
                            return -1;
                        current = dir_idx;
                    }
                }

                idx = 0;
            }
        }
        else
        {
            if (idx < ZFS_MAX_FILENAME - 1)
            {
                temp[idx++] = path[i];
            }
        }
    }

    out_name[0] = '\0';
    *out_parent = current;
    return 0;
}

zfs_error_t zfs_format(uint8_t drive)
{
    if (drive >= 4)
    {
        return ZFS_ERR_INVALID_DRIVE;
    }

    if (ata_drive_exists(drive) != ATA_SUCCESS)
    {
        return ZFS_ERR_INVALID_DRIVE;
    }

    log("ZenFS: Formatting drive %d...", 1, 0, drive);

    memset(&superblock, 0, sizeof(zfs_superblock_t));
    superblock.magic = ZFS_MAGIC;
    superblock.drive_number = drive;

    uint32_t total_sectors = 1048576;
    uint32_t data_sectors = total_sectors - ZFS_DATA_START_LBA;
    superblock.total_blocks = data_sectors / 8;
    superblock.free_blocks = superblock.total_blocks;
    superblock.entry_count = 1;

    memset(entry_table, 0, sizeof(entry_table));

    strcpy(entry_table[0].name, "/");
    entry_table[0].type = ZFS_TYPE_DIRECTORY;
    entry_table[0].parent_index = 0;

    if (write_superblock() != ZFS_OK)
    {
        log("ZenFS: Failed to write superblock", 3, 1);
        return ZFS_ERR_WRITE_FAILED;
    }

    if (write_entry_table() != ZFS_OK)
    {
        log("ZenFS: Failed to write entry table", 3, 1);
        return ZFS_ERR_WRITE_FAILED;
    }

    log("ZenFS: Format complete! %d blocks available", 4, 0, superblock.total_blocks);
    return ZFS_OK;
}

zfs_error_t zfs_init(uint8_t drive)
{
    if (drive >= 4)
    {
        return ZFS_ERR_INVALID_DRIVE;
    }

    if (ata_drive_exists(drive) != ATA_SUCCESS)
    {
        return ZFS_ERR_INVALID_DRIVE;
    }

    if (ata_read_sectors(drive, ZFS_SUPERBLOCK_LBA, 1, &superblock) != ATA_SUCCESS)
    {
        log("ZenFS: Failed to read superblock", 3, 1);
        return ZFS_ERR_READ_FAILED;
    }

    if (superblock.magic != ZFS_MAGIC)
    {
        log("ZenFS: Invalid magic number (expected 0x%x, got 0x%x)", 3, 1,
            ZFS_MAGIC, superblock.magic);
        return ZFS_ERR_NOT_ZFS;
    }

    if (ata_read_sectors(drive, ZFS_FILETABLE_LBA, ZFS_FILETABLE_SIZE,
                         entry_table) != ATA_SUCCESS)
    {
        log("ZenFS: Failed to read entry table", 3, 1);
        return ZFS_ERR_READ_FAILED;
    }

    initialized = 1;
    current_dir = ZFS_ROOT_DIR_INDEX;

    log("ZenFS: Initialized successfully", 4, 0);
    return ZFS_OK;
}

zfs_error_t zfs_mkdir(const char *dirname)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!dirname || strlen(dirname) == 0)
        return ZFS_ERR_INVALID_PARAM;

    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(dirname, &parent, name) < 0 || strlen(name) == 0)
    {
        return ZFS_ERR_INVALID_PARAM;
    }

    if (find_entry(name, parent, ZFS_TYPE_DIRECTORY) >= 0 ||
        find_entry(name, parent, ZFS_TYPE_FILE) >= 0)
    {
        log("ZenFS: '%s' already exists", 2, 0, name);
        return ZFS_ERR_ALREADY_EXISTS;
    }

    int free_idx = find_free_entry();
    if (free_idx < 0)
    {
        log("ZenFS: No free entries", 3, 1);
        return ZFS_ERR_TOO_MANY_ENTRIES;
    }

    strcpy(entry_table[free_idx].name, name);
    entry_table[free_idx].type = ZFS_TYPE_DIRECTORY;
    entry_table[free_idx].parent_index = parent;
    entry_table[free_idx].size = 0;
    entry_table[free_idx].start_block = 0;
    entry_table[free_idx].block_count = 0;

    superblock.entry_count++;

    if (write_entry_table() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    if (write_superblock() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    return ZFS_OK;
}

zfs_error_t zfs_rmdir(const char *dirname)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!dirname)
        return ZFS_ERR_INVALID_PARAM;

    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(dirname, &parent, name) < 0 || strlen(name) == 0)
    {
        return ZFS_ERR_INVALID_PARAM;
    }

    int dir_idx = find_entry(name, parent, ZFS_TYPE_DIRECTORY);
    if (dir_idx < 0)
    {
        return ZFS_ERR_FILE_NOT_FOUND;
    }

    if (dir_idx == ZFS_ROOT_DIR_INDEX)
    {
        log("ZenFS: Cannot remove root directory", 3, 1);
        return ZFS_ERR_INVALID_PARAM;
    }

    for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
    {
        if (entry_table[i].type != ZFS_TYPE_UNUSED &&
            entry_table[i].parent_index == dir_idx)
        {
            log("ZenFS: Directory not empty", 3, 1);
            return ZFS_ERR_NOT_EMPTY;
        }
    }

    memset(&entry_table[dir_idx], 0, sizeof(zfs_entry_t));
    superblock.entry_count--;

    if (write_entry_table() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    if (write_superblock() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    return ZFS_OK;
}

zfs_error_t zfs_chdir(const char *dirname)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!dirname)
        return ZFS_ERR_INVALID_PARAM;

    if (strcmp(dirname, "/") == 0)
    {
        current_dir = ZFS_ROOT_DIR_INDEX;
        return ZFS_OK;
    }

    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(dirname, &parent, name) < 0)
    {
        return ZFS_ERR_FILE_NOT_FOUND;
    }

    if (strlen(name) == 0)
    {
        current_dir = parent;
        return ZFS_OK;
    }

    int dir_idx = find_entry(name, parent, ZFS_TYPE_DIRECTORY);
    if (dir_idx < 0)
    {
        return ZFS_ERR_FILE_NOT_FOUND;
    }

    current_dir = dir_idx;
    return ZFS_OK;
}

void zfs_get_cwd(char *buffer, size_t size)
{
    if (!initialized || !buffer || size == 0)
    {
        if (buffer && size > 0)
            buffer[0] = '\0';
        return;
    }

    if (current_dir == ZFS_ROOT_DIR_INDEX)
    {
        if (size >= 2)
        {
            buffer[0] = '/';
            buffer[1] = '\0';
        }
        return;
    }

    int depth = 0;
    uint8_t idx = current_dir;
    char *parts[16];

    while (idx != ZFS_ROOT_DIR_INDEX && depth < 16)
    {
        parts[depth++] = entry_table[idx].name;
        idx = entry_table[idx].parent_index;
    }

    buffer[0] = '\0';
    for (int i = depth - 1; i >= 0; i--)
    {
        strcat(buffer, "/");
        strcat(buffer, parts[i]);
    }

    if (buffer[0] == '\0')
    {
        buffer[0] = '/';
        buffer[1] = '\0';
    }
}

zfs_error_t zfs_create(const char *filename, uint32_t size)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!filename || strlen(filename) == 0)
        return ZFS_ERR_INVALID_PARAM;

    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(filename, &parent, name) < 0 || strlen(name) == 0)
    {
        return ZFS_ERR_INVALID_PARAM;
    }

    if (find_entry(name, parent, ZFS_TYPE_FILE) >= 0 ||
        find_entry(name, parent, ZFS_TYPE_DIRECTORY) >= 0)
    {
        log("ZenFS: '%s' already exists", 2, 0, name);
        return ZFS_ERR_ALREADY_EXISTS;
    }

    uint32_t blocks_needed = (size + ZFS_BLOCK_SIZE - 1) / ZFS_BLOCK_SIZE;
    if (blocks_needed == 0)
        blocks_needed = 1;

    if (blocks_needed > superblock.free_blocks)
    {
        log("ZenFS: Not enough space (%d blocks needed, %d free)", 3, 1,
            blocks_needed, superblock.free_blocks);
        return ZFS_ERR_NO_SPACE;
    }

    int free_idx = find_free_entry();
    if (free_idx < 0)
    {
        log("ZenFS: No free entries", 3, 1);
        return ZFS_ERR_TOO_MANY_ENTRIES;
    }

    uint32_t start_block = 0;
    uint32_t found_blocks = 0;

    for (uint32_t block = 0; block < superblock.total_blocks; block++)
    {
        int is_free = 1;

        for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
        {
            if (entry_table[i].type == ZFS_TYPE_FILE)
            {
                uint32_t file_start = entry_table[i].start_block;
                uint32_t file_end = file_start + entry_table[i].block_count;

                if (block >= file_start && block < file_end)
                {
                    is_free = 0;
                    break;
                }
            }
        }

        if (is_free)
        {
            if (found_blocks == 0)
            {
                start_block = block;
            }
            found_blocks++;

            if (found_blocks >= blocks_needed)
            {
                break;
            }
        }
        else
        {
            found_blocks = 0;
        }
    }

    if (found_blocks < blocks_needed)
    {
        log("ZenFS: Could not find contiguous space", 3, 1);
        return ZFS_ERR_NO_SPACE;
    }

    strcpy(entry_table[free_idx].name, name);
    entry_table[free_idx].size = size;
    entry_table[free_idx].start_block = start_block;
    entry_table[free_idx].block_count = blocks_needed;
    entry_table[free_idx].type = ZFS_TYPE_FILE;
    entry_table[free_idx].parent_index = parent;

    superblock.entry_count++;
    superblock.free_blocks -= blocks_needed;

    if (write_entry_table() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    if (write_superblock() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    return ZFS_OK;
}

zfs_error_t zfs_open(const char *filename, zfs_file_t *file)
{
    __asm__ volatile("cli"); 
    if (!initialized) {
        __asm__ volatile("sti"); 
        return ZFS_ERR_NOT_INITIALIZED;
    }
    if (!filename || !file) {
        __asm__ volatile("sti");
        return ZFS_ERR_INVALID_PARAM;
    }
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(filename, &parent, name) < 0 || strlen(name) == 0)
    {
        return ZFS_ERR_INVALID_PARAM;
    }

    int file_idx = find_entry(name, parent, ZFS_TYPE_FILE);
    if (file_idx < 0)
    {
        log("ZenFS: File not found: '%s'", 2, 0, name);
        return ZFS_ERR_FILE_NOT_FOUND;
    }

    strcpy(file->name, entry_table[file_idx].name);
    file->size = entry_table[file_idx].size;
    file->start_block = entry_table[file_idx].start_block;
    file->position = 0;
    file->entry_index = file_idx;
    file->is_open = 1;
    __asm__ volatile("sti");
    return ZFS_OK;
}

zfs_error_t zfs_read(zfs_file_t *file, void *buffer, uint32_t size, uint32_t *bytes_read)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!file || !file->is_open)
        return ZFS_ERR_NOT_OPEN;

    if (!buffer || size == 0)
    {
        if (bytes_read)
            *bytes_read = 0;
        return ZFS_OK;
    }

    if (file->position >= file->size)
    {
        if (bytes_read)
            *bytes_read = 0;
        return ZFS_ERR_EOF;
    }

    if (file->position + size > file->size)
    {
        size = file->size - file->position;
    }

    uint32_t total_read = 0;
    uint8_t *dest = (uint8_t *)buffer;
    uint8_t block_buffer[ZFS_BLOCK_SIZE];

    while (total_read < size)
    {
        uint32_t current_block = file->start_block + (file->position / ZFS_BLOCK_SIZE);
        uint32_t offset_in_block = file->position % ZFS_BLOCK_SIZE;
        uint32_t bytes_to_read = ZFS_BLOCK_SIZE - offset_in_block;

        if (bytes_to_read > size - total_read)
        {
            bytes_to_read = size - total_read;
        }

        uint32_t lba = block_to_lba(current_block);
        if (ata_read_sectors(superblock.drive_number, lba, 8, block_buffer) != ATA_SUCCESS)
        {
            if (bytes_read)
                *bytes_read = total_read;
            return ZFS_ERR_READ_FAILED;
        }

        memcpy(dest + total_read, block_buffer + offset_in_block, bytes_to_read);

        total_read += bytes_to_read;
        file->position += bytes_to_read;
    }

    if (bytes_read)
    {
        *bytes_read = total_read;
    }

    return ZFS_OK;
}

zfs_error_t zfs_write(zfs_file_t *file, const void *buffer, uint32_t size)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!file || !file->is_open)
        return ZFS_ERR_NOT_OPEN;

    if (!buffer || size == 0)
        return ZFS_OK;

    if (file->position + size > file->size)
    {
        log("ZenFS: Write would exceed file size", 3, 1);
        return ZFS_ERR_INVALID_PARAM;
    }

    uint32_t total_written = 0;
    const uint8_t *src = (const uint8_t *)buffer;
    uint8_t block_buffer[ZFS_BLOCK_SIZE];

    while (total_written < size)
    {
        uint32_t current_block = file->start_block + (file->position / ZFS_BLOCK_SIZE);
        uint32_t offset_in_block = file->position % ZFS_BLOCK_SIZE;
        uint32_t bytes_to_write = ZFS_BLOCK_SIZE - offset_in_block;

        if (bytes_to_write > size - total_written)
        {
            bytes_to_write = size - total_written;
        }

        uint32_t lba = block_to_lba(current_block);

        if (offset_in_block != 0 || bytes_to_write < ZFS_BLOCK_SIZE)
        {
            if (ata_read_sectors(superblock.drive_number, lba, 8, block_buffer) != ATA_SUCCESS)
            {
                return ZFS_ERR_READ_FAILED;
            }
        }

        memcpy(block_buffer + offset_in_block, src + total_written, bytes_to_write);

        if (ata_write_sectors(superblock.drive_number, lba, 8, block_buffer) != ATA_SUCCESS)
        {
            return ZFS_ERR_WRITE_FAILED;
        }

        total_written += bytes_to_write;
        file->position += bytes_to_write;
    }

    return ZFS_OK;
}

zfs_error_t zfs_close(zfs_file_t *file)
{
    if (!file)
        return ZFS_OK;

    file->is_open = 0;
    file->position = 0;

    return ZFS_OK;
}

zfs_error_t zfs_delete(const char *filename)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    if (!filename)
        return ZFS_ERR_INVALID_PARAM;

    uint8_t parent;
    char name[ZFS_MAX_FILENAME];

    if (resolve_path(filename, &parent, name) < 0 || strlen(name) == 0)
    {
        return ZFS_ERR_INVALID_PARAM;
    }

    int file_idx = find_entry(name, parent, ZFS_TYPE_FILE);
    if (file_idx < 0)
    {
        return ZFS_ERR_FILE_NOT_FOUND;
    }

    uint32_t freed_blocks = entry_table[file_idx].block_count;
    memset(&entry_table[file_idx], 0, sizeof(zfs_entry_t));

    superblock.entry_count--;
    superblock.free_blocks += freed_blocks;

    if (write_entry_table() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    if (write_superblock() != ZFS_OK)
        return ZFS_ERR_WRITE_FAILED;

    return ZFS_OK;
}

zfs_error_t zfs_seek(zfs_file_t *file, uint32_t position)
{
    if (!file || !file->is_open)
        return ZFS_ERR_NOT_OPEN;

    if (position > file->size)
    {
        position = file->size;
    }

    file->position = position;
    return ZFS_OK;
}

zfs_error_t zfs_list(void)
{
    if (!initialized)
        return ZFS_ERR_NOT_INITIALIZED;

    char cwd[256];
    zfs_get_cwd(cwd, sizeof(cwd));
    log("Directory listing for: %s", 1, 1, cwd);
    log("", 1, 1);

    int file_count = 0;
    int dir_count = 0;

    for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
    {
        if (entry_table[i].type != ZFS_TYPE_UNUSED &&
            entry_table[i].parent_index == current_dir)
        {
            if (entry_table[i].type == ZFS_TYPE_DIRECTORY)
            {
                log("[DIR]  %s", 1, 1, entry_table[i].name);
                dir_count++;
            }
            else if (entry_table[i].type == ZFS_TYPE_FILE)
            {
                log("       %s  (%d bytes)", 1, 1,
                    entry_table[i].name,
                    entry_table[i].size);
                file_count++;
            }
        }
    }

    if (file_count == 0 && dir_count == 0)
    {
        log("(empty)", 1, 1);
    }

    log("", 1, 1);
    log("Total: %d directories, %d files", 1, 1, dir_count, file_count);

    return ZFS_OK;
}

void zfs_print_stats(void)
{
    if (!initialized)
    {
        log("ZenFS not initialized.", 2, 1);
        return;
    }

    int file_count = 0;
    int dir_count = 0;

    for (int i = 0; i < ZFS_MAX_ENTRIES; i++)
    {
        if (entry_table[i].type == ZFS_TYPE_FILE)
            file_count++;
        else if (entry_table[i].type == ZFS_TYPE_DIRECTORY)
            dir_count++;
    }

    log("ZenFS Statistics:", 1, 1);
    log("  Total space: %d KB", 1, 1, superblock.total_blocks * 4);
    log("  Used space: %d KB", 1, 1,
        (superblock.total_blocks - superblock.free_blocks) * 4);
    log("  Free space: %d KB", 1, 1, superblock.free_blocks * 4);
    log("  Entries: %d / %d", 1, 1, superblock.entry_count, ZFS_MAX_ENTRIES);
    log("  Directories: %d", 1, 1, dir_count);
    log("  Files: %d", 1, 1, file_count);
}

zfs_error_t zfs_unmount(void)
{
    if (!initialized)
    {
        return ZFS_ERR_NOT_INITIALIZED;
    }

    if (write_entry_table() != ZFS_OK)
    {
        return ZFS_ERR_WRITE_FAILED;
    }

    if (write_superblock() != ZFS_OK)
    {
        return ZFS_ERR_WRITE_FAILED;
    }

    memset(&superblock, 0, sizeof(superblock));
    memset(entry_table, 0, sizeof(entry_table));

    initialized = 0;
    current_dir = ZFS_ROOT_DIR_INDEX;
    log("ZenFS: Unmounted successfully", 4, 0);
    return ZFS_OK;
}