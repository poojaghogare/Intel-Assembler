#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#define COMMA ','
#define TAB '\t'
#define SPACE ' '
#define COLON ':'
#define NEXTLINE '\n'
#define MAX_SYM 256
#define MAX_IR 2048
#define MAX_LST 4096

typedef struct
{
	int line_no;
	uint32_t addr;
	uint8_t bytes[16];
	int len;
	int has_addr;     
	int unresolved;     
	char src[512];
} LstEntry;

LstEntry lst[MAX_LST];
int lst_count = 0;

unsigned int lc_data = 0;
unsigned int lc_bss = 0;
unsigned int lc_text = 0;

typedef enum
{
	SEC_NONE,
	SEC_DATA,
	SEC_BSS,
	SEC_TEXT
} Section;

Section current_section = SEC_NONE;

typedef struct
{
	char name[32];
	Section sec;
	int addr;
	int size;
	int valid; 
} Symbol;

Symbol symtab[MAX_SYM];

int symcount = 0;
char label[32] = {0};

void write_lst(const char *fname)
{

	FILE *fp = fopen(fname, "w");
	if (!fp)
		return;

	for (int i = 0; i < lst_count; i++)
	{
		LstEntry *e = &lst[i];

		if (!e->has_addr)
		{
			fprintf(fp, "%6d %-43s%s\n", e->line_no, "", e->src);
			continue;
		}

		
		char bytefield[64] = {0}; 
		int bf = 0;
		int show = e->len < 9 ? e->len : 9;

		if (e->unresolved && e->len >= 4)
		{
			
			int pre = e->len - 4;
			for (int j = 0; j < pre && bf < 62; j++)
				bf += sprintf(bytefield + bf, "%02X", e->bytes[j]);
			
			if (bf + 10 <= 62)
			{
				bytefield[bf++] = '[';
				for (int j = pre; j < pre + 4; j++)
					bf += sprintf(bytefield + bf, "%02X", e->bytes[j]);
				bytefield[bf++] = ']';
			}
		}
		else
		{
			for (int j = 0; j < show && bf < 62; j++)
				bf += sprintf(bytefield + bf, "%02X", e->bytes[j]);
		}

		
		fprintf(fp, "%6d %08X %-18s  %s\n",
		        e->line_no, e->addr, bytefield, e->src);

		if (!e->unresolved && e->len > 9)
		{
			int j = 9;
			while (j < e->len)
			{
				char cont[32] = {0};
				int ci = 0;
				int chunk = (e->len - j) < 9 ? (e->len - j) : 9;
				for (int k = 0; k < chunk; k++)
					ci += sprintf(cont + ci, "%02X", e->bytes[j + k]);
				fprintf(fp, "%6s %08X-%-18s\n", "", e->addr + j, cont);
				j += chunk;
			}
		}
	}

	fclose(fp);
}

void lst_add(int line_no, uint32_t addr, uint8_t *bytes, int len,
             int has_addr, int unresolved, const char *src)
{
	if (lst_count >= MAX_LST)
		return;
	LstEntry *e = &lst[lst_count++];

	e->line_no    = line_no;
	e->addr       = addr;
	e->len        = len;
	e->has_addr   = has_addr;
	e->unresolved = unresolved;

	if (len > 0 && bytes)
		memcpy(e->bytes, bytes, len < 16 ? len : 16);

	strncpy(e->src, src, sizeof(e->src) - 1);
	e->src[sizeof(e->src) - 1] = '\0';
}

int add_symbol(const char *name, Section sec, unsigned int addr, unsigned int size)
{
	for (int i = 0; i < symcount; i++)
	{
		if (!strcmp(symtab[i].name, name))
		{
			if (symtab[i].valid == 0)
				return 0; 

			printf("Duplicate symbol: %s\n", name);
			symtab[i].valid = 0;
			return 0;
		}
	}

	strncpy(symtab[symcount].name, name, sizeof(symtab[symcount].name) - 1);
	symtab[symcount].name[sizeof(symtab[symcount].name) - 1] = '\0';
	symtab[symcount].sec = sec;
	symtab[symcount].addr = addr;
	symtab[symcount].size = size;
	symtab[symcount].valid = 1;
	symcount++;
	return 1;
}

void reject_symbol(const char *name)
{
	for (int i = 0; i < symcount; i++)
	{
		if (!strcmp(symtab[i].name, name))
		{
			symtab[i].valid = 0;
			return;
		}
	}

	// add as permanently rejected
	strncpy(symtab[symcount].name, name, sizeof(symtab[symcount].name) - 1);
	symtab[symcount].name[sizeof(symtab[symcount].name) - 1] = '\0';
	symtab[symcount].valid = 0;
	symcount++;
}

typedef enum
{
	T_UNKNOWN,
	T_IDENTIFIER,
	T_LABEL,
	T_MNEMONIC,
	T_REGISTER,
	T_DIRECTIVE,
	T_STRING,
	T_OPERATOR,
	T_IMMEDIATE,
	T_MEMORY,
	T_COMMENT,
	T_END
} TokenType;

typedef enum
{
	INS_UNKNOWN,
	INS_MOV,
	INS_ADD,
	INS_SUB,
	INS_JMP,
	INS_XOR,
	INS_CMP,
	INS_INC,
	INS_DEC,
	INS_PUSH,
	INS_POP,
	INS_LEA,
	INS_CALL,
	INS_RET,
	INS_NOP,
	INS_JCC,    /* all conditional jumps: je, jne, jg, jge, jl, jle, etc. */
} InstructionType;

typedef enum
{
	DIR_NONE,
	DIR_SECTION,
	DIR_DB,
	DIR_DW,
	DIR_DD,
	DIR_DQ,
	DIR_RESB,
	DIR_RESW,
	DIR_RESD,
	DIR_RESQ,
	DIR_GLOBAL,
	DIR_EXTERN
} DirectiveType;

