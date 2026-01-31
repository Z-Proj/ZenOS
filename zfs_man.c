#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define ZFS_MAGIC 0x2153465a
#define ZFS_BLOCK_SIZE 4096
#define ZFS_MAX_ENTRIES 64
#define ZFS_MAX_FILENAME 28
#define ZFS_SUPERBLOCK_LBA 0
#define ZFS_FILETABLE_LBA 1
#define ZFS_FILETABLE_SIZE 8
#define ZFS_DATA_START_LBA 9

#define ZFS_ROOT_DIR_INDEX 0

#define ZFS_TYPE_UNUSED 0
#define ZFS_TYPE_FILE 1
#define ZFS_TYPE_DIRECTORY 2

#define VHD_COOKIE 0x636f6e6563746978ULL

typedef struct {
    uint64_t cookie;
    uint32_t features;
    uint32_t file_format_version;
    uint64_t data_offset;
    uint32_t timestamp;
    uint8_t creator_application[4];
    uint32_t creator_version;
    uint32_t creator_host_os;
    uint64_t original_size;
    uint64_t current_size;
    uint32_t disk_geometry;
    uint32_t disk_type;
    uint32_t checksum;
    uint8_t unique_id[16];
    uint8_t saved_state;
    uint8_t reserved[427];
} __attribute__((packed)) vhd_footer_t;

typedef struct {
    uint32_t magic;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t entry_count;
    uint8_t drive_number;
    uint8_t reserved[495];
} __attribute__((packed)) zfs_superblock_t;

typedef struct {
    char name[ZFS_MAX_FILENAME];
    uint32_t size;
    uint32_t start_block;
    uint32_t block_count;
    uint8_t type;
    uint8_t parent_index;
    uint8_t reserved[26];
} __attribute__((packed)) zfs_entry_t;

static int vhd_fd = -1;
static off_t vhd_data_offset = 0;
static zfs_superblock_t superblock;
static zfs_entry_t entry_table[ZFS_MAX_ENTRIES];

