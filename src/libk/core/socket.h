#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOCKET_MAX_FILES 64
#define SOCKET_FILE_SIZE (256 * 1024)
#define SOCKET_TOTAL_BUDGET (16 * 1024 * 1024)
#define SOCKET_NAME_MAX 64

typedef enum
{
    SOCKET_OK = 0,
    SOCKET_ERROR_NOT_FOUND = -1,
    SOCKET_ERROR_EXISTS = -2,
    SOCKET_ERROR_FULL = -3,
    SOCKET_ERROR_NO_SPACE = -4,
    SOCKET_ERROR_INVALID = -5,
    SOCKET_ERROR_BUFFER_FULL = -6,
    SOCKET_ERROR_NO_DATA = -7
} socket_error_t;

typedef struct
{
    char name[SOCKET_NAME_MAX];
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t available;
    bool in_use;
} socket_file_t;

void socket_init(void);
socket_error_t socket_create(const char *name);
socket_error_t socket_open(const char *name, socket_file_t **file);
socket_error_t socket_read(socket_file_t *file, void *buffer, uint32_t size, uint32_t *bytes_read);
socket_error_t socket_write(socket_file_t *file, const void *buffer, uint32_t size);
socket_error_t socket_delete(const char *name);
socket_error_t socket_close(socket_file_t *file);
uint32_t socket_available(socket_file_t *file);
bool socket_exists(const char *name);
void socket_list(void);

#endif