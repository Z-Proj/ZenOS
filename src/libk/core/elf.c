#include "elf.h"
#include "../string.h"
#include "../debug/log.h"
#include "../../drv/vga.h"
#include "../../drv/disk/fat.h"
#include "../../drv/disk/vfs.h"
#include "mem.h"
#include "syscall.h"
#include "../spinlock.h"

#define EXEC_MAX_ARGS    128
#define EXEC_MAX_ENVP    128
#define EXEC_MAX_STRLEN  4096
#define USER_PIE_BASE    0x0000000000400000ULL
#define USER_INTERP_BASE 0x0000100000000000ULL
extern void exec_enter_user(uint64_t entry, uint64_t user_rsp) __attribute__((noreturn));

typedef struct {
    uint64_t map_start;
    uint64_t map_end;
    uint64_t entry;
    uint64_t base;
    uint64_t phdr_addr;
    uint64_t phentsize;
    uint64_t phnum;
    uint16_t type;
    char interp_path[256];
} elf_load_result_t;

typedef struct {
    uint64_t at_phdr;
    uint64_t at_phent;
    uint64_t at_phnum;
    uint64_t at_base;
    uint64_t at_entry;
    const char *execfn;
} elf_stack_info_t;

typedef struct {
    uint64_t type;
    uint64_t value;
} elf_auxv_entry_t;

static spinlock_t exec_lock = {0};

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

static void clear_user_range(page_table_t *pml4, uint64_t start, uint64_t end)
{
    if (!pml4 || end <= start) return;

    for (uint64_t v = start; v < end; v += PAGE_SIZE) {
        if (virt_to_phys(pml4, v))
            unmap_page(pml4, v);
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
        if (virt_to_phys(pml4, virt) != phys) {
            free_page(phys);
            free_user_range(pml4, USER_STACK_BASE, USER_STACK_BASE + (i * PAGE_SIZE));
            return -1;
        }
        memset((void *)(phys + KERNEL_VIRT_OFFSET), 0, PAGE_SIZE);
    }

    return 0;
}

static void fill_aux_random(uint8_t *buf, size_t len)
{
    rtc_time_t time = rtc_get_time();
    uint64_t state = rtc_get_ticks();
    state ^= (uint64_t)time.hours << 56;
    state ^= (uint64_t)time.minutes << 48;
    state ^= (uint64_t)time.seconds << 40;
    state ^= (uint64_t)time.milliseconds << 24;
    state ^= 0x9E3779B97F4A7C15ULL;

    for (size_t i = 0; i < len; i++)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        buf[i] = (uint8_t)(state >> ((i & 7) * 8));
    }
}

