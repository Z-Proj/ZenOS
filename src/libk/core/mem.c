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
static uint64_t pmm_hint = 0;

#define HEAP_ALIGN          16
#define HEAP_MIN_BLOCK      (HEAP_ALIGN * 2)
#define NUM_SIZE_CLASSES    12
#define HEAP_MAGIC          0xDEADBEEFCAFEBABEULL
#define USER_SHM_PML4_INDEX ((0x0000500000000000ULL >> 39) & 0x1FF)

#define BLK_OVERHEAD        (((sizeof(blk_hdr_t) + sizeof(size_t)) + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1))
#define BLK_PAD             (BLK_OVERHEAD - sizeof(blk_hdr_t) - sizeof(size_t))

typedef struct blk_hdr {
    size_t          size;
    uint8_t         used;
    uint8_t         _pad[7];
    struct blk_hdr *prev_free;
    struct blk_hdr *next_free;
} blk_hdr_t;

static blk_hdr_t *free_lists[NUM_SIZE_CLASSES];
static uint8_t   *heap_base  = NULL;
static uint8_t   *heap_end   = NULL;

static int size_class(size_t sz)
{
    if (sz <= 32)   return 0;
    if (sz <= 64)   return 1;
    if (sz <= 128)  return 2;
    if (sz <= 256)  return 3;
    if (sz <= 512)  return 4;
    if (sz <= 1024) return 5;
    if (sz <= 2048) return 6;
    if (sz <= 4096) return 7;
    if (sz <= 8192) return 8;
    if (sz <= 16384) return 9;
    if (sz <= 32768) return 10;
    return 11;
}

static inline size_t *blk_footer(blk_hdr_t *b)
{
    return (size_t *)((uint8_t *)b + sizeof(blk_hdr_t) + b->size + BLK_PAD);
}

static inline blk_hdr_t *blk_next(blk_hdr_t *b)
{
    return (blk_hdr_t *)((uint8_t *)b + BLK_OVERHEAD + b->size);
}

static inline blk_hdr_t *blk_prev(blk_hdr_t *b)
{
    size_t *prev_foot = (size_t *)((uint8_t *)b - sizeof(size_t));
    return (blk_hdr_t *)((uint8_t *)b - BLK_OVERHEAD - *prev_foot);
}

static void fl_insert(blk_hdr_t *b)
{
    int c = size_class(b->size);
    b->next_free = free_lists[c];
    b->prev_free = NULL;
    if (free_lists[c])
        free_lists[c]->prev_free = b;
    free_lists[c] = b;
}

static void fl_remove(blk_hdr_t *b)
{
    int c = size_class(b->size);
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        free_lists[c] = b->next_free;
    if (b->next_free)
        b->next_free->prev_free = b->prev_free;
    b->prev_free = b->next_free = NULL;
}

static void write_tags(blk_hdr_t *b)
{
    *blk_footer(b) = b->size;
}

static blk_hdr_t *coalesce(blk_hdr_t *b)
{
    uint8_t can_prev = ((uint8_t *)b > heap_base);
    uint8_t can_next = ((uint8_t *)blk_next(b) < heap_end);

    blk_hdr_t *prev = can_prev ? blk_prev(b) : NULL;
    blk_hdr_t *next = can_next ? blk_next(b) : NULL;

    int merge_prev = (prev && !prev->used);
    int merge_next = (next && !next->used);

    if (merge_prev) fl_remove(prev);
    if (merge_next) fl_remove(next);

    if (merge_prev && merge_next) {
        prev->size += BLK_OVERHEAD + b->size + BLK_OVERHEAD + next->size;
        write_tags(prev);
        b = prev;
    } else if (merge_prev) {
        prev->size += BLK_OVERHEAD + b->size;
        write_tags(prev);
        b = prev;
    } else if (merge_next) {
        b->size += BLK_OVERHEAD + next->size;
        write_tags(b);
    }

    return b;
}

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

static uint64_t find_free_pages_range(size_t count, uint64_t start, uint64_t end)
{
    if (count == 0 || start >= end || count > end - start)
        return UINT64_MAX;

    if (count == 1)
    {
        for (uint64_t i = start; i < end; i++)
        {
            if (!test_bit(i))
                return i;
        }
    }
    else
    {
        uint64_t limit = end - count + 1;
        for (uint64_t i = start; i < limit;)
        {
            for (size_t j = 0; j < count; j++)
            {
                if (test_bit(i + j))
                {
                    i += j + 1;
                    goto next_page_run;
                }
            }
            return i;
next_page_run:
            ;
        }
    }
    return UINT64_MAX;
}

