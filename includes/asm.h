#ifndef ASM_H
#define ASM_H

#include <stdint.h>
#include <stddef.h>

#define MAX_ASM_SIZE 4096

typedef struct {
    uint8_t code[MAX_ASM_SIZE];
    size_t size;
    int error;
    char error_msg[256];
} asm_context_t;

// Initialize assembler context
void asm_init(asm_context_t *ctx);

// Assemble a line of assembly
int asm_line(asm_context_t *ctx, const char *line);

// Assemble multiple lines
int asm_program(asm_context_t *ctx, const char *program);

// Get assembled code
uint8_t *asm_get_code(asm_context_t *ctx, size_t *size);

#endif