/*

We're going to need to create stack & Heap
Allocate addresses from heap 

Heap grows upwards, while stack grows downwards
Starting from the middle, one grows downwards the other upwards, until they collide at an address
Create heap corrutpion errors & Prevent collisions




*/


#include <efi.h>
#include <efilib.h>

void SPIMEM (CHAR16* hexcode);

typedef struct FreePage {
    struct FreePage* next;
} FreePage;

static FreePage* free_list = NULL;
static uint64_t kernel_heap_base;
static uint64_t kernel_heap_top;   // for bump allocator

void kernel_main() {
    CHAR16 msg[] = L"kernel_main running\r\n";
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, msg);
    
    CHAR16 letters[] = L"Welcome to memory, Superuser.\r\n";

    SPIMEM(letters);
}

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

int map_page(uint64_t virtual_addr, void* physical_addr) {
    return 0;
    // TODO: fill this later in a week or two.
}

void init_kernel_stack(void) {
    void* stack_bottom = pmm_alloc_page();     // 4 KB
    uint64_t rsp = (uint64_t)stack_bottom + 4096;

    asm volatile("mov %0, %%rsp" :: "r"(rsp));
}

void init_kernel_heap(void) {
    void* heap_page = pmm_alloc_page();
    kernel_heap_base = (uint64_t)heap_page;
    kernel_heap_top = kernel_heap_base;
}


void identity_map_addresses(void) {
    void* page = pmm_alloc_page();  // get a physical page
    uint64_t virt = (uint64_t)page; // virtual = physical, temporarily.
}

void* kmalloc(size_t size) {
    void* addr = (void*)kernel_heap_top;
    kernel_heap_top += size;  // simple bump
    return addr;
}

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

uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, hexcode);

}// Store Print In MEMory (SPIMEM)
