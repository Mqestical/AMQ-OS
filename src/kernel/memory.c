#include <efi.h>
#include <efilib.h>
#include "memory.h"
#include "definitions.h"
#include "print.h"
static FreePage* free_list = NULL;

// Kernel memory management
static uint64_t kernel_heap_base;
static uint64_t kernel_heap_top;
uint64_t kernel_stack_base;  // Bottom of stack
uint64_t kernel_stack_top;   // Top of stack (initial RSP)

void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size) {
    for (UINTN i = 0; i < desc_count; i++) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + i * desc_size);

        if (d->Type == EfiConventionalMemory) {
            uint64_t base = d->PhysicalStart;
            uint64_t pages = d->NumberOfPages;

            for (uint64_t p = 0; p < pages; p++) {
                FreePage* page = (FreePage*)(base + p * 4096);
                page->next = free_list;
                free_list = page;
            }
        }
    }
}

void* pmm_alloc_page() {
    if (!free_list) return NULL;

    FreePage* page = free_list;
    free_list = free_list->next;

    return (void*)page;
}

void pmm_free_page(void* addr) {
    FreePage* page = (FreePage*)addr;
    page->next = free_list;
    free_list = page;
}

// Allocate a stack
int stackalloc(int pages, int page_size) {
    if (pages <= 0 || page_size <= 0) return EXIT_FAILURE;

    kernel_stack_base = (uint64_t)pmm_alloc_page();
    if (!kernel_stack_base) return EXIT_FAILURE;

    // Allocate additional pages
    for (int i = 1; i < pages; i++) {
        pmm_alloc_page();
    }

    // Stack grows downwards
    kernel_stack_top = kernel_stack_base + (uint64_t)pages * page_size;

    // Set RSP to top of stack
    asm volatile("mov %0, %%rsp" :: "r"(kernel_stack_top));

    return EXIT_SUCCESS;
}

// Initialize the kernel heap
void init_kernel_heap(void) {
    void* page = pmm_alloc_page();
    kernel_heap_base = (uint64_t)page;
    kernel_heap_top  = kernel_heap_base;
}

// Allocate memory from kernel heap
void* kmalloc(size_t size) {
    if (kernel_heap_top + size > kernel_stack_base) {
        // Collision with stack! Out of memory
        return NULL;
    }

    void* addr = (void*)kernel_heap_top;
    kernel_heap_top += size;
    return addr;
}

// Temporary identity mapping (physical == virtual)
void identity_map_addresses(void) {
    void* page = pmm_alloc_page();
    (void)page; // suppress unused variable warning
}

// Store a UEFI string in memory
void SPIMEM (CHAR16* hexcode) {
    void* heap_page = pmm_alloc_page();      // 4 KiB
    uint16_t* mem = (uint16_t*)heap_page;    // treat it as CHAR16 array
    uint64_t i = 0;

while (hexcode[i] != '\0') {
    mem[i] = (uint16_t)hexcode[i];  // convert ASCII to CHAR16
    i++;
}

// Null terminator
mem[i] = 0x0000;
//uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, hexcode);

}// Store Print In MEMory (SPIMEM)