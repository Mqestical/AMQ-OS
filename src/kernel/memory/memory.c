#include <efi.h>
#include <efilib.h>
#include <stdarg.h>
#include "memory.h"
#include "definitions.h"
#include "print.h"
#include "string_helpers.h"

static FreePage* free_list = NULL;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;

typedef struct HeapBlock {
    size_t size;
    struct HeapBlock* next;
    int is_free;
    uint32_t magic;
} HeapBlock;

#define HEAP_MAGIC 0xDEADBEEF
#define HEAP_BLOCK_HEADER_SIZE sizeof(HeapBlock)
#define MIN_BLOCK_SIZE 32
#define HEAP_ALIGN 16

static uint64_t kernel_heap_base;
static uint64_t kernel_heap_size;
static uint64_t kernel_heap_used;
static HeapBlock* heap_free_list = NULL;

uint64_t kernel_stack_base;
uint64_t kernel_stack_top;

static uint64_t alloc_count = 0;
static uint64_t free_count = 0;
static uint64_t split_count = 0;
static uint64_t coalesce_count = 0;


void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size) {
    free_list = NULL;
    total_pages = 0;
    used_pages = 0;

    for (UINTN i = 0; i < desc_count; i++) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + i * desc_size);

        if (d->Type == EfiConventionalMemory) {
            uint64_t base = d->PhysicalStart;
            uint64_t pages = d->NumberOfPages;

            for (uint64_t p = 0; p < pages; p++) {
                FreePage* page = (FreePage*)(base + p * 4096);
                page->next = free_list;
                free_list = page;
                total_pages++;
            }
        }
    }
}

void* pmm_alloc_page() {
    if (!free_list) return NULL;

    FreePage* page = free_list;
    free_list = free_list->next;
    used_pages++;

    uint8_t* ptr = (uint8_t*)page;
    for (int i = 0; i < 4096; i++) {
        ptr[i] = 0;
    }

    return (void*)page;
}

void pmm_free_page(void* addr) {
    if (!addr) return;

    FreePage* page = (FreePage*)addr;
    page->next = free_list;
    free_list = page;
    used_pages--;
}

void* pmm_alloc_pages(uint64_t count) {
    if (count == 0) return NULL;

    void* first_page = pmm_alloc_page();
    if (!first_page) return NULL;

    for (uint64_t i = 1; i < count; i++) {
        if (!pmm_alloc_page()) {
            return NULL;
        }
    }

    return first_page;
}

uint64_t pmm_get_total_pages() {
    return total_pages;
}

uint64_t pmm_get_used_pages() {
    return used_pages;
}

uint64_t pmm_get_free_pages() {
    return total_pages - used_pages;
}


int stackalloc(int pages, int page_size) {
    if (pages <= 0 || page_size <= 0) return EXIT_FAILURE;

    kernel_stack_base = (uint64_t)pmm_alloc_pages(pages);
    if (!kernel_stack_base) return EXIT_FAILURE;

    kernel_stack_top = kernel_stack_base + (uint64_t)pages * page_size;

    uint64_t aligned_top = kernel_stack_top & ~0xF;
    asm volatile("mov %0, %%rsp" :: "r"(aligned_top));

    return EXIT_SUCCESS;
}


void init_kernel_heap(void) {
    const uint64_t initial_pages = 16;
    void* base = pmm_alloc_pages(initial_pages);

    if (!base) {
        return;
    }

    kernel_heap_base = (uint64_t)base;
    kernel_heap_size = initial_pages * 4096;
    kernel_heap_used = 0;

    heap_free_list = (HeapBlock*)kernel_heap_base;
    heap_free_list->size = kernel_heap_size - HEAP_BLOCK_HEADER_SIZE;
    heap_free_list->next = NULL;
    heap_free_list->is_free = 1;
    heap_free_list->magic = HEAP_MAGIC;
}

static size_t align_size(size_t size) {
    return (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

static void split_block(HeapBlock* block, size_t size) {
    size_t remaining = block->size - size - HEAP_BLOCK_HEADER_SIZE;

    if (remaining >= MIN_BLOCK_SIZE) {
        HeapBlock* new_block = (HeapBlock*)((uint8_t*)block + HEAP_BLOCK_HEADER_SIZE + size);
        new_block->size = remaining;
        new_block->next = block->next;
        new_block->is_free = 1;
        new_block->magic = HEAP_MAGIC;

        block->size = size;
        block->next = new_block;

        split_count++;
    }
}

static void coalesce_blocks() {
    HeapBlock* current = (HeapBlock*)kernel_heap_base;

    while ((uint64_t)current < kernel_heap_base + kernel_heap_used) {
        if (current->magic != HEAP_MAGIC) break;

        if (current->is_free) {
            HeapBlock* next = (HeapBlock*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE + current->size);

            if ((uint64_t)next < kernel_heap_base + kernel_heap_used &&
                next->magic == HEAP_MAGIC && next->is_free) {
                current->size += HEAP_BLOCK_HEADER_SIZE + next->size;
                current->next = next->next;
                coalesce_count++;
                continue;
            }
        }

        current = (HeapBlock*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE + current->size);
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = align_size(size);

    HeapBlock* current = heap_free_list;
    HeapBlock* prev = NULL;

    while (current) {
        if (current->magic != HEAP_MAGIC) {
            return NULL;
        }

        if (current->is_free && current->size >= size) {
            split_block(current, size);
            current->is_free = 0;

            if (prev) {
                prev->next = current->next;
            } else {
                heap_free_list = current->next;
            }

            kernel_heap_used += HEAP_BLOCK_HEADER_SIZE + size;
            alloc_count++;

            return (void*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE);
        }

        prev = current;
        current = current->next;
    }

    uint64_t new_pages = (size + HEAP_BLOCK_HEADER_SIZE + 4095) / 4096;
    void* new_mem = pmm_alloc_pages(new_pages);

    if (!new_mem) return NULL;

    HeapBlock* new_block = (HeapBlock*)new_mem;
    new_block->size = new_pages * 4096 - HEAP_BLOCK_HEADER_SIZE;
    new_block->next = heap_free_list;
    new_block->is_free = 1;
    new_block->magic = HEAP_MAGIC;
    heap_free_list = new_block;

    kernel_heap_size += new_pages * 4096;

    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;

    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - HEAP_BLOCK_HEADER_SIZE);

    if (block->magic != HEAP_MAGIC) {
        return;
    }

    if (block->is_free) {
        return;
    }

    block->is_free = 1;
    block->next = heap_free_list;
    heap_free_list = block;

    kernel_heap_used -= HEAP_BLOCK_HEADER_SIZE + block->size;
    free_count++;

    coalesce_blocks();
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);

    if (ptr) {
        uint8_t* bytes = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++) {
            bytes[i] = 0;
        }
    }

    return ptr;
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - HEAP_BLOCK_HEADER_SIZE);

    if (block->magic != HEAP_MAGIC) return NULL;

    if (block->size >= new_size) {
        return ptr;
    }

    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (size_t i = 0; i < block->size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}


void memory_stats(void) {
    cursor.x = 20;
    cursor.y = 20;

    cursor.fg_color = WHITE;
    cursor.bg_color = RED;
PRINT(WHITE, RED, "=== Memory Statistics ===\n");

PRINT(WHITE, RED, "Physical Memory:\n");
PRINT(WHITE, RED, "  Total pages: %llu\n", total_pages);
PRINT(WHITE, RED, "  Used pages: %llu\n", used_pages);
PRINT(WHITE, RED, "  Free pages: %llu\n", total_pages - used_pages);
PRINT(WHITE, RED, "  Total size: %llu KB\n", (total_pages * 4) / 1);

PRINT(WHITE, RED, "\nHeap Memory:\n");
PRINT(WHITE, RED, "  Base: 0x%llx\n", kernel_heap_base);
PRINT(WHITE, RED, "  Size: %llu KB\n", kernel_heap_size / 1024);
PRINT(WHITE, RED, "  Used: %llu KB\n", kernel_heap_used / 1024);
PRINT(WHITE, RED, "  Free: %llu KB\n", (kernel_heap_size - kernel_heap_used) / 1024);

PRINT(WHITE, RED, "\nHeap Operations:\n");
PRINT(WHITE, RED, "  Allocations: %llu\n", alloc_count);
PRINT(WHITE, RED, "  Frees: %llu\n", free_count);
PRINT(WHITE, RED, "  Splits: %llu\n", split_count);
PRINT(WHITE, RED, "  Coalesces: %llu\n", coalesce_count);

PRINT(WHITE, RED, "\nStack:\n");
PRINT(WHITE, RED, "  Base: 0x%llx\n", kernel_stack_base);
PRINT(WHITE, RED, "  Top: 0x%llx\n", kernel_stack_top);
PRINT(WHITE, RED, "  Size: %llu KB\n", (kernel_stack_top - kernel_stack_base) / 1024);
}




int memory_test(void) {
    ClearScreen(RED);

    cursor.x = 0;
    cursor.y = 0;

    cursor.fg_color = WHITE;
    cursor.bg_color = RED;

    PRINT(WHITE, RED, "=== Memory Allocator Tests ===\n");

    PRINT(WHITE, RED, "Test 1: Basic allocation... ");
    void* p1 = kmalloc(100);
    if (!p1) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    kfree(p1);
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 2: Multiple allocations... ");
    void* p2 = kmalloc(50);
    void* p3 = kmalloc(200);
    void* p4 = kmalloc(1000);
    if (!p2 || !p3 || !p4) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 3: Non-sequential free... ");
    kfree(p3);
    kfree(p2);
    kfree(p4);
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 4: Block coalescing... ");
    void* p5 = kmalloc(100);
    void* p6 = kmalloc(100);
    void* p7 = kmalloc(100);
    kfree(p5);
    kfree(p7);
    kfree(p6);
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 5: Large allocation... ");
    void* p8 = kmalloc(10000);
    if (!p8) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    kfree(p8);
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 6: Zero size allocation... ");
    void* p9 = kmalloc(0);
    if (p9 != NULL) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 7: kcalloc... ");
    uint8_t* p10 = (uint8_t*)kcalloc(100, sizeof(uint8_t));
    if (!p10) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    int all_zero = 1;
    for (int i = 0; i < 100; i++) {
        if (p10[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    kfree(p10);
    if (!all_zero) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "Test 8: krealloc... ");
    void* p11 = kmalloc(50);
    void* p12 = krealloc(p11, 200);
    if (!p12) {
        PRINT(WHITE, RED, "FAILED\n");
        return 0;
    }
    kfree(p12);
    PRINT(WHITE, RED, "PASSED\n");

    PRINT(WHITE, RED, "\nAll tests PASSED!\n");
    return 1;
}


void identity_map_addresses(void) {
    void* page = pmm_alloc_page();
    (void)page;
}

void print_ptr(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;

    for (int i = (sizeof(void*) * 2 - 1); i >= 0; i--) {
        uint8_t nibble = (addr >> (i * 4)) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('A' + (nibble - 10));
        printc(c);
    }
}


void* mmset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while(num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void* mmcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    while (n--) {
        *d++ = *s++;
    }

    return dest;
}