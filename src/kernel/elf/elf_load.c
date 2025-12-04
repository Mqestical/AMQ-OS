#include "elf_loader.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"


static uint64_t align_down(uint64_t addr, uint64_t align) {
    return addr & ~(align - 1);
}

static uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}

static void *elf_map_memory(uint64_t vaddr, size_t size, uint32_t prot_flags) {
    uint64_t aligned_addr = align_down(vaddr, 4096);
    size_t aligned_size = align_up(size + (vaddr - aligned_addr), 4096);

    size_t num_pages = aligned_size / 4096;
    void *phys = pmm_alloc_pages(num_pages);

    if (!phys) {
        return NULL;
    }

    uint8_t *ptr = (uint8_t *)phys;
    for (size_t i = 0; i < aligned_size; i++) {
        ptr[i] = 0;
    }


    return phys;
}


int elf_validate(void *elf_data, size_t size) {
    if (!elf_data || size < sizeof(Elf64_Ehdr)) {
        return ELF_ERROR_INVALID;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        PRINT(YELLOW, BLACK, "[ELF] Invalid magic number\n");
        return ELF_ERROR_INVALID;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        PRINT(YELLOW, BLACK, "[ELF] Only 64-bit ELF supported\n");
        return ELF_ERROR_UNSUPPORTED;
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        PRINT(YELLOW, BLACK, "[ELF] Only little-endian supported\n");
        return ELF_ERROR_UNSUPPORTED;
    }

    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        PRINT(YELLOW, BLACK, "[ELF] Invalid version\n");
        return ELF_ERROR_INVALID;
    }

    if (ehdr->e_machine != EM_X86_64) {
        PRINT(YELLOW, BLACK, "[ELF] Unsupported machine type: %u\n", ehdr->e_machine);
        return ELF_ERROR_UNSUPPORTED;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN && ehdr->e_type != ET_REL) {
        PRINT(YELLOW, BLACK, "[ELF] Unsupported ELF type: %u\n", ehdr->e_type);
        return ELF_ERROR_UNSUPPORTED;
    }

    if (ehdr->e_ehsize != sizeof(Elf64_Ehdr)) {
        PRINT(YELLOW, BLACK, "[ELF] Invalid header size\n");
        return ELF_ERROR_INVALID;
    }

    if (ehdr->e_phentsize != sizeof(Elf64_Phdr) && ehdr->e_phnum > 0) {
        PRINT(YELLOW, BLACK, "[ELF] Invalid program header size\n");
        return ELF_ERROR_INVALID;
    }

    if (ehdr->e_shentsize != sizeof(Elf64_Shdr) && ehdr->e_shnum > 0) {
        PRINT(YELLOW, BLACK, "[ELF] Invalid section header size\n");
        return ELF_ERROR_INVALID;
    }

    return ELF_SUCCESS;
}


