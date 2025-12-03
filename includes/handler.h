#pragma once
#include <stdint.h>

typedef struct registers {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t int_no;
    uint64_t err_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} registers_t;

void isr0(registers_t* r);
void isr1(registers_t* r);
void isr2(registers_t* r);
void isr3(registers_t* r);
void isr4(registers_t* r);
void isr5(registers_t* r);
void isr6(registers_t* r);
void isr7(registers_t* r);
void isr8(registers_t* r);
void isr9(registers_t* r);
void isr10(registers_t* r);
void isr11(registers_t* r);
void isr12(registers_t* r);
void isr13(registers_t* r);
void isr14(registers_t* r);
void isr15(registers_t* r);
void isr16(registers_t* r);
void isr17(registers_t* r);
void isr18(registers_t* r);
void isr19(registers_t* r);
void isr20(registers_t* r);
void isr21(registers_t* r);
void isr22(registers_t* r);
void isr23(registers_t* r);
void isr24(registers_t* r);
void isr25(registers_t* r);
void isr26(registers_t* r);
void isr27(registers_t* r);
void isr28(registers_t* r);
void isr29(registers_t* r);
void isr30(registers_t* r);
void isr31(registers_t* r);