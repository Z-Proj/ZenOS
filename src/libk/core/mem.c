#include "mem.h"
#include "../debug/log.h"
#include "../debug/serial.h"
#include "../string.h"
#include "../limine.h"
#include "../spinlock.h"

static spinlock_t heap_lock;
static spinlock_t pmm_lock;

static uint8_t *pmm_bitmap = NULL;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static uint64_t bitmap_size = 0;

static uint64_t memory_base = 0;
static uint64_t memory_top = 0;

typedef struct header
{
    size_t size;
    int free;
    struct header *next;
} header_t;

#define HEADER_SIZE sizeof(header_t)
static header_t *heap_start = NULL;

static page_table_t *kernel_pml4 = NULL;

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

static void set_bit(uint64_t bit)
{
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static void clear_bit(uint64_t bit)
{
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static int test_bit(uint64_t bit)
{
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

static uint64_t find_free_pages(size_t count)
{
    if (count == 1)
    {
        for (uint64_t i = 0; i < total_pages; i++)
        {
            if (!test_bit(i))
            {
                return i;
            }
        }
    }
    else
    {
        for (uint64_t i = 0; i <= total_pages - count; i++)
        {
            int found = 1;
            for (size_t j = 0; j < count; j++)
            {
                if (test_bit(i + j))
                {
                    found = 0;
                    break;
                }
            }
            if (found)
            {
                return i;
            }
        }
    }
    return UINT64_MAX;
}

void init_pmm(void)
{
    struct limine_memmap_response *memmap = memmap_request.response;
    if (!memmap)
    {
        serial_write_string("\x1b[38;2;255;50;50m[src/libk/core/mem.c:???]- Failed to get memory map!\n");
        return;
    }
    memory_base = UINT64_MAX;
    memory_top = 0;

    for (size_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            if (entry->base < memory_base)
            {
                memory_base = entry->base;
            }
            if (entry->base + entry->length > memory_top)
            {
                memory_top = entry->base + entry->length;
            }
        }
    }
    total_pages = (memory_top - memory_base) / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;
    uint64_t bitmap_phys = 0;
    for (size_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size)
        {
            bitmap_phys = entry->base;
            break;
        }
    }
    if (!bitmap_phys)
    {
        serial_write_string("\x1b[38;2;255;50;50m[src/libk/core/mem.c:???]- Can't find space for bitmap!\n");
        return;
    }
    pmm_bitmap = (uint8_t *)(bitmap_phys + KERNEL_VIRT_OFFSET);
    for (uint64_t i = 0; i < bitmap_size; i++)
    {
        pmm_bitmap[i] = 0xFF;
    }
    used_pages = total_pages;
    for (size_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t start_page = (entry->base - memory_base) / PAGE_SIZE;
            uint64_t page_count = entry->length / PAGE_SIZE;

            for (uint64_t j = 0; j < page_count; j++)
            {
                if (start_page + j < total_pages)
                {
                    clear_bit(start_page + j);
                    used_pages--;
                }
            }
        }
    }
    uint64_t bitmap_start_page = (bitmap_phys - memory_base) / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < bitmap_pages; i++)
    {
        if (bitmap_start_page + i < total_pages && !test_bit(bitmap_start_page + i))
        {
            set_bit(bitmap_start_page + i);
            used_pages++;
        }
    }
    spinlock_init(&pmm_lock);
    
    uint64_t total_mem = get_total_memory();
    total_mem += 1024*1024; // account for the minor difference
    if (total_mem < (80 * 1024 * 1024)) {
        serial_write_string("\x1b[38;2;255;50;50m\nInduced Kernel Panic\n\n    - At : src/libk/core/mem.c\n    - Line : ???\n\n    - Error Log : Minimum 80MB RAM required (found ");
        char mem_str[32];
        itoa(total_mem / (1024 * 1024), mem_str);
        serial_write_string(mem_str);
        serial_write_string(" MB)\n\x1b[0m");
        __asm__ __volatile__("cli");
        for (;;) __asm__ __volatile__("hlt");
    }
    
    serial_write_string("[src/libk/core/mem.c:???]- PMM Initialized successfully\n");
}

uint64_t alloc_page(void)
{
    return alloc_pages(1);
}

uint64_t alloc_pages(size_t count)
{
    if (!pmm_bitmap || count == 0)
        return 0;

    spinlock_acquire(&pmm_lock);
    uint64_t page_idx = find_free_pages(count);
    if (page_idx == UINT64_MAX)
    {
        spinlock_release(&pmm_lock);
        return 0;
    }
    for (size_t i = 0; i < count; i++)
    {
        set_bit(page_idx + i);
        used_pages++;
    }
    spinlock_release(&pmm_lock);
    return memory_base + (page_idx * PAGE_SIZE);
}

void free_page(uint64_t addr)
{
    free_pages(addr, 1);
}

