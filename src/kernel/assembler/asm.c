#include "asm.h"
#include "print.h"
#include "string_helpers.h"

// ============================================================================
// STRING UTILITIES
// ============================================================================

static int str_equals(const char *s1, const char *s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

// ============================================================================
// CONTEXT MANAGEMENT
// ============================================================================

void asm_init(asm_context_t *ctx) {
    for (size_t i = 0; i < MAX_ASM_SIZE; i++) {
        ctx->code[i] = 0;
    }
    ctx->size = 0;
    ctx->error = 0;
    ctx->error_msg[0] = '\0';
}

static void asm_error(asm_context_t *ctx, const char *msg) {
    ctx->error = 1;
    int i = 0;
    while (msg[i] && i < 255) {
        ctx->error_msg[i] = msg[i];
        i++;
    }
    ctx->error_msg[i] = '\0';
}

static void asm_emit(asm_context_t *ctx, uint8_t byte) {
    if (ctx->size >= MAX_ASM_SIZE) {
        asm_error(ctx, "Code size exceeded");
        return;
    }
    ctx->code[ctx->size++] = byte;
}

static void asm_emit_bytes(asm_context_t *ctx, const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        asm_emit(ctx, bytes[i]);
    }
}

// ============================================================================
// REGISTER AND IMMEDIATE PARSING
// ============================================================================

static int parse_register(const char *reg) {
    // 64-bit registers
    if (reg[0] == 'r' && reg[1] == 'a' && reg[2] == 'x' && reg[3] == '\0') return 0;
    if (reg[0] == 'r' && reg[1] == 'c' && reg[2] == 'x' && reg[3] == '\0') return 1;
    if (reg[0] == 'r' && reg[1] == 'd' && reg[2] == 'x' && reg[3] == '\0') return 2;
    if (reg[0] == 'r' && reg[1] == 'b' && reg[2] == 'x' && reg[3] == '\0') return 3;
    if (reg[0] == 'r' && reg[1] == 's' && reg[2] == 'p' && reg[3] == '\0') return 4;
    if (reg[0] == 'r' && reg[1] == 'b' && reg[2] == 'p' && reg[3] == '\0') return 5;
    if (reg[0] == 'r' && reg[1] == 's' && reg[2] == 'i' && reg[3] == '\0') return 6;
    if (reg[0] == 'r' && reg[1] == 'd' && reg[2] == 'i' && reg[3] == '\0') return 7;
    if (reg[0] == 'r' && reg[1] >= '8' && reg[1] <= '9' && reg[2] == '\0') 
        return 8 + (reg[1] - '8');
    if (reg[0] == 'r' && reg[1] == '1' && reg[2] >= '0' && reg[2] <= '5' && reg[3] == '\0') 
        return 10 + (reg[2] - '0');
    
    // 32-bit registers
    if (reg[0] == 'e' && reg[1] == 'a' && reg[2] == 'x' && reg[3] == '\0') return 0;
    if (reg[0] == 'e' && reg[1] == 'c' && reg[2] == 'x' && reg[3] == '\0') return 1;
    if (reg[0] == 'e' && reg[1] == 'd' && reg[2] == 'x' && reg[3] == '\0') return 2;
    if (reg[0] == 'e' && reg[1] == 'b' && reg[2] == 'x' && reg[3] == '\0') return 3;
    if (reg[0] == 'e' && reg[1] == 's' && reg[2] == 'p' && reg[3] == '\0') return 4;
    if (reg[0] == 'e' && reg[1] == 'b' && reg[2] == 'p' && reg[3] == '\0') return 5;
    if (reg[0] == 'e' && reg[1] == 's' && reg[2] == 'i' && reg[3] == '\0') return 6;
    if (reg[0] == 'e' && reg[1] == 'd' && reg[2] == 'i' && reg[3] == '\0') return 7;
    
    return -1;
}

static int64_t parse_immediate(const char *str) {
    int64_t val = 0;
    int neg = 0;
    int i = 0;
    
    // Handle negative
    if (str[i] == '-') {
        neg = 1;
        i++;
    }
    
    // Hexadecimal
    if (str[i] == '0' && (str[i+1] == 'x' || str[i+1] == 'X')) {
        i += 2;
        while (str[i]) {
            if (str[i] >= '0' && str[i] <= '9') {
                val = val * 16 + (str[i] - '0');
            } else if (str[i] >= 'a' && str[i] <= 'f') {
                val = val * 16 + (str[i] - 'a' + 10);
            } else if (str[i] >= 'A' && str[i] <= 'F') {
                val = val * 16 + (str[i] - 'A' + 10);
            } else {
                break;
            }
            i++;
        }
    } 
    // Decimal
    else {
        while (str[i] >= '0' && str[i] <= '9') {
            val = val * 10 + (str[i] - '0');
            i++;
        }
    }
    
    return neg ? -val : val;
}

