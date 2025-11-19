#ifndef MEMORY_H
#define MEMORY_H

#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <stddef.h>

// Physical memory page structure
typedef struct FreePage {
    struct FreePage* next;
} FreePage;

// Kernel memory regions
extern uint64_t kernel_stack_base;
extern uint64_t kernel_stack_top;

// ============================================================================
// PHYSICAL MEMORY MANAGER
// ============================================================================

// Initialize physical memory manager with UEFI memory map
void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);

// Allocate a single 4KB page
void* pmm_alloc_page(void);

// Free a single page
void pmm_free_page(void* addr);

// Allocate multiple contiguous pages
void* pmm_alloc_pages(uint64_t count);

// Get memory statistics
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_used_pages(void);
uint64_t pmm_get_free_pages(void);

// ============================================================================
// STACK ALLOCATOR
// ============================================================================

// Allocate kernel stack
// pages: number of pages to allocate
// page_size: size of each page (typically 4096)
// Returns: EXIT_SUCCESS or EXIT_FAILURE
int stackalloc(int pages, int page_size);

// ============================================================================
// HEAP ALLOCATOR
// ============================================================================

// Initialize kernel heap
void init_kernel_heap(void);

// Allocate memory from heap
void* kmalloc(size_t size);

// Free memory back to heap
void kfree(void* ptr);

// Allocate and zero-initialize memory
void* kcalloc(size_t num, size_t size);

// Reallocate memory block
void* krealloc(void* ptr, size_t new_size);

// ============================================================================
// DEBUG AND TESTING
// ============================================================================

// Print detailed memory statistics
void memory_stats(void);

// Run comprehensive memory allocator tests
// Returns: 1 if all tests pass, 0 if any fail
int memory_test(void);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Setup identity mapping for paging (placeholder)
void identity_map_addresses(void);

// Store UEFI string in memory
void* memset(void* ptr, int value, size_t num);
#endif // MEMORY_H