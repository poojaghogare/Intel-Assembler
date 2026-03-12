#x86 Intel Assembler (ELF32)


DESCRIPTION

A simple two-pass x86 assembler written in C.
It reads Intel syntax assembly (.asm) files and produces machine code output along with a NASM-style listing file.


BUILD

    gcc -o assembler program5.c


USAGE

    ./assembler input.asm

Output files:
    output.lst    - listing file in NASM ELF32 format


LISTING FORMAT

The listing file (output.lst) follows the NASM listing format:

    line  address  bytes             source
    5     00000000 89D8              mov eax, ebx
    11    0000000B 8B03              mov eax, [ebx]
    29    00000030 E8[00000000]      call L2

    - line      : source line number
    - address   : hex address within the section
    - bytes     : encoded machine code bytes (up to 9 shown)
    - [addr]    : square brackets mean the address is a relocatable reference
    - source    : original source line


SUPPORTED INSTRUCTIONS

    mov   add   sub   xor   cmp
    inc   dec   push  pop   lea
    jmp   call  ret   nop
    je    jne   jg    jge   jl    jle
    ja    jae   jb    jbe   jo    jno
    js    jns   jp    jnp


SUPPORTED DIRECTIVES

    section .text
    section .data
    section .bss
    global
    extern
    db   dw   dd   dq
    resb resw resd resq


SUPPORTED OPERAND FORMS

    mov eax, ebx              register to register
    mov eax, 4                immediate to register
    mov eax, [ebx]            memory (base)
    mov eax, [ebx+4]          memory (base + displacement)
    mov eax, [ebx+esi*4]      memory (base + index * scale)
    mov eax, [ebx+esi*2+16]   memory (base + index * scale + displacement)
    mov eax, [ebp-8]          memory (negative displacement)


REGISTERS

    eax   ecx   edx   ebx
    esp   ebp   esi   edi


EXAMPLE

input file (hello.asm):

    global _start

    section .data
    msg db "Hello", 0x0a

    section .text
    _start:
        mov eax, 4
        mov ebx, 1
        mov ecx, msg
        mov edx, 6
        xor ebx, ebx
        ret

run:

    gcc -o assembler program5.c
    ./assembler hello.asm
    cat output.lst


NOTES

    - Labels must end with a colon  ( example: _start: )
    - Comments start with semicolon ( ; this is a comment )
    - Hex values use 0x prefix      ( mov eax, 0xFF )
    - Jump instructions use a fixed 5-byte near slot in pass 1.
      Short jump relaxation (as NASM does) is not performed.
    - The assembler performs two passes:
        pass 1 - tokenize, build symbol table, encode known instructions
        pass 2 - resolve labels, re-encode label-dependent instructions
