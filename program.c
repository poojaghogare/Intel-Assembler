#include<string.h>
#include<stdio.h>
#include<stdlib.h>

//Macros

#define MAX_SIZE 2000
#define SPACE     ' ' 
#define TAB       '\t'
#define NEXTLINE  '\n'
#define COMMA     ','
#define CBR       ']'
#define OBR       '['
#define PLUS      '+'
#define MUL       '*'
#define MAIN      "main:"


//Enum
typedef enum {
        eax, ecx, edx, ebx, esp, ebp, esi, edi,
        mov, add, xor, ret, sub,
        Obr, Cbr, plus, mul,
	NUMBER,END_OF_INPUT, nextline, other
} wordtype;


//Structures

typedef struct {
	char name[10];
	wordtype type;
} WORD;

//Function Declaration

void nextword(char input[]);
wordtype getType(char *str);
int is_mnemonics(wordtype type);
int is_reg(wordtype type);


//Variable Declaration
int CURRENT_BYTE = 0;
WORD word;

//Function Defination 

int is_reg(wordtype type)
{
	if(type == eax || type == edx || type == ecx || type == esp || type == ebp || type == esi || type == edi )
		return 1;
	return 0;
}

int is_mnemonics(wordtype type) 
{
	if(type == mov || type == add || type == xor || type == sub )
		return 1;
	return 0;
}

wordtype getType(char *str)
{
	if (strcmp(str, "eax") == 0) return eax;
	else if (strcmp(str, "ecx") == 0) return ecx;
	else if (strcmp(str, "edx") == 0) return edx;
        else if (strcmp(str, "ebx") == 0) return ebx;
        else if (strcmp(str, "esp") == 0) return esp;
        else if (strcmp(str, "ebp") == 0) return ebp;
        else if (strcmp(str, "esi") == 0) return esi;
        else if (strcmp(str, "edi") == 0) return edi;
        else if (strcmp(str, "mov") == 0) return mov;
	else if (strcmp(str, "add") == 0) return add;
        else if (strcmp(str, "xor") == 0) return xor;
        else if (strcmp(str, "ret") == 0) return ret;
        else if (strcmp(str, "sub") == 0) return sub;
        else if (strcmp(str, "[") == 0) return Obr;
        else if (strcmp(str, "]") == 0) return Cbr;
        else if (strcmp(str, "+") == 0) return plus;
        else if (strcmp(str, "*") == 0) return mul;
	else if (strcmp(str, "\n") == 0) return nextline;
        return other;


}
void nextword(char input[]) {

	int current_name = 0;
	memset(word.name, '\0', sizeof(word.name));

	while(input[CURRENT_BYTE] == SPACE || input[CURRENT_BYTE] == TAB || input[CURRENT_BYTE] == COMMA && CURRENT_BYTE < MAX_SIZE && input[CURRENT_BYTE] != '\0') 
	{
		CURRENT_BYTE ++;
	}

	if(input[CURRENT_BYTE] == NEXTLINE || input[CURRENT_BYTE] == PLUS || input[CURRENT_BYTE] == CBR || input[CURRENT_BYTE] == OBR || input[CURRENT_BYTE] == MUL && input[CURRENT_BYTE] != '\0') 
	{
		word.name[current_name] = input[CURRENT_BYTE];
		current_name++;
		CURRENT_BYTE++;
	}
	else {

		while(input[CURRENT_BYTE] != SPACE && input[CURRENT_BYTE] != TAB && input[CURRENT_BYTE] != COMMA && CURRENT_BYTE < MAX_SIZE && input[CURRENT_BYTE] != '\0')
		{
			if(input[CURRENT_BYTE] == NEXTLINE || input[CURRENT_BYTE] == PLUS || input[CURRENT_BYTE] == CBR || input[CURRENT_BYTE] == OBR || input[CURRENT_BYTE] == MUL) 
			{
				break;
			}
			word.name[current_name] = input[CURRENT_BYTE];
			current_name++;
			CURRENT_BYTE++;
		}
	}

	word.name[current_name] = '\0';
	word.type = getType(word.name);
	return;
}

int main(int argc, char* argv[]) {
	FILE *file = fopen(argv[1],"r");
	char c;
	char buffer[MAX_SIZE];
	if(file == NULL) {
		printf("Error in file opening\n");
		return 1;
	}

	int i;
	for(i = 0; (c=fgetc(file)) != EOF && i < MAX_SIZE; i++) {
		buffer[i] = c;
		printf("%c", buffer[i]);
	}
	fclose(file);

	while (strcmp(word.name, "main:") && CURRENT_BYTE < strlen(buffer))
		nextword(buffer);

	nextword(buffer);
	printf("-------------------------------------------------------\n");
	for(i = CURRENT_BYTE; i< strlen(buffer); i++)
	{
		printf("%c", buffer[i]);
	}


}