elf_context_t *elf_create_context(void *elf_data, size_t size) {
    if (elf_validate(elf_data, size) != ELF_SUCCESS) {
        return NULL;
    }

    elf_context_t *ctx = (elf_context_t *)kmalloc(sizeof(elf_context_t));
    if (!ctx) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(elf_context_t); i++) {
        ((uint8_t *)ctx)[i] = 0;
    }

    ctx->elf_data = elf_data;
    ctx->elf_size = size;
    ctx->ehdr = (Elf64_Ehdr *)elf_data;

    if (ctx->ehdr->e_phnum > 0) {
        ctx->phdrs = (Elf64_Phdr *)((uint8_t *)elf_data + ctx->ehdr->e_phoff);
    }

    if (ctx->ehdr->e_shnum > 0) {
        ctx->shdrs = (Elf64_Shdr *)((uint8_t *)elf_data + ctx->ehdr->e_shoff);

        if (ctx->ehdr->e_shstrndx != SHN_UNDEF && ctx->ehdr->e_shstrndx < ctx->ehdr->e_shnum) {
            Elf64_Shdr *shstrtab_hdr = &ctx->shdrs[ctx->ehdr->e_shstrndx];
            ctx->shstrtab = (char *)((uint8_t *)elf_data + shstrtab_hdr->sh_offset);
        }
    }

    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        Elf64_Shdr *shdr = &ctx->shdrs[i];

        if (shdr->sh_type == SHT_STRTAB && ctx->shstrtab) {
            const char *name = ctx->shstrtab + shdr->sh_name;
            if (name[0] == '.' && name[1] == 's' && name[2] == 't' &&
                name[3] == 'r' && name[4] == 't' && name[5] == 'a' &&
                name[6] == 'b' && name[7] == '\0') {
                ctx->strtab = (char *)((uint8_t *)elf_data + shdr->sh_offset);
            } else if (name[0] == '.' && name[1] == 'd' && name[2] == 'y' &&
                       name[3] == 'n' && name[4] == 's' && name[5] == 't' &&
                       name[6] == 'r' && name[7] == '\0') {
                ctx->dynstr = (char *)((uint8_t *)elf_data + shdr->sh_offset);
            }
        } else if (shdr->sh_type == SHT_SYMTAB) {
            ctx->symtab = (Elf64_Sym *)((uint8_t *)elf_data + shdr->sh_offset);
            ctx->symtab_count = shdr->sh_size / sizeof(Elf64_Sym);
        } else if (shdr->sh_type == SHT_DYNSYM) {
            ctx->dynsym = (Elf64_Sym *)((uint8_t *)elf_data + shdr->sh_offset);
            ctx->dynsym_count = shdr->sh_size / sizeof(Elf64_Sym);
        } else if (shdr->sh_type == SHT_DYNAMIC) {
            ctx->dynamic = (Elf64_Dyn *)((uint8_t *)elf_data + shdr->sh_offset);
        }
    }

    return ctx;
}

void elf_destroy_context(elf_context_t *ctx) {
    if (!ctx) return;

    for (int i = 0; i < 256; i++) {
        elf_symbol_t *sym = ctx->symbol_hash[i];
        while (sym) {
            elf_symbol_t *next = sym->next;
            if (sym->name) kfree(sym->name);
            kfree(sym);
            sym = next;
        }
    }

    kfree(ctx);
}


const char *elf_get_section_name(elf_context_t *ctx, Elf64_Shdr *shdr) {
    if (!ctx->shstrtab || shdr->sh_name == 0) {
        return NULL;
    }
    return ctx->shstrtab + shdr->sh_name;
}

Elf64_Shdr *elf_find_section(elf_context_t *ctx, const char *name) {
    if (!ctx->shdrs || !ctx->shstrtab) {
        return NULL;
    }

    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        Elf64_Shdr *shdr = &ctx->shdrs[i];
        const char *section_name = elf_get_section_name(ctx, shdr);

        if (section_name) {
            const char *n1 = name;
            const char *n2 = section_name;
            while (*n1 && *n2 && *n1 == *n2) {
                n1++;
                n2++;
            }
            if (*n1 == '\0' && *n2 == '\0') {
                return shdr;
            }
        }
    }

    return NULL;
}

Elf64_Shdr *elf_find_section_by_type(elf_context_t *ctx, uint32_t type) {
    if (!ctx->shdrs) {
        return NULL;
    }

    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        if (ctx->shdrs[i].sh_type == type) {
            return &ctx->shdrs[i];
        }
    }

    return NULL;
}


