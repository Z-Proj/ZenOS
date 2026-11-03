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
#include "../../kernel/sched.h"

static socket_file_t socket_files[SOCKET_MAX_FILES];
static bool initialized = false;
static spinlock_t socket_table_lock;

static socket_error_t socket_lock_pointer(socket_file_t *pointer, socket_file_t **out, uint64_t *irq_flags);

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
        socket_files[i].owner_pid = 0;
        socket_files[i].generation = 0;
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

            socket_files[i].generation++;
            if (socket_files[i].generation == 0)
                socket_files[i].generation = 1;
            strncpy(socket_files[i].name, name, SOCKET_NAME_MAX - 1);
            socket_files[i].name[SOCKET_NAME_MAX - 1] = '\0';
            socket_files[i].capacity = SOCKET_FILE_SIZE;
            socket_files[i].read_pos = 0;
            socket_files[i].write_pos = 0;
            socket_files[i].available = 0;
            socket_files[i].in_use = true;
            task_t *owner = sched_current_task();
            socket_files[i].owner_pid = owner ? owner->pid : 0;
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
    socket_file_t *locked = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_pointer(file, &locked, &flags);
    if (result != SOCKET_OK) return result;
    if (!buffer || !bytes_read) result = SOCKET_ERROR_INVALID;
    else if (!locked->available) { *bytes_read = 0; result = SOCKET_ERROR_NO_DATA; }
    else { uint32_t n = size < locked->available ? size : locked->available;
           *bytes_read = socket_ring_read_locked(locked, (uint8_t *)buffer, n);
           locked->available -= n; result = SOCKET_OK; }
    spinlock_release_irqrestore(&locked->lock, flags);
    return result;
}

socket_error_t socket_write(socket_file_t *file, const void *buffer, uint32_t size)
{
    socket_file_t *locked = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_pointer(file, &locked, &flags);
    if (result != SOCKET_OK) return result;
    if (!buffer || !size) result = SOCKET_ERROR_INVALID;
    else if (locked->available > locked->capacity || size > locked->capacity - locked->available) result = SOCKET_ERROR_BUFFER_FULL;
    else { socket_ring_write_locked(locked, (const uint8_t *)buffer, size);
           locked->available += size; result = SOCKET_OK; }
    spinlock_release_irqrestore(&locked->lock, flags);
    return result;
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
            socket_files[i].owner_pid = 0;
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

void socket_close_owned_by(uint64_t pid)
{
    if (!initialized || pid == 0)
    {
        return;
    }

    uint64_t table_flags = spinlock_acquire_irqsave(&socket_table_lock);

    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        if (socket_files[i].in_use && socket_files[i].owner_pid == pid)
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
            socket_files[i].owner_pid = 0;
            memset(socket_files[i].name, 0, SOCKET_NAME_MAX);
            spinlock_release_irqrestore(&socket_files[i].lock, flags);
        }
    }

    spinlock_release_irqrestore(&socket_table_lock, table_flags);
}

socket_error_t socket_close(socket_file_t *file)
{
    socket_file_t *locked = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_pointer(file, &locked, &flags);
    if (result == SOCKET_OK) spinlock_release_irqrestore(&locked->lock, flags);
    return result;
}

uint32_t socket_available(socket_file_t *file)
{
    socket_file_t *locked = NULL;
    uint64_t flags = 0;
    if (socket_lock_pointer(file, &locked, &flags) != SOCKET_OK) return 0;
    uint32_t available = locked->available;
    spinlock_release_irqrestore(&locked->lock, flags);
    return available;
}

/* Lock a validated socket while still holding the table lock. Deletion and
 * owner cleanup take locks in the same table->file order, so the backing
 * ring buffer cannot be freed or the slot reused during a handle operation. */
static socket_error_t socket_lock_slot(socket_file_t *candidate,
                                         uint32_t expected_generation,
                                         socket_file_t **out,
                                         uint64_t *irq_flags)
{
    if (!initialized || !candidate || !out || !irq_flags)
        return SOCKET_ERROR_INVALID;

    uint64_t flags = spinlock_acquire_irqsave(&socket_table_lock);
    int valid_slot = 0;
    for (int i = 0; i < SOCKET_MAX_FILES; i++)
        if (candidate == &socket_files[i])
        {
            valid_slot = 1;
            break;
        }
    if (!valid_slot || !candidate->in_use || !candidate->data ||
        (expected_generation && candidate->generation != expected_generation))
    {
        spinlock_release_irqrestore(&socket_table_lock, flags);
        return SOCKET_ERROR_NOT_FOUND;
    }

    spinlock_acquire(&candidate->lock);
    spinlock_release_noirq(&socket_table_lock);
    *out = candidate;
    *irq_flags = flags;
    return SOCKET_OK;
}

