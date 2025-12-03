#include "elf_loader.h"
#include "vfs.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "string_helpers.h"
// ============================================================================
// ELF EXECUTION CONTEXT
// ============================================================================

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

// ============================================================================
// AUXILIARY VECTOR SETUP
// ============================================================================

// Auxiliary vector types (for passing info to userspace)
#define AT_NULL         0   // End of vector
#define AT_IGNORE       1   // Entry should be ignored
#define AT_EXECFD       2   // File descriptor of program
#define AT_PHDR         3   // Program headers for program
#define AT_PHENT        4   // Size of program header entry
#define AT_PHNUM        5   // Number of program headers
#define AT_PAGESZ       6   // System page size
#define AT_BASE         7   // Base address of interpreter
#define AT_FLAGS        8   // Flags
#define AT_ENTRY        9   // Entry point of program
#define AT_NOTELF       10  // Program is not ELF
#define AT_UID          11  // Real uid
#define AT_EUID         12  // Effective uid
#define AT_GID          13  // Real gid
#define AT_EGID         14  // Effective gid
#define AT_PLATFORM     15  // String identifying platform
#define AT_HWCAP        16  // Machine dependent hints about processor capabilities
#define AT_CLKTCK       17  // Frequency of times()
#define AT_SECURE       23  // Secure mode boolean
#define AT_RANDOM       25  // Address of 16 random bytes
#define AT_EXECFN       31  // Filename of executable

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} Elf64_auxv_t;

// Setup auxiliary vector on user stack
static void setup_auxv(uint64_t *stack_ptr, elf_load_info_t *info) {
    Elf64_auxv_t *auxv = (Elf64_auxv_t *)stack_ptr;
    int idx = 0;
    
    auxv[idx].a_type = AT_PHDR;
    auxv[idx++].a_val = info->phdr_addr;
    
    auxv[idx].a_type = AT_PHENT;
    auxv[idx++].a_val = info->phdr_entsize;
    
    auxv[idx].a_type = AT_PHNUM;
    auxv[idx++].a_val = info->phdr_count;
    
    auxv[idx].a_type = AT_PAGESZ;
    auxv[idx++].a_val = 4096;
    
    auxv[idx].a_type = AT_BASE;
    auxv[idx++].a_val = info->interp_base;
    
    auxv[idx].a_type = AT_ENTRY;
    auxv[idx++].a_val = info->entry_point;
    
    auxv[idx].a_type = AT_UID;
    auxv[idx++].a_val = 0;
    
    auxv[idx].a_type = AT_EUID;
    auxv[idx++].a_val = 0;
    
    auxv[idx].a_type = AT_GID;
    auxv[idx++].a_val = 0;
    
    auxv[idx].a_type = AT_EGID;
    auxv[idx++].a_val = 0;
    
    auxv[idx].a_type = AT_SECURE;
    auxv[idx++].a_val = 0;
    
    auxv[idx].a_type = AT_CLKTCK;
    auxv[idx++].a_val = 1000; // 1000 Hz
    
    auxv[idx].a_type = AT_NULL;
    auxv[idx++].a_val = 0;
}

// ============================================================================
// USERSPACE STACK SETUP
// ============================================================================

static uint64_t setup_user_stack(elf_exec_context_t *exec_ctx) {
    // Allocate user stack (2MB)
    exec_ctx->stack_size = 2 * 1024 * 1024;
    exec_ctx->stack_base = (uint64_t)kmalloc(exec_ctx->stack_size);
    
    if (!exec_ctx->stack_base) {
        return 0;
    }
    
    // Stack grows downward
    uint64_t stack_top = exec_ctx->stack_base + exec_ctx->stack_size;
    uint64_t sp = stack_top;
    
    // Align to 16 bytes
    sp &= ~0xF;
    
    // Reserve space for auxiliary vector
    sp -= sizeof(Elf64_auxv_t) * 16;
    uint64_t auxv_ptr = sp;
    
    // Push environment pointers
    sp -= sizeof(char *) * (exec_ctx->envc + 1);
    char **envp = (char **)sp;
    for (int i = 0; i < exec_ctx->envc; i++) {
        envp[i] = exec_ctx->envp[i];
    }
    envp[exec_ctx->envc] = NULL;
    
    // Push argument pointers
    sp -= sizeof(char *) * (exec_ctx->argc + 1);
    char **argv = (char **)sp;
    for (int i = 0; i < exec_ctx->argc; i++) {
        argv[i] = exec_ctx->argv[i];
    }
    argv[exec_ctx->argc] = NULL;
    
    // Push argc
    sp -= sizeof(uint64_t);
    *(uint64_t *)sp = exec_ctx->argc;
    
    // Setup auxiliary vector
    setup_auxv((uint64_t *)auxv_ptr, &exec_ctx->load_info);
    
    // Align to 16 bytes
    sp &= ~0xF;
    
    return sp;
}

