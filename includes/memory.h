#ifndef MEMORY_H
#define MEMORY_H

#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <stddef.h>

#define NO_THROW        __attribute__((nothrow))
#define NON_NULL(...)   __attribute__((nonnull(__VA_ARGS__)))
#define WUR             __attribute__((warn_unused_result))
#define HOT             __attribute__((hot))
#define COLD            __attribute__((cold))
#define OPT_O3          __attribute__((optimize("O3")))

typedef struct FreePage {
    struct FreePage* next;
} FreePage;

extern uint64_t kernel_stack_base;
extern uint64_t kernel_stack_top;


void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size)
    NO_THROW NON_NULL(1);

void* pmm_alloc_page(void) NO_THROW WUR HOT;

void pmm_free_page(void* addr) NO_THROW NON_NULL(1) HOT;

void* pmm_alloc_pages(uint64_t count) NO_THROW WUR HOT;

uint64_t pmm_get_total_pages(void) NO_THROW WUR;
uint64_t pmm_get_used_pages(void)  NO_THROW WUR;
uint64_t pmm_get_free_pages(void)  NO_THROW WUR;


int stackalloc(int pages, int page_size) NO_THROW WUR HOT;


void init_kernel_heap(void) NO_THROW;

void* kmalloc(size_t size) NO_THROW WUR HOT;

void kfree(void* ptr) NO_THROW NON_NULL(1);

void* kcalloc(size_t num, size_t size) NO_THROW WUR HOT;

void* krealloc(void* ptr, size_t new_size) NO_THROW WUR HOT;


void memory_stats(void) NO_THROW COLD;

int memory_test(void) NO_THROW WUR COLD;


void identity_map_addresses(void) NO_THROW;
void print_ptr(void* ptr);
void* mmset(void* ptr, int value, size_t num) NO_THROW NON_NULL(1) WUR OPT_O3 HOT;
void* mmcpy(void* dest, const void* src, size_t n) NO_THROW NON_NULL(1) WUR OPT_O3 HOT;
#endif