static uint64_t find_free_pages(size_t count)
{
    if (count == 0 || count > total_pages)
        return UINT64_MAX;

    uint64_t start = pmm_hint;
    if (start >= total_pages)
        start = 0;

    uint64_t page_idx = find_free_pages_range(count, start, total_pages);
    if (page_idx != UINT64_MAX || start == 0)
        return page_idx;

    return find_free_pages_range(count, 0, start);
}

void init_pmm(void)
{
    struct limine_memmap_response *memmap = memmap_request.response;
    if (!memmap)
    {
        serial_write_string("\x1b[38;2;255;50;50m[mem.c:???]- Failed to get memory map!\n");
        return;
    }
    memory_base = UINT64_MAX;
    memory_top = 0;
    pmm_hint = 0;

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
        serial_write_string("\x1b[38;2;255;50;50m[mem.c:???]- Can't find space for bitmap!\n");
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
    total_mem += 1024*1024;
    if (total_mem < (16 * 1024 * 1024)) {
        serial_write_string("\x1b[38;2;255;50;50m\nInduced Kernel Panic\n\n    - At : src/libk/core/mem.c\n    - Line : ???\n\n    - Error Log : Minimum 80MB RAM required (found ");
        char mem_str[32];
        itoa(total_mem / (1024 * 1024), mem_str);
        serial_write_string(mem_str);
        serial_write_string(" MB)\n\x1b[0m");
        __asm__ __volatile__("cli");
        for (;;) __asm__ __volatile__("hlt");
    }
    
    serial_write_string("[mem.c:???]- PMM Initialized successfully\n");
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
    pmm_hint = page_idx + count;
    if (pmm_hint >= total_pages)
        pmm_hint = 0;
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
    if (page_idx < pmm_hint)
        pmm_hint = page_idx;
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

    for (int i = 0; i < NUM_SIZE_CLASSES; i++)
        free_lists[i] = NULL;

    uint64_t heap_pages = 2048;
    uint64_t heap_phys  = alloc_pages(heap_pages);
    if (!heap_phys) {
        serial_write_string("[mem.c:???]- Failed to allocate heap pages!\n");
        return;
    }

    heap_base = (uint8_t *)(heap_phys + KERNEL_VIRT_OFFSET);
    size_t total = heap_pages * PAGE_SIZE;
    heap_end  = heap_base + total;

    blk_hdr_t *blk = (blk_hdr_t *)heap_base;
    blk->size      = total - BLK_OVERHEAD;
    blk->used      = 0;
    blk->prev_free = NULL;
    blk->next_free = NULL;
    write_tags(blk);
    fl_insert(blk);

    log("Kernel heap initialized.", 4, 0);
}

void print_mem_info(int vis)
{
    log("\nMemory Statistics:\n\n - Total Memory:\n   %lu MBs.\n\n - Free Memory:\n   %lu MBs.\n\n - Used Memory:\n   %lu MBs.\n", 1, vis, get_total_memory() / 1048576, get_free_memory() / 1048576, (get_total_memory() - get_free_memory()) / 1048576);
}

