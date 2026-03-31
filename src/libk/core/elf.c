#include "elf.h"
#include "../string.h"
#include "../debug/log.h"
#include "../../drv/vga.h"
#include "../../drv/disk/fat.h"
#include "../../drv/disk/vfs.h"
#include "mem.h"
#include "syscall.h"

#define EXEC_MAX_ARGS    128
#define EXEC_MAX_ENVP    128
#define EXEC_MAX_STRLEN  4096
#define USER_STACK_BASE  0x700000000000ULL

extern void exec_enter_user(uint64_t entry, uint64_t user_rsp) __attribute__((noreturn));

typedef struct {
    uint64_t map_start;
    uint64_t map_end;
    uint64_t entry;
} elf_load_result_t;

static void free_string_vector(char **vec, int count)
{
    if (!vec) return;
    for (int i = 0; i < count; i++) {
        if (vec[i]) kfree(vec[i]);
    }
    kfree(vec);
}

static size_t bounded_strlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max) {
        if (s[n] == '\0') return n;
        n++;
    }
    return max;
}

static int copy_user_string(const char *src, char **out)
{
    if (!src || !out) return -1;

    size_t len = bounded_strlen(src, EXEC_MAX_STRLEN);
    if (len >= EXEC_MAX_STRLEN) return -1;

    char *dst = (char *)kmalloc(len + 1);
    if (!dst) return -1;

    memcpy(dst, src, len);
    dst[len] = '\0';
    *out = dst;
    return 0;
}

static int copy_user_argv(int argc, char **argv, char ***out_argv)
{
    if (!out_argv) return -1;
    if (argc < 0 || argc > EXEC_MAX_ARGS) return -1;
    if (argc > 0 && !argv) return -1;

    char **kargv = (char **)kmalloc(sizeof(char *) * (size_t)(argc + 1));
    if (!kargv) return -1;
    memset(kargv, 0, sizeof(char *) * (size_t)(argc + 1));

    for (int i = 0; i < argc; i++) {
        if (!argv[i] || copy_user_string(argv[i], &kargv[i]) < 0) {
            free_string_vector(kargv, argc);
            return -1;
        }
    }

    kargv[argc] = NULL;
    *out_argv = kargv;
    return 0;
}

static int copy_user_envp(char **envp, char ***out_envp, int *out_envc)
{
    if (!out_envp || !out_envc) return -1;

    if (!envp) {
        *out_envp = NULL;
        *out_envc = 0;
        return 0;
    }

    int envc = 0;
    while (envp[envc]) {
        envc++;
        if (envc > EXEC_MAX_ENVP) return -1;
    }

    char **kenvp = (char **)kmalloc(sizeof(char *) * (size_t)(envc + 1));
    if (!kenvp) return -1;
    memset(kenvp, 0, sizeof(char *) * (size_t)(envc + 1));

    for (int i = 0; i < envc; i++) {
        if (copy_user_string(envp[i], &kenvp[i]) < 0) {
            free_string_vector(kenvp, envc);
            return -1;
        }
    }

    kenvp[envc] = NULL;
    *out_envp = kenvp;
    *out_envc = envc;
    return 0;
}

static int write_user_bytes(page_table_t *pml4, uint64_t vaddr, const void *src, size_t len)
{
    size_t written = 0;
    const uint8_t *in = (const uint8_t *)src;

    while (written < len) {
        uint64_t page_base = (vaddr + written) & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t page_off = (vaddr + written) & (PAGE_SIZE - 1);
        uint64_t phys = virt_to_phys(pml4, page_base);
        if (!phys) return -1;

        size_t chunk = PAGE_SIZE - page_off;
        if (chunk > (len - written)) chunk = len - written;

        uint8_t *dst = (uint8_t *)(phys + KERNEL_VIRT_OFFSET + page_off);
        memcpy(dst, in + written, chunk);
        written += chunk;
    }

    return 0;
}

static int push_user_qword(page_table_t *pml4, uint64_t *sp, uint64_t value)
{
    if (!pml4 || !sp) return -1;
    *sp -= sizeof(uint64_t);
    return write_user_bytes(pml4, *sp, &value, sizeof(uint64_t));
}

static void free_user_range(page_table_t *pml4, uint64_t start, uint64_t end)
{
    if (!pml4 || end <= start) return;

    for (uint64_t v = start; v < end; v += PAGE_SIZE) {
        uint64_t phys = virt_to_phys(pml4, v);
        if (phys) {
            free_page(phys);
            unmap_page(pml4, v);
        }
    }
}