uint32_t swap32(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

uint64_t swap64(uint64_t val) {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8) |
           ((val & 0x00000000FF000000ULL) << 8) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

int detect_vhd_offset() {
    uint8_t sector[512];
    
    for (off_t offset = 0; offset < 65536; offset += 512) {
        if (lseek(vhd_fd, offset, SEEK_SET) < 0) {
            continue;
        }
        
        if (read(vhd_fd, sector, 512) != 512) {
            continue;
        }
        
        uint32_t *magic_ptr = (uint32_t*)sector;
        if (*magic_ptr == ZFS_MAGIC) {
            vhd_data_offset = offset;
            printf("Found ZenFS at offset 0x%lx (%ld bytes)\n", offset, offset);
            return 0;
        }
    }
    
    vhd_data_offset = 0;
    printf("No ZenFS found, starting at offset 0 (use 'format' to initialize)\n");
    return 0;
}

int read_sector(uint32_t lba, void *buffer, uint32_t count) {
    off_t offset = vhd_data_offset + ((off_t)lba * 512);
    if (lseek(vhd_fd, offset, SEEK_SET) != offset) {
        return -1;
    }
    if (read(vhd_fd, buffer, count * 512) != (ssize_t)(count * 512)) {
        return -1;
    }
    return 0;
}

int write_sector(uint32_t lba, const void *buffer, uint32_t count) {
    off_t offset = vhd_data_offset + ((off_t)lba * 512);
    if (lseek(vhd_fd, offset, SEEK_SET) != offset) {
        return -1;
    }
    if (write(vhd_fd, buffer, count * 512) != (ssize_t)(count * 512)) {
        return -1;
    }
    return 0;
}

int load_filesystem() {
    if (read_sector(ZFS_SUPERBLOCK_LBA, &superblock, 1) != 0) {
        fprintf(stderr, "Failed to read superblock\n");
        return -1;
    }
    
    if (superblock.magic != ZFS_MAGIC) {
        fprintf(stderr, "Invalid ZenFS magic (got 0x%x, expected 0x%x)\n", 
                superblock.magic, ZFS_MAGIC);
        return -1;
    }
    
    if (read_sector(ZFS_FILETABLE_LBA, entry_table, ZFS_FILETABLE_SIZE) != 0) {
        fprintf(stderr, "Failed to read entry table\n");
        return -1;
    }
    
    return 0;
}

int save_filesystem() {
    if (write_sector(ZFS_SUPERBLOCK_LBA, &superblock, 1) != 0) {
        fprintf(stderr, "Failed to write superblock\n");
        return -1;
    }
    
    if (write_sector(ZFS_FILETABLE_LBA, entry_table, ZFS_FILETABLE_SIZE) != 0) {
        fprintf(stderr, "Failed to write entry table\n");
        return -1;
    }
    
    return 0;
}

void build_path(uint8_t index, char *path, size_t max_size) {
    if (index == ZFS_ROOT_DIR_INDEX) {
        strncpy(path, "/", max_size);
        return;
    }
    
    char temp[256] = {0};
    char *parts[16];
    int depth = 0;
    uint8_t idx = index;
    
    while (idx != ZFS_ROOT_DIR_INDEX && depth < 16) {
        parts[depth++] = entry_table[idx].name;
        idx = entry_table[idx].parent_index;
    }
    
    path[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        strcat(path, "/");
        strcat(path, parts[i]);
    }
    
    if (path[0] == '\0') {
        strncpy(path, "/", max_size);
    }
}

int resolve_path(const char *path, uint8_t *out_parent, char *out_name) {
    if (!path || !out_parent || !out_name) return -1;
    
    if (path[0] == '/') {
        *out_parent = ZFS_ROOT_DIR_INDEX;
        path++;
    } else {
        *out_parent = ZFS_ROOT_DIR_INDEX;
    }
    
    if (strlen(path) == 0) {
        out_name[0] = '\0';
        return 0;
    }
    
    char temp[ZFS_MAX_FILENAME];
    int idx = 0;
    uint8_t current = *out_parent;
    
    for (size_t i = 0; i <= strlen(path); i++) {
        if (path[i] == '/' || path[i] == '\0') {
            if (idx > 0) {
                temp[idx] = '\0';
                
                if (strcmp(temp, "..") == 0) {
                    if (current != ZFS_ROOT_DIR_INDEX) {
                        current = entry_table[current].parent_index;
                    }
                } else if (strcmp(temp, ".") != 0) {
                    if (path[i] == '\0') {
                        strncpy(out_name, temp, ZFS_MAX_FILENAME - 1);
                        out_name[ZFS_MAX_FILENAME - 1] = '\0';
                        *out_parent = current;
                        return 0;
                    } else {
                        int found = -1;
                        for (int j = 0; j < ZFS_MAX_ENTRIES; j++) {
                            if (entry_table[j].type == ZFS_TYPE_DIRECTORY &&
                                entry_table[j].parent_index == current &&
                                strcmp(entry_table[j].name, temp) == 0) {
                                found = j;
                                break;
                            }
                        }
                        if (found < 0) return -1;
                        current = found;
                    }
                }
                idx = 0;
            }
        } else {
            if (idx < ZFS_MAX_FILENAME - 1) {
                temp[idx++] = path[i];
            }
        }
    }
    
    out_name[0] = '\0';
    *out_parent = current;
    return 0;
}

void cmd_format() {
    printf("Formatting ZenFS...\n");
    
    memset(&superblock, 0, sizeof(zfs_superblock_t));
    superblock.magic = ZFS_MAGIC;
    superblock.drive_number = 0;
    
    uint32_t total_sectors = 1048576;
    uint32_t data_sectors = total_sectors - ZFS_DATA_START_LBA;
    superblock.total_blocks = data_sectors / 8;
    superblock.free_blocks = superblock.total_blocks;
    superblock.entry_count = 1;
    
    memset(entry_table, 0, sizeof(entry_table));
    
    strcpy(entry_table[0].name, "/");
    entry_table[0].type = ZFS_TYPE_DIRECTORY;
    entry_table[0].parent_index = 0;
    
    if (save_filesystem() != 0) {
        fprintf(stderr, "Format failed\n");
        return;
    }
    
    printf("Format complete! %d blocks (%d KB) available\n", 
           superblock.total_blocks, superblock.total_blocks * 4);
}

void cmd_info() {
    int file_count = 0;
    int dir_count = 0;
    
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type == ZFS_TYPE_FILE) file_count++;
        else if (entry_table[i].type == ZFS_TYPE_DIRECTORY) dir_count++;
    }
    
    printf("\nZenFS Statistics:\n");
    printf("  Total space:  %d KB\n", superblock.total_blocks * 4);
    printf("  Used space:   %d KB\n", 
           (superblock.total_blocks - superblock.free_blocks) * 4);
    printf("  Free space:   %d KB\n", superblock.free_blocks * 4);
    printf("  Entries:      %d / %d\n", superblock.entry_count, ZFS_MAX_ENTRIES);
    printf("  Directories:  %d\n", dir_count);
    printf("  Files:        %d\n\n", file_count);
}

