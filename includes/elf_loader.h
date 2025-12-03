#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include <stddef.h>

// ELF Magic Numbers
#define EI_NIDENT 16
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// ELF Classes
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

// ELF Data Encodings
#define ELFDATANONE 0
#define ELFDATA2LSB 1  // Little-endian
#define ELFDATA2MSB 2  // Big-endian

// ELF Versions
#define EV_NONE    0
#define EV_CURRENT 1

// ELF Types
#define ET_NONE   0  // No file type
#define ET_REL    1  // Relocatable file
#define ET_EXEC   2  // Executable file
#define ET_DYN    3  // Shared object file
#define ET_CORE   4  // Core file

// Machine Types
#define EM_NONE  0
#define EM_386   3   // Intel x86
#define EM_X86_64 62 // AMD x86-64

// ELF Identification Indices
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9

// Program Header Types
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

// Program Header Flags
#define PF_X 0x1  // Execute
#define PF_W 0x2  // Write
#define PF_R 0x4  // Read

// Section Header Types
#define SHT_NULL          0
#define SHT_PROGBITS      1
#define SHT_SYMTAB        2
#define SHT_STRTAB        3
#define SHT_RELA          4
#define SHT_HASH          5
#define SHT_DYNAMIC       6
#define SHT_NOTE          7
#define SHT_NOBITS        8
#define SHT_REL           9
#define SHT_SHLIB         10
#define SHT_DYNSYM        11
#define SHT_INIT_ARRAY    14
#define SHT_FINI_ARRAY    15
#define SHT_PREINIT_ARRAY 16
#define SHT_GROUP         17
#define SHT_SYMTAB_SHNDX  18

// Section Header Flags
#define SHF_WRITE            0x1
#define SHF_ALLOC            0x2
#define SHF_EXECINSTR        0x4
#define SHF_MERGE            0x10
#define SHF_STRINGS          0x20
#define SHF_INFO_LINK        0x40
#define SHF_LINK_ORDER       0x80
#define SHF_OS_NONCONFORMING 0x100
#define SHF_GROUP            0x200
#define SHF_TLS              0x400

// Special Section Indices
#define SHN_UNDEF     0
#define SHN_LORESERVE 0xff00
#define SHN_LOPROC    0xff00
#define SHN_HIPROC    0xff1f
#define SHN_ABS       0xfff1
#define SHN_COMMON    0xfff2
#define SHN_HIRESERVE 0xffff

// Symbol Binding
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

// Symbol Types
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6

// Symbol Visibility
#define STV_DEFAULT   0
#define STV_INTERNAL  1
#define STV_HIDDEN    2
#define STV_PROTECTED 3

// Dynamic Array Tags
#define DT_NULL            0
#define DT_NEEDED          1
#define DT_PLTRELSZ        2
#define DT_PLTGOT          3
#define DT_HASH            4
#define DT_STRTAB          5
#define DT_SYMTAB          6
#define DT_RELA            7
#define DT_RELASZ          8
#define DT_RELAENT         9
#define DT_STRSZ           10
#define DT_SYMENT          11
#define DT_INIT            12
#define DT_FINI            13
#define DT_SONAME          14
#define DT_RPATH           15
#define DT_SYMBOLIC        16
#define DT_REL             17
#define DT_RELSZ           18
#define DT_RELENT          19
#define DT_PLTREL          20
#define DT_DEBUG           21
#define DT_TEXTREL         22
#define DT_JMPREL          23
#define DT_BIND_NOW        24
#define DT_INIT_ARRAY      25
#define DT_FINI_ARRAY      26
#define DT_INIT_ARRAYSZ    27
#define DT_FINI_ARRAYSZ    28
#define DT_RUNPATH         29
#define DT_FLAGS           30
#define DT_PREINIT_ARRAY   32
#define DT_PREINIT_ARRAYSZ 33

// Relocation Types (x86-64)
#define R_X86_64_NONE            0
#define R_X86_64_64              1
#define R_X86_64_PC32            2
#define R_X86_64_GOT32           3
#define R_X86_64_PLT32           4
#define R_X86_64_COPY            5
#define R_X86_64_GLOB_DAT        6
#define R_X86_64_JUMP_SLOT       7
#define R_X86_64_RELATIVE        8
#define R_X86_64_GOTPCREL        9
#define R_X86_64_32              10
#define R_X86_64_32S             11
#define R_X86_64_16              12
#define R_X86_64_PC16            13
#define R_X86_64_8               14
#define R_X86_64_PC8             15
#define R_X86_64_DTPMOD64        16
#define R_X86_64_DTPOFF64        17
#define R_X86_64_TPOFF64         18
#define R_X86_64_TLSGD           19
#define R_X86_64_TLSLD           20
#define R_X86_64_DTPOFF32        21
#define R_X86_64_GOTTPOFF        22
#define R_X86_64_TPOFF32         23

// Relocation Types (i386)
#define R_386_NONE          0
#define R_386_32            1
#define R_386_PC32          2
#define R_386_GOT32         3
#define R_386_PLT32         4
#define R_386_COPY          5
#define R_386_GLOB_DAT      6
#define R_386_JMP_SLOT      7
#define R_386_RELATIVE      8
#define R_386_GOTOFF        9
#define R_386_GOTPC         10
#define R_386_TLS_TPOFF     14
#define R_386_TLS_IE        15
#define R_386_TLS_GOTIE     16
#define R_386_TLS_LE        17
#define R_386_TLS_GD        18
#define R_386_TLS_LDM       19