static int map_user_stack(page_table_t *pml4)
{
    if (!pml4) return -1;

    size_t stack_pages = (TASK_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < stack_pages; i++) {
        uint64_t virt = USER_STACK_BASE + (i * PAGE_SIZE);
        uint64_t phys = alloc_page();
        if (!phys) {
            free_user_range(pml4, USER_STACK_BASE, USER_STACK_BASE + (i * PAGE_SIZE));
            return -1;
        }
        map_page(pml4, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        memset((void *)(phys + KERNEL_VIRT_OFFSET), 0, PAGE_SIZE);
    }

    return 0;
}

static int build_user_stack(page_table_t *pml4, int argc, char **argv, int envc, char **envp, uint64_t *out_rsp)
{
    if (!pml4 || !out_rsp) return -1;
    if ((argc > 0 && !argv) || (envc > 0 && !envp)) return -1;

    uint64_t *argv_user = NULL;
    uint64_t *envp_user = NULL;

    if (argc > 0) {
        argv_user = (uint64_t *)kmalloc(sizeof(uint64_t) * (size_t)argc);
        if (!argv_user) return -1;
    }

    if (envc > 0) {
        envp_user = (uint64_t *)kmalloc(sizeof(uint64_t) * (size_t)envc);
        if (!envp_user) {
            if (argv_user) kfree(argv_user);
            return -1;
        }
    }

    uint64_t sp = USER_STACK_BASE + TASK_STACK_SIZE;
    sp &= ~0xFULL;

    for (int i = envc - 1; i >= 0; i--) {
        size_t len = bounded_strlen(envp[i], EXEC_MAX_STRLEN);
        if (len >= EXEC_MAX_STRLEN) goto fail;
        len++;

        sp -= len;
        if (write_user_bytes(pml4, sp, envp[i], len) < 0) goto fail;
        envp_user[i] = sp;
    }

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = bounded_strlen(argv[i], EXEC_MAX_STRLEN);
        if (len >= EXEC_MAX_STRLEN) goto fail;
        len++;

        sp -= len;
        if (write_user_bytes(pml4, sp, argv[i], len) < 0) goto fail;
        argv_user[i] = sp;
    }

    sp &= ~0xFULL;

    size_t slots = (size_t)argc + (size_t)envc + 3;
    if (slots & 1U) sp -= sizeof(uint64_t);

    if (push_user_qword(pml4, &sp, 0) < 0) goto fail;
    for (int i = envc - 1; i >= 0; i--) {
        if (push_user_qword(pml4, &sp, envp_user[i]) < 0) goto fail;
    }

    if (push_user_qword(pml4, &sp, 0) < 0) goto fail;
    for (int i = argc - 1; i >= 0; i--) {
        if (push_user_qword(pml4, &sp, argv_user[i]) < 0) goto fail;
    }

    if (push_user_qword(pml4, &sp, (uint64_t)argc) < 0) goto fail;

    *out_rsp = sp;
    if (argv_user) kfree(argv_user);
    if (envp_user) kfree(envp_user);
    return 0;

fail:
    if (argv_user) kfree(argv_user);
    if (envp_user) kfree(envp_user);
    return -1;
}

static int load_elf_file(const char *filename, uint8_t **elf_data_out, uint32_t *filesize_out)
{
    if (!filename || !elf_data_out || !filesize_out) return -1;

    int fd = vfs_open(filename, 0);
    if (fd < 0) return -1;

    uint32_t filesize = vfs_size(fd);
    if (!filesize) {
        vfs_close(fd);
        return -1;
    }

    uint8_t *elf_data = (uint8_t *)kmalloc(filesize);
    if (!elf_data) {
        vfs_close(fd);
        return -1;
    }

    uint32_t bytes_read = 0;
    if (vfs_read(fd, elf_data, filesize, &bytes_read) != 0 || bytes_read != filesize) {
        kfree(elf_data);
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    *elf_data_out = elf_data;
    *filesize_out = filesize;
    return 0;
}

static int load_elf_into_pml4(page_table_t *pml4, uint8_t *elf_data, uint32_t filesize, elf_load_result_t *out)
{
    if (!pml4 || !elf_data || !out) return -1;

    if (filesize < sizeof(elf64_ehdr_t)) return -1;

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elf_data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
        return -1;

    if (ehdr->e_ident[4] != ELF_CLASS_64) return -1;

    uint64_t ph_table_end = ehdr->e_phoff + (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ph_table_end > filesize) return -1;

    uint64_t min_addr = UINT64_MAX;
    uint64_t max_addr = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD) continue;

        if (phdr->p_memsz < phdr->p_filesz) return -1;
        if (phdr->p_offset + phdr->p_filesz > filesize) return -1;

        if (phdr->p_vaddr < min_addr) min_addr = phdr->p_vaddr;
        if (phdr->p_vaddr + phdr->p_memsz > max_addr) max_addr = phdr->p_vaddr + phdr->p_memsz;
    }

    if (min_addr == UINT64_MAX || max_addr <= min_addr) return -1;

    uint64_t map_start = min_addr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t map_end = (max_addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    for (uint64_t v = map_start; v < map_end; v += PAGE_SIZE) {
        uint64_t phys = alloc_page();
        if (!phys) {
            free_user_range(pml4, map_start, v);
            return -1;
        }

        map_page(pml4, v, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        memset((void *)(phys + KERNEL_VIRT_OFFSET), 0, PAGE_SIZE);
    }

    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD || phdr->p_filesz == 0) continue;

        if (write_user_bytes(pml4, phdr->p_vaddr, elf_data + phdr->p_offset, phdr->p_filesz) < 0) {
            free_user_range(pml4, map_start, map_end);
            return -1;
        }
    }

    out->map_start = map_start;
    out->map_end = map_end;
    out->entry = ehdr->e_entry;
    return 0;
}