void cmd_list(const char *path) {
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];
    
    if (!path || strcmp(path, "") == 0 || strcmp(path, "/") == 0) {
        parent = ZFS_ROOT_DIR_INDEX;
    } else {
        if (resolve_path(path, &parent, name) < 0) {
            fprintf(stderr, "Invalid path: %s\n", path);
            return;
        }
        
        if (strlen(name) > 0) {
            int found = -1;
            for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
                if (entry_table[i].type == ZFS_TYPE_DIRECTORY &&
                    entry_table[i].parent_index == parent &&
                    strcmp(entry_table[i].name, name) == 0) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                fprintf(stderr, "Directory not found: %s\n", path);
                return;
            }
            parent = found;
        }
    }
    
    char full_path[256];
    build_path(parent, full_path, sizeof(full_path));
    
    printf("\nDirectory: %s\n", full_path);
    printf("%-5s %-40s %10s %10s\n", "Type", "Name", "Size", "Block");
    printf("--------------------------------------------------------------------------------\n");
    
    int file_count = 0;
    int dir_count = 0;
    
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type != ZFS_TYPE_UNUSED &&
            entry_table[i].parent_index == parent) {
            if (entry_table[i].type == ZFS_TYPE_DIRECTORY) {
                printf("%-5s %-40s %10s %10s\n", 
                       "[DIR]", entry_table[i].name, "-", "-");
                dir_count++;
            } else if (entry_table[i].type == ZFS_TYPE_FILE) {
                printf("%-5s %-40s %10u %10u\n",
                       "", entry_table[i].name,
                       entry_table[i].size,
                       entry_table[i].start_block);
                file_count++;
            }
        }
    }
    
    if (file_count == 0 && dir_count == 0) {
        printf("(empty)\n");
    }
    printf("\nTotal: %d directories, %d files\n\n", dir_count, file_count);
}

void cmd_mkdir(const char *path) {
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];
    
    if (resolve_path(path, &parent, name) < 0 || strlen(name) == 0) {
        fprintf(stderr, "Invalid path: %s\n", path);
        return;
    }
    
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type != ZFS_TYPE_UNUSED &&
            entry_table[i].parent_index == parent &&
            strcmp(entry_table[i].name, name) == 0) {
            fprintf(stderr, "Already exists: %s\n", name);
            return;
        }
    }
    
    int free_idx = -1;
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type == ZFS_TYPE_UNUSED) {
            free_idx = i;
            break;
        }
    }
    
    if (free_idx < 0) {
        fprintf(stderr, "No free entries\n");
        return;
    }
    
    strncpy(entry_table[free_idx].name, name, ZFS_MAX_FILENAME - 1);
    entry_table[free_idx].name[ZFS_MAX_FILENAME - 1] = '\0';
    entry_table[free_idx].type = ZFS_TYPE_DIRECTORY;
    entry_table[free_idx].parent_index = parent;
    entry_table[free_idx].size = 0;
    entry_table[free_idx].start_block = 0;
    entry_table[free_idx].block_count = 0;
    
    superblock.entry_count++;
    
    if (save_filesystem() != 0) {
        fprintf(stderr, "Failed to update filesystem\n");
        return;
    }
    
    printf("Created directory: %s\n", name);
}

uint32_t find_free_space(uint32_t blocks_needed) {
    uint32_t start_block = 0;
    uint32_t found_blocks = 0;
    
    for (uint32_t block = 0; block < superblock.total_blocks; block++) {
        int is_free = 1;
        
        for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
            if (entry_table[i].type == ZFS_TYPE_FILE) {
                uint32_t file_start = entry_table[i].start_block;
                uint32_t file_end = file_start + entry_table[i].block_count;
                
                if (block >= file_start && block < file_end) {
                    is_free = 0;
                    break;
                }
            }
        }
        
        if (is_free) {
            if (found_blocks == 0) {
                start_block = block;
            }
            found_blocks++;
            
            if (found_blocks >= blocks_needed) {
                return start_block;
            }
        } else {
            found_blocks = 0;
        }
    }
    
    return 0xFFFFFFFF;
}