static int build_user_stack(page_table_t *pml4, int argc, char **argv, int envc, char **envp,
                            const elf_stack_info_t *stack_info, uint64_t *out_rsp)
{
    if (!pml4 || !stack_info || !stack_info->execfn || !out_rsp) return -1;
    if ((argc > 0 && !argv) || (envc > 0 && !envp)) return -1;

    uint64_t *argv_user = NULL;
    uint64_t *envp_user = NULL;
    uint8_t random_bytes[16];
    elf_auxv_entry_t auxv[] = {
        {AT_PHDR, stack_info->at_phdr},
        {AT_PHENT, stack_info->at_phent},
        {AT_PHNUM, stack_info->at_phnum},
        {AT_PAGESZ, PAGE_SIZE},
        {AT_BASE, stack_info->at_base},
        {AT_ENTRY, stack_info->at_entry},
        {AT_SECURE, 0},
        {AT_EXECFN, 0},
        {AT_RANDOM, 0},
        {AT_NULL, 0}
    };
    size_t auxc = sizeof(auxv) / sizeof(auxv[0]);

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
    fill_aux_random(random_bytes, sizeof(random_bytes));

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

    size_t execfn_len = bounded_strlen(stack_info->execfn, EXEC_MAX_STRLEN);
    if (execfn_len >= EXEC_MAX_STRLEN) goto fail;
    execfn_len++;
    sp -= execfn_len;
    if (write_user_bytes(pml4, sp, stack_info->execfn, execfn_len) < 0) goto fail;
    auxv[7].value = sp;

    sp -= sizeof(random_bytes);
    if (write_user_bytes(pml4, sp, random_bytes, sizeof(random_bytes)) < 0) goto fail;
    auxv[8].value = sp;

    sp &= ~0xFULL;

    size_t slots = (size_t)argc + (size_t)envc + 3 + auxc * 2;
    if (slots & 1U) sp -= sizeof(uint64_t);

    for (size_t i = auxc; i > 0; i--) {
        if (push_user_qword(pml4, &sp, auxv[i - 1].value) < 0) goto fail;
        if (push_user_qword(pml4, &sp, auxv[i - 1].type) < 0) goto fail;
    }

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
    int read_rc = vfs_read(fd, elf_data, filesize, &bytes_read);
    if (read_rc != 0 || bytes_read != filesize) {
        kfree(elf_data);
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    *elf_data_out = elf_data;
    *filesize_out = filesize;
    return 0;
}

static int load_elf_into_pml4(page_table_t *pml4, uint8_t *elf_data, uint32_t filesize,
                              uint64_t base_hint, elf_load_result_t *out)
{
    if (!pml4 || !elf_data || !out) return -1;

    if (filesize < sizeof(elf64_ehdr_t)) return -1;

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elf_data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
        return -1;

    if (ehdr->e_ident[4] != ELF_CLASS_64 || ehdr->e_machine != EM_X86_64) return -1;
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) return -1;

    uint64_t ph_table_end = ehdr->e_phoff + (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ph_table_end > filesize) return -1;

    uint64_t min_addr = UINT64_MAX;
    uint64_t max_addr = 0;
    uint64_t phdr_addr = 0;
    memset(out, 0, sizeof(*out));

    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (phdr->p_offset + phdr->p_filesz > filesize) return -1;

        if (phdr->p_type == PT_INTERP) {
            if (phdr->p_filesz == 0 || phdr->p_filesz >= sizeof(out->interp_path))
                return -1;
            memcpy(out->interp_path, elf_data + phdr->p_offset, phdr->p_filesz);
            out->interp_path[phdr->p_filesz - 1] = '\0';
            continue;
        }

        if (phdr->p_type == PT_PHDR) {
            phdr_addr = phdr->p_vaddr;
            continue;
        }

        if (phdr->p_type != PT_LOAD) continue;

        if (phdr->p_memsz < phdr->p_filesz) return -1;

        if (phdr->p_vaddr < min_addr) min_addr = phdr->p_vaddr;
        if (phdr->p_vaddr + phdr->p_memsz > max_addr) max_addr = phdr->p_vaddr + phdr->p_memsz;

        if (!phdr_addr &&
            ehdr->e_phoff >= phdr->p_offset &&
            ph_table_end <= phdr->p_offset + phdr->p_filesz) {
            phdr_addr = phdr->p_vaddr + (ehdr->e_phoff - phdr->p_offset);
        }
    }

    if (min_addr == UINT64_MAX || max_addr <= min_addr) return -1;
    uint64_t map_start = min_addr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t map_end = (max_addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t load_bias = 0;

    if (ehdr->e_type == ET_DYN) {
        uint64_t load_base = base_hint ? (base_hint & ~(uint64_t)(PAGE_SIZE - 1)) : USER_PIE_BASE;
        if (load_base < map_start)
            return -1;
        load_bias = load_base - map_start;
        map_end += load_bias;
        map_start = load_base;
    }

    clear_user_range(pml4, map_start, map_end);

    for (uint64_t v = map_start; v < map_end; v += PAGE_SIZE) {
        uint64_t phys = alloc_page();
        if (!phys) {
            free_user_range(pml4, map_start, v);
            return -1;
        }

        map_page(pml4, v, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        if (virt_to_phys(pml4, v) != phys) {
            free_page(phys);
            free_user_range(pml4, map_start, v);
            return -1;
        }
        memset((void *)(phys + KERNEL_VIRT_OFFSET), 0, PAGE_SIZE);
    }

    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD || phdr->p_filesz == 0) continue;

        if (write_user_bytes(pml4, load_bias + phdr->p_vaddr, elf_data + phdr->p_offset, phdr->p_filesz) < 0) {
            free_user_range(pml4, map_start, map_end);
            return -1;
        }
    }

    out->map_start = map_start;
    out->map_end = map_end;
    out->entry = load_bias + ehdr->e_entry;
    out->base = ehdr->e_type == ET_DYN ? load_bias : 0;
    out->phdr_addr = phdr_addr ? load_bias + phdr_addr : 0;
    out->phentsize = ehdr->e_phentsize;
    out->phnum = ehdr->e_phnum;
    out->type = ehdr->e_type;
    return 0;
}