void *kmalloc(size_t size)
{
    if (!heap_base || size == 0)
        return NULL;

    size = (size + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1);
    if (size < HEAP_MIN_BLOCK)
        size = HEAP_MIN_BLOCK;

    spinlock_acquire(&heap_lock);

    for (int c = size_class(size); c < NUM_SIZE_CLASSES; c++) {
        blk_hdr_t *b = free_lists[c];
        while (b) {
            if (b->size >= size) {
                fl_remove(b);

                if (b->size >= size + BLK_OVERHEAD + HEAP_MIN_BLOCK) {
                    size_t old_size  = b->size;
                    b->size          = size;
                    blk_hdr_t *split = blk_next(b);
                    split->size      = old_size - size - BLK_OVERHEAD;
                    split->used      = 0;
                    split->prev_free = NULL;
                    split->next_free = NULL;
                    write_tags(b);
                    write_tags(split);
                    fl_insert(split);
                } else {
                    write_tags(b);
                }

                b->used = 1;
                spinlock_release(&heap_lock);
                return (void *)((uint8_t *)b + sizeof(blk_hdr_t));
            }
            b = b->next_free;
        }
    }

    serial_write_string("\x1b[38;2;255;50;50m[mem.c:???]- No suitable block found.\n");
    spinlock_release(&heap_lock);
    return NULL;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    spinlock_acquire(&heap_lock);

    blk_hdr_t *b = (blk_hdr_t *)((uint8_t *)ptr - sizeof(blk_hdr_t));
    b->used = 0;
    b = coalesce(b);
    fl_insert(b);

    spinlock_release(&heap_lock);
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    blk_hdr_t *b = (blk_hdr_t *)((uint8_t *)ptr - sizeof(blk_hdr_t));
    size_t aligned = (size + HEAP_ALIGN - 1) & ~(size_t)(HEAP_ALIGN - 1);

    if (b->size >= aligned)
        return ptr;

    void *new_mem = kmalloc(size);
    if (new_mem) {
        size_t copy_size = b->size < aligned ? b->size : aligned;
        memcpy(new_mem, ptr, copy_size);
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

int protect_page(page_table_t *pml4, uint64_t virt, uint64_t flags)
{
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx = get_pd_index(virt);
    uint64_t pt_idx = get_pt_index(virt);

    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
        return -1;
    page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT) || (pdpt->entries[pdpt_idx] & (1ULL << 7)))
        return -1;
    page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pd->entries[pd_idx] & PAGE_PRESENT) || (pd->entries[pd_idx] & (1ULL << 7)))
        return -1;
    page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pt->entries[pt_idx] & PAGE_PRESENT))
        return -1;

    uint64_t phys = pt->entries[pt_idx] & 0x000FFFFFFFFFF000;
    pt->entries[pt_idx] = phys | (flags & 0x8000000000000FFF);
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void switch_page_directory(page_table_t *pml4)
{
    uint64_t phys = (uint64_t)pml4 - KERNEL_VIRT_OFFSET;
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

uint64_t virt_to_phys(page_table_t *pml4, uint64_t virt) {
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx   = get_pd_index(virt);
    uint64_t pt_idx   = get_pt_index(virt);

    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) return 0;
    page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) return 0;
    if (pdpt->entries[pdpt_idx] & (1ULL << 7))
        return (pdpt->entries[pdpt_idx] & 0xFFFFFFC000000000) + (virt & 0x3FFFFFFF);

    page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return 0;
    if (pd->entries[pd_idx] & (1ULL << 7))
        return (pd->entries[pd_idx] & 0xFFFFFFFFFFE00000) + (virt & 0x1FFFFF);

    page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
    if (!(pt->entries[pt_idx] & PAGE_PRESENT)) return 0;
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
    page_table_t *new_pml4 = create_page_directory();
    if (!new_pml4) return NULL;

    if (!src)
        return new_pml4;

    for (int i = 256; i < 512; i++)
    {
        new_pml4->entries[i] = src->entries[i];
    }
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
    {
        uint64_t src_pml4e = src->entries[pml4_idx];
        if (!(src_pml4e & PAGE_PRESENT))
            continue;

        uint64_t pdpt_phys = alloc_page();
        if (!pdpt_phys)
        {
            free_page_directory(new_pml4);
            return NULL;
        }
        page_table_t *dst_pdpt = (page_table_t *)(pdpt_phys + KERNEL_VIRT_OFFSET);
        memset(dst_pdpt, 0, PAGE_SIZE);

        page_table_t *src_pdpt = (page_table_t *)((src_pml4e & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
        new_pml4->entries[pml4_idx] = pdpt_phys | (src_pml4e & ~0x000FFFFFFFFFF000ULL);

        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
        {
            uint64_t src_pdpte = src_pdpt->entries[pdpt_idx];
            if (!(src_pdpte & PAGE_PRESENT))
                continue;

            if (src_pdpte & (1ULL << 7))
            {
                dst_pdpt->entries[pdpt_idx] = src_pdpte;
                continue;
            }

            uint64_t pd_phys = alloc_page();
            if (!pd_phys)
            {
                free_page_directory(new_pml4);
                return NULL;
            }
            page_table_t *dst_pd = (page_table_t *)(pd_phys + KERNEL_VIRT_OFFSET);
            memset(dst_pd, 0, PAGE_SIZE);
            page_table_t *src_pd = (page_table_t *)((src_pdpte & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
            dst_pdpt->entries[pdpt_idx] = pd_phys | (src_pdpte & ~0x000FFFFFFFFFF000ULL);

            for (int pd_idx = 0; pd_idx < 512; pd_idx++)
            {
                uint64_t src_pde = src_pd->entries[pd_idx];
                if (!(src_pde & PAGE_PRESENT))
                    continue;

                if (src_pde & (1ULL << 7))
                {
                    dst_pd->entries[pd_idx] = src_pde;
                    continue;
                }

                uint64_t pt_phys = alloc_page();
                if (!pt_phys)
                {
                    free_page_directory(new_pml4);
                    return NULL;
                }

                page_table_t *dst_pt = (page_table_t *)(pt_phys + KERNEL_VIRT_OFFSET);
                page_table_t *src_pt = (page_table_t *)((src_pde & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
                memcpy(dst_pt, src_pt, PAGE_SIZE);
                dst_pd->entries[pd_idx] = pt_phys | (src_pde & ~0x000FFFFFFFFFF000ULL);
            }
        }
    }

    return new_pml4;
}

static void free_page_table_struct(page_table_t *table)
{
    if (!table) return;
    uint64_t phys = (uint64_t)table - KERNEL_VIRT_OFFSET;
    free_page(phys);
}

void free_user_pages(page_table_t *pml4)
{
    if (!pml4) return;

    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
    {
        if (pml4_idx == USER_SHM_PML4_INDEX)
            continue;

        if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
            continue;

        page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
        {
            uint64_t pdpte = pdpt->entries[pdpt_idx];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            page_table_t *pd = (page_table_t *)((pdpte & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

            for (int pd_idx = 0; pd_idx < 512; pd_idx++)
            {
                uint64_t pde = pd->entries[pd_idx];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                page_table_t *pt = (page_table_t *)((pde & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);

                for (int pt_idx = 0; pt_idx < 512; pt_idx++)
                {
                    uint64_t pte = pt->entries[pt_idx];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER))
                        continue;

                    free_page(pte & 0xFFFFFFFFFFFFF000);
                    pt->entries[pt_idx] = 0;
                }
            }
        }
    }
}

void free_page_directory(page_table_t *pml4)
{
    if (!pml4) return;
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
    {
        if (!(pml4->entries[pml4_idx] & PAGE_PRESENT))
            continue;
            
        page_table_t *pdpt = (page_table_t *)((pml4->entries[pml4_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
        
        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
        {
            if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT))
                continue;

            if (pdpt->entries[pdpt_idx] & (1ULL << 7))
                continue;
                
            page_table_t *pd = (page_table_t *)((pdpt->entries[pdpt_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
            
            for (int pd_idx = 0; pd_idx < 512; pd_idx++)
            {
                if (!(pd->entries[pd_idx] & PAGE_PRESENT))
                    continue;
                
                if (pd->entries[pd_idx] & (1ULL << 7))
                    continue;
                    
                page_table_t *pt = (page_table_t *)((pd->entries[pd_idx] & 0xFFFFFFFFFFFFF000) + KERNEL_VIRT_OFFSET);
                free_page_table_struct(pt);
            }
            free_page_table_struct(pd);
        }
        free_page_table_struct(pdpt);
    }
    free_page_table_struct(pml4);
}

void free_task_address_space(page_table_t *pml4, uint64_t user_start, uint64_t user_end)
{
    if (!pml4) return;
    for (uint64_t virt = user_start; virt < user_end; virt += PAGE_SIZE)
    {
        uint64_t phys = virt_to_phys(pml4, virt);
        if (phys)
        {
            free_page(phys);
            unmap_page(pml4, virt);
        }
    }
    free_page_directory(pml4);
}

void init_vmm(void)
{
    uint64_t old_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(old_cr3));
    page_table_t *limine_pml4 = (page_table_t *)(old_cr3 + KERNEL_VIRT_OFFSET);
    
    page_table_t *new_kernel_pml4 = create_page_directory();
    if (!new_kernel_pml4) {
        serial_write_string("Failed to create kernel PML4!\n");
        for(;;) __asm__("hlt");
    }
    
    for (int i = 256; i < 512; i++)
    {
        new_kernel_pml4->entries[i] = limine_pml4->entries[i];
    }
    
    for (int i = 0; i < 256; i++)
    {
        if (limine_pml4->entries[i] & PAGE_PRESENT)
            new_kernel_pml4->entries[i] = limine_pml4->entries[i];
    }
    
    kernel_pml4 = new_kernel_pml4;
    
    uint64_t new_cr3 = (uint64_t)new_kernel_pml4 - KERNEL_VIRT_OFFSET;
    __asm__ volatile("mov %0, %%cr3" : : "r"(new_cr3) : "memory");
    
    serial_write_string("[mem.c:???]- VMM initialized.\n");
}

page_table_t *get_kernel_pml4(void)
{
    return kernel_pml4;
}