int elf_load_segments(elf_context_t *ctx, elf_load_info_t *info) {
    if (!ctx || !info) {
        return ELF_ERROR_INVALID;
    }

    uint64_t min_vaddr = (uint64_t)-1;
    uint64_t max_vaddr = 0;

    for (int i = 0; i < ctx->ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &ctx->phdrs[i];

        if (phdr->p_type == PT_LOAD) {
            if (phdr->p_vaddr < min_vaddr) {
                min_vaddr = phdr->p_vaddr;
            }
            uint64_t end = phdr->p_vaddr + phdr->p_memsz;
            if (end > max_vaddr) {
                max_vaddr = end;
            }
        }
    }

    if (ctx->ehdr->e_type == ET_DYN) {
        size_t total_size = max_vaddr - min_vaddr;
        void *base = elf_map_memory(0x400000, total_size, PF_R | PF_W | PF_X);
        if (!base) {
            return ELF_ERROR_NOMEM;
        }
        ctx->load_bias = (uint64_t)base - min_vaddr;
        info->base_addr = (uint64_t)base;
    } else {
        ctx->load_bias = 0;
        info->base_addr = min_vaddr;
    }

    for (int i = 0; i < ctx->ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &ctx->phdrs[i];

        if (phdr->p_type == PT_LOAD) {
            uint64_t vaddr = phdr->p_vaddr + ctx->load_bias;

            PRINT(WHITE, BLACK, "[ELF] Loading segment: vaddr=0x%llx, filesz=%llu, memsz=%llu\n",
                  vaddr, phdr->p_filesz, phdr->p_memsz);

            void *mem = elf_map_memory(vaddr, phdr->p_memsz, phdr->p_flags);
            if (!mem) {
                PRINT(YELLOW, BLACK, "[ELF] Failed to map segment\n");
                return ELF_ERROR_NOMEM;
            }

            if (phdr->p_filesz > 0) {
                uint8_t *src = (uint8_t *)ctx->elf_data + phdr->p_offset;
                uint8_t *dst = (uint8_t *)mem + (vaddr - align_down(vaddr, 4096));

                for (size_t j = 0; j < phdr->p_filesz; j++) {
                    dst[j] = src[j];
                }
            }

            if (phdr->p_memsz > phdr->p_filesz) {
                uint8_t *bss_start = (uint8_t *)mem + (vaddr - align_down(vaddr, 4096)) + phdr->p_filesz;
                size_t bss_size = phdr->p_memsz - phdr->p_filesz;

                for (size_t j = 0; j < bss_size; j++) {
                    bss_start[j] = 0;
                }
            }
        } else if (phdr->p_type == PT_DYNAMIC) {
            info->dynamic_addr = phdr->p_vaddr + ctx->load_bias;
            info->is_dynamic = 1;
        } else if (phdr->p_type == PT_INTERP) {
            info->interp_path = (void *)((uint8_t *)ctx->elf_data + phdr->p_offset);
            info->is_dynamic = 1;
            PRINT(WHITE, BLACK, "[ELF] Interpreter: %s\n", (char *)info->interp_path);
        } else if (phdr->p_type == PT_TLS) {
            info->tls_base = phdr->p_vaddr + ctx->load_bias;
            info->tls_size = phdr->p_memsz;
            info->tls_align = phdr->p_align;
            info->has_tls = 1;
            PRINT(WHITE, BLACK, "[ELF] TLS: base=0x%llx, size=%llu, align=%llu\n",
                  info->tls_base, info->tls_size, info->tls_align);
        } else if (phdr->p_type == PT_PHDR) {
            info->phdr_addr = phdr->p_vaddr + ctx->load_bias;
        }
    }

    info->entry_point = ctx->ehdr->e_entry + ctx->load_bias;
    info->phdr_count = ctx->ehdr->e_phnum;
    info->phdr_entsize = ctx->ehdr->e_phentsize;

    PRINT(MAGENTA, BLACK, "[ELF] Entry point: 0x%llx\n", info->entry_point);

    return ELF_SUCCESS;
}