static int load_elf_path_into_pml4(page_table_t *pml4, const char *filename, uint64_t base_hint, elf_load_result_t *out)
{
    uint8_t *elf_data = NULL;
    uint32_t filesize = 0;

    if (load_elf_file(filename, &elf_data, &filesize) < 0)
        return -1;

    int rc = load_elf_into_pml4(pml4, elf_data, filesize, base_hint, out);
    kfree(elf_data);
    return rc;
}

int elf_exec(const char *filename, int argc, char **argv)
{
    spinlock_acquire_raw(&exec_lock);
    page_table_t *pml4 = clone_page_directory(get_kernel_pml4());
    if (!pml4) {
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    elf_load_result_t loaded;
    if (load_elf_path_into_pml4(pml4, filename, 0, &loaded) < 0) {
        free_page_directory(pml4);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    elf_load_result_t interp_loaded;
    memset(&interp_loaded, 0, sizeof(interp_loaded));
    if (loaded.interp_path[0] && load_elf_path_into_pml4(pml4, loaded.interp_path, USER_INTERP_BASE, &interp_loaded) < 0) {
        free_user_range(pml4, loaded.map_start, loaded.map_end);
        free_page_directory(pml4);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    if (map_user_stack(pml4) < 0) {
        if (interp_loaded.map_end > interp_loaded.map_start)
            free_user_range(pml4, interp_loaded.map_start, interp_loaded.map_end);
        free_user_range(pml4, loaded.map_start, loaded.map_end);
        free_page_directory(pml4);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    elf_stack_info_t stack_info = {
        .at_phdr = loaded.phdr_addr,
        .at_phent = loaded.phentsize,
        .at_phnum = loaded.phnum,
        .at_base = interp_loaded.base,
        .at_entry = loaded.entry,
        .execfn = filename
    };
    uint64_t user_rsp = 0;
    if (build_user_stack(pml4, argc, argv, 0, NULL, &stack_info, &user_rsp) < 0) {
        free_user_range(pml4, USER_STACK_BASE, USER_STACK_BASE + TASK_STACK_SIZE);
        if (interp_loaded.map_end > interp_loaded.map_start)
            free_user_range(pml4, interp_loaded.map_start, interp_loaded.map_end);
        free_user_range(pml4, loaded.map_start, loaded.map_end);
        free_page_directory(pml4);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    uint64_t entry = interp_loaded.entry ? interp_loaded.entry : loaded.entry;

    task_t *task = task_create_user((void (*)(void))entry, filename, pml4, user_rsp, argc, argv, NULL);
    if (!task) {
        free_user_range(pml4, USER_STACK_BASE, USER_STACK_BASE + TASK_STACK_SIZE);
        if (interp_loaded.map_end > interp_loaded.map_start)
            free_user_range(pml4, interp_loaded.map_start, interp_loaded.map_end);
        free_user_range(pml4, loaded.map_start, loaded.map_end);
        free_page_directory(pml4);
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    spinlock_release_raw(&exec_lock);
    return 0;
}

int elf_execve_replace(const char *filename, int argc, char **argv, char **envp)
{
    spinlock_acquire_raw(&exec_lock);
    if (!filename) {
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    task_t *task = sched_current_task();
    if (!task || task->is_kernel_task) {
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    char **kargv = NULL;
    char **kenvp = NULL;
    int envc = 0;

    if (copy_user_argv(argc, argv, &kargv) < 0) {
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    if (copy_user_envp(envp, &kenvp, &envc) < 0) {
        free_string_vector(kargv, argc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }

    page_table_t *new_pml4 = clone_page_directory(get_kernel_pml4());
    if (!new_pml4) {
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    elf_load_result_t loaded;
    if (load_elf_path_into_pml4(new_pml4, filename, 0, &loaded) < 0) {
        free_page_directory(new_pml4);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    elf_load_result_t interp_loaded;
    memset(&interp_loaded, 0, sizeof(interp_loaded));
    if (loaded.interp_path[0] && load_elf_path_into_pml4(new_pml4, loaded.interp_path, USER_INTERP_BASE, &interp_loaded) < 0) {
        free_user_range(new_pml4, loaded.map_start, loaded.map_end);
        free_page_directory(new_pml4);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    if (map_user_stack(new_pml4) < 0) {
        if (interp_loaded.map_end > interp_loaded.map_start)
            free_user_range(new_pml4, interp_loaded.map_start, interp_loaded.map_end);
        free_user_range(new_pml4, loaded.map_start, loaded.map_end);
        free_page_directory(new_pml4);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    elf_stack_info_t stack_info = {
        .at_phdr = loaded.phdr_addr,
        .at_phent = loaded.phentsize,
        .at_phnum = loaded.phnum,
        .at_base = interp_loaded.base,
        .at_entry = loaded.entry,
        .execfn = filename
    };
    uint64_t user_rsp = 0;
    if (build_user_stack(new_pml4, argc, kargv, envc, kenvp, &stack_info, &user_rsp) < 0) {
        free_user_range(new_pml4, USER_STACK_BASE, USER_STACK_BASE + TASK_STACK_SIZE);
        if (interp_loaded.map_end > interp_loaded.map_start)
            free_user_range(new_pml4, interp_loaded.map_start, interp_loaded.map_end);
        free_user_range(new_pml4, loaded.map_start, loaded.map_end);
        free_page_directory(new_pml4);
        free_string_vector(kargv, argc);
        free_string_vector(kenvp, envc);
        spinlock_release_raw(&exec_lock);
        return -1;
    }
    free_string_vector(kargv, argc);
    free_string_vector(kenvp, envc);

    page_table_t *old_pml4 = task->pml4;
    uint8_t old_owns_user_pages = task->owns_user_pages;
    uint64_t old_user_stack = task->user_stack;

    task->pml4 = new_pml4;
    task->owns_user_pages = 1;
    task->heap_brk = USER_HEAP_START;
    task->mmap_base = 0x0000200000000000ULL;
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
    task->user_fs_base = 0;
    task->user_gs_base = 0;

    strncpy(task->name, filename, sizeof(task->name) - 1);
    task->name[sizeof(task->name) - 1] = '\0';

    memset(&task->regs, 0, sizeof(task->regs));
    task->regs.rip = interp_loaded.entry ? interp_loaded.entry : loaded.entry;
    task->regs.rbp = user_rsp;
    task->regs.userrsp = user_rsp;
    task->regs.rflags = 0x202;
    task->regs.cs = 0x23;
    task->regs.ss = 0x1B;
    task->regs.ds = 0x1B;

    switch_page_directory(new_pml4);
    sched_set_active_pml4(new_pml4);
    syscall_prepare_user_return(task->user_gs_base);
    if (old_pml4 && old_pml4 != get_kernel_pml4() && old_pml4 != new_pml4) {
        if (old_owns_user_pages) {
            free_user_pages(old_pml4);
        } else if (old_user_stack) {
            free_user_range(old_pml4, old_user_stack, old_user_stack + TASK_STACK_SIZE);
        }
        free_page_directory(old_pml4);
    }

    spinlock_release_raw(&exec_lock);
    exec_enter_user(task->regs.rip, user_rsp);
}