// ============================================================================
// INSTRUCTION ENCODERS
// ============================================================================

// MOV rax, imm64 - Full 64-bit immediate
static void asm_mov_rax_imm64(asm_context_t *ctx, int64_t imm) {
    asm_emit(ctx, 0x48);  // REX.W
    asm_emit(ctx, 0xB8);  // MOV rax, imm64
    for (int i = 0; i < 8; i++) {
        asm_emit(ctx, (imm >> (i * 8)) & 0xFF);
    }
}

// MOV reg, imm32 - Sign-extended 32-bit immediate
static void asm_mov_reg_imm32(asm_context_t *ctx, int reg, int32_t imm) {
    uint8_t rex = 0x48;  // REX.W
    if (reg >= 8) {
        rex |= 0x01;  // REX.B
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC7);  // MOV r/m64, imm32
    asm_emit(ctx, 0xC0 | reg);  // ModR/M: 11 000 reg
    for (int i = 0; i < 4; i++) {
        asm_emit(ctx, (imm >> (i * 8)) & 0xFF);
    }
}

// MOV reg, reg - Register to register
static void asm_mov_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;  // REX.W
    if (src >= 8) {
        rex |= 0x04;  // REX.R
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;  // REX.B
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x89);  // MOV r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);  // ModR/M: 11 src dst
}

// ADD reg, imm8
static void asm_add_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);  // ADD r/m64, imm8
    asm_emit(ctx, 0xC0 | reg);
    asm_emit(ctx, imm);
}

// ADD reg, reg
static void asm_add_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x01);  // ADD r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// SUB reg, imm8
static void asm_sub_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);  // SUB r/m64, imm8
    asm_emit(ctx, 0xE8 | reg);  // ModR/M: 11 101 reg (opcode /5)
    asm_emit(ctx, imm);
}

// SUB reg, reg
static void asm_sub_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x29);  // SUB r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// XOR reg, reg
static void asm_xor_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x31);  // XOR r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// OR reg, reg
static void asm_or_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x09);  // OR r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// AND reg, reg
static void asm_and_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x21);  // AND r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// CMP reg, imm8
static void asm_cmp_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);  // CMP r/m64, imm8
    asm_emit(ctx, 0xF8 | reg);  // ModR/M: 11 111 reg (opcode /7)
    asm_emit(ctx, imm);
}

// CMP reg, reg
static void asm_cmp_reg_reg(asm_context_t *ctx, int dst, int src) {
    uint8_t rex = 0x48;
    if (src >= 8) {
        rex |= 0x04;
        src -= 8;
    }
    if (dst >= 8) {
        rex |= 0x01;
        dst -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x39);  // CMP r/m64, r64
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

// PUSH reg
static void asm_push_reg(asm_context_t *ctx, int reg) {
    if (reg >= 8) {
        asm_emit(ctx, 0x41);  // REX.B
        reg -= 8;
    }
    asm_emit(ctx, 0x50 | reg);  // PUSH reg
}

// POP reg
static void asm_pop_reg(asm_context_t *ctx, int reg) {
    if (reg >= 8) {
        asm_emit(ctx, 0x41);  // REX.B
        reg -= 8;
    }
    asm_emit(ctx, 0x58 | reg);  // POP reg
}

// SYSCALL
static void asm_syscall(asm_context_t *ctx) {
    asm_emit(ctx, 0x0F);
    asm_emit(ctx, 0x05);
}

// RET
static void asm_ret(asm_context_t *ctx) {
    asm_emit(ctx, 0xC3);
}

// NOP
static void asm_nop(asm_context_t *ctx) {
    asm_emit(ctx, 0x90);
}

// INC reg
static void asm_inc_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xFF);  // INC r/m64
    asm_emit(ctx, 0xC0 | reg);  // ModR/M: 11 000 reg (opcode /0)
}

// DEC reg
static void asm_dec_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xFF);  // DEC r/m64
    asm_emit(ctx, 0xC8 | reg);  // ModR/M: 11 001 reg (opcode /1)
}

// NEG reg
static void asm_neg_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xF7);  // NEG r/m64
    asm_emit(ctx, 0xD8 | reg);  // ModR/M: 11 011 reg (opcode /3)
}

// NOT reg
static void asm_not_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xF7);  // NOT r/m64
    asm_emit(ctx, 0xD0 | reg);  // ModR/M: 11 010 reg (opcode /2)
}

// SHL reg, imm8
static void asm_shl_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC1);  // SHL r/m64, imm8
    asm_emit(ctx, 0xE0 | reg);  // ModR/M: 11 100 reg (opcode /4)
    asm_emit(ctx, imm);
}

// SHR reg, imm8
static void asm_shr_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC1);  // SHR r/m64, imm8
    asm_emit(ctx, 0xE8 | reg);  // ModR/M: 11 101 reg (opcode /5)
    asm_emit(ctx, imm);
}

// ============================================================================
// INSTRUCTION PARSING AND ASSEMBLY
// ============================================================================

int asm_line(asm_context_t *ctx, const char *line) {
    if (ctx->error) return -1;
    
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Skip empty lines and comments
    if (*line == '\0' || *line == ';' || *line == '#') {
        return 0;
    }
    
    char mnemonic[32];
    char arg1[64];
    char arg2[64];
    
    // Parse mnemonic
    int i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i < 31) {
        // Convert to lowercase for case-insensitive matching
        mnemonic[i] = line[i];
        if (mnemonic[i] >= 'A' && mnemonic[i] <= 'Z') {
            mnemonic[i] += 32;
        }
        i++;
    }
    mnemonic[i] = '\0';
    
    // Skip whitespace
    while (line[i] == ' ' || line[i] == '\t') i++;
    
    // Parse arg1
    int j = 0;
    while (line[i] && line[i] != ',' && line[i] != ' ' && line[i] != '\t' && 
           line[i] != ';' && line[i] != '#' && j < 63) {
        arg1[j++] = line[i++];
    }
    arg1[j] = '\0';
    
    // Skip comma and whitespace
    while (line[i] == ',' || line[i] == ' ' || line[i] == '\t') i++;
    
    // Parse arg2
    j = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && 
           line[i] != ';' && line[i] != '#' && j < 63) {
        arg2[j++] = line[i++];
    }
    arg2[j] = '\0';
    
    // ========================================================================
    // INSTRUCTION DISPATCH
    // ========================================================================
    
    // MOV - Move data
    if (str_equals(mnemonic, "mov")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        
        if (reg1 < 0) {
            asm_error(ctx, "Invalid destination register");
            return -1;
        }
        
        if (reg2 >= 0) {
            // mov reg, reg
            asm_mov_reg_reg(ctx, reg1, reg2);
        } else {
            // mov reg, imm
            int64_t imm = parse_immediate(arg2);
            if (reg1 == 0 && (imm > 0x7FFFFFFF || imm < (int64_t)0xFFFFFFFF80000000LL)) {
                asm_mov_rax_imm64(ctx, imm);
            } else {
                asm_mov_reg_imm32(ctx, reg1, (int32_t)imm);
            }
        }
    }
    
    // ADD - Addition
    else if (str_equals(mnemonic, "add")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        
        if (reg1 < 0) {
            asm_error(ctx, "Invalid destination register");
            return -1;
        }
        
        if (reg2 >= 0) {
            // add reg, reg
            asm_add_reg_reg(ctx, reg1, reg2);
        } else {
            // add reg, imm
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_add_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                asm_error(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    
    // SUB - Subtraction
    else if (str_equals(mnemonic, "sub")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        
        if (reg1 < 0) {
            asm_error(ctx, "Invalid destination register");
            return -1;
        }
        
        if (reg2 >= 0) {
            // sub reg, reg
            asm_sub_reg_reg(ctx, reg1, reg2);
        } else {
            // sub reg, imm
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_sub_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                asm_error(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    
    // XOR - Exclusive OR
    else if (str_equals(mnemonic, "xor")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_xor_reg_reg(ctx, reg1, reg2);
    }
    
    // OR - Bitwise OR
    else if (str_equals(mnemonic, "or")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_or_reg_reg(ctx, reg1, reg2);
    }
    
    // AND - Bitwise AND
    else if (str_equals(mnemonic, "and")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_and_reg_reg(ctx, reg1, reg2);
    }
    
    // CMP - Compare
    else if (str_equals(mnemonic, "cmp")) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        
        if (reg1 < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        
        if (reg2 >= 0) {
            // cmp reg, reg
            asm_cmp_reg_reg(ctx, reg1, reg2);
        } else {
            // cmp reg, imm
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_cmp_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                asm_error(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    
    // PUSH - Push onto stack
    else if (str_equals(mnemonic, "push")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_push_reg(ctx, reg);
    }
    
    // POP - Pop from stack
    else if (str_equals(mnemonic, "pop")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_pop_reg(ctx, reg);
    }
    
    // INC - Increment
    else if (str_equals(mnemonic, "inc")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_inc_reg(ctx, reg);
    }
    
    // DEC - Decrement
    else if (str_equals(mnemonic, "dec")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_dec_reg(ctx, reg);
    }
    
    // NEG - Negate
    else if (str_equals(mnemonic, "neg")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_neg_reg(ctx, reg);
    }
    
    // NOT - Bitwise NOT
    else if (str_equals(mnemonic, "not")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        asm_not_reg(ctx, reg);
    }
    
    // SHL - Shift left
    else if (str_equals(mnemonic, "shl")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        int64_t imm = parse_immediate(arg2);
        if (imm < 0 || imm > 63) {
            asm_error(ctx, "Shift count out of range (0-63)");
            return -1;
        }
        asm_shl_reg_imm8(ctx, reg, (int8_t)imm);
    }
    
    // SHR - Shift right
    else if (str_equals(mnemonic, "shr")) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            asm_error(ctx, "Invalid register");
            return -1;
        }
        int64_t imm = parse_immediate(arg2);
        if (imm < 0 || imm > 63) {
            asm_error(ctx, "Shift count out of range (0-63)");
            return -1;
        }
        asm_shr_reg_imm8(ctx, reg, (int8_t)imm);
    }
    
    // SYSCALL - System call
    else if (str_equals(mnemonic, "syscall")) {
        asm_syscall(ctx);
    }
    
    // RET - Return
    else if (str_equals(mnemonic, "ret")) {
        asm_ret(ctx);
    }
    
    // NOP - No operation
    else if (str_equals(mnemonic, "nop")) {
        asm_nop(ctx);
    }
    
    // Unknown instruction
    else {
        asm_error(ctx, "Unknown instruction");
        return -1;
    }
    
    return 0;
}

int asm_program(asm_context_t *ctx, const char *program) {
    char line[256];
    int line_idx = 0;
    int prog_idx = 0;
    int line_num = 1;
    
    while (program[prog_idx]) {
        // Read one line
        line_idx = 0;
        while (program[prog_idx] && program[prog_idx] != '\n' && line_idx < 255) {
            line[line_idx++] = program[prog_idx++];
        }
        line[line_idx] = '\0';
        
        // Skip newline
        if (program[prog_idx] == '\n') prog_idx++;
        
        // Assemble line
        if (asm_line(ctx, line) < 0) {
            // Add line number to error message
            char new_msg[256];
            int i = 0;
            new_msg[i++] = 'L';
            new_msg[i++] = 'i';
            new_msg[i++] = 'n';
            new_msg[i++] = 'e';
            new_msg[i++] = ' ';
            
            // Convert line number to string
            char num_buf[16];
            int num_len = 0;
            int temp_line = line_num;
            do {
                num_buf[num_len++] = '0' + (temp_line % 10);
                temp_line /= 10;
            } while (temp_line > 0);
            
            for (int j = num_len - 1; j >= 0; j--) {
                new_msg[i++] = num_buf[j];
            }
            
            new_msg[i++] = ':';
            new_msg[i++] = ' ';
            
            // Append original error message
            int j = 0;
            while (ctx->error_msg[j] && i < 255) {
                new_msg[i++] = ctx->error_msg[j++];
            }
            new_msg[i] = '\0';
            
            // Copy back
            for (i = 0; new_msg[i]; i++) {
                ctx->error_msg[i] = new_msg[i];
            }
            ctx->error_msg[i] = '\0';
            
            return -1;
        }
        
        line_num++;
    }
    
    return 0;
}

uint8_t *asm_get_code(asm_context_t *ctx, size_t *size) {
    if (ctx->error) {
        *size = 0;
        return NULL;
    }
    *size = ctx->size;
    return ctx->code;
}