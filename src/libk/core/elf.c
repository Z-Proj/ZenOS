#include "elf.h"
#include "../string.h"
#include "../debug/log.h"
#include "../../drv/vga.h"
#include "mem.h"

int elf_exec(const char *filename, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    zfs_file_t file;
    if (zfs_open(filename, &file) != ZFS_OK)
        return -1;
    
    uint8_t *elf_data = (uint8_t *)kmalloc(file.size);
    if (!elf_data)
    {
        zfs_close(&file);
        return -1;
    }
    
    uint32_t bytes_read;
    if (zfs_read(&file, elf_data, file.size, &bytes_read) != ZFS_OK)
    {
        kfree(elf_data);
        zfs_close(&file);
        return -1;
    }
    zfs_close(&file);
    
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elf_data;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F')
    {
        kfree(elf_data);
        return -1;
    }
    
    if (ehdr->e_ident[4] != ELF_CLASS_64)
    {
        kfree(elf_data);
        return -1;
    }
    
    uint64_t min_addr = 0xFFFFFFFFFFFFFFFF;
    uint64_t max_addr = 0;
    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_LOAD)
        {
            if (phdr->p_vaddr < min_addr) min_addr = phdr->p_vaddr;
            if (phdr->p_vaddr + phdr->p_memsz > max_addr) max_addr = phdr->p_vaddr + phdr->p_memsz;
        }
    }
    
    uint64_t total_size = max_addr - min_addr;
    size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t user_virt = min_addr;
    page_table_t *pml4 = get_kernel_pml4();
    
    for (size_t i = 0; i < pages; i++)
    {
        uint64_t phys = alloc_page();
        if (!phys)
        {
            for (size_t j = 0; j < i; j++)
            {
                uint64_t p = virt_to_phys(pml4, user_virt + j * PAGE_SIZE);
                if (p) free_page(p);
                unmap_page(pml4, user_virt + j * PAGE_SIZE);
            }
            kfree(elf_data);
            return -1;
        }
        map_page(pml4, user_virt + i * PAGE_SIZE, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        uint64_t kern_addr = phys + KERNEL_VIRT_OFFSET;
        memset((void*)kern_addr, 0, PAGE_SIZE);
    }
    
    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_LOAD)
        {
            for (size_t offset = 0; offset < phdr->p_filesz; offset++)
            {
                uint64_t vaddr = phdr->p_vaddr + offset;
                uint64_t phys = virt_to_phys(pml4, vaddr);
                if (!phys) continue;
                
                uint8_t *dest = (uint8_t*)(phys + KERNEL_VIRT_OFFSET);
                uint8_t byte = elf_data[phdr->p_offset + offset];
                *dest = byte;
            }
        }
    }
    
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    uint64_t entry_point = ehdr->e_entry;
    kfree(elf_data);
    
    task_t *task = task_create_user((void(*)(void))entry_point, filename);
    if (!task)
    {
        for (size_t i = 0; i < pages; i++)
        {
            uint64_t phys = virt_to_phys(pml4, user_virt + i * PAGE_SIZE);
            if (phys) free_page(phys);
            unmap_page(pml4, user_virt + i * PAGE_SIZE);
        }
        return -1;
    }
    return 0;
}