// ============================================================================
// ELF EXECUTION FROM FILE
// ============================================================================

int elf_exec_file(const char *path, char **argv, char **envp) {
    PRINT(WHITE, BLACK, "[ELF EXEC] Loading: %s\n", path);
    
    // Open file
    int fd = vfs_open(path, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to open file: %s\n", path);
        return -1;
    }
    
    // Get file size
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        vfs_close(fd);
        return -1;
    }
    
    size_t file_size = node->size;
    
    // Allocate buffer for ELF file
    void *elf_buffer = kmalloc(file_size);
    if (!elf_buffer) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to allocate buffer\n");
        vfs_close(fd);
        return -1;
    }
    
    // Read entire file
    int bytes_read = vfs_read(fd, (uint8_t *)elf_buffer, file_size);
    vfs_close(fd);
    
    if (bytes_read != (int)file_size) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to read file\n");
        kfree(elf_buffer);
        return -1;
    }
    
    // Setup execution context
    elf_exec_context_t exec_ctx;
    for (size_t i = 0; i < sizeof(elf_exec_context_t); i++) {
        ((uint8_t *)&exec_ctx)[i] = 0;
    }
    
    exec_ctx.argv = argv;
    exec_ctx.envp = envp;
    
    // Count argc
    exec_ctx.argc = 0;
    if (argv) {
        while (argv[exec_ctx.argc]) {
            exec_ctx.argc++;
        }
    }
    
    // Count envc
    exec_ctx.envc = 0;
    if (envp) {
        while (envp[exec_ctx.envc]) {
            exec_ctx.envc++;
        }
    }
    
    // Load ELF
    int result = elf_load(elf_buffer, file_size, &exec_ctx.load_info);
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to load ELF: %d\n", result);
        kfree(elf_buffer);
        return -1;
    }
    
    // Create execution context
    exec_ctx.elf_ctx = elf_create_context(elf_buffer, file_size);
    if (!exec_ctx.elf_ctx) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to create context\n");
        kfree(elf_buffer);
        return -1;
    }
    
    // Setup user stack
    uint64_t user_sp = setup_user_stack(&exec_ctx);
    if (!user_sp) {
        PRINT(YELLOW, BLACK, "[ELF EXEC] Failed to setup stack\n");
        elf_destroy_context(exec_ctx.elf_ctx);
        kfree(elf_buffer);
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[ELF EXEC] Ready to execute:\n");
    PRINT(MAGENTA, BLACK, "  Entry point: 0x%llx\n", exec_ctx.load_info.entry_point);
    PRINT(MAGENTA, BLACK, "  Stack: 0x%llx\n", user_sp);
    PRINT(MAGENTA, BLACK, "  Base: 0x%llx\n", exec_ctx.load_info.base_addr);
    
    // In a real implementation, you would:
    // 1. Create a new process
    // 2. Setup page tables
    // 3. Switch to user mode
    // 4. Jump to entry point with user_sp
    
    // For now, we'll just clean up
    elf_destroy_context(exec_ctx.elf_ctx);
    kfree(elf_buffer);
    
    return 0;
}

// ============================================================================
// ELF INFORMATION DISPLAY
// ============================================================================

int elf_info(const char *path) {
    PRINT(WHITE, BLACK, "[ELF INFO] Analyzing: %s\n", path);
    
    // Open file
    int fd = vfs_open(path, FILE_READ);
    if (fd < 0) {
        PRINT(YELLOW, BLACK, "[ELF INFO] Failed to open file\n");
        return -1;
    }
    
    // Get file size
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        vfs_close(fd);
        return -1;
    }
    
    size_t file_size = node->size;
    
    // Allocate buffer
    void *elf_buffer = kmalloc(file_size);
    if (!elf_buffer) {
        vfs_close(fd);
        return -1;
    }
    
    // Read file
    vfs_read(fd, (uint8_t *)elf_buffer, file_size);
    vfs_close(fd);
    
    // Validate
    if (elf_validate(elf_buffer, file_size) != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[ELF INFO] Invalid ELF file\n");
        kfree(elf_buffer);
        return -1;
    }
    
    // Create context
    elf_context_t *ctx = elf_create_context(elf_buffer, file_size);
    if (!ctx) {
        kfree(elf_buffer);
        return -1;
    }
    
    // Print information
    elf_print_header(ctx->ehdr);
    elf_print_program_headers(ctx);
    elf_print_section_headers(ctx);
    
    // Resolve and print symbols
    elf_resolve_symbols(ctx);
    elf_print_symbols(ctx);
    
    // Print dynamic section if present
    if (ctx->dynamic) {
        elf_print_dynamic(ctx);
    }
    
    // Cleanup
    elf_destroy_context(ctx);
    kfree(elf_buffer);
    
    return 0;
}

