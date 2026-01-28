#ifndef ZFS_H
#define ZFS_H

#include <stdint.h>
#include <stddef.h>

#define ZFS_MAGIC           0x53465321      
#define ZFS_MAX_ENTRIES     64              
#define ZFS_MAX_FILENAME    28              
#define ZFS_BLOCK_SIZE      4096            
#define ZFS_SUPERBLOCK_LBA  0
#define ZFS_FILETABLE_LBA   1
#define ZFS_FILETABLE_SIZE  8               
#define ZFS_DATA_START_LBA  9               

#define ZFS_ROOT_DIR_INDEX  0               


#define ZFS_TYPE_UNUSED     0
#define ZFS_TYPE_FILE       1
#define ZFS_TYPE_DIRECTORY  2


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


typedef struct {
    char name[ZFS_MAX_FILENAME];
    uint32_t size;
    uint32_t start_block;
    uint32_t position;                  
    uint8_t entry_index;                
    uint8_t is_open;
} zfs_file_t;


typedef enum {
    ZFS_OK = 0,
    ZFS_ERR_NOT_INITIALIZED,
    ZFS_ERR_INVALID_DRIVE,
    ZFS_ERR_READ_FAILED,
    ZFS_ERR_WRITE_FAILED,
    ZFS_ERR_NOT_ZFS,
    ZFS_ERR_FILE_NOT_FOUND,
    ZFS_ERR_NO_SPACE,
    ZFS_ERR_ALREADY_EXISTS,
    ZFS_ERR_TOO_MANY_ENTRIES,
    ZFS_ERR_NOT_OPEN,
    ZFS_ERR_EOF,
    ZFS_ERR_INVALID_PARAM,
    ZFS_ERR_NOT_EMPTY,
    ZFS_ERR_NOT_A_DIRECTORY,
    ZFS_ERR_IS_DIRECTORY
} zfs_error_t;


zfs_error_t zfs_format(uint8_t drive);
zfs_error_t zfs_init(uint8_t drive);


zfs_error_t zfs_mkdir(const char* dirname);
zfs_error_t zfs_rmdir(const char* dirname);
zfs_error_t zfs_chdir(const char* dirname);
void zfs_get_cwd(char* buffer, size_t size);


zfs_error_t zfs_create(const char* filename, uint32_t size);
zfs_error_t zfs_open(const char* filename, zfs_file_t* file);
zfs_error_t zfs_read(zfs_file_t* file, void* buffer, uint32_t size, uint32_t* bytes_read);
zfs_error_t zfs_write(zfs_file_t* file, const void* buffer, uint32_t size);
zfs_error_t zfs_close(zfs_file_t* file);
zfs_error_t zfs_delete(const char* filename);
zfs_error_t zfs_seek(zfs_file_t* file, uint32_t position);


zfs_error_t zfs_list(void);  
void zfs_print_stats(void);


zfs_error_t zfs_unmount(void);

#endif 