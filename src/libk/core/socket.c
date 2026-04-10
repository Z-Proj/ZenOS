/**
 * 
 * @file : /src/libk/core/socket.c
 * @brief : Named socket IPC - ring buffer based message passing between tasks.
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

#include "socket.h"
#include "mem.h"
#include "string.h"
#include "../debug/log.h"

static socket_file_t socket_files[SOCKET_MAX_FILES];
static bool initialized = false;
static spinlock_t socket_table_lock;

static uint32_t socket_ring_read_locked(socket_file_t *file, uint8_t *dest, uint32_t size)
{
    uint32_t first = file->capacity - file->read_pos;
    if (first > size)
        first = size;
    memcpy(dest, file->data + file->read_pos, first);
    file->read_pos = (file->read_pos + first) % file->capacity;
    if (first == size)
        return first;
    uint32_t second = size - first;
    memcpy(dest + first, file->data + file->read_pos, second);
    file->read_pos = (file->read_pos + second) % file->capacity;
    return size;
}

static void socket_ring_write_locked(socket_file_t *file, const uint8_t *src, uint32_t size)
{
    uint32_t first = file->capacity - file->write_pos;
    if (first > size)
        first = size;
    memcpy(file->data + file->write_pos, src, first);
    file->write_pos = (file->write_pos + first) % file->capacity;
    if (first == size)
        return;
    uint32_t second = size - first;
    memcpy(file->data + file->write_pos, src + first, second);
    file->write_pos = (file->write_pos + second) % file->capacity;
}

void socket_init(void)
{
    spinlock_init(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        socket_files[i].in_use = false;
        socket_files[i].data = NULL;
        socket_files[i].read_pos = 0;
        socket_files[i].write_pos = 0;
        socket_files[i].available = 0;
        socket_files[i].capacity = 0;
        spinlock_init(&socket_files[i].lock);
        memset(socket_files[i].name, 0, SOCKET_NAME_MAX);
    }

    initialized = true;

    log("Sockets initialized (%d files, %dKB each)", 4, 0,
        SOCKET_MAX_FILES, SOCKET_FILE_SIZE / 1024);
}

socket_error_t socket_create(const char *name)
{
    if (!initialized || !name)
    {
        return SOCKET_ERROR_INVALID;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use && strcmp(socket_files[i].name, name) == 0)
        {
            spinlock_release_irqrestore(&socket_table_lock, table_flags);
            return SOCKET_ERROR_EXISTS;
        }
    }

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (!socket_files[i].in_use)
        {
            socket_files[i].data = (uint8_t *)kmalloc(SOCKET_FILE_SIZE);
            if (!socket_files[i].data)
            {
                spinlock_release_irqrestore(&socket_table_lock, table_flags);
                return SOCKET_ERROR_NO_SPACE;
            }

            strncpy(socket_files[i].name, name, SOCKET_NAME_MAX - 1);
            socket_files[i].name[SOCKET_NAME_MAX - 1] = '\0';
            socket_files[i].capacity = SOCKET_FILE_SIZE;
            socket_files[i].read_pos = 0;
            socket_files[i].write_pos = 0;
            socket_files[i].available = 0;
            socket_files[i].in_use = true;
            spinlock_release_irqrestore(&socket_table_lock, table_flags);

            log("Socket created: %s", 1, 0, name);
            return SOCKET_OK;
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
    return SOCKET_ERROR_FULL;
}

socket_error_t socket_open(const char *name, socket_file_t **file)
{
    if (!initialized || !name || !file)
    {
        return SOCKET_ERROR_INVALID;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use && strcmp(socket_files[i].name, name) == 0)
        {
            *file = &socket_files[i];
            spinlock_release_irqrestore(&socket_table_lock, table_flags);
            return SOCKET_OK;
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
    return SOCKET_ERROR_NOT_FOUND;
}

socket_error_t socket_read(socket_file_t *file, void *buffer, uint32_t size, uint32_t *bytes_read)
{
    if (!initialized || !file || !buffer || !bytes_read)
    {
        return SOCKET_ERROR_INVALID;
    }

    uint64_t flags = spinlock_acquire_irqsave(&file->lock);

    if (!file->in_use || !file->data)
    {
        spinlock_release_irqrestore(&file->lock, flags);
        *bytes_read = 0;
        return SOCKET_ERROR_NOT_FOUND;
    }

    if (file->available == 0)
    {
        *bytes_read = 0;
        spinlock_release_irqrestore(&file->lock, flags);
        return SOCKET_ERROR_NO_DATA;
    }

    uint32_t to_read = (size < file->available) ? size : file->available;
    uint32_t read_count = socket_ring_read_locked(file, (uint8_t *)buffer, to_read);

    file->available -= to_read;
    *bytes_read = read_count;
    spinlock_release_irqrestore(&file->lock, flags);

    return SOCKET_OK;
}

socket_error_t socket_write(socket_file_t *file, const void *buffer, uint32_t size)
{
    if (!initialized || !file || !buffer || size == 0)
    {
        return SOCKET_ERROR_INVALID;
    }

    uint64_t flags = spinlock_acquire_irqsave(&file->lock);

    if (!file->in_use || !file->data)
    {
        spinlock_release_irqrestore(&file->lock, flags);
        return SOCKET_ERROR_NOT_FOUND;
    }

    if (file->available + size > file->capacity)
    {
        spinlock_release_irqrestore(&file->lock, flags);
        return SOCKET_ERROR_BUFFER_FULL;
    }

    socket_ring_write_locked(file, (const uint8_t *)buffer, size);

    file->available += size;
    spinlock_release_irqrestore(&file->lock, flags);

    return SOCKET_OK;
}

socket_error_t socket_delete(const char *name)
{
    if (!initialized || !name)
    {
        return SOCKET_ERROR_INVALID;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use && strcmp(socket_files[i].name, name) == 0)
        {
            uint64_t flags = spinlock_acquire_irqsave(&socket_files[i].lock);
            if (socket_files[i].data)
            {
                kfree(socket_files[i].data);
                socket_files[i].data = NULL;
            }
            socket_files[i].capacity = 0;
            socket_files[i].read_pos = 0;
            socket_files[i].write_pos = 0;
            socket_files[i].available = 0;
            socket_files[i].in_use = false;
            memset(socket_files[i].name, 0, SOCKET_NAME_MAX);
            spinlock_release_irqrestore(&socket_files[i].lock, flags);
            spinlock_release_irqrestore(&socket_table_lock, table_flags);

            log("Socket deleted: %s", 1, 0, name);
            return SOCKET_OK;
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
    return SOCKET_ERROR_NOT_FOUND;
}

socket_error_t socket_close(socket_file_t *file)
{

    if (!file)
    {
        return SOCKET_ERROR_INVALID;
    }
    return SOCKET_OK;
}

uint32_t socket_available(socket_file_t *file)
{
    if (!initialized || !file)
    {
        return 0;
    }

    uint64_t flags = spinlock_acquire_irqsave(&file->lock);
    uint32_t avail = (file->in_use && file->data) ? file->available : 0;
    spinlock_release_irqrestore(&file->lock, flags);

    return avail;
}

bool socket_exists(const char *name)
{
    if (!initialized || !name)
    {
        return false;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use && strcmp(socket_files[i].name, name) == 0)
        {
            spinlock_release_irqrestore(&socket_table_lock, table_flags);
            return true;
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
    return false;
}

void socket_list(void)
{
    if (!initialized)
    {
        return;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);
    int count = 0;

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use)
        {
            log("  [%d] %s - %d/%d bytes", 1, 0,
                i, socket_files[i].name,
                socket_files[i].available,
                socket_files[i].capacity);
            count++;
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
    log("Total: %d/%d sockets", 1, 0, count, SOCKET_MAX_FILES);
}