// ============================================================================
// SIMPLE ELF LOADER TEST
// ============================================================================

void elf_test_simple(void) {
    PRINT(WHITE, BLACK, "\n=== ELF Loader Test ===\n");
    
    // Create a simple test ELF in memory (normally would read from file)
    // This is just a minimal valid ELF header for testing
    
    Elf64_Ehdr test_ehdr;
    for (size_t i = 0; i < sizeof(Elf64_Ehdr); i++) {
        ((uint8_t *)&test_ehdr)[i] = 0;
    }
    
    test_ehdr.e_ident[EI_MAG0] = ELFMAG0;
    test_ehdr.e_ident[EI_MAG1] = ELFMAG1;
    test_ehdr.e_ident[EI_MAG2] = ELFMAG2;
    test_ehdr.e_ident[EI_MAG3] = ELFMAG3;
    test_ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    test_ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    test_ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    test_ehdr.e_type = ET_EXEC;
    test_ehdr.e_machine = EM_X86_64;
    test_ehdr.e_version = EV_CURRENT;
    test_ehdr.e_entry = 0x400000;
    test_ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    test_ehdr.e_phentsize = sizeof(Elf64_Phdr);
    test_ehdr.e_shentsize = sizeof(Elf64_Shdr);
    
    // Validate
    int result = elf_validate(&test_ehdr, sizeof(Elf64_Ehdr));
    
    if (result == ELF_SUCCESS) {
        PRINT(MAGENTA, BLACK, "[TEST] ELF validation: PASSED\n");
    } else {
        PRINT(YELLOW, BLACK, "[TEST] ELF validation: FAILED (%d)\n", result);
    }
    
    // Print header
    elf_print_header(&test_ehdr);
}

// ============================================================================
// SHELL INTEGRATION
// ============================================================================

// Add to your shell commands:
void shell_cmd_elfinfo(const char *path) {
    elf_info(path);
}

void shell_cmd_elfexec(const char *path) {
    char *argv[] = { (char *)path, NULL };
    char *envp[] = { "PATH=/bin", "HOME=/root", NULL };
    
    elf_exec_file(path, argv, envp);
}

void shell_cmd_elftest(void) {
    elf_test_simple();
}

// ============================================================================
// PROCESS INTEGRATION
// ============================================================================

// Create a process from an ELF file
int process_create_from_elf(const char *path, const char *name) {
    PRINT(WHITE, BLACK, "[PROC] Creating process from ELF: %s\n", path);
    
    // Open and read ELF file
    int fd = vfs_open(path, FILE_READ);
    if (fd < 0) {
        return -1;
    }
    
    vfs_node_t *node = vfs_resolve_path(path);
    if (!node) {
        vfs_close(fd);
        return -1;
    }
    
    size_t file_size = node->size;
    void *elf_buffer = kmalloc(file_size);
    if (!elf_buffer) {
        vfs_close(fd);
        return -1;
    }
    
    vfs_read(fd, (uint8_t *)elf_buffer, file_size);
    vfs_close(fd);
    
    // Load ELF
    elf_load_info_t load_info;
    int result = elf_load(elf_buffer, file_size, &load_info);
    
    if (result != ELF_SUCCESS) {
        PRINT(YELLOW, BLACK, "[PROC] Failed to load ELF\n");
        kfree(elf_buffer);
        return -1;
    }
    
    // Create process
    int pid = process_create(name, load_info.base_addr);
    if (pid < 0) {
        kfree(elf_buffer);
        return -1;
    }
    
    // Create main thread with entry point
    void (*entry)(void) = (void (*)(void))load_info.entry_point;
    int tid = thread_create(pid, entry, 65536, 10000000, 1000000000, 1000000000);
    
    if (tid < 0) {
        PRINT(YELLOW, BLACK, "[PROC] Failed to create thread\n");
        kfree(elf_buffer);
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[PROC] Created process %d with thread %d from ELF\n", pid, tid);
    
    // i should probably not free elf_buffer yet . . .c
    // as the loaded code is still needed
    
    return pid;
}