#ifndef ELF_TEST_H
#define ELF_TEST_H

#include <stdint.h>
#include <stddef.h>

#include "elf_loader.h"
#include "vfs.h"
#include "memory.h"
#include "print.h"
#include "process.h"


typedef struct {
    elf_load_info_t load_info;
    elf_context_t *elf_ctx;
    uint64_t stack_base;
    uint64_t stack_size;
    char **argv;
    char **envp;
    int argc;
    int envc;
} elf_exec_context_t;


#define AT_NULL         0
#define AT_IGNORE       1
#define AT_EXECFD       2
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_FLAGS        8
#define AT_ENTRY        9
#define AT_NOTELF       10
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_PLATFORM     15
#define AT_HWCAP        16
#define AT_CLKTCK       17
#define AT_SECURE       23
#define AT_RANDOM       25
#define AT_EXECFN       31

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} Elf64_auxv_t;


int elf_exec_file(const char *path, char **argv, char **envp);

int elf_info(const char *path);

void elf_test_simple(void);

void shell_cmd_elfinfo(const char *path);
void shell_cmd_elfexec(const char *path);
void shell_cmd_elftest(void);

int process_create_from_elf(const char *path, const char *name);

#endif
