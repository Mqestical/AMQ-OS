#ifndef TSS_H
#define TSS_H

#include <stdint.h>
#include "gdt.h"

// Note: TSS64 structure is now defined in GDT.h to avoid circular dependencies

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);
void tss_set_ist(int ist_index, uint64_t stack_addr);

#endif // TSS_H