// 64-bit ELF structures
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
} Elf64_Rel;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

typedef struct {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
} Elf64_Dyn;

// 32-bit ELF structures
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} Elf32_Rela;

typedef struct {
    int32_t d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} Elf32_Dyn;

// Helper macros for symbol info
#define ELF64_ST_BIND(i)   ((i) >> 4)
#define ELF64_ST_TYPE(i)   ((i) & 0xf)
#define ELF64_ST_INFO(b,t) (((b) << 4) + ((t) & 0xf))
#define ELF64_ST_VISIBILITY(o) ((o) & 0x3)

#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0xf)
#define ELF32_ST_INFO(b,t) (((b) << 4) + ((t) & 0xf))
#define ELF32_ST_VISIBILITY(o) ((o) & 0x3)

// Helper macros for relocation info
#define ELF64_R_SYM(i)    ((i) >> 32)
#define ELF64_R_TYPE(i)   ((i) & 0xffffffffL)
#define ELF64_R_INFO(s,t) (((s) << 32) + ((t) & 0xffffffffL))

#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((uint8_t)(i))
#define ELF32_R_INFO(s,t) (((s) << 8) + (uint8_t)(t))

// Loader structures
typedef struct {
    uint64_t base_addr;      // Load base address
    uint64_t entry_point;    // Entry point
    uint64_t phdr_addr;      // Program header address
    uint64_t phdr_count;     // Number of program headers
    uint64_t phdr_entsize;   // Program header entry size
    uint64_t interp_base;    // Interpreter base (if dynamic)
    uint64_t dynamic_addr;   // Dynamic section address
    uint64_t tls_base;       // TLS template base
    uint64_t tls_size;       // TLS template size
    uint64_t tls_align;      // TLS alignment
    void *   interp_path;    // Path to interpreter
    int      is_dynamic;     // Is dynamically linked
    int      has_tls;        // Has TLS segment
} elf_load_info_t;

typedef struct elf_symbol {
    char *name;
    uint64_t value;
    uint64_t size;
    uint8_t  type;
    uint8_t  binding;
    uint16_t section;
    struct elf_symbol *next;
} elf_symbol_t;

typedef struct {
    void *elf_data;           // ELF file in memory
    size_t elf_size;          // Size of ELF file
    Elf64_Ehdr *ehdr;         // ELF header
    Elf64_Phdr *phdrs;        // Program headers
    Elf64_Shdr *shdrs;        // Section headers
    char *shstrtab;           // Section header string table
    char *strtab;             // String table (.strtab)
    char *dynstr;             // Dynamic string table (.dynstr)
    Elf64_Sym *symtab;        // Symbol table (.symtab)
    Elf64_Sym *dynsym;        // Dynamic symbol table (.dynsym)
    size_t symtab_count;      // Number of symbols in .symtab
    size_t dynsym_count;      // Number of symbols in .dynsym
    Elf64_Dyn *dynamic;       // Dynamic section
    elf_symbol_t *symbol_hash[256]; // Symbol hash table
    uint64_t load_bias;       // Load bias for PIE/shared objects
} elf_context_t;

// Error codes
#define ELF_SUCCESS          0
#define ELF_ERROR_INVALID   -1
#define ELF_ERROR_UNSUPPORTED -2
#define ELF_ERROR_NOMEM     -3
#define ELF_ERROR_NOFILE    -4
#define ELF_ERROR_RELOC     -5
#define ELF_ERROR_SYMBOL    -6
#define ELF_ERROR_TLS       -7

// Function prototypes
int elf_validate(void *elf_data, size_t size);
int elf_load(void *elf_data, size_t size, elf_load_info_t *info);
int elf_load_segments(elf_context_t *ctx, elf_load_info_t *info);
int elf_apply_relocations(elf_context_t *ctx);
int elf_resolve_symbols(elf_context_t *ctx);
int elf_setup_tls(elf_context_t *ctx, elf_load_info_t *info);

// Context management
elf_context_t *elf_create_context(void *elf_data, size_t size);
void elf_destroy_context(elf_context_t *ctx);

// Symbol resolution
elf_symbol_t *elf_find_symbol(elf_context_t *ctx, const char *name);
uint64_t elf_get_symbol_value(elf_context_t *ctx, const char *name);

// Helper functions
const char *elf_get_section_name(elf_context_t *ctx, Elf64_Shdr *shdr);
Elf64_Shdr *elf_find_section(elf_context_t *ctx, const char *name);
Elf64_Shdr *elf_find_section_by_type(elf_context_t *ctx, uint32_t type);

// Debug functions
void elf_print_header(Elf64_Ehdr *ehdr);
void elf_print_program_headers(elf_context_t *ctx);
void elf_print_section_headers(elf_context_t *ctx);
void elf_print_symbols(elf_context_t *ctx);
void elf_print_dynamic(elf_context_t *ctx);

#endif // ELF_LOADER_H