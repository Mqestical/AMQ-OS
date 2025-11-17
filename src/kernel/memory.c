/*

We're going to need to create stack & Heap
Allocate addresses from heap 

Heap grows upwards, while stack grows downwards
Starting from the middle, one grows downwards the other upwards, until they collide at an address
Create heap corrutpion errors & Prevent collisions




*/

#include <efi.h>
#include <efilib.h>

typedef struct FreePage {
    struct FreePage* next;
} FreePage;

static FreePage* free_list = NULL;

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

void map_page(uint64_t virtual_addr, void* physical_addr) {

}

void init_kernel_stack() {
    void* stack_bottom = pmm_alloc_page();     // 4 KB
    uint64_t rsp = (uint64_t)stack_bottom + 4096;

    asm volatile("mov %0, %%rsp" :: "r"(rsp));
    void* page = pmm_alloc_page();
    map_page(0xFFFF800000000000, page);
}