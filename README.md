# x86 Intel Assembler (ELF32)


## Description

A two-pass x86 assembler written in C that translates Intel syntax assembly
into ELF32 machine code. Produces a NASM-compatible listing file showing
addresses, encoded bytes, and source lines side by side.


## Files

    program.h       - all macros, enums, structs, and function prototypes
    program.c       - global variable definitions and all function bodies


## Build

    gcc -o assembler program.c


## Usage

    ./assembler input.asm

Output files:

    output.lst      - listing file in NASM ELF32 format


## Listing Format

The listing file follows the NASM ELF32 listing format:

    line   address  bytes              source
       5   00000000 89D8               mov eax, ebx
      11   0000000B 8B03               mov eax, [ebx]
      29   00000030 E8[00000000]       call L2

    line      - source line number, right-aligned in 6 characters
    address   - 8-digit hex address within the current section
    bytes     - encoded machine code bytes, up to 9 bytes shown inline
    [addr]    - square brackets indicate a relocatable reference
                the 4-byte address will be filled in by the linker
    source    - original source line text

Lines with no machine code (labels, directives, blank lines, section headers)
are printed with the address column left blank, matching NASM behavior.


## Supported Instructions

    mov   add   sub   xor   cmp
    inc   dec   push  pop   lea
    jmp   call  ret   nop

    Conditional jumps:
    je    jne   jg    jge   jl    jle
    ja    jae   jb    jbe   jo    jno
    js    jns   jp    jnp


## Supported Directives

    section .text
    section .data
    section .bss
    global
    extern
    db    dw    dd    dq
    resb  resw  resd  resq


## Supported Operand Forms

    mov eax, ebx              register to register
    mov eax, 4                immediate to register
    mov eax, [ebx]            memory - base register
    mov eax, [ebx+4]          memory - base + displacement
    mov eax, [ebx+esi*4]      memory - base + index * scale
    mov eax, [ebx+esi*2+16]   memory - base + index * scale + displacement
    mov eax, [ebp-8]          memory - negative displacement
    mov [ecx], eax            memory destination
    mov [esp+16], eax         memory destination with displacement


## Registers

    eax   ecx   edx   ebx
    esp   ebp   esi   edi


## How It Works

The assembler runs in two passes over the source file.

    Pass 1
        - Tokenizes each line into labels, mnemonics, operands, and directives
        - Builds the symbol table with section and address for each label
        - Encodes instructions that have no label operands immediately
          to get exact byte sizes and lay out addresses correctly
        - Records forward label references to resolve in pass 2

    Pass 2
        - Resolves all label operands to their final addresses
        - Re-encodes instructions that reference labels
        - Writes machine code into the output text buffer
        - Builds listing entries with final addresses and byte values

    Pass 3
        - Reads every raw source line
        - Inserts any line not already in the listing (blank lines,
          section headers, label-only lines, global and extern lines)
        - Sorts all listing entries by line number
        - Writes the final output.lst file


## Encoding Details

    ModRM byte  - encodes register and memory operand combinations
    SIB byte    - encodes scaled index addressing (base + index * scale)
    disp8       - 1-byte signed displacement when value fits in -128 to 127
    disp32      - 4-byte displacement for larger values
    imm8        - 1-byte immediate when value fits in -128 to 127
    imm32       - 4-byte immediate for larger values

    jmp and call to labels always use the 5-byte near form (E9/E8 cd).
    Short jump relaxation is not performed.


## Example

input file (hello.asm):

    global _start

    section .data
    msg db "Hello", 0x0a

    section .bss
    buf resb 64

    section .text
    _start:
        mov eax, 4
        mov ebx, 1
        mov ecx, msg
        mov edx, 6
        xor ebx, ebx
        ret

run:

    gcc -o assembler program.c
    ./assembler hello.asm
    cat output.lst

expected output.lst:

         1                                            global _start
         2
         3                                            section .data
         4 00000000                     msg db "Hello", 0x0a
         5
         6                                            section .bss
         7 00000000                     buf resb 64
         8
         9                                            section .text
        10                                            _start:
        11 00000000 B804000000          mov eax, 4
        12 00000005 BB01000000          mov ebx, 1
        13 0000000A B9[00000000]        mov ecx, msg
        14 0000000F BA06000000          mov edx, 6
        15 00000014 31DB                xor ebx, ebx
        16 00000016 C3                  ret


## Notes

    - Labels must end with a colon        ( _start: )
    - Comments start with a semicolon     ( ; this is a comment )
    - Hex immediates use 0x prefix        ( mov eax, 0xFF )
    - Hex suffix notation is also valid   ( mov eax, 0FFh )
    - String data uses double quotes      ( db "Hello", 0x0a )
    - The symbol table is printed to the terminal after assembly
    - Duplicate labels are detected and rejected with an error message