static uint32_t elf_hash(const char *name) {
    uint32_t h = 0;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        uint32_t g = h & 0xf0000000;
        if (g) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

int elf_resolve_symbols(elf_context_t *ctx) {
    if (!ctx) {
        return ELF_ERROR_INVALID;
    }

    if (ctx->symtab && ctx->strtab) {
        for (size_t i = 0; i < ctx->symtab_count; i++) {
            Elf64_Sym *sym = &ctx->symtab[i];

            if (sym->st_name == 0 || ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                continue;
            }

            const char *name = ctx->strtab + sym->st_name;

            elf_symbol_t *entry = (elf_symbol_t *)kmalloc(sizeof(elf_symbol_t));
            if (!entry) {
                continue;
            }

            size_t name_len = 0;
            while (name[name_len]) name_len++;

            entry->name = (char *)kmalloc(name_len + 1);
            if (!entry->name) {
                kfree(entry);
                continue;
            }

            for (size_t j = 0; j <= name_len; j++) {
                entry->name[j] = name[j];
            }

            entry->value = sym->st_value + ctx->load_bias;
            entry->size = sym->st_size;
            entry->type = ELF64_ST_TYPE(sym->st_info);
            entry->binding = ELF64_ST_BIND(sym->st_info);
            entry->section = sym->st_shndx;

            uint32_t hash = elf_hash(name) % 256;
            entry->next = ctx->symbol_hash[hash];
            ctx->symbol_hash[hash] = entry;
        }
    }

    if (ctx->dynsym && ctx->dynstr) {
        for (size_t i = 0; i < ctx->dynsym_count; i++) {
            Elf64_Sym *sym = &ctx->dynsym[i];

            if (sym->st_name == 0 || ELF64_ST_TYPE(sym->st_info) == STT_SECTION) {
                continue;
            }

            const char *name = ctx->dynstr + sym->st_name;

            uint32_t hash = elf_hash(name) % 256;
            elf_symbol_t *existing = ctx->symbol_hash[hash];
            int found = 0;

            while (existing) {
                const char *n1 = name;
                const char *n2 = existing->name;
                while (*n1 && *n2 && *n1 == *n2) {
                    n1++;
                    n2++;
                }
                if (*n1 == '\0' && *n2 == '\0') {
                    found = 1;
                    break;
                }
                existing = existing->next;
            }

            if (found) {
                continue;
            }

            elf_symbol_t *entry = (elf_symbol_t *)kmalloc(sizeof(elf_symbol_t));
            if (!entry) {
                continue;
            }

            size_t name_len = 0;
            while (name[name_len]) name_len++;

            entry->name = (char *)kmalloc(name_len + 1);
            if (!entry->name) {
                kfree(entry);
                continue;
            }

            for (size_t j = 0; j <= name_len; j++) {
                entry->name[j] = name[j];
            }

            entry->value = sym->st_value + ctx->load_bias;
            entry->size = sym->st_size;
            entry->type = ELF64_ST_TYPE(sym->st_info);
            entry->binding = ELF64_ST_BIND(sym->st_info);
            entry->section = sym->st_shndx;

            entry->next = ctx->symbol_hash[hash];
            ctx->symbol_hash[hash] = entry;
        }
    }

    return ELF_SUCCESS;
}

elf_symbol_t *elf_find_symbol(elf_context_t *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }

    uint32_t hash = elf_hash(name) % 256;
    elf_symbol_t *sym = ctx->symbol_hash[hash];

    while (sym) {
        const char *n1 = name;
        const char *n2 = sym->name;
        while (*n1 && *n2 && *n1 == *n2) {
            n1++;
            n2++;
        }
        if (*n1 == '\0' && *n2 == '\0') {
            return sym;
        }
        sym = sym->next;
    }

    return NULL;
}

uint64_t elf_get_symbol_value(elf_context_t *ctx, const char *name) {
    elf_symbol_t *sym = elf_find_symbol(ctx, name);
    return sym ? sym->value : 0;
}