void free_pages(uint64_t addr, size_t count)
{
    if (!pmm_bitmap || addr < memory_base || addr >= memory_top)
    {
        return;
    }
    spinlock_acquire(&pmm_lock);

    uint64_t page_idx = (addr - memory_base) / PAGE_SIZE;

    for (size_t i = 0; i < count; i++)
    {
        if (page_idx + i < total_pages && test_bit(page_idx + i))
        {
            clear_bit(page_idx + i);
            used_pages--;
        }
    }
    spinlock_release(&pmm_lock);
}

uint64_t get_total_memory(void)
{
    return total_pages * PAGE_SIZE;
}

uint64_t get_free_memory(void)
{
    return (total_pages - used_pages) * PAGE_SIZE;
}

void init_kernel_heap(void)
{
    spinlock_init(&heap_lock);
    uint64_t heap_pages = 16384;
    uint64_t heap_phys = alloc_pages(heap_pages);
    if (!heap_phys)
    {
        serial_write_string("[src/libk/core/mem.c:???]- Failed to allocate heap pages!\n");
        return;
    }
    uint64_t heap_virt = heap_phys + KERNEL_VIRT_OFFSET;
    heap_start = (header_t *)heap_virt;
    heap_start->size = (heap_pages * PAGE_SIZE) - HEADER_SIZE;
    heap_start->free = 1;
    heap_start->next = NULL;
    log("Kernel heap initialized.", 4, 0);
}

void print_mem_info(int vis)
{
    log("\n -> Memory Statistics:\n\n - Total Memory:\n   %lu MBs.\n\n - Free Memory:\n   %lu MBs.\n\n - Used Memory:\n   %lu MBs.\n", 1, vis, get_total_memory() / 1048576, get_free_memory() / 1048576, (get_total_memory() - get_free_memory()) / 1048576);
}

void *kmalloc(size_t size)
{
    if (!heap_start)
    {
        return NULL;
    }
    if (size == 0)
    {
        return NULL;
    }
    spinlock_acquire(&heap_lock);
    size = (size + 7) & ~7;

    header_t *curr = heap_start;
    while (curr)
    {
        if (curr->free && curr->size >= size)
        {
            if (curr->size > size + HEADER_SIZE + 8)
            {
                header_t *next = (header_t *)((uint8_t *)curr + HEADER_SIZE + size);
                next->size = curr->size - size - HEADER_SIZE;
                next->free = 1;
                next->next = curr->next;
                curr->size = size;
                curr->next = next;
            }

            curr->free = 0;
            spinlock_release(&heap_lock);
            return (void *)((uint8_t *)curr + HEADER_SIZE);
        }
        curr = curr->next;
    }
    serial_write_string("\x1b[38;2;255;50;50m[src/libk/core/mem.c:???]- No suitable block found.\n");
    spinlock_release(&heap_lock);
    return NULL;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    spinlock_acquire(&heap_lock);
    header_t *block = (header_t *)((uint8_t *)ptr - HEADER_SIZE);
    block->free = 1;
    if (block->next && block->next->free)
    {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }
    header_t *curr = heap_start;
    while (curr && curr->next != block)
    {
        curr = curr->next;
    }
    if (curr && curr->free)
    {
        curr->size += HEADER_SIZE + block->size;
        curr->next = block->next;
    }
    spinlock_release(&heap_lock);
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0)
    {
        kfree(ptr);
        return NULL;
    }
    header_t *block = (header_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (block->size >= size)
    {
        return ptr;
    }
    void *new_mem = kmalloc(size);
    if (new_mem)
    {
        size_t copy_size = (block->size < size) ? block->size : size;
        for (size_t i = 0; i < copy_size; i++)
        {
            ((char *)new_mem)[i] = ((char *)ptr)[i];
        }
        kfree(ptr);
    }
    return new_mem;
}

static uint64_t get_pml4_index(uint64_t vaddr)
{
    return (vaddr >> 39) & 0x1FF;
}

static uint64_t get_pdpt_index(uint64_t vaddr)
{
    return (vaddr >> 30) & 0x1FF;
}

static uint64_t get_pd_index(uint64_t vaddr)
{
    return (vaddr >> 21) & 0x1FF;
}

static uint64_t get_pt_index(uint64_t vaddr)
{
    return (vaddr >> 12) & 0x1FF;
}

page_table_t *create_page_directory(void)
{
    uint64_t phys = alloc_page();
    if (!phys)
        return NULL;

    page_table_t *pml4 = (page_table_t *)(phys + KERNEL_VIRT_OFFSET);
    memset(pml4, 0, PAGE_SIZE);
    return pml4;
}

