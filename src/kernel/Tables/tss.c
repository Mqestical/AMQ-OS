#include "TSS.h"
#include "gdt.h"

static struct TSS64 tss;

void tss_init(void) {
    // Zero out the TSS
    for (int i = 0; i < sizeof(tss) / 8; i++) {
        ((uint64_t*)&tss)[i] = 0;
    }

    // Set io_map_base to indicate no I/O permission bitmap
    tss.io_map_base = sizeof(struct TSS64);

    // Install TSS into GDT
    gdt_set_tss(&tss);

    // Load TSS selector into TR register
    tss_load();
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void tss_set_ist(int ist_index, uint64_t stack_addr) {
    if (ist_index >= 1 && ist_index <= 7) {
        tss.ist[ist_index - 1] = stack_addr;
    }
}