static int elf_apply_rela_relocations(elf_context_t *ctx, Elf64_Rela *rela, size_t count,
                                       Elf64_Sym *symtab, char *strtab) {
    for (size_t i = 0; i < count; i++) {
        Elf64_Rela *rel = &rela[i];
        uint64_t r_type = ELF64_R_TYPE(rel->r_info);
        uint64_t r_sym = ELF64_R_SYM(rel->r_info);

        uint64_t *ref = (uint64_t *)(rel->r_offset + ctx->load_bias);
        uint64_t symval = 0;

        if (r_sym != 0 && symtab) {
            Elf64_Sym *sym = &symtab[r_sym];

            if (sym->st_shndx == SHN_UNDEF) {
                if (strtab && sym->st_name != 0) {
                    const char *sym_name = strtab + sym->st_name;
                    elf_symbol_t *resolved = elf_find_symbol(ctx, sym_name);

                    if (resolved) {
                        symval = resolved->value;
                    } else if (ELF64_ST_BIND(sym->st_info) == STB_WEAK) {
                        symval = 0;
                    } else {
                        PRINT(YELLOW, BLACK, "[ELF] Undefined symbol: %s\n", sym_name);
                        return ELF_ERROR_SYMBOL;
                    }
                }
            } else if (sym->st_shndx == SHN_ABS) {
                symval = sym->st_value;
            } else {
                symval = sym->st_value + ctx->load_bias;
            }
        }

        switch (r_type) {
            case R_X86_64_NONE:
                break;

            case R_X86_64_64:
                *ref = symval + rel->r_addend;
                break;

            case R_X86_64_PC32:
            case R_X86_64_PLT32:
                *(uint32_t *)ref = (uint32_t)(symval + rel->r_addend - (uint64_t)ref);
                break;

            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                *ref = symval;
                break;

            case R_X86_64_RELATIVE:
                *ref = ctx->load_bias + rel->r_addend;
                break;

            case R_X86_64_32:
                *(uint32_t *)ref = (uint32_t)(symval + rel->r_addend);
                break;

            case R_X86_64_32S:
                *(int32_t *)ref = (int32_t)(symval + rel->r_addend);
                break;

            case R_X86_64_GOTPCREL:
                *(uint32_t *)ref = (uint32_t)(symval + rel->r_addend - (uint64_t)ref);
                break;

            case R_X86_64_TPOFF64:
                *ref = symval + rel->r_addend;
                break;

            case R_X86_64_DTPMOD64:
                *ref = 1;
                break;

            case R_X86_64_DTPOFF64:
                *ref = symval + rel->r_addend;
                break;

            default:
                PRINT(YELLOW, BLACK, "[ELF] Unsupported relocation type: %llu\n", r_type);
                return ELF_ERROR_RELOC;
        }
    }

    return ELF_SUCCESS;
}

int elf_apply_relocations(elf_context_t *ctx) {
    if (!ctx) {
        return ELF_ERROR_INVALID;
    }

    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        Elf64_Shdr *shdr = &ctx->shdrs[i];

        if (shdr->sh_type == SHT_RELA) {
            Elf64_Rela *rela = (Elf64_Rela *)((uint8_t *)ctx->elf_data + shdr->sh_offset);
            size_t count = shdr->sh_size / sizeof(Elf64_Rela);

            Elf64_Shdr *symtab_shdr = NULL;
            char *strtab = NULL;

            if (shdr->sh_link < ctx->ehdr->e_shnum) {
                symtab_shdr = &ctx->shdrs[shdr->sh_link];
                Elf64_Sym *symtab = (Elf64_Sym *)((uint8_t *)ctx->elf_data + symtab_shdr->sh_offset);

                if (symtab_shdr->sh_link < ctx->ehdr->e_shnum) {
                    Elf64_Shdr *strtab_shdr = &ctx->shdrs[symtab_shdr->sh_link];
                    strtab = (char *)((uint8_t *)ctx->elf_data + strtab_shdr->sh_offset);
                }

                int result = elf_apply_rela_relocations(ctx, rela, count, symtab, strtab);
                if (result != ELF_SUCCESS) {
                    return result;
                }
            }
        }
    }

    if (ctx->dynamic) {
        Elf64_Rela *rela = NULL;
        size_t rela_size = 0;
        Elf64_Rela *jmprel = NULL;
        size_t jmprel_size = 0;

        for (Elf64_Dyn *dyn = ctx->dynamic; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_RELA) {
                rela = (Elf64_Rela *)(dyn->d_un.d_ptr + ctx->load_bias);
            } else if (dyn->d_tag == DT_RELASZ) {
                rela_size = dyn->d_un.d_val;
            } else if (dyn->d_tag == DT_JMPREL) {
                jmprel = (Elf64_Rela *)(dyn->d_un.d_ptr + ctx->load_bias);
            } else if (dyn->d_tag == DT_PLTRELSZ) {
                jmprel_size = dyn->d_un.d_val;
            }
        }

        if (rela && rela_size > 0) {
            int result = elf_apply_rela_relocations(ctx, rela, rela_size / sizeof(Elf64_Rela),
                                                     ctx->dynsym, ctx->dynstr);
            if (result != ELF_SUCCESS) {
                return result;
            }
        }

        if (jmprel && jmprel_size > 0) {
            int result = elf_apply_rela_relocations(ctx, jmprel, jmprel_size / sizeof(Elf64_Rela),
                                                     ctx->dynsym, ctx->dynstr);
            if (result != ELF_SUCCESS) {
                return result;
            }
        }
    }

    return ELF_SUCCESS;
}