int elf_exec(const char *filename, int argc, char **argv)
{
    uint8_t *elf_data = NULL;
    uint32_t filesize = 0;
    if (load_elf_file(filename, &elf_data, &filesize) < 0)
        return -1;

    page_table_t *pml4 = clone_page_directory(get_kernel_pml4());
    if (!pml4) {
        kfree(elf_data);
        return -1;
    }

    elf_load_result_t loaded;
    if (load_elf_into_pml4(pml4, elf_data, filesize, &loaded) < 0) {
        free_page_directory(pml4);
        kfree(elf_data);
        return -1;
    }

    kfree(elf_data);

    task_t *task = task_create_user((void (*)(void))loaded.entry, filename, pml4, argc, argv);
    if (!task) {
        free_user_range(pml4, loaded.map_start, loaded.map_end);
        free_page_directory(pml4);
        return -1;
    }

    return 0;
}

int elf_execve_replace(const char *filename, int argc, char **argv, char **envp)
{
    if (!filename) return -1;

    task_t *task = sched_current_task();
    if (!task || task->is_kernel_task) return -1;

    char **kargv = NULL;
    char **kenvp = NULL;
    int envc = 0;

    if (copy_user_argv(argc, argv, &kargv) < 0) {
        return -1;
    }

    if (copy_user_envp(envp, &kenvp, &envc) < 0) {
        free_string_vector(kargv, argc);
        return -1;
    }

    uint8_t *elf_data = NULL;
    uint32_t filesize = 0;
    if (load_elf_file(filename, &elf_data, &filesize) < 0) {
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        return -1;
    }

    page_table_t *new_pml4 = clone_page_directory(get_kernel_pml4());
    if (!new_pml4) {
        kfree(elf_data);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        return -1;
    }

    elf_load_result_t loaded;
    if (load_elf_into_pml4(new_pml4, elf_data, filesize, &loaded) < 0) {
        free_page_directory(new_pml4);
        kfree(elf_data);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        return -1;
    }

    if (map_user_stack(new_pml4) < 0) {
        free_user_range(new_pml4, loaded.map_start, loaded.map_end);
        free_page_directory(new_pml4);
        kfree(elf_data);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        return -1;
    }

    uint64_t user_rsp = 0;
    if (build_user_stack(new_pml4, argc, kargv, envc, kenvp, &user_rsp) < 0) {
        free_user_range(new_pml4, USER_STACK_BASE, USER_STACK_BASE + TASK_STACK_SIZE);
        free_user_range(new_pml4, loaded.map_start, loaded.map_end);
        free_page_directory(new_pml4);
        kfree(elf_data);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        return -1;
    }

    kfree(elf_data);
    free_string_vector(kargv, argc);
    free_string_vector(kenvp, envc);

    page_table_t *old_pml4 = task->pml4;

    task->pml4 = new_pml4;
    task->heap_brk = USER_HEAP_START;
    task->argc = argc;
    task->argv = NULL;
    task->envp = NULL;
    task->user_stack = USER_STACK_BASE;

    task->wait_status = 0;
    task->waiting_on_pid = 0;
    task->wait_pid_target = -1;
    task->wait_result_pid = -1;

    memset(task->sighandlers, 0, sizeof(task->sighandlers));
    task->sig_pending = 0;
    task->sig_mask = 0;
    task->sig_trampoline = 0;

    strncpy(task->name, filename, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';

    memset(&task->regs, 0, sizeof(task->regs));
    task->regs.rip = loaded.entry;
    task->regs.rbp = user_rsp;
    task->regs.userrsp = user_rsp;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x23;
    task->regs.ss = 0x1B;
    task->regs.ds = 0x1B;

    switch_page_directory(new_pml4);
    syscall_prepare_user_return(0);

    if (old_pml4 && old_pml4 != get_kernel_pml4() && old_pml4 != new_pml4)
        free_page_directory(old_pml4);

    exec_enter_user(loaded.entry, user_rsp);
}
