#ifndef PROGRAM_H
#define PROGRAM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#define COMMA    ','
#define TAB      '\t'
#define SPACE    ' '
#define COLON    ':'
#define NEXTLINE '\n'
#define MAX_SYM  256
#define MAX_IR   2048
#define MAX_LST  4096

typedef enum { SEC_NONE, SEC_DATA, SEC_BSS, SEC_TEXT } Section;

typedef enum {
    T_UNKNOWN, T_IDENTIFIER, T_LABEL, T_MNEMONIC, T_REGISTER,
    T_DIRECTIVE, T_STRING, T_OPERATOR, T_IMMEDIATE, T_MEMORY,
    T_COMMENT, T_END
} TokenType;

typedef enum {
    INS_UNKNOWN, INS_MOV, INS_ADD, INS_SUB, INS_JMP, INS_XOR,
    INS_CMP, INS_INC, INS_DEC, INS_PUSH, INS_POP, INS_LEA,
    INS_CALL, INS_RET, INS_NOP, INS_JCC
} InstructionType;

typedef enum {
    DIR_NONE, DIR_SECTION, DIR_DB, DIR_DW, DIR_DD, DIR_DQ,
    DIR_RESB, DIR_RESW, DIR_RESD, DIR_RESQ, DIR_GLOBAL, DIR_EXTERN
} DirectiveType;

typedef enum {
    REG_EAX = 0, REG_ECX = 1, REG_EDX = 2, REG_EBX = 3,
    REG_ESP = 4, REG_EBP = 5, REG_ESI = 6, REG_EDI = 7,
    REG_INVALID = -1
} RegCode;

typedef enum { INS_NONE, INS_ONE, INS_TWO } InstrArgCount;
typedef enum { OP_NONE, OP_REG, OP_IMM, OP_LABEL, OP_MEM } OperandType;
typedef enum { O_SQ_BRACKET, C_SQ_BRACKET, PLUS, MINUS, MULTIPLICATION, UNKNOWN } OPERATOR;

typedef enum {
    CC_O = 0x0, CC_NO = 0x1, CC_B  = 0x2, CC_AE = 0x3,
    CC_E = 0x4, CC_NE = 0x5, CC_BE = 0x6, CC_A  = 0x7,
    CC_S = 0x8, CC_NS = 0x9, CC_P  = 0xA, CC_NP = 0xB,
    CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G  = 0xF
} CondCode;

typedef struct {
    int      line_no;
    uint32_t addr;
    uint8_t  bytes[16];
    int      len;
    int      has_addr;
    int      unresolved;
    char     src[512];
} LstEntry;

typedef struct {
    char    name[32];
    Section sec;
    int     addr;
    int     size;
    int     valid;
} Symbol;

typedef struct {
    unsigned int has_modrm;
    unsigned int has_sib;
    unsigned int has_disp;
    unsigned int has_imm;
} EncodingInfo;

typedef struct {
    InstructionType type;
    InstrArgCount   argc;
    OperandType     operand_types[3];
    EncodingInfo    encoding;
} Instruction;

typedef struct { uint8_t rm : 3; uint8_t reg : 3; uint8_t mod : 2; } ModRM;
typedef struct { uint8_t base : 3; uint8_t index : 3; uint8_t scale : 2; } SIB;
typedef struct { InstructionType instr; InstrArgCount argcnt; } INSTR_MAP;
typedef struct { char name[256]; TokenType type; } TOKEN;

typedef struct {
    OperandType type;
    RegCode     reg;
    int32_t     imm;
    char        label[32];
    RegCode     base;
    RegCode     index;
    int         scale;
    int         has_disp;
    int32_t     disp;
} Operand;

typedef struct {
    InstructionType ins;
    char            mnemonic[16];
    Operand         op1;
    Operand         op2;
    uint32_t        addr;
    uint32_t        size;
    uint32_t        line_no;
    char            src_line[200];
    uint8_t         enc[16];
    int             enc_len;
} IR;

extern LstEntry     lst[MAX_LST];
extern int          lst_count;
extern unsigned int lc_data;
extern unsigned int lc_bss;
extern unsigned int lc_text;
extern Section      current_section;
extern Symbol       symtab[MAX_SYM];
extern int          symcount;
extern char         label[32];
extern IR           ir_list[MAX_IR];
extern int          ir_count;

void          write_lst(const char *fname);
void          lst_add(int line_no, uint32_t addr, uint8_t *bytes, int len, int has_addr, int unresolved, const char *src);
int           add_symbol(const char *name, Section sec, unsigned int addr, unsigned int size);
void          reject_symbol(const char *name);
void          filterline(char *line);
TOKEN         get_next_token(const char *line, unsigned int *i);
TokenType     get_token_type(const char *word);
Section       get_section_type(const char *name);
int           handle_section(char *name);
DirectiveType get_directive_type(const char *word);
int           validate_data_directive(void);
int           validate_bss_directive(DirectiveType dir);
int           directiveSize(DirectiveType d);
InstructionType get_instruction_type(const char *word);
InstrArgCount   get_instruction_argcount(InstructionType instr);
RegCode         get_register_code(const char *reg);
int             is_mnemonic(const char *word);
int             is_immediate(const char *word);
int             is_memory_operand(const char *op);
void            parse_operand(const char *s, Operand *op);
int             fits_imm8(int32_t v);
static inline int fits_rel8(int32_t d) { return (d >= -128 && d <= 127); }
uint8_t         scale_bits(int scale);
int             needs_sib(Operand *op);
uint8_t         encode_modrm(uint8_t mod, uint8_t reg, uint8_t rm);
uint8_t         encode_sib(uint8_t scale, uint8_t index, uint8_t base);
int             encode_rm(uint8_t *out, Operand *rmop, uint8_t regfield);
int             encode_imm_rm(uint8_t *out, uint8_t opcode8, uint8_t opcode32, uint8_t subcode, Operand *rm, int32_t imm);
int             encode_instruction(uint8_t *out, InstructionType ins, const char *mnemonic, Operand *op1, Operand *op2, uint32_t pc);
int             is_jcc(const char *m);
int             get_cond_code(const char *m);
int             estimate_text_size(InstructionType ins);
int             calc_db_size(const char *line, unsigned int start);
int             calc_numeric_data_size(const char *line, unsigned int start, int elem_size);
int             string_byte_size(const char *s);
void            tokenize_line(FILE *fp);
int             emit_data(FILE *fp, uint8_t *data);
int             pass2_emit(uint8_t *text_out);
OPERATOR        get_operator_type(char op);
int             is_operator(char c);

#endif