int elf_setup_tls(elf_context_t *ctx, elf_load_info_t *info) {
    if (!info->has_tls) {
        return ELF_SUCCESS;
    }

    size_t tls_size = align_up(info->tls_size, info->tls_align);
    void *tls_block = kmalloc(tls_size);

    if (!tls_block) {
        return ELF_ERROR_NOMEM;
    }

    uint8_t *src = (uint8_t *)info->tls_base;
    uint8_t *dst = (uint8_t *)tls_block;

    for (size_t i = 0; i < info->tls_size; i++) {
        dst[i] = src[i];
    }

    for (size_t i = info->tls_size; i < tls_size; i++) {
        dst[i] = 0;
    }

    PRINT(MAGENTA, BLACK, "[ELF] TLS block allocated at 0x%p\n", tls_block);

    return ELF_SUCCESS;
}


int elf_load(void *elf_data, size_t size, elf_load_info_t *info) {
    if (!elf_data || !info) {
        return ELF_ERROR_INVALID;
    }

    for (size_t i = 0; i < sizeof(elf_load_info_t); i++) {
        ((uint8_t *)info)[i] = 0;
    }

    PRINT(WHITE, BLACK, "[ELF] Loading ELF binary...\n");

    elf_context_t *ctx = elf_create_context(elf_data, size);
    if (!ctx) {
        PRINT(YELLOW, BLACK, "[ELF] Failed to create context\n");
        return ELF_ERROR_INVALID;
    }

    int result = elf_load_segments(ctx, info);
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF] Failed to load segments\n");
        elf_destroy_context(ctx);
        return result;
    }

    result = elf_resolve_symbols(ctx);
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF] Failed to resolve symbols\n");
        elf_destroy_context(ctx);
        return result;
    }

    result = elf_apply_relocations(ctx);
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF] Failed to apply relocations\n");
        elf_destroy_context(ctx);
        return result;
    }

    result = elf_setup_tls(ctx, info);
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF] Failed to setup TLS\n");
        elf_destroy_context(ctx);
        return result;
    }

    PRINT(MAGENTA, BLACK, "[ELF] Successfully loaded ELF binary\n");


    return ELF_SUCCESS;
}


void elf_print_header(Elf64_Ehdr *ehdr) {
    PRINT(WHITE, BLACK, "\n=== ELF Header ===\n");
    PRINT(WHITE, BLACK, "Type: ");

    switch (ehdr->e_type) {
        case ET_NONE: PRINT(WHITE, BLACK, "None\n"); break;
        case ET_REL:  PRINT(WHITE, BLACK, "Relocatable\n"); break;
        case ET_EXEC: PRINT(WHITE, BLACK, "Executable\n"); break;
        case ET_DYN:  PRINT(WHITE, BLACK, "Shared Object\n"); break;
        case ET_CORE: PRINT(WHITE, BLACK, "Core\n"); break;
        default:      PRINT(WHITE, BLACK, "Unknown (%u)\n", ehdr->e_type); break;
    }

    PRINT(WHITE, BLACK, "Machine: x86-64\n");
    PRINT(WHITE, BLACK, "Entry point: 0x%llx\n", ehdr->e_entry);
    PRINT(WHITE, BLACK, "Program headers: %u (offset 0x%llx)\n", ehdr->e_phnum, ehdr->e_phoff);
    PRINT(WHITE, BLACK, "Section headers: %u (offset 0x%llx)\n", ehdr->e_shnum, ehdr->e_shoff);
}