typedef enum
{
	REG_EAX = 0,
	REG_ECX = 1,
	REG_EDX = 2,
	REG_EBX = 3,
	REG_ESP = 4,
	REG_EBP = 5,
	REG_ESI = 6,
	REG_EDI = 7,
	REG_INVALID = -1
} RegCode;

typedef enum
{
	INS_NONE,
	INS_ONE,
	INS_TWO,
} InstrArgCount;

typedef enum
{
	OP_NONE,
	OP_REG,
	OP_IMM,
	OP_LABEL,
	OP_MEM
} OperandType;


typedef enum
{
	O_SQ_BRACKET,
	C_SQ_BRACKET,
	PLUS,
	MINUS,
	MULTIPLICATION,
	UNKNOWN
} OPERATOR;

typedef struct
{
	unsigned int has_modrm;
	unsigned int has_sib;
	unsigned has_disp;
	unsigned has_imm;
} EncodingInfo;

typedef struct
{
	InstructionType type;
	InstrArgCount argc;
	OperandType operand_types[3];
	EncodingInfo encoding;
} Instruction;

typedef enum
{
	CC_O = 0x0,
	CC_NO = 0x1,
	CC_B = 0x2,
	CC_AE = 0x3,
	CC_E = 0x4,
	CC_NE = 0x5,
	CC_BE = 0x6,
	CC_A = 0x7,
	CC_S = 0x8,
	CC_NS = 0x9,
	CC_P = 0xA,
	CC_NP = 0xB,
	CC_L = 0xC,
	CC_GE = 0xD,
	CC_LE = 0xE,
	CC_G = 0xF
} CondCode;

typedef struct
{
	uint8_t rm : 3;
	uint8_t reg : 3;
	uint8_t mod : 2;
} ModRM;

typedef struct
{
	uint8_t base : 3;
	uint8_t index : 3;
	uint8_t scale : 2;
} SIB;

typedef struct
{
	InstructionType instr;
	InstrArgCount argcnt;
} INSTR_MAP;

typedef struct
{
	char name[256];
	TokenType type;
} TOKEN;

typedef struct
{
	OperandType type; // OP_REG, OP_MEM, OP_IMM, OP_LABEL

	RegCode reg; // OP_REG
	int32_t imm; // OP_IMM

	char label[32]; // OP_LABEL

	RegCode base;  // base register
	RegCode index; // index register or REG_INVALID
	int scale;	   // 1,2,4,8

	int has_disp; // IMPORTANT
	int32_t disp; // displacement
} Operand;

typedef struct
{
	InstructionType ins; // instruction enum
	char mnemonic[16];	 // mnemonic string

	Operand op1;
	Operand op2;

	uint32_t addr; // address in .text
	uint32_t size; // encoded size (exact, from pass1 encode)

	uint32_t line_no;	// source line number
	char src_line[200]; // original source line

	uint8_t  enc[16];   // encoded bytes (valid if enc_len > 0)
	int      enc_len;   // 0 = not yet encoded / label operand pending

} IR;

IR ir_list[MAX_IR];
int ir_count = 0;

/* ---------- tokenizer / lexer ---------- */
void filterline(char *line);
TOKEN get_next_token(const char *line, unsigned int *i);
TokenType get_token_type(const char *word);

/* ---------- section / directive ---------- */
Section get_section_type(const char *name);
int handle_section(char *name);
DirectiveType get_directive_type(const char *word);
int validate_data_directive(void);
int validate_bss_directive(DirectiveType dir);
int directiveSize(DirectiveType d);

/* ---------- symbol table ---------- */
int add_symbol(const char *name, Section sec, unsigned int addr, unsigned int size);
void reject_symbol(const char *name);

/* ---------- instruction / operand ---------- */
InstructionType get_instruction_type(const char *word);
InstrArgCount get_instruction_argcount(InstructionType instr);
RegCode get_register_code(const char *reg);
int is_mnemonic(const char *word);
int is_immediate(const char *word);
int is_memory_operand(const char *op);
void parse_operand(const char *s, Operand *op);

/* ---------- encoding helpers ---------- */
int fits_imm8(int32_t v);
static inline int fits_rel8(int32_t d);
uint8_t scale_bits(int scale);
int needs_sib(Operand *op);
uint8_t encode_modrm(uint8_t mod, uint8_t reg, uint8_t rm);
uint8_t encode_sib(uint8_t scale, uint8_t index, uint8_t base);

/* ---------- encoders ---------- */
int encode_rm(uint8_t *out, Operand *rmop, uint8_t regfield);
int encode_imm_rm(uint8_t *out, uint8_t opcode8, uint8_t opcode32, uint8_t subcode, Operand *rm, int32_t imm);

int encode_instruction(uint8_t *out, InstructionType ins, const char *mnemonic, Operand *op1, Operand *op2, uint32_t pc);

/* ---------- jumps ---------- */
int is_jcc(const char *m);
int get_cond_code(const char *m);

/* ---------- size / estimation ---------- */
int estimate_text_size(InstructionType ins);
int calc_db_size(const char *line, unsigned int start);
int calc_numeric_data_size(const char *line, unsigned int start, int elem_size);
int string_byte_size(const char *s);

/* ---------- passes ---------- */
void tokenize_line(FILE *fp);
int emit_data(FILE *fp, uint8_t *data);
int pass2_emit(uint8_t *);

int is_jcc(const char *m)
{
	static const char *jcc[] = {
		"je", "jne", "jg", "jge", "jl", "jle",
		"ja", "jae", "jb", "jbe", "jo", "jno",
		"js", "jns", "jp", "jnp"};
	for (int i = 0; i < 16; i++)
		if (!strcmp(m, jcc[i]))
			return 1;
	return 0;
}