/* User-visible handles encode both slot and generation. A deleted socket
 * can reuse its table slot without making an old client handle valid again. */
socket_error_t socket_open_handle(const char *name, uint64_t *handle)
{
    if (!initialized || !name || !handle)
        return SOCKET_ERROR_INVALID;
    uint64_t flags = spinlock_acquire_irqsave(&socket_table_lock);
    for (int i = 0; i < SOCKET_MAX_FILES; i++)
    {
        socket_file_t *file = &socket_files[i];
        if (!file->in_use || !file->data || strcmp(file->name, name) != 0)
            continue;
        *handle = SOCKET_HANDLE_MAGIC |
                  ((uint64_t)file->generation << 8) |
                  (uint64_t)(i + 1);
        spinlock_release_irqrestore(&socket_table_lock, flags);
        return SOCKET_OK;
    }
    spinlock_release_irqrestore(&socket_table_lock, flags);
    return SOCKET_ERROR_NOT_FOUND;
}

static socket_error_t socket_lock_handle(uint64_t handle, socket_file_t **out,
                                         uint64_t *irq_flags)
{
    if ((handle & 0xFF00000000000000ULL) != SOCKET_HANDLE_MAGIC)
        return SOCKET_ERROR_INVALID;
    uint32_t slot = (uint32_t)(handle & 0xFF);
    uint32_t generation = (uint32_t)((handle >> 8) & 0xFFFFFFFFULL);
    if (slot == 0 || slot > SOCKET_MAX_FILES || generation == 0)
        return SOCKET_ERROR_INVALID;
    return socket_lock_slot(&socket_files[slot - 1], generation, out, irq_flags);
}

static socket_error_t socket_lock_pointer(socket_file_t *pointer,
                                          socket_file_t **out,
                                          uint64_t *irq_flags)
{
    return socket_lock_slot(pointer, 0, out, irq_flags);
}

socket_error_t socket_read_handle(uint64_t handle, void *buffer, uint32_t size,
                                  uint32_t *bytes_read)
{
    if (!buffer || !bytes_read)
        return SOCKET_ERROR_INVALID;
    socket_file_t *file = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_handle(handle, &file, &flags);
    if (result != SOCKET_OK)
        return result;

    if (!file->in_use || !file->data)
    {
        *bytes_read = 0;
        result = SOCKET_ERROR_NOT_FOUND;
    }
    else if (file->available == 0)
    {
        *bytes_read = 0;
        result = SOCKET_ERROR_NO_DATA;
    }
    else
    {
        uint32_t to_read = size < file->available ? size : file->available;
        *bytes_read = socket_ring_read_locked(file, (uint8_t *)buffer, to_read);
        file->available -= to_read;
        result = SOCKET_OK;
    }
    spinlock_release_irqrestore(&file->lock, flags);
    return result;
}

socket_error_t socket_write_handle(uint64_t handle, const void *buffer, uint32_t size)
{
    if (!buffer || size == 0)
        return SOCKET_ERROR_INVALID;
    socket_file_t *file = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_handle(handle, &file, &flags);
    if (result != SOCKET_OK)
        return result;

    if (!file->in_use || !file->data)
        result = SOCKET_ERROR_NOT_FOUND;
    else if (file->available > file->capacity || size > file->capacity - file->available)
        result = SOCKET_ERROR_BUFFER_FULL;
    else
    {
        socket_ring_write_locked(file, (const uint8_t *)buffer, size);
        file->available += size;
        result = SOCKET_OK;
    }
    spinlock_release_irqrestore(&file->lock, flags);
    return result;
}

uint32_t socket_available_handle(uint64_t handle)
{
    socket_file_t *file = NULL;
    uint64_t flags = 0;
    if (socket_lock_handle(handle, &file, &flags) != SOCKET_OK)
        return 0;
    uint32_t available = file->in_use && file->data ? file->available : 0;
    spinlock_release_irqrestore(&file->lock, flags);
    return available;
}

socket_error_t socket_close_handle(uint64_t handle)
{
    socket_file_t *file = NULL;
    uint64_t flags = 0;
    socket_error_t result = socket_lock_handle(handle, &file, &flags);
    if (result != SOCKET_OK)
        return result;
    spinlock_release_irqrestore(&file->lock, flags);
    return SOCKET_OK;
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
