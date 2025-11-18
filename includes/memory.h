#include <efi.h>
#include <efilib.h>
typedef struct FreePage {
    struct FreePage* next;
} FreePage;

void SPIMEM (CHAR16* hexcode);
void kernel_main();
void pmm_init(EFI_MEMORY_DESCRIPTOR* map, UINTN desc_count, UINTN desc_size);
void* pmm_alloc_page();
void pmm_free_page(void* addr);
int stackalloc(int pages, int size);
void init_kernel_heap(void);
void identity_map_addresses(void);
void* kmalloc(size_t size);
void SPIMEM (CHAR16* hexcode);