void cmd_import(const char *host_path, const char *zfs_path) {
    FILE *f = fopen(host_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open host file: %s\n", host_path);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];
    
    if (resolve_path(zfs_path, &parent, name) < 0 || strlen(name) == 0) {
        fprintf(stderr, "Invalid path: %s\n", zfs_path);
        fclose(f);
        return;
    }
    
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type != ZFS_TYPE_UNUSED &&
            entry_table[i].parent_index == parent &&
            strcmp(entry_table[i].name, name) == 0) {
            fprintf(stderr, "Already exists: %s\n", name);
            fclose(f);
            return;
        }
    }
    
    uint32_t blocks_needed = (size + ZFS_BLOCK_SIZE - 1) / ZFS_BLOCK_SIZE;
    if (blocks_needed == 0) blocks_needed = 1;
    
    if (blocks_needed > superblock.free_blocks) {
        fprintf(stderr, "Not enough space (%d blocks needed, %d free)\n",
                blocks_needed, superblock.free_blocks);
        fclose(f);
        return;
    }
    
    int free_entry = -1;
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type == ZFS_TYPE_UNUSED) {
            free_entry = i;
            break;
        }
    }
    
    if (free_entry == -1) {
        fprintf(stderr, "Too many entries (max %d)\n", ZFS_MAX_ENTRIES);
        fclose(f);
        return;
    }
    
    uint32_t start_block = find_free_space(blocks_needed);
    if (start_block == 0xFFFFFFFF) {
        fprintf(stderr, "Could not find contiguous space\n");
        fclose(f);
        return;
    }
    
    uint8_t *block_buffer = malloc(ZFS_BLOCK_SIZE);
    if (!block_buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return;
    }
    
    for (uint32_t i = 0; i < blocks_needed; i++) {
        memset(block_buffer, 0, ZFS_BLOCK_SIZE);
        size_t bytes_read = fread(block_buffer, 1, ZFS_BLOCK_SIZE, f);
        
        uint32_t lba = ZFS_DATA_START_LBA + ((start_block + i) * 8);
        if (write_sector(lba, block_buffer, 8) != 0) {
            fprintf(stderr, "Write failed at block %d\n", i);
            free(block_buffer);
            fclose(f);
            return;
        }
    }
    
    free(block_buffer);
    fclose(f);
    
    strncpy(entry_table[free_entry].name, name, ZFS_MAX_FILENAME - 1);
    entry_table[free_entry].name[ZFS_MAX_FILENAME - 1] = '\0';
    entry_table[free_entry].size = size;
    entry_table[free_entry].start_block = start_block;
    entry_table[free_entry].block_count = blocks_needed;
    entry_table[free_entry].type = ZFS_TYPE_FILE;
    entry_table[free_entry].parent_index = parent;
    
    superblock.entry_count++;
    superblock.free_blocks -= blocks_needed;
    
    if (save_filesystem() != 0) {
        fprintf(stderr, "Failed to update filesystem\n");
        return;
    }
    
    char full_path[256];
    build_path(parent, full_path, sizeof(full_path));
    printf("Imported: %s -> %s/%s (%ld bytes, %d blocks)\n", 
           host_path, full_path, name, size, blocks_needed);
}

void cmd_export(const char *zfs_path, const char *host_path) {
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];
    
    if (resolve_path(zfs_path, &parent, name) < 0 || strlen(name) == 0) {
        fprintf(stderr, "Invalid path: %s\n", zfs_path);
        return;
    }
    
    int idx = -1;
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type == ZFS_TYPE_FILE &&
            entry_table[i].parent_index == parent &&
            strcmp(entry_table[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        fprintf(stderr, "File not found: %s\n", zfs_path);
        return;
    }
    
    FILE *f = fopen(host_path, "wb");
    if (!f) {
        fprintf(stderr, "Cannot create host file: %s\n", host_path);
        return;
    }
    
    zfs_entry_t *entry = &entry_table[idx];
    uint8_t *block_buffer = malloc(ZFS_BLOCK_SIZE);
    if (!block_buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        return;
    }
    
    uint32_t bytes_left = entry->size;
    
    for (uint32_t i = 0; i < entry->block_count; i++) {
        uint32_t lba = ZFS_DATA_START_LBA + ((entry->start_block + i) * 8);
        if (read_sector(lba, block_buffer, 8) != 0) {
            fprintf(stderr, "Read failed at block %d\n", i);
            free(block_buffer);
            fclose(f);
            return;
        }
        
        uint32_t to_write = (bytes_left > ZFS_BLOCK_SIZE) ? ZFS_BLOCK_SIZE : bytes_left;
        fwrite(block_buffer, 1, to_write, f);
        bytes_left -= to_write;
    }
    
    free(block_buffer);
    fclose(f);
    
    printf("Exported: %s -> %s (%u bytes)\n", name, host_path, entry->size);
}

