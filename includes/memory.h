#ifndef MEMORY_H
#define MEMORY_H

#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <stddef.h>

// Attribute helpers
#define NO_THROW        __attribute__((nothrow))
#define NON_NULL(...)   __attribute__((nonnull(__VA_ARGS__)))
#define WUR             __attribute__((warn_unused_result))
#define HOT             __attribute__((hot))
#define COLD            __attribute__((cold))
#define OPT_O3          __attribute__((optimize("O3")))

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

// Initialize PMM (touches UEFI memory map → DO NOT O3 optimize)
void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size)
    NO_THROW NON_NULL(1);

// Allocate a single 4KB page
void* pmm_alloc_page(void) NO_THROW WUR HOT;

// Free a single page
void pmm_free_page(void* addr) NO_THROW NON_NULL(1) HOT;

// Allocate multiple contiguous pages
void* pmm_alloc_pages(uint64_t count) NO_THROW WUR HOT;

// Get memory statistics
uint64_t pmm_get_total_pages(void) NO_THROW WUR;
uint64_t pmm_get_used_pages(void)  NO_THROW WUR;
uint64_t pmm_get_free_pages(void)  NO_THROW WUR;

// ============================================================================
// STACK ALLOCATOR
// ============================================================================

// Allocate kernel stack
int stackalloc(int pages, int page_size) NO_THROW WUR HOT;

// ============================================================================
// HEAP ALLOCATOR
// ============================================================================

// Initialize kernel heap
void init_kernel_heap(void) NO_THROW;

// Allocate memory from heap
void* kmalloc(size_t size) NO_THROW WUR HOT;

// Free memory back to heap
void kfree(void* ptr) NO_THROW NON_NULL(1);

// Allocate and zero-initialize memory
void* kcalloc(size_t num, size_t size) NO_THROW WUR HOT;

// Reallocate memory block
void* krealloc(void* ptr, size_t new_size) NO_THROW WUR HOT;

// ============================================================================
// DEBUG AND TESTING
// ============================================================================

void memory_stats(void) NO_THROW COLD;

// Returns: 1 if all tests pass, 0 otherwise
int memory_test(void) NO_THROW WUR COLD;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Identity map setup (paging code → don’t optimize away)
void identity_map_addresses(void) NO_THROW;

// Safe memset equivalent
void* mmset(void* ptr, int value, size_t num) NO_THROW NON_NULL(1) WUR OPT_O3 HOT;
void* mmcpy(void* dest, const void* src, size_t n) NO_THROW NON_NULL(1) WUR OPT_O3 HOT;
#endif // MEMORY_H