void map_page(page_table_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx = get_pd_index(virt);
    uint64_t pt_idx = get_pt_index(virt);

    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
    {
        uint64_t pdpt_phys = alloc_page();
        if (!pdpt_phys)
            return;
        page_table_t *pdpt = (page_table_t *)(pdpt_phys + KERNEL_VIRT_OFFSET);
        memset(pdpt, 0, PAGE_SIZE);
        pml4->entries[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    else if (flags & PAGE_USER)
    {
        pml4->entries[pml4_idx] |= PAGE_USER;
    }
    page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT))
    {
        uint64_t pd_phys = alloc_page();
        if (!pd_phys)
            return;
        page_table_t *pd = (page_table_t *)(pd_phys + KERNEL_VIRT_OFFSET);
        memset(pd, 0, PAGE_SIZE);
        pdpt->entries[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    else if (flags & PAGE_USER)
    {
        pdpt->entries[pdpt_idx] |= PAGE_USER;
    }
    page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
    if (!(pd->entries[pd_idx] & PAGE_PRESENT))
    {
        uint64_t pt_phys = alloc_page();
        if (!pt_phys)
            return;
        page_table_t *pt = (page_table_t *)(pt_phys + KERNEL_VIRT_OFFSET);
        memset(pt, 0, PAGE_SIZE);
        pd->entries[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    else if (flags & PAGE_USER)
    {
            if (pd->entries[pd_idx] & (1ULL << 7)) {
                uint64_t pt_phys = alloc_page();
                if (!pt_phys)
                    return;
                page_table_t *pt = (page_table_t *)(pt_phys + KERNEL_VIRT_OFFSET);
                memset(pt, 0, PAGE_SIZE);
                pd->entries[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            } else {
                pd->entries[pd_idx] |= PAGE_USER;
            }
    }

    page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
    pt->entries[pt_idx] = (phys & 0x000FFFFFFFFFF000) | (flags & 0x8000000000000FFF);
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void switch_page_directory(page_table_t *pml4)
{
    uint64_t phys = (uint64_t)pml4 - KERNEL_VIRT_OFFSET;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

uint64_t virt_to_phys(page_table_t *pml4, uint64_t virt)
{
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx = get_pd_index(virt);
    uint64_t pt_idx = get_pt_index(virt);

    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
        return 0;
    page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT))
        return 0;
    page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pd->entries[pd_idx] & PAGE_PRESENT))
        return 0;
    page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pt->entries[pt_idx] & PAGE_PRESENT))
        return 0;
    return (pt->entries[pt_idx] & 0xFFFFFFFFFFFFF000) + (virt & 0xFFF);
}

void unmap_page(page_table_t *pml4, uint64_t virt)
{
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx = get_pd_index(virt);
    uint64_t pt_idx = get_pt_index(virt);

    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
        return;
    page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT))
        return;
    page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pd->entries[pd_idx] & PAGE_PRESENT))
        return;
    page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    pt->entries[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

page_table_t *clone_page_directory(page_table_t *src)
{
    // Get ACTUAL current CR3
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    page_table_t *real_pml4 = (page_table_t*)(cr3 + KERNEL_VIRT_OFFSET);
    
    page_table_t *new_pml4 = create_page_directory();
    if (!new_pml4) return NULL;

    for (int i = 0; i < 512; i++)
    {
        new_pml4->entries[i] = real_pml4->entries[i];
    }

    return new_pml4;
}

/**
 * Free a single page table structure (PT, PD, PDPT)
 * Does NOT free the pages it points to - only the table itself
 */
static void free_page_table_struct(page_table_t *table)
{
    if (!table) return;
    uint64_t phys = (uint64_t)table - KERNEL_VIRT_OFFSET;
    free_page(phys);
}

/**
 * Free an entire page directory hierarchy
 * This ONLY frees the page table structures themselves, NOT user pages
 * User pages should be freed separately before calling this
 */
void free_page_directory(page_table_t *pml4)
{
    if (!pml4) return;
    
    // Only free user space (lower half, entries 0-255)
    // Kernel space (upper half, entries 256-511) is shared and shouldn't be freed
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
    {
        if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
            continue;
            
        page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
        
        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
        {
            if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT))
                continue;
                
            page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
            
            for (int pd_idx = 0; pd_idx < 512; pd_idx++)
            {
                if (!(pd->entries[pd_idx] & PAGE_PRESENT))
                    continue;
                
                // Check if this is a 2MB page (bit 7 set)
                if (pd->entries[pd_idx] & (1ULL << 7))
                    continue;  // Don't free PT for huge pages
                    
                page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
                
                // Free the PT structure (not the pages it points to!)
                free_page_table_struct(pt);
            }
            
            // Free the PD structure
            free_page_table_struct(pd);
        }
        
        // Free the PDPT structure
        free_page_table_struct(pdpt);
    }
    
    // Finally free the PML4 itself
    free_page_table_struct(pml4);
}

/**
 * Free all user-space pages and page tables for a task
 * Call this when a task dies to reclaim all its memory
 */
void free_task_address_space(page_table_t *pml4, uint64_t user_start, uint64_t user_end)
{
    if (!pml4) return;
    
    // Free all user pages in the range
    for (uint64_t virt = user_start; virt < user_end; virt += PAGE_SIZE)
    {
        uint64_t phys = virt_to_phys(pml4, virt);
        if (phys)
        {
            free_page(phys);
            unmap_page(pml4, virt);
        }
    }
    
    // Now free the page table structures
    free_page_directory(pml4);
}

void init_vmm(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (page_table_t *)(cr3 + KERNEL_VIRT_OFFSET);
    serial_write_string("[src/libk/core/mem.c:???]- VMM initialized.\n");
}

page_table_t *get_kernel_pml4(void)
{
    return kernel_pml4;
}