void elf_print_program_headers(elf_context_t *ctx) {
    PRINT(WHITE, BLACK, "\n=== Program Headers ===\n");

    for (int i = 0; i < ctx->ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &ctx->phdrs[i];

        PRINT(WHITE, BLACK, "[%d] ", i);

        switch (phdr->p_type) {
            case PT_NULL:    PRINT(WHITE, BLACK, "NULL    "); break;
            case PT_LOAD:    PRINT(WHITE, BLACK, "LOAD    "); break;
            case PT_DYNAMIC: PRINT(WHITE, BLACK, "DYNAMIC "); break;
            case PT_INTERP:  PRINT(WHITE, BLACK, "INTERP  "); break;
            case PT_NOTE:    PRINT(WHITE, BLACK, "NOTE    "); break;
            case PT_PHDR:    PRINT(WHITE, BLACK, "PHDR    "); break;
            case PT_TLS:     PRINT(WHITE, BLACK, "TLS     "); break;
            default:         PRINT(WHITE, BLACK, "0x%x ", phdr->p_type); break;
        }

        PRINT(WHITE, BLACK, "0x%016llx 0x%016llx ", phdr->p_vaddr, phdr->p_paddr);
        PRINT(WHITE, BLACK, "0x%08llx 0x%08llx ", phdr->p_filesz, phdr->p_memsz);

        PRINT(WHITE, BLACK, "%c%c%c\n",
              (phdr->p_flags & PF_R) ? 'R' : '-',
              (phdr->p_flags & PF_W) ? 'W' : '-',
              (phdr->p_flags & PF_X) ? 'X' : '-');
    }
}

void elf_print_section_headers(elf_context_t *ctx) {
    PRINT(WHITE, BLACK, "\n=== Section Headers ===\n");

    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        Elf64_Shdr *shdr = &ctx->shdrs[i];
        const char *name = elf_get_section_name(ctx, shdr);

        PRINT(WHITE, BLACK, "[%2d] %-20s ", i, name ? name : "(null)");
        PRINT(WHITE, BLACK, "0x%016llx 0x%08llx ", shdr->sh_addr, shdr->sh_size);

        PRINT(WHITE, BLACK, "%c%c%c\n",
              (shdr->sh_flags & SHF_WRITE) ? 'W' : '-',
              (shdr->sh_flags & SHF_ALLOC) ? 'A' : '-',
              (shdr->sh_flags & SHF_EXECINSTR) ? 'X' : '-');
    }
}

void elf_print_symbols(elf_context_t *ctx) {
    PRINT(WHITE, BLACK, "\n=== Symbols ===\n");

    for (int i = 0; i < 256; i++) {
        elf_symbol_t *sym = ctx->symbol_hash[i];

        while (sym) {
            PRINT(WHITE, BLACK, "%-30s 0x%016llx %4llu %s %s\n",
                  sym->name, sym->value, sym->size,
                  (sym->type == STT_FUNC) ? "FUNC" :
                  (sym->type == STT_OBJECT) ? "OBJ " :
                  (sym->type == STT_TLS) ? "TLS " : "    ",
                  (sym->binding == STB_GLOBAL) ? "GLOBAL" :
                  (sym->binding == STB_WEAK) ? "WEAK  " : "LOCAL ");

            sym = sym->next;
        }
    }
}

void elf_print_dynamic(elf_context_t *ctx) {
    if (!ctx->dynamic) {
        PRINT(WHITE, BLACK, "\n=== No Dynamic Section ===\n");
        return;
    }

    PRINT(WHITE, BLACK, "\n=== Dynamic Section ===\n");

    for (Elf64_Dyn *dyn = ctx->dynamic; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_NEEDED:
                PRINT(WHITE, BLACK, "NEEDED: %s\n",
                      ctx->dynstr ? ctx->dynstr + dyn->d_un.d_val : "(no dynstr)");
                break;
            case DT_INIT:
                PRINT(WHITE, BLACK, "INIT: 0x%llx\n", dyn->d_un.d_ptr);
                break;
            case DT_FINI:
                PRINT(WHITE, BLACK, "FINI: 0x%llx\n", dyn->d_un.d_ptr);
                break;
            case DT_SONAME:
                PRINT(WHITE, BLACK, "SONAME: %s\n",
                      ctx->dynstr ? ctx->dynstr + dyn->d_un.d_val : "(no dynstr)");
                break;
            default:
                PRINT(WHITE, BLACK, "TAG 0x%llx: 0x%llx\n", dyn->d_tag, dyn->d_un.d_val);
                break;
        }
    }
}