static const INSTR_MAP instruction_table[] = {
	{INS_MOV, INS_TWO},
	{INS_ADD, INS_TWO},
	{INS_SUB, INS_TWO},
	{INS_JMP, INS_ONE},
	{INS_XOR, INS_TWO},
	{INS_CMP, INS_TWO},
	{INS_INC, INS_ONE},
	{INS_DEC, INS_ONE},
	{INS_PUSH, INS_ONE},
	{INS_POP, INS_ONE},
	{INS_LEA, INS_TWO},
	{INS_CALL, INS_ONE},
	{INS_RET, INS_NONE},
	{INS_NOP, INS_NONE},
	{INS_JCC, INS_ONE},
	{INS_UNKNOWN, INS_NONE}};

uint8_t scale_bits(int scale)
{
	switch (scale)
	{
	case 1:
		return 0;
	case 2:
		return 1;
	case 4:
		return 2;
	case 8:
		return 3;
	default:
		return 0;
	}
}

int get_cond_code(const char *m)
{
	if (!strcmp(m, "je"))
		return CC_E;
	if (!strcmp(m, "jne"))
		return CC_NE;
	if (!strcmp(m, "jg"))
		return CC_G;
	if (!strcmp(m, "jge"))
		return CC_GE;
	if (!strcmp(m, "jl"))
		return CC_L;
	if (!strcmp(m, "jle"))
		return CC_LE;
	if (!strcmp(m, "ja"))
		return CC_A;
	if (!strcmp(m, "jae"))
		return CC_AE;
	if (!strcmp(m, "jb"))
		return CC_B;
	if (!strcmp(m, "jbe"))
		return CC_BE;
	return -1;
}

static inline int fits_rel8(int32_t d)
{
	return (d >= -128 && d <= 127);
}

int fits_imm8(int32_t v)
{
	return (v >= -128 && v <= 127);
}

int encode_imm_rm(uint8_t *out, uint8_t opcode8, uint8_t opcode32, uint8_t subcode, Operand *rm, int32_t imm)
{
	int len = 0;

	if (fits_imm8(imm))
	{
		out[len++] = opcode8;
		len += encode_rm(out + len, rm, subcode);
		out[len++] = (int8_t)imm;
	}
	else
	{
		out[len++] = opcode32;
		len += encode_rm(out + len, rm, subcode);
		memcpy(out + len, &imm, 4);
		len += 4;
	}
	return len;
}

InstructionType get_instruction_type(const char *word)
{
	if (!strcmp(word, "mov"))
		return INS_MOV;
	if (!strcmp(word, "add"))
		return INS_ADD;
	if (!strcmp(word, "sub"))
		return INS_SUB;
	if (!strcmp(word, "jmp"))
		return INS_JMP;
	if (!strcmp(word, "xor"))
		return INS_XOR;
	if (!strcmp(word, "cmp"))
		return INS_CMP;
	if (!strcmp(word, "inc"))
		return INS_INC;
	if (!strcmp(word, "dec"))
		return INS_DEC;
	if (!strcmp(word, "push"))
		return INS_PUSH;
	if (!strcmp(word, "pop"))
		return INS_POP;
	if (!strcmp(word, "lea"))
		return INS_LEA;
	if (!strcmp(word, "call"))
		return INS_CALL;
	if (!strcmp(word, "ret"))
		return INS_RET;
	if (!strcmp(word, "nop"))
		return INS_NOP;
	if (is_jcc(word))
		return INS_JCC;
	return INS_UNKNOWN;
}

RegCode get_register_code(const char *reg)
{
	if (!reg)
		return REG_INVALID;

	if (!strcmp(reg, "eax"))
		return REG_EAX;
	if (!strcmp(reg, "ecx"))
		return REG_ECX;
	if (!strcmp(reg, "edx"))
		return REG_EDX;
	if (!strcmp(reg, "ebx"))
		return REG_EBX;
	if (!strcmp(reg, "esp"))
		return REG_ESP;
	if (!strcmp(reg, "ebp"))
		return REG_EBP;
	if (!strcmp(reg, "esi"))
		return REG_ESI;
	if (!strcmp(reg, "edi"))
		return REG_EDI;

	return REG_INVALID;
}


DirectiveType get_directive_type(const char *word)
{
	if (!strcmp(word, "section"))
		return DIR_SECTION;
	if (!strcmp(word, "db"))
		return DIR_DB;
	if (!strcmp(word, "dw"))
		return DIR_DW;
	if (!strcmp(word, "dd"))
		return DIR_DD;
	if (!strcmp(word, "dq"))
		return DIR_DQ;
	if (!strcmp(word, "resb"))
		return DIR_RESB;
	if (!strcmp(word, "resw"))
		return DIR_RESW;
	if (!strcmp(word, "resd"))
		return DIR_RESD;
	if (!strcmp(word, "global"))
		return DIR_GLOBAL;
	if (!strcmp(word, "extern"))
		return DIR_EXTERN;
	return DIR_NONE;
}

int directiveSize(DirectiveType d)
{
	switch (d)
	{
	case DIR_DB:
		return 1;
	case DIR_DW:
		return 2;
	case DIR_DD:
		return 4;
	case DIR_DQ:
		return 8;
	case DIR_RESB:
		return 1;
	case DIR_RESW:
		return 2;
	case DIR_RESD:
		return 4;
	case DIR_RESQ:
		return 8;
	default:
		return 0;
	}
}


InstrArgCount get_instruction_argcount(InstructionType instr)
{
	for (int i = 0; instruction_table[i].instr != INS_UNKNOWN; i++)
	{
		if (instruction_table[i].instr == instr)
			return instruction_table[i].argcnt;
	}
	return INS_NONE;
}

int is_memory_operand(const char *op)
{
	return (op[0] == '[' && op[strlen(op) - 1] == ']');
}

uint8_t encode_modrm(uint8_t mod, uint8_t reg, uint8_t rm)
{
	return ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);
}

uint8_t encode_sib(uint8_t scale, uint8_t index, uint8_t base)
{
	return ((scale & 0x3) << 6) | ((index & 0x7) << 3) | (base & 0x7);
}

int needs_sib(Operand *op)
{
	if (op->type != OP_MEM)
		return 0;
	if (op->index != REG_INVALID)
		return 1;
	if (op->base == REG_ESP)
		return 1;
	return 0;
}

int encode_rm(uint8_t *out, Operand *rm, uint8_t reg)
{
    int len = 0;

    /* REG */
    if (rm->type == OP_REG)
    {
        out[len++] = encode_modrm(3, reg, rm->reg);
        return len;
    }

    /* force [ebp] → disp8=0 */
    if (rm->base == REG_EBP && !rm->has_disp)
    {
        rm->has_disp = 1;
        rm->disp = 0;
    }

    /* [disp32] */
    if (rm->type == OP_MEM &&
        rm->base == REG_INVALID &&
        rm->index == REG_INVALID &&
        rm->has_disp)
    {
        out[len++] = encode_modrm(0, reg, 5);
        memcpy(out + len, &rm->disp, 4);
        return len + 4;
    }

    int need_sib = (rm->index != REG_INVALID || rm->base == REG_ESP);
    int mod;

    if (!rm->has_disp)
        mod = (rm->base == REG_EBP) ? 1 : 0;
    else if (rm->disp >= -128 && rm->disp <= 127)
        mod = 1;
    else
        mod = 2;

    int rmfield = need_sib ? 4 : rm->base;
    out[len++] = encode_modrm(mod, reg, rmfield);

    if (need_sib)
    {
        int scale = scale_bits(rm->scale);
        int index = (rm->index == REG_INVALID) ? 4 : rm->index;
        int base = rm->base;

        if (base == REG_INVALID && mod == 0)
            base = 5;

        out[len++] = encode_sib(scale, index, base);
    }

    if (mod == 1)
        out[len++] = (int8_t)rm->disp;
    else if (mod == 2 || (rm->base == REG_INVALID && mod == 0))
    {
        memcpy(out + len, &rm->disp, 4);
        len += 4;
    }

    return len;
}

int encode_instruction(uint8_t *out, InstructionType ins, const char *mnemonic, Operand *op1, Operand *op2, uint32_t pc)
{
	int len = 0;

	switch (ins)
	{
	case INS_MOV:
		if (op1->type == OP_REG && op2->type == OP_REG)
		{
			/* 89 /r  — MOV r/m32, r32  (NASM preferred direction) */
			out[len++] = 0x89;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else if (op1->type == OP_REG && op2->type == OP_MEM)
		{
			/* 8B /r  — MOV r32, r/m32 */
			out[len++] = 0x8B;
			len += encode_rm(out + len, op2, op1->reg);
		}
		else if (op1->type == OP_MEM && op2->type == OP_REG)
		{
			/* 89 /r  — MOV r/m32, r32 */
			out[len++] = 0x89;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else if (op1->type == OP_REG && op2->type == OP_IMM)
		{
			out[len++] = 0xB8 + op1->reg;
			memcpy(out + len, &op2->imm, 4);
			len += 4;
		}
		else if (op1->type == OP_MEM && op2->type == OP_IMM)
		{
			out[len++] = 0xC7;
			len += encode_rm(out + len, op1, 0); // /0
			memcpy(out + len, &op2->imm, 4);
			len += 4;
		}

		break;

	case INS_ADD:
		if (op2->type == OP_IMM)
		{
			len += encode_imm_rm(out, 0x83, 0x81, 0x0, op1, op2->imm);
		}
		else if (op1->type == OP_REG && op2->type == OP_REG)
		{
			/* 01 /r  — ADD r/m32, r32 */
			out[len++] = 0x01;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else if (op1->type == OP_MEM && op2->type == OP_REG)
		{
			/* 01 /r  — ADD r/m32, r32 (memory destination) */
			out[len++] = 0x01;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else
		{
			/* 03 /r  — ADD r32, r/m32 (register destination, memory source) */
			out[len++] = 0x03;
			len += encode_rm(out + len, op2, op1->reg);
		}
		break;

	case INS_SUB:
		if (op2->type == OP_IMM)
		{
			len += encode_imm_rm(out, 0x83, 0x81, 0x5, op1, op2->imm);
		}
		else if (op1->type == OP_REG && op2->type == OP_REG)
		{
			/* 29 /r  — SUB r/m32, r32 */
			out[len++] = 0x29;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else if (op1->type == OP_MEM && op2->type == OP_REG)
		{
			/* 29 /r  — SUB r/m32, r32 (memory destination) */
			out[len++] = 0x29;
			len += encode_rm(out + len, op1, op2->reg);
		}
		else
		{
			/* 2B /r  — SUB r32, r/m32 (register destination, memory source) */
			out[len++] = 0x2B;
			len += encode_rm(out + len, op2, op1->reg);
		}
		break;

	case INS_CMP:
		if (op2->type == OP_IMM)
		{
			len += encode_imm_rm(out, 0x83, 0x81, 0x7, op1, op2->imm);
		}
		else
		{
			out[len++] = 0x39;
			len += encode_rm(out + len, op1, op2->reg);
		}
		break;

	case INS_XOR:
		out[len++] = 0x31;
		len += encode_rm(out + len, op1, op2->reg);
		break;

	case INS_JMP:
		if (fits_rel8(op1->imm))
		{
			out[len++] = 0xEB;
			out[len++] = (int8_t)op1->imm;
		}
		else
		{
			out[len++] = 0xE9; 
			memcpy(out + len, &op1->imm, 4);
			len += 4;
		}
		break;

	case INS_CALL:
		out[len++] = 0xE8;
		memcpy(out + len, &op1->imm, 4);
		len += 4;
		break;

	case INS_RET:
		out[len++] = 0xC3;
		break;

	case INS_NOP:
		out[len++] = 0x90;
		break;

	case INS_LEA:
		out[len++] = 0x8D;
		len += encode_rm(out + len, op2, op1->reg);
		break;

	case INS_PUSH:
		if (op1->type == OP_REG)
		{
			out[len++] = 0x50 + op1->reg;
		}
		else if (op1->type == OP_IMM)
		{
			if (fits_imm8(op1->imm))
			{
				out[len++] = 0x6A;
				out[len++] = (int8_t)op1->imm;
			}
			else
			{
				out[len++] = 0x68;
				memcpy(out + len, &op1->imm, 4);
				len += 4;
			}
		}
		else if (op1->type == OP_MEM)
		{
			out[len++] = 0xFF;
			len += encode_rm(out + len, op1, 6); // /6
		}
		break;
	case INS_POP:
		if (op1->type == OP_REG)
		{
			out[len++] = 0x58 + op1->reg;
		}
		else if (op1->type == OP_MEM)
		{
			out[len++] = 0x8F;
			len += encode_rm(out + len, op1, 0); // /0
		}
		break;
	case INS_INC:
		if (op1->type == OP_REG)
		{
			out[len++] = 0x40 + op1->reg;
		}
		else if (op1->type == OP_MEM)
		{
			out[len++] = 0xFF;
			len += encode_rm(out + len, op1, 0); // /0
		}
		break;

	case INS_DEC:
		if (op1->type == OP_REG)
		{
			out[len++] = 0x48 + op1->reg;
		}
		else if (op1->type == OP_MEM)
		{
			out[len++] = 0xFF;
			len += encode_rm(out + len, op1, 1); // /1
		}
		break;

	case INS_JCC:
	{
		int cc = get_cond_code(mnemonic);
		int32_t rel8 = op1->imm - (pc + 2);
		int32_t rel32 = op1->imm - (pc + 6);

		if (fits_rel8(rel8))
		{
			out[len++] = 0x70 | cc;
			out[len++] = (int8_t)rel8;
		}
		else
		{
			out[len++] = 0x0F;
			out[len++] = 0x80 | cc;
			memcpy(out + len, &rel32, 4);
			len += 4;
		}
		break;
	}
	default:
		break;
	}
	return len;
}

int is_immediate(const char *word)
{
	if (word[0] == '+' || word[0] == '-')
		++word;
	if (!*word)
		return 0;
	if (word[0] == '0' && (word[1] == 'x' || word[1] == 'X'))
	{
		for (int i = 2; word[i]; ++i)
			if (!isxdigit((unsigned char)word[i]))
				return 0;
		return 1;
	}
	int len = strlen(word);
	if (len > 1 && (word[len - 1] == 'h' || word[len - 1] == 'H'))
	{
		for (int i = 0; i < len - 1; ++i)
			if (!isxdigit((unsigned char)word[i]))
				return 0;
		return 1;
	}
	for (int i = 0; word[i]; ++i)
		if (!isdigit((unsigned char)word[i]))
			return 0;
	return 1;
}

OPERATOR get_operator_type(const char op)
{
	if (op == '[')
		return O_SQ_BRACKET;
	if (op == ']')
		return C_SQ_BRACKET;
	if (op == '+')
		return PLUS;
	if (op == '-')
		return MINUS;
	if (op == '*')
		return MULTIPLICATION;
	return UNKNOWN;
}

int is_operator(const char c)
{
	if (get_operator_type(c) != UNKNOWN)
		return 1;
	return 0;
}

int is_mnemonic(const char *word)
{
	return get_instruction_type(word) != INS_UNKNOWN;
}

Section get_section_type(const char *name)
{
	if (*name != '.')
		return SEC_NONE;

	if (strncmp(name, ".data", 5) == 0)
		return SEC_DATA;

	if (strncmp(name, ".bss", 4) == 0)
		return SEC_BSS;

	if (strncmp(name, ".text", 5) == 0)
		return SEC_TEXT;

	return SEC_NONE;
}

int handle_section(char *name)
{
	Section s = get_section_type(name);
	if (s == SEC_NONE)
	{
		printf("Invalid section\n");
		return 0;
	}
	current_section = s;
	return 1;
}

int validate_data_directive()
{
	if (current_section != SEC_DATA)
	{
		printf("Data directive outside .data section\n");
		return 0;
	}
	return 1;
}

int validate_bss_directive(DirectiveType dir)
{
	(void)dir;
	if (current_section != SEC_BSS)
	{
		printf("BSS directive outside .bss section\n");
		return 0;
	}
	return 1;
}

TokenType get_token_type(const char *word)
{
	if (!word || !*word)
		return T_UNKNOWN;
	if (word[0] == ';')
		return T_COMMENT;
	if (word[0] == '"')
		return T_STRING;
	if (is_operator(word[0]))
		return T_OPERATOR;
	if (get_directive_type(word) != DIR_NONE)
		return T_DIRECTIVE;
	if (get_register_code(word) != REG_INVALID)
		return T_REGISTER;
	if (is_immediate(word))
		return T_IMMEDIATE;
	if (is_mnemonic(word))
		return T_MNEMONIC;
	if (word[0] == '[')
		return T_MEMORY;
	size_t len = strlen(word);
	if (len > 0 && word[len - 1] == ':')
		return T_LABEL;
	return T_IDENTIFIER;
}

void filterline(char *line)
{
	int i = 0, j = 0;
	int in_space = 0;

	while (line[i] == ' ' || line[i] == '\t')
		i++;

	for (; line[i] != '\0'; i++)
	{
		if (line[i] == ' ' || line[i] == '\t')
		{
			if (!in_space)
			{
				line[j++] = ' ';
				in_space = 1;
			}
		}
		else if (line[i] == '\n')
		{
			break;
		}
		else
		{
			line[j++] = line[i];
			in_space = 0;
		}
	}

	// remove trailing space
	if (j > 0 && line[j - 1] == ' ')
		j--;

	line[j] = '\0';
}


int string_byte_size(const char *s)
{
	int size = 0;

	// skip opening quote
	for (int i = 1; s[i] && s[i] != '"'; i++)
	{
		if (s[i] == '\\')
		{
			i++; // escape sequence
			if (!s[i])
				break;

			switch (s[i])
			{
			case 'n':  // \n
			case 't':  // \t
			case 'r':  // \r
			case '\\': // \\"
			case '"':  // \"
			case '0':  // \0
				size += 1;
				break;

			case 'x': // \xNN
				if (isxdigit(s[i + 1]) && isxdigit(s[i + 2]))
					i += 2;
				size += 1;
				break;

			default:
				size += 1; 
			}
		}
		else
		{
			size += 1;
		}
	}

	return size;
}

int calc_db_size(const char *line, unsigned int start)
{
	unsigned int i = start;
	int size = 0;
	int in_string = 0;

	while (line[i])
	{
		if (line[i] == '"')
		{
			in_string = !in_string;
			i++;
			continue;
		}

		if (in_string)
		{
			if (line[i] == '\\')
			{
				i++;
				if (!line[i])
					break;

				switch (line[i])
				{
				case 'n':  
				case 't':  
				case 'r':  
				case '\\': 
				case '"':  
				case '0':  
					size += 1;
					break;
				case 'x':
					if (isxdigit(line[i + 1]) && isxdigit(line[i + 2]))
						i += 2;
					size += 1;
					break;
				default:
					size += 1; 
				}
			}
			else
			{
				size += 1;
			}
		}
		else if (line[i] == ',' || line[i] == ' ' || line[i] == '\t')
		{
			i++; 
			continue;
		}
		else if (isdigit(line[i]) || line[i] == '+' || line[i] == '-')
		{
			while (line[i] && line[i] != ',' && line[i] != ' ' && line[i] != '\t')
				i++;
			size += 1;
			continue;
		}

		i++;
	}

	return size;
}

int calc_numeric_data_size(const char *line, unsigned int start, int elem_size)
{
	unsigned int i = start;
	int count = 0;

	while (line[i])
	{
		TOKEN tok = get_next_token(line, &i);

		if (tok.type == T_IMMEDIATE)
		{
			count++;
		}
		else if (tok.type == T_STRING)
		{
			printf("Error: string not allowed here: %s\n", tok.name);
			return -1;
		}
		else if (tok.type == T_END)
			break;
	}

	return count * elem_size;
}

TOKEN get_next_token(const char *line, unsigned int *i)
{
	TOKEN token;
	memset(token.name, 0, sizeof(token.name));
	unsigned int j = 0;

	while (line[*i] == SPACE || line[*i] == TAB || line[*i] == COMMA)
	{
		(*i)++;
		continue;
	}

	if (line[*i] == '[')
	{
		while (line[*i] && line[*i] != ']' && j < sizeof(token.name) - 2)
			token.name[j++] = line[(*i)++];
		if (line[*i] == ']')
			token.name[j++] = line[(*i)++];
		token.name[j] = '\0';
		token.type = T_MEMORY;
		return token;
	}

	if (line[*i] == ';')
	{
		strncpy(token.name, line + (*i), sizeof(token.name) - 1);
		token.name[sizeof(token.name) - 1] = '\0';
		token.type = T_COMMENT;
		return token;
	}

	while (line[*i] && line[*i] != SPACE && !is_operator((line[*i])) && line[*i] != COMMA && line[*i] != TAB)
	{
		if (j >= sizeof(token.name) - 1)
			break;
		token.name[j] = line[*i];
		(*i)++;
		if (token.name[j] == COLON)
		{
			j++;
			break;
		}
		j++;
	}

	token.name[j] = '\0';
	if (j == 0)
	{
		token.type = T_END;
		return token;
	}
	token.type = get_token_type(token.name);
	return token;
}

int estimate_text_size(InstructionType ins)
{
	switch (ins)
	{
	case INS_RET:
	case INS_NOP:
		return 1;

	case INS_INC:
	case INS_DEC:
		return 1;  /* 0x40+r / 0x48+r = 1 byte for register form */

	case INS_PUSH:
	case INS_POP:
		return 2;

	case INS_XOR:
		return 2;
	case INS_JMP:
	case INS_CALL:
		return 5;
	case INS_JCC:
		return 2;

	case INS_MOV:
		return 5;

	case INS_ADD:
	case INS_SUB:
	case INS_CMP:
	case INS_LEA:
		return 6;  

	default:
		return 0;
	}
}

void parse_operand(const char *s, Operand *op)
{
	memset(op, 0, sizeof(*op));
	op->type = OP_NONE;
	op->base = REG_INVALID;
	op->index = REG_INVALID;
	op->scale = 1;
	op->has_disp = 0;

	/* register */
	RegCode r = get_register_code(s);
	if (r != REG_INVALID)
	{
		op->type = OP_REG;
		op->reg = r;
		return;
	}

	if (is_immediate(s))
	{
		op->type = OP_IMM;
		op->imm = strtol(s, NULL, 0);
		return;
	}

	if (s[0] == '[')
	{
		op->type = OP_MEM;

		char expr[128];
		strncpy(expr, s + 1, sizeof(expr) - 1);
		expr[sizeof(expr) - 1] = 0;
		char *end = strchr(expr, ']');
		if (end)
			*end = 0;

		char *p = expr;
		int sign = +1;

		while (*p)
		{
			if (*p == '+')
			{
				sign = +1;
				p++;
				continue;
			}
			if (*p == '-')
			{
				sign = -1;
				p++;
				continue;
			}

			char token[32];
			int ti = 0;

			while (*p && *p != '+' && *p != '-')
				token[ti++] = *p++;
			token[ti] = 0;

			char *mul = strchr(token, '*');
			if (mul)
			{
				*mul = 0;
				op->index = get_register_code(token);
				op->scale = atoi(mul + 1);
				continue;
			}

			r = get_register_code(token);
			if (r != REG_INVALID)
			{
				if (op->base == REG_INVALID)
					op->base = r;
				else
					op->index = r;
				continue;
			}

			op->disp += sign * strtol(token, NULL, 0);
			op->has_disp = 1;
		}

		return;
	}

	op->type = OP_LABEL;
	strncpy(op->label, s, sizeof(op->label) - 1);
	op->label[sizeof(op->label) - 1] = '\0';
}

void tokenize_line(FILE *fp)
{
	char line[2000];
	int line_no = 0;
	while (fgets(line, sizeof(line), fp))
	{
		line_no++;
		filterline(line);
		unsigned int i = 0;
		label[0] = '\0';

		TOKEN first = get_next_token(line, &i);

		if (first.type == T_LABEL)
		{
			strncpy(label, first.name, strlen(first.name) - 1);
			label[strlen(first.name) - 1] = '\0';
			first = get_next_token(line, &i); 
		}

		else if (first.type == T_IDENTIFIER)
		{
			unsigned int save = i;
			TOKEN next = get_next_token(line, &i);
			if (next.type == T_DIRECTIVE)
			{
				strncpy(label, first.name, sizeof(label) - 1);
				label[sizeof(label) - 1] = '\0';
				first = next;
			}
			else
			{
				i = save;
			}
		}

		TOKEN token = (first.type == T_DIRECTIVE || first.type == T_MNEMONIC) ? first : get_next_token(line, &i);

		// ---- section directive ----
		if (token.type == T_DIRECTIVE && get_directive_type(token.name) == DIR_SECTION)
		{
			TOKEN sec = get_next_token(line, &i);
			handle_section(sec.name);
			continue;
		}

		// ---- .data section ----
		if (current_section == SEC_DATA && token.type == T_DIRECTIVE)
		{
			DirectiveType d = get_directive_type(token.name);
			if (!validate_data_directive())
				continue;
			int size = 0;

			if (d == DIR_DB)
				size = calc_db_size(line, i);
			else if (d == DIR_DW)
				size = calc_numeric_data_size(line, i, 2);
			else if (d == DIR_DD)
				size = calc_numeric_data_size(line, i, 4);
			else if (d == DIR_DQ)
				size = calc_numeric_data_size(line, i, 8);

			if (label[0])
			{
				add_symbol(label, SEC_DATA, lc_data, size);
				label[0] = '\0';
			}
			lst_add(line_no, lc_data, NULL, 0, 1, 0, line);
			lc_data += size;
		}

		// ---- .bss section ----
		else if (current_section == SEC_BSS && token.type == T_DIRECTIVE)
		{
			DirectiveType d = get_directive_type(token.name);
			TOKEN cnt = get_next_token(line, &i);
			int count = atoi(cnt.name);
			if (!validate_bss_directive(d))
				continue;
			int size = directiveSize(d) * count;

			if (label[0])
			{
				add_symbol(label, SEC_BSS, lc_bss, size);
				label[0] = '\0';
			}
			lst_add(line_no, lc_bss, NULL, 0, 1, 0, line);
			lc_bss += size;
		}

		// ---- .text section ----
		else if (current_section == SEC_TEXT)
		{
			if (token.type == T_MNEMONIC)
			{
				InstructionType ins = get_instruction_type(token.name);

				IR *ir = &ir_list[ir_count++];
				ir->ins = ins;
				ir->addr = lc_text;
				strncpy(ir->mnemonic, token.name, sizeof(ir->mnemonic) - 1);
				ir->mnemonic[sizeof(ir->mnemonic) - 1] = '\0';
				ir->size = estimate_text_size(ins);
				ir->line_no = line_no;
				strncpy(ir->src_line, line, sizeof(ir->src_line) - 1);
				ir->src_line[sizeof(ir->src_line) - 1] = '\0';

				// parse operands
				TOKEN t1 = get_next_token(line, &i);
				TOKEN t2 = get_next_token(line, &i);

				if (t2.type == T_OPERATOR && t2.name[0] == ',')
					t2 = get_next_token(line, &i);
				if (t1.type != T_END)
					parse_operand(t1.name, &ir->op1);
				if (t2.type != T_END)
					parse_operand(t2.name, &ir->op2);

				int has_label_op = (ir->op1.type == OP_LABEL ||
				                    ir->op2.type == OP_LABEL);
				ir->enc_len = 0;
				if (!has_label_op)
				{
					ir->enc_len = encode_instruction(ir->enc, ir->ins,
					              ir->mnemonic, &ir->op1, &ir->op2, lc_text);
					if (ir->enc_len > 0)
						ir->size = ir->enc_len;
				}

				if (label[0])
				{
					add_symbol(label, SEC_TEXT, lc_text, 0);
					label[0] = '\0';
				}

				lc_text += ir->size;
			}
		}
	}
}

int emit_data(FILE *fp, uint8_t *data)
{
	unsigned int pc = 0;

	for (int i = 0; i < symcount; i++)
	{
		if (!symtab[i].valid || symtab[i].sec != SEC_DATA)
			continue;

		printf("; %s at %u (%d bytes)\n", symtab[i].name, symtab[i].addr, symtab[i].size);

		fseek(fp, 0, SEEK_SET); 
		char line[2000];

		while (fgets(line, sizeof(line), fp))
		{
			filterline(line);
			unsigned int j = 0;
			TOKEN first = get_next_token(line, &j);

			if (first.type == T_LABEL && strncmp(first.name, symtab[i].name, strlen(symtab[i].name)) == 0)
			{
				TOKEN token = get_next_token(line, &j);
				DirectiveType d = get_directive_type(token.name);

				if (d == DIR_DB)
				{
					while (line[j])
					{
						if (line[j] == '"')
						{
							j++;
							while (line[j] && line[j] != '"')
							{
								if (line[j] == '\\')
								{
									j++;
									switch (line[j])
									{
									case 'n':
										data[pc++] = '\n';
										break;
									case 't':
										data[pc++] = '\t';
										break;
									case 'r':
										data[pc++] = '\r';
										break;
									case '"':
										data[pc++] = '"';
										break;
									case '\\':
										data[pc++] = '\\';
										break;
									case '0':
										data[pc++] = 0;
										break;
									case 'x':
									{
										char buf[3] = {line[j + 1], line[j + 2], 0};
										data[pc++] = (uint8_t)strtol(buf, NULL, 16);
										j += 2;
									}
									break;
									default:
										data[pc++] = line[j];
										break;
									}
								}
								else
									data[pc++] = line[j];
								j++;
							}
							j++; 
						}
						else if (isdigit(line[j]) || line[j] == '-' || line[j] == '+')
						{
							int val = strtol(line + j, NULL, 0);
							data[pc++] = val & 0xFF;
							while (line[j] && line[j] != ',' && line[j] != ' ' && line[j] != '\t')
								j++;
						}
						else
							j++;
					}
				}
				break;
			}
		}
	}

	return pc;
}

int pass2_emit(uint8_t *text_out)
{
	uint8_t buf[16];

	for (int i = 0; i < ir_count; i++)
	{
		IR *ir = &ir_list[i];

		int unresolved = (ir->op1.type == OP_LABEL || ir->op2.type == OP_LABEL);

		if (ir->op1.type == OP_LABEL)
		{
			for (int s = 0; s < symcount; s++)
			{
				if (symtab[s].valid && !strcmp(symtab[s].name, ir->op1.label))
				{
					ir->op1.imm =
						symtab[s].addr - (ir->addr + ir->size);
					break;
				}
			}
		}
		if (ir->op2.type == OP_LABEL)
		{
			for (int s = 0; s < symcount; s++)
			{
				if (symtab[s].valid && !strcmp(symtab[s].name, ir->op2.label))
				{
					ir->op2.type = OP_IMM;
					ir->op2.imm  = symtab[s].addr;
					break;
				}
			}
		}
		if (ir->op1.type == OP_MEM && ir->op1.has_disp == 0 && ir->op1.base == REG_INVALID)
		{
			for (int s = 0; s < symcount; s++)
				if (symtab[s].valid && !strcmp(symtab[s].name, ir->op1.label))
				{
					ir->op1.disp = symtab[s].addr;
					ir->op1.has_disp = 1;
				}
		}

		int len;
		if (!unresolved && ir->enc_len > 0)
		{
			len = ir->enc_len;
			memcpy(buf, ir->enc, len);
		}
		else
		{
			len = encode_instruction(buf, ir->ins, ir->mnemonic, &ir->op1, &ir->op2, ir->addr);
		}

		if (len > 0)
			memcpy(text_out + ir->addr, buf, len);

		lst_add(ir->line_no, ir->addr, len ? buf : NULL, len, 1, unresolved, ir->src_line);
	}

	return lc_text;
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("Usage: %s <file.asm>\n", argv[0]);
		return 1;
	}

	FILE *fp = fopen(argv[1], "r");
	if (!fp)
	{
		perror("Error opening file");
		return 1;
	}

	tokenize_line(fp); // pass 1: build IR & symbol table
	rewind(fp);
	uint8_t text[8192];
	uint8_t data[8192];
	int data_size = emit_data(fp, data);
	// output .data bytes
	printf("Data section bytes (%d):\n", data_size);
	for (int k = 0; k < data_size; k++)
		printf("%02X ", data[k]);
	if (data_size > 0) printf("\n");

	printf("Text section machine code:\n");

	int text_size = pass2_emit(text);
	for (int k = 0; k < text_size; k++)
		printf("%02X ", text[k]);
	if (text_size > 0) printf("\n");

	/* ---- pass 3: insert every source line not already in the listing ---- */
	rewind(fp);
	{
		char raw[2000];
		int ln = 1;
		while (fgets(raw, sizeof(raw), fp))
		{
			/* strip trailing newline for display */
			int rlen = (int)strlen(raw);
			if (rlen > 0 && raw[rlen - 1] == '\n')
				raw[rlen - 1] = '\0';

			/* check if this line number already has an entry */
			int found = 0;
			for (int k = 0; k < lst_count; k++)
				if (lst[k].line_no == ln) { found = 1; break; }

			if (!found)
				lst_add(ln, 0, NULL, 0, 0, 0, raw);

			ln++;
		}
	}

	/* sort listing by line number (insertion sort — lst is small) */
	for (int a = 1; a < lst_count; a++)
	{
		LstEntry tmp = lst[a];
		int b = a - 1;
		while (b >= 0 && lst[b].line_no > tmp.line_no)
		{
			lst[b + 1] = lst[b];
			b--;
		}
		lst[b + 1] = tmp;
	}

	write_lst("output.lst");
	printf("Listing written to output.lst\n");

	printf("\nSymbol Table:\n");
	for (int i = 0; i < symcount; i++)
		if (symtab[i].valid)
			printf("  %-20s  section=%d  addr=0x%08X  size=%d\n",
			       symtab[i].name, symtab[i].sec, symtab[i].addr, symtab[i].size);

	fclose(fp);
	return 0;
}