void cmd_delete(const char *zfs_path) {
    uint8_t parent;
    char name[ZFS_MAX_FILENAME];
    
    if (resolve_path(zfs_path, &parent, name) < 0 || strlen(name) == 0) {
        fprintf(stderr, "Invalid path: %s\n", zfs_path);
        return;
    }
    
    int idx = -1;
    for (int i = 0; i < ZFS_MAX_ENTRIES; i++) {
        if (entry_table[i].type == ZFS_TYPE_FILE &&
            entry_table[i].parent_index == parent &&
            strcmp(entry_table[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        fprintf(stderr, "File not found: %s\n", zfs_path);
        return;
    }
    
    uint32_t freed_blocks = entry_table[idx].block_count;
    memset(&entry_table[idx], 0, sizeof(zfs_entry_t));
    
    superblock.entry_count--;
    superblock.free_blocks += freed_blocks;
    
    if (save_filesystem() != 0) {
        fprintf(stderr, "Failed to update filesystem\n");
        return;
    }
    
    printf("Deleted: %s (%d blocks freed)\n", name, freed_blocks);
}

void show_help() {
    printf("ZenFS Manager - Manage ZenFS filesystems from Linux\n\n");
    printf("Usage: zfs_man <vhd_file> <command> [args]\n\n");
    printf("Commands:\n");
    printf("  format                Format the VHD with ZenFS\n");
    printf("  info                  Show filesystem statistics\n");
    printf("  list [path]           List directory contents (default: /)\n");
    printf("  mkdir <path>          Create directory\n");
    printf("  import <src> <dst>    Import file from host to ZenFS\n");
    printf("  export <src> <dst>    Export file from ZenFS to host\n");
    printf("  delete <file>         Delete file from ZenFS\n");
    printf("  help                  Show this help\n\n");
    printf("Examples:\n");
    printf("  zfs_man ZenOS.vhd format\n");
    printf("  zfs_man ZenOS.vhd mkdir /boot\n");
    printf("  zfs_man ZenOS.vhd import kernel.bin /boot/kernel\n");
    printf("  zfs_man ZenOS.vhd list /boot\n");
    printf("  zfs_man ZenOS.vhd export /boot/kernel kernel_backup.bin\n");
}

int main(int argc, char **argv) {
    if (argc < 3) {
        show_help();
        return 1;
    }
    
    const char *vhd_path = argv[1];
    const char *command = argv[2];
    
    if (strcmp(command, "help") == 0) {
        show_help();
        return 0;
    }
    
    vhd_fd = open(vhd_path, O_RDWR);
    if (vhd_fd < 0) {
        fprintf(stderr, "Cannot open VHD file: %s\n", vhd_path);
        return 1;
    }
    
    if (detect_vhd_offset() != 0) {
        fprintf(stderr, "Failed to detect VHD format\n");
        close(vhd_fd);
        return 1;
    }
    
    if (strcmp(command, "format") == 0) {
        cmd_format();
    } else {
        if (load_filesystem() != 0) {
            close(vhd_fd);
            return 1;
        }
        
        if (strcmp(command, "info") == 0) {
            cmd_info();
        } else if (strcmp(command, "list") == 0) {
            const char *path = (argc >= 4) ? argv[3] : "/";
            cmd_list(path);
        } else if (strcmp(command, "mkdir") == 0) {
            if (argc != 4) {
                fprintf(stderr, "Usage: mkdir <path>\n");
            } else {
                cmd_mkdir(argv[3]);
            }
        } else if (strcmp(command, "import") == 0) {
            if (argc != 5) {
                fprintf(stderr, "Usage: import <host_file> <zfs_path>\n");
            } else {
                cmd_import(argv[3], argv[4]);
            }
        } else if (strcmp(command, "export") == 0) {
            if (argc != 5) {
                fprintf(stderr, "Usage: export <zfs_path> <host_file>\n");
            } else {
                cmd_export(argv[3], argv[4]);
            }
        } else if (strcmp(command, "delete") == 0) {
            if (argc != 4) {
                fprintf(stderr, "Usage: delete <zfs_path>\n");
            } else {
                cmd_delete(argv[3]);
            }
        } else {
            fprintf(stderr, "Unknown command: %s\n", command);
            show_help();
        }
    }
    
    close(vhd_fd);
    return 0;
}