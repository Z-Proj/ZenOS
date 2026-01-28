#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define KERNEL_VIRT_OFFSET 0xffff800000000000
#define USER_SPACE_BASE 0x400000
#define USER_STACK_TOP 0x800000000000
#define USER_SPACE_START 0x400000      
#define USER_SPACE_END   0x800000000000 
#define USER_HEAP_START  0x10000000 

#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1) 
#define PAGE_USER       (1ULL << 2)
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)

typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(4096))) page_table_t;

void init_pmm(void);
uint64_t alloc_page(void);
uint64_t alloc_pages(size_t count);
void free_page(uint64_t addr);
void free_pages(uint64_t addr, size_t count);
uint64_t get_total_memory(void);
uint64_t get_free_memory(void);
void print_mem_info(int vis);

void init_kernel_heap(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t size);

void init_vmm(void);
page_table_t* create_page_directory(void);
void map_page(page_table_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void switch_page_directory(page_table_t* pml4);
uint64_t virt_to_phys(page_table_t* pml4, uint64_t virt);
void unmap_page(page_table_t* pml4, uint64_t virt);
page_table_t* clone_page_directory(page_table_t* src);
page_table_t* get_kernel_pml4(void);

// New functions for proper cleanup
void free_page_directory(page_table_t* pml4);
void free_task_address_space(page_table_t* pml4, uint64_t user_start, uint64_t user_end);

#endif