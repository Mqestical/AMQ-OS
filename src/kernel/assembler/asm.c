#include "asm.h"
#include "print.h"
#include "string_helpers.h"

extern int strlen_local(const char* str);




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
        ASM_ERROR(ctx, "Code size exceeded");
        return;
    }
    ctx->code[ctx->size++] = byte;
}

static void asm_emit_bytes(asm_context_t *ctx, const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        asm_emit(ctx, bytes[i]);
    }
}

static void asm_call_rel32(asm_context_t *ctx, int32_t offset) {
    asm_emit(ctx, 0xE8);  // CALL rel32 opcode
    for (int i = 0; i < 4; i++) {
        asm_emit(ctx, (offset >> (i * 8)) & 0xFF);
    }
}

static void asm_call_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xFF);
    asm_emit(ctx, 0xD0 | reg);  // CALL r64
}


static int parse_register(const char *reg) {

    while (*reg == ' ' || *reg == '\t') reg++;

    if (reg[0] == 'r' && reg[1] == 'a' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 0;
    if (reg[0] == 'r' && reg[1] == 'c' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 1;
    if (reg[0] == 'r' && reg[1] == 'd' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 2;
    if (reg[0] == 'r' && reg[1] == 'b' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 3;
    if (reg[0] == 'r' && reg[1] == 's' && reg[2] == 'p' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 4;
    if (reg[0] == 'r' && reg[1] == 'b' && reg[2] == 'p' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 5;
    if (reg[0] == 'r' && reg[1] == 's' && reg[2] == 'i' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 6;
    if (reg[0] == 'r' && reg[1] == 'd' && reg[2] == 'i' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 7;
    if (reg[0] == 'r' && reg[1] >= '8' && reg[1] <= '9' && (reg[2] == '\0' || reg[2] == ' ' || reg[2] == ','))
        return 8 + (reg[1] - '8');
    if (reg[0] == 'r' && reg[1] == '1' && reg[2] >= '0' && reg[2] <= '5' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ','))
        return 10 + (reg[2] - '0');

    if (reg[0] == 'e' && reg[1] == 'a' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 0;
    if (reg[0] == 'e' && reg[1] == 'c' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 1;
    if (reg[0] == 'e' && reg[1] == 'd' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 2;
    if (reg[0] == 'e' && reg[1] == 'b' && reg[2] == 'x' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 3;
    if (reg[0] == 'e' && reg[1] == 's' && reg[2] == 'p' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 4;
    if (reg[0] == 'e' && reg[1] == 'b' && reg[2] == 'p' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 5;
    if (reg[0] == 'e' && reg[1] == 's' && reg[2] == 'i' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 6;
    if (reg[0] == 'e' && reg[1] == 'd' && reg[2] == 'i' && (reg[3] == '\0' || reg[3] == ' ' || reg[3] == ',')) return 7;

    return -1;
}

static int64_t parse_immediate(const char *str) {

    while (*str == ' ' || *str == '\t') str++;

    int64_t val = 0;
    int neg = 0;
    int i = 0;

    if (str[i] == '-') {
        neg = 1;
        i++;
    }

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
    else {
        while (str[i] >= '0' && str[i] <= '9') {
            val = val * 10 + (str[i] - '0');
            i++;
        }
    }

    return neg ? -val : val;
}


static void asm_mov_rax_imm64(asm_context_t *ctx, int64_t imm) {
    asm_emit(ctx, 0x48);
    asm_emit(ctx, 0xB8);
    for (int i = 0; i < 8; i++) {
        asm_emit(ctx, (imm >> (i * 8)) & 0xFF);
    }
}

static void asm_mov_reg_imm32(asm_context_t *ctx, int reg, int32_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC7);
    asm_emit(ctx, 0xC0 | reg);
    for (int i = 0; i < 4; i++) {
        asm_emit(ctx, (imm >> (i * 8)) & 0xFF);
    }
}

static void asm_mov_reg_reg(asm_context_t *ctx, int dst, int src) {
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
    asm_emit(ctx, 0x89);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

static void asm_add_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);
    asm_emit(ctx, 0xC0 | reg);
    asm_emit(ctx, imm);
}

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
    asm_emit(ctx, 0x01);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

static void asm_sub_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);
    asm_emit(ctx, 0xE8 | reg);
    asm_emit(ctx, imm);
}

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
    asm_emit(ctx, 0x29);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

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
    asm_emit(ctx, 0x31);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

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
    asm_emit(ctx, 0x09);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

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
    asm_emit(ctx, 0x21);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

static void asm_cmp_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0x83);
    asm_emit(ctx, 0xF8 | reg);
    asm_emit(ctx, imm);
}

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
    asm_emit(ctx, 0x39);
    asm_emit(ctx, 0xC0 | (src << 3) | dst);
}

static void asm_push_reg(asm_context_t *ctx, int reg) {
    if (reg >= 8) {
        asm_emit(ctx, 0x41);
        reg -= 8;
    }
    asm_emit(ctx, 0x50 | reg);
}

static void asm_pop_reg(asm_context_t *ctx, int reg) {
    if (reg >= 8) {
        asm_emit(ctx, 0x41);
        reg -= 8;
    }
    asm_emit(ctx, 0x58 | reg);
}

static void asm_syscall(asm_context_t *ctx) {
    asm_emit(ctx, 0x0F);
    asm_emit(ctx, 0x05);
}

static void asm_ret(asm_context_t *ctx) {
    asm_emit(ctx, 0xC3);
}

static void asm_nop(asm_context_t *ctx) {
    asm_emit(ctx, 0x90);
}

static void asm_inc_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xFF);
    asm_emit(ctx, 0xC0 | reg);
}

static void asm_dec_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xFF);
    asm_emit(ctx, 0xC8 | reg);
}

static void asm_neg_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xF7);
    asm_emit(ctx, 0xD8 | reg);
}

static void asm_not_reg(asm_context_t *ctx, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xF7);
    asm_emit(ctx, 0xD0 | reg);
}

static void asm_shl_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC1);
    asm_emit(ctx, 0xE0 | reg);
    asm_emit(ctx, imm);
}

static void asm_shr_reg_imm8(asm_context_t *ctx, int reg, int8_t imm) {
    uint8_t rex = 0x48;
    if (reg >= 8) {
        rex |= 0x01;
        reg -= 8;
    }
    asm_emit(ctx, rex);
    asm_emit(ctx, 0xC1);
    asm_emit(ctx, 0xE8 | reg);
    asm_emit(ctx, imm);
}


int asm_line(asm_context_t *ctx, const char *line) {
    if (ctx->error) return -1;

    while (*line == ' ' || *line == '\t') line++;

    if (*line == '\0' || *line == ';' || *line == '#') {
        return 0;
    }

    char mnemonic[32];
    char arg1[64];
    char arg2[64];
    int i = 0;

    while (line[i] && line[i] != ' ' && line[i] != '\t' && i < 31) {
        mnemonic[i] = line[i];
        if (mnemonic[i] >= 'A' && mnemonic[i] <= 'Z') {
            mnemonic[i] += 32;
        }
        i++;
    }
    mnemonic[i] = '\0';

    while (line[i] == ' ' || line[i] == '\t') i++;

    int j = 0;
    while (line[i] && line[i] != ',' && line[i] != ' ' && line[i] != '\t' &&
           line[i] != ';' && line[i] != '#' && line[i] != '\n' && line[i] != '\r' && j < 63) {
        arg1[j++] = line[i++];
    }
    arg1[j] = '\0';

    while (line[i] == ' ' || line[i] == '\t' || line[i] == ',') i++;

    j = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' &&
           line[i] != ';' && line[i] != '#' && line[i] != '\n' && line[i] != '\r' && line[i] != ',' && j < 63) {
        arg2[j++] = line[i++];
    }
    arg2[j] = '\0';

    if (STRNCMP(mnemonic, "mov", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);

        if (reg1 < 0) {
            ASM_ERROR(ctx, "Invalid destination register");
            return -1;
        }

        if (reg2 >= 0) {
            asm_mov_reg_reg(ctx, reg1, reg2);
        } else {
            int64_t imm = parse_immediate(arg2);
            if (reg1 == 0 && (imm > 0x7FFFFFFF || imm < (int64_t)0xFFFFFFFF80000000LL)) {
                asm_mov_rax_imm64(ctx, imm);
            } else {
                asm_mov_reg_imm32(ctx, reg1, (int32_t)imm);
            }
        }
    }
    else if (STRNCMP(mnemonic, "add", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);

        if (reg1 < 0) {
            ASM_ERROR(ctx, "Invalid destination register");
            return -1;
        }

        if (reg2 >= 0) {
            asm_add_reg_reg(ctx, reg1, reg2);
        } else {
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_add_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                ASM_ERROR(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    else if (STRNCMP(mnemonic, "sub", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);

        if (reg1 < 0) {
            ASM_ERROR(ctx, "Invalid destination register");
            return -1;
        }

        if (reg2 >= 0) {
            asm_sub_reg_reg(ctx, reg1, reg2);
        } else {
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_sub_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                ASM_ERROR(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    else if (STRNCMP(mnemonic, "xor", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_xor_reg_reg(ctx, reg1, reg2);
    }
    else if (STRNCMP(mnemonic, "or", 2) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_or_reg_reg(ctx, reg1, reg2);
    }
    else if (STRNCMP(mnemonic, "and", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);
        if (reg1 < 0 || reg2 < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_and_reg_reg(ctx, reg1, reg2);
    }
    else if (STRNCMP(mnemonic, "cmp", 3) == 0) {
        int reg1 = parse_register(arg1);
        int reg2 = parse_register(arg2);

        if (reg1 < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }

        if (reg2 >= 0) {
            asm_cmp_reg_reg(ctx, reg1, reg2);
        } else {
            int64_t imm = parse_immediate(arg2);
            if (imm >= -128 && imm <= 127) {
                asm_cmp_reg_imm8(ctx, reg1, (int8_t)imm);
            } else {
                ASM_ERROR(ctx, "Immediate out of range (-128 to 127)");
                return -1;
            }
        }
    }
    else if (STRNCMP(mnemonic, "call", 4) == 0) {
        int reg = parse_register(arg1);
        if (reg >= 0) {
            asm_call_reg(ctx, reg);
        } else {
            int64_t offset = parse_immediate(arg1);
            asm_call_rel32(ctx, (int32_t)offset);
        }
    }
    else if (STRNCMP(mnemonic, "push", 4) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_push_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "pop", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_pop_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "inc", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_inc_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "dec", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_dec_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "neg", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_neg_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "not", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        asm_not_reg(ctx, reg);
    }
    else if (STRNCMP(mnemonic, "shl", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        int64_t imm = parse_immediate(arg2);
        if (imm < 0 || imm > 63) {
            ASM_ERROR(ctx, "Shift count out of range (0-63)");
            return -1;
        }
        asm_shl_reg_imm8(ctx, reg, (int8_t)imm);
    }
    else if (STRNCMP(mnemonic, "shr", 3) == 0) {
        int reg = parse_register(arg1);
        if (reg < 0) {
            ASM_ERROR(ctx, "Invalid register");
            return -1;
        }
        int64_t imm = parse_immediate(arg2);
        if (imm < 0 || imm > 63) {
            ASM_ERROR(ctx, "Shift count out of range (0-63)");
            return -1;
        }
        asm_shr_reg_imm8(ctx, reg, (int8_t)imm);
    }
    else if (STRNCMP(mnemonic, "syscall", 7) == 0) {
        asm_syscall(ctx);
    }
    else if (STRNCMP(mnemonic, "ret", 3) == 0) {
        asm_ret(ctx);
    }
    else if (STRNCMP(mnemonic, "nop", 3) == 0) {
        asm_nop(ctx);
    }
    else {
        ASM_ERROR(ctx, "Unknown instruction");
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
        line_idx = 0;
        while (program[prog_idx] && program[prog_idx] != '\n' && line_idx < 255) {
            line[line_idx++] = program[prog_idx++];
        }
        line[line_idx] = '\0';

        if (program[prog_idx] == '\n') prog_idx++;

        if (asm_line(ctx, line) < 0) {
            char new_msg[256];
            int i = 0;
            new_msg[i++] = 'L';
            new_msg[i++] = 'i';
            new_msg[i++] = 'n';
            new_msg[i++] = 'e';
            new_msg[i++] = ' ';

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

            int j = 0;
            while (ctx->error_msg[j] && i < 255) {
                new_msg[i++] = ctx->error_msg[j++];
            }
            new_msg[i] = '\0';

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