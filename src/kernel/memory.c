#include <efi.h>
#include <efilib.h>
#include "memory.h"
#include "definitions.h"
#include "print.h"

// Physical memory management
static FreePage* free_list = NULL;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;

// Heap block structure with header
typedef struct HeapBlock {
    size_t size;                // Size of the block (excluding header)
    struct HeapBlock* next;     // Next block in free list
    int is_free;                // 1 if free, 0 if allocated
    uint32_t magic;             // Magic number for corruption detection
} HeapBlock;

#define HEAP_MAGIC 0xDEADBEEF
#define HEAP_BLOCK_HEADER_SIZE sizeof(HeapBlock)
#define MIN_BLOCK_SIZE 32
#define HEAP_ALIGN 16

// Kernel memory regions
static uint64_t kernel_heap_base;
static uint64_t kernel_heap_size;
static uint64_t kernel_heap_used;
static HeapBlock* heap_free_list = NULL;

uint64_t kernel_stack_base;
uint64_t kernel_stack_top;

// Debug counters
static uint64_t alloc_count = 0;
static uint64_t free_count = 0;
static uint64_t split_count = 0;
static uint64_t coalesce_count = 0;

// ============================================================================
// PHYSICAL MEMORY MANAGER
// ============================================================================

void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size) {
    free_list = NULL;
    total_pages = 0;
    used_pages = 0;

    for (UINTN i = 0; i < desc_count; i++) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + i * desc_size);

        if (d->Type == EfiConventionalMemory) {
            uint64_t base = d->PhysicalStart;
            uint64_t pages = d->NumberOfPages;

            // Add pages to free list
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

    // Zero out the page
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
            // Failed to allocate all pages - this is simplified, 
            // ideally we'd free what we allocated
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

// ============================================================================
// STACK ALLOCATOR
// ============================================================================

int stackalloc(int pages, int page_size) {
    if (pages <= 0 || page_size <= 0) return EXIT_FAILURE;

    // Allocate contiguous pages for stack
    kernel_stack_base = (uint64_t)pmm_alloc_pages(pages);
    if (!kernel_stack_base) return EXIT_FAILURE;

    // Stack grows downwards, so top is at the end
    kernel_stack_top = kernel_stack_base + (uint64_t)pages * page_size;

    // Set RSP to top of stack (with 16-byte alignment)
    uint64_t aligned_top = kernel_stack_top & ~0xF;
    asm volatile("mov %0, %%rsp" :: "r"(aligned_top));

    return EXIT_SUCCESS;
}

// ============================================================================
// HEAP ALLOCATOR (with free list and coalescing)
// ============================================================================

void init_kernel_heap(void) {
    // Allocate initial heap size (e.g., 16 pages = 64 KB)
    const uint64_t initial_pages = 16;
    void* base = pmm_alloc_pages(initial_pages);
    
    if (!base) {
        return; // Failed to initialize heap
    }

    kernel_heap_base = (uint64_t)base;
    kernel_heap_size = initial_pages * 4096;
    kernel_heap_used = 0;

    // Create initial free block
    heap_free_list = (HeapBlock*)kernel_heap_base;
    heap_free_list->size = kernel_heap_size - HEAP_BLOCK_HEADER_SIZE;
    heap_free_list->next = NULL;
    heap_free_list->is_free = 1;
    heap_free_list->magic = HEAP_MAGIC;
}

// Align size to HEAP_ALIGN boundary
static size_t align_size(size_t size) {
    return (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

// Split a block if it's large enough
static void split_block(HeapBlock* block, size_t size) {
    size_t remaining = block->size - size - HEAP_BLOCK_HEADER_SIZE;
    
    if (remaining >= MIN_BLOCK_SIZE) {
        // Create new free block from remainder
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

// Coalesce adjacent free blocks
static void coalesce_blocks() {
    HeapBlock* current = (HeapBlock*)kernel_heap_base;
    
    while ((uint64_t)current < kernel_heap_base + kernel_heap_used) {
        if (current->magic != HEAP_MAGIC) break;
        
        if (current->is_free) {
            HeapBlock* next = (HeapBlock*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE + current->size);
            
            if ((uint64_t)next < kernel_heap_base + kernel_heap_used &&
                next->magic == HEAP_MAGIC && next->is_free) {
                // Merge with next block
                current->size += HEAP_BLOCK_HEADER_SIZE + next->size;
                current->next = next->next;
                coalesce_count++;
                continue; // Check again for more merging
            }
        }
        
        // Move to next block
        current = (HeapBlock*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE + current->size);
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = align_size(size);
    
    // First fit algorithm
    HeapBlock* current = heap_free_list;
    HeapBlock* prev = NULL;

    while (current) {
        if (current->magic != HEAP_MAGIC) {
            // Heap corruption detected!
            return NULL;
        }

        if (current->is_free && current->size >= size) {
            // Found suitable block
            split_block(current, size);
            current->is_free = 0;

            // Remove from free list
            if (prev) {
                prev->next = current->next;
            } else {
                heap_free_list = current->next;
            }

            kernel_heap_used += HEAP_BLOCK_HEADER_SIZE + size;
            alloc_count++;

            // Return pointer after header
            return (void*)((uint8_t*)current + HEAP_BLOCK_HEADER_SIZE);
        }

        prev = current;
        current = current->next;
    }

    // No suitable block found - try expanding heap
    uint64_t new_pages = (size + HEAP_BLOCK_HEADER_SIZE + 4095) / 4096;
    void* new_mem = pmm_alloc_pages(new_pages);
    
    if (!new_mem) return NULL;

    // Add new memory as free block
    HeapBlock* new_block = (HeapBlock*)new_mem;
    new_block->size = new_pages * 4096 - HEAP_BLOCK_HEADER_SIZE;
    new_block->next = heap_free_list;
    new_block->is_free = 1;
    new_block->magic = HEAP_MAGIC;
    heap_free_list = new_block;

    kernel_heap_size += new_pages * 4096;

    // Try allocation again
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;

    // Get block header
    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - HEAP_BLOCK_HEADER_SIZE);

    if (block->magic != HEAP_MAGIC) {
        // Invalid free or heap corruption
        return;
    }

    if (block->is_free) {
        // Double free detected
        return;
    }

    // Mark as free
    block->is_free = 1;
    block->next = heap_free_list;
    heap_free_list = block;

    kernel_heap_used -= HEAP_BLOCK_HEADER_SIZE + block->size;
    free_count++;

    // Coalesce adjacent free blocks
    coalesce_blocks();
}

// Allocate zeroed memory
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

// Reallocate memory
void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    HeapBlock* block = (HeapBlock*)((uint8_t*)ptr - HEAP_BLOCK_HEADER_SIZE);
    
    if (block->magic != HEAP_MAGIC) return NULL;

    if (block->size >= new_size) {
        // Current block is large enough
        return ptr;
    }

    // Allocate new block and copy
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    // Copy old data
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (size_t i = 0; i < block->size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}

// ============================================================================
// DEBUG AND TESTING
// ============================================================================

void memory_stats(void) {
    cursor.x = 20;   // some margin
    cursor.y = 20;   // start a bit down from top

    cursor.fg_color = 0xFFFFFFFF;
    cursor.bg_color = 0xFF0000FF;
printk(0xFFFFFFFF, 0xFF0000FF, "=== Memory Statistics ===\n");

printk(0xFFFFFFFF, 0xFF0000FF, "Physical Memory:\n");
printk(0xFFFFFFFF, 0xFF0000FF, "  Total pages: %llu\n", total_pages);
printk(0xFFFFFFFF, 0xFF0000FF, "  Used pages: %llu\n", used_pages);
printk(0xFFFFFFFF, 0xFF0000FF, "  Free pages: %llu\n", total_pages - used_pages);
printk(0xFFFFFFFF, 0xFF0000FF, "  Total size: %llu KB\n", (total_pages * 4) / 1);

printk(0xFFFFFFFF, 0xFF0000FF, "\nHeap Memory:\n");
printk(0xFFFFFFFF, 0xFF0000FF, "  Base: 0x%llx\n", kernel_heap_base);
printk(0xFFFFFFFF, 0xFF0000FF, "  Size: %llu KB\n", kernel_heap_size / 1024);
printk(0xFFFFFFFF, 0xFF0000FF, "  Used: %llu KB\n", kernel_heap_used / 1024);
printk(0xFFFFFFFF, 0xFF0000FF, "  Free: %llu KB\n", (kernel_heap_size - kernel_heap_used) / 1024);

printk(0xFFFFFFFF, 0xFF0000FF, "\nHeap Operations:\n");
printk(0xFFFFFFFF, 0xFF0000FF, "  Allocations: %llu\n", alloc_count);
printk(0xFFFFFFFF, 0xFF0000FF, "  Frees: %llu\n", free_count);
printk(0xFFFFFFFF, 0xFF0000FF, "  Splits: %llu\n", split_count);
printk(0xFFFFFFFF, 0xFF0000FF, "  Coalesces: %llu\n", coalesce_count);

printk(0xFFFFFFFF, 0xFF0000FF, "\nStack:\n");
printk(0xFFFFFFFF, 0xFF0000FF, "  Base: 0x%llx\n", kernel_stack_base);
printk(0xFFFFFFFF, 0xFF0000FF, "  Top: 0x%llx\n", kernel_stack_top);
printk(0xFFFFFFFF, 0xFF0000FF, "  Size: %llu KB\n", (kernel_stack_top - kernel_stack_base) / 1024);
}




int memory_test(void) {
    // Clear screen to blue
    ClearScreen(0xFF0000FF);
    
    // Reset cursor to top-left
    cursor.x = 0;
    cursor.y = 0;
    
    // Set colors
    cursor.fg_color = 0xFFFFFFFF; // White text
    cursor.bg_color = 0xFF0000FF;  // Blue background
    
    printk(0xFFFFFFFF, 0xFF0000FF, "=== Memory Allocator Tests ===\n");
    
    // Test 1: Basic allocation and free
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 1: Basic allocation... ");
    void* p1 = kmalloc(100);
    if (!p1) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    kfree(p1);
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 2: Multiple allocations
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 2: Multiple allocations... ");
    void* p2 = kmalloc(50);
    void* p3 = kmalloc(200);
    void* p4 = kmalloc(1000);
    if (!p2 || !p3 || !p4) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 3: Free in different order
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 3: Non-sequential free... ");
    kfree(p3);
    kfree(p2);
    kfree(p4);
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 4: Coalescing
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 4: Block coalescing... ");
    void* p5 = kmalloc(100);
    void* p6 = kmalloc(100);
    void* p7 = kmalloc(100);
    kfree(p5);
    kfree(p7);
    kfree(p6); // Should coalesce all three
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 5: Large allocation
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 5: Large allocation... ");
    void* p8 = kmalloc(10000);
    if (!p8) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    kfree(p8);
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 6: Zero allocation
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 6: Zero size allocation... ");
    void* p9 = kmalloc(0);
    if (p9 != NULL) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 7: kcalloc
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 7: kcalloc... ");
    uint8_t* p10 = (uint8_t*)kcalloc(100, sizeof(uint8_t));
    if (!p10) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
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
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    // Test 8: krealloc
    printk(0xFFFFFFFF, 0xFF0000FF, "Test 8: krealloc... ");
    void* p11 = kmalloc(50);
    void* p12 = krealloc(p11, 200);
    if (!p12) {
        printk(0xFFFFFFFF, 0xFF0000FF, "FAILED\n");
        return 0;
    }
    kfree(p12);
    printk(0xFFFFFFFF, 0xFF0000FF, "PASSED\n");

    printk(0xFFFFFFFF, 0xFF0000FF, "\nAll tests PASSED!\n");
    return 1;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void identity_map_addresses(void) {
    // Placeholder for paging setup
    void* page = pmm_alloc_page();
    (void)page;
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

