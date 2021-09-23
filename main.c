#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 1024

#define ctoi(c) (c - '0')

typedef unsigned char u8;

typedef struct {
    char name[32];
    int pos; // Position of function.
} Function;

u8 memory[65536] = {0};
u8 registers[32] = {0};

////////////////////////////////
// Ueditable registers:
// REG[31] = used for CMP flag.
////////////////////////////////

int line = 1;
int curpos = 0;

Function functions[512];
int function_count = 0;

int stack[512] = {0};
int sp = 0;

enum {
    TYPE_REGISTER,
    TYPE_MEMORY,
    TYPE_CHAR,
    TYPE_INT
};

enum {
    OPERATION_ADD,
    OPERATION_SUB,
    OPERATION_DIV,
    OPERATION_MUL
};

static int recalculate_line(const char *input) {
    const unsigned len = strlen(input);
    line = 1;
    for (int i = 0; i < curpos; ++i) {
        if (input[i] == '\n') line++;
    }
}

static int get_index(char parameter[32]) {
    char string[32];
    strncpy(string, parameter, 32);
    for (int i = 0; i < 32; i += 1)
        if (string[i] == ']') string[i] = 0;
    int out = atoi(string+4);
    return out;
}

static int get_type(char parameter[32]) {
    switch (parameter[0]) {
        case 'R':  return TYPE_REGISTER;
        case 'M':  return TYPE_MEMORY;
        case '\'': return TYPE_CHAR;
        default:   return TYPE_INT;
    }
}

static void jump_to_subroutine(int pos) {
    if (sp == 512) {
        fprintf(stderr, "Error (Line %d): Stack overflow error!\n", line);
        exit(1);
    }
    stack[sp++] = curpos; // Push stack
    curpos = pos;
}

static void return_from_subroutine(void) {
    curpos = stack[--sp]; // Pop stack
}

static void function_pass(const char *input, int input_length) {
    char curr_line[32];
    int ch = 0;
    int ln = 1;
    for (int i = 0; i < input_length; i += 1) {
        if (input[i] == '\n') {
            ln++;
            ch = 0;
            memset(curr_line, 0, 32);
            continue;
        }
        if (input[i] == ':') {
            char str[32] = {0};
            strncpy(str, curr_line, 3);
            if (0 != strcmp(str, "FUN")) {
                fprintf(stderr, "Error(Line %d): Colon used without a function.\n", ln);
            }
            char name[32] = {0};
            strcpy(name, curr_line+4);
            name[strlen(name)] = 0;
            strcpy(functions[function_count].name, name);
            functions[function_count].pos = i+1;
            function_count++;
            memset(curr_line, 0, 32);
            ch = 0;
            continue;
        }
        curr_line[ch++] = input[i];
    }
}

static void execute_command(const char *input, char params[32][MAX_TOKENS], int argc) {
    const unsigned input_length = strlen(input);
    if (0==strcmp(params[0], "SET")) {
        switch (get_type(params[1])) {
            case TYPE_REGISTER: {
                int index = get_index(params[1]);
                if (index == 31) {
                    fprintf(stderr, "Error (Line %d): Cannot write to REG[31]!\n");
                    return;
                }
                switch (get_type(params[2])) {
                    case TYPE_REGISTER: {
                        int to_index = get_index(params[2]);
                        registers[index] = registers[to_index];
                    } break;
                    case TYPE_MEMORY: {
                        int to_index = get_index(params[2]);
                        registers[index] = memory[to_index];
                    } break;
                    case TYPE_CHAR: {
                        registers[index] = params[2][1];
                    } break;
                    case TYPE_INT: {
                        registers[index] = atoi(params[2]);
                    } break;
                }
            } break;
            case TYPE_MEMORY: {
                int index = get_index(params[1]);
                switch (get_type(params[2])) {
                    case TYPE_REGISTER: {
                        int to_index = get_index(params[2]);
                        memory[index] = registers[to_index];
                    } break;
                    case TYPE_MEMORY: {
                        int to_index = get_index(params[2]);
                        memory[index] = memory[to_index];
                    } break;
                    case TYPE_CHAR: {
                        memory[index] = params[2][1];
                    } break;
                    case TYPE_INT: {
                        memory[index] = atoi(params[2]);
                    } break;
                }
            } break;
        }
    } else if (0==strcmp(params[0], "OUT")) {
        if (get_type(params[1]) == TYPE_REGISTER) {
            int index = get_index(params[1]);
            //putchar(registers[index]);
            printf("%d\n", registers[index]);
        } else {
            fprintf(stderr, "Error(Line %d): \"OUT\" only takes in registers.\n", line);
        }
    } else if (0==strcmp(params[0], "GET")) {
        if (get_type(params[1]) == TYPE_REGISTER) {
            int index = get_index(params[1]);
            registers[index] = getchar();
        } else {
            fprintf(stderr, "Error(Line %d): \"GET\" only writes to a register.\n", line);
        }
    } else if (0==strcmp(params[0], "CMP")) {
        if (get_type(params[1]) != TYPE_REGISTER || get_type(params[2]) != TYPE_REGISTER) {
            fprintf(stderr, "Error(Line %d): \"CMP\" requires two registers.\n", line);
        } else {
            int from_index = get_index(params[1]);
            int to_index   = get_index(params[2]);
            if (registers[to_index] == registers[from_index])     registers[31] = 1;
            else if (registers[to_index] < registers[from_index]) registers[31] = 2;
            else                                                  registers[31] = 0;
        }
    } else if (0==strcmp(params[0], "JSR")) {
        for (int i = 0; i < function_count; i += 1) {
            if (0==strcmp(functions[i].name, params[1])) {
                jump_to_subroutine(functions[i].pos);
                recalculate_line(input);
            }
        }
    } else if (0==strcmp(params[0], "RET")) {
        return_from_subroutine();
        recalculate_line(input);
    } else if (0==strcmp(params[0], "FUN")) {
        // Skip over subroutine. Only enter them when called by JSR or derivatives.
        char str[32] = {0};
        int ch = 0;
        for (; curpos < input_length; ++curpos) {
            if (input[curpos] == '\n') {
                if (0==strcmp(str, "RET")) {
                    return;
                }
                memset(str, 0, 32);
                ch = 0;
                continue;
            }
            str[ch++] = input[curpos];
        }
    } else if (0==strcmp(params[0], "ADD") || 0==strcmp(params[0], "SUB") || 0==strcmp(params[0], "MUL") || 0==strcmp(params[0], "DIV")) {
        int operation;
        if (0==strcmp(params[0], "ADD")) {
            operation = OPERATION_ADD;
        } else if (0==strcmp(params[0], "SUB")) {
            operation = OPERATION_SUB;
        } else if (0==strcmp(params[0], "MUL")) {
            operation = OPERATION_MUL;
        } else if (0==strcmp(params[0], "DIV")) {
            operation = OPERATION_DIV;
        }
        if (get_type(params[1]) == TYPE_REGISTER && get_type(params[2]) == TYPE_REGISTER) {
            int from_index = get_index(params[2]);
            int to_index = get_index(params[1]);
            if (to_index == 31) {
                fprintf(stderr, "Error (Line %d): Cannot write to REG[31]!\n");
                return;
            }
            switch (operation) {
                case OPERATION_ADD: {
                    registers[to_index] += registers[from_index];
                } break;
                case OPERATION_SUB: {
                    registers[to_index] -= registers[from_index];
                } break;
                case OPERATION_MUL: {
                    registers[to_index] *= registers[from_index];
                } break;
                case OPERATION_DIV: {
                    registers[to_index] /= registers[from_index];
                } break;
            }
        } else {
            fprintf(stderr, "Error (Line %d): Maths operations work with registers only.\n");
        }
    } else if (0==strcmp(params[0], "INC") || 0==strcmp(params[0], "DEC")) {
        int operation;
        if (0==strcmp(params[0], "INC")) operation = OPERATION_ADD;
        if (0==strcmp(params[0], "DEC")) operation = OPERATION_SUB;
        if (get_type(params[1]) == TYPE_REGISTER) {
            int index = get_index(params[1]);
            if (operation == OPERATION_ADD)
                registers[index]++;
            else
                registers[index]--;
        } else {
            fprintf(stderr, "Error (Line %d): Maths operations work with registers only.\n");
        }
    }
}

int main(void) {
    char tokens[32][MAX_TOKENS];
    
    const char *input =
        "JSR add\n"
        "FUN add:\n"
        "INC REG[0]\n"
        "OUT REG[0]\n"
        "JSR add\n";
    const unsigned input_length = strlen(input);
    
    if (input[input_length-1] != '\n') {
        fprintf(stderr, "Error (Bottom of file): Your program must end with a newline.\n");
        return 1;
    }
    
    int argc = 0, ch = 0;
    
    // Finding all the functions.
    function_pass(input, input_length);
    
    // Tokenizing + Running the code.
    
    char params[32][MAX_TOKENS];
    
    ch = 0;
    for (curpos = 0; curpos < input_length; curpos += 1) {
        if (input[curpos] == ' ') {
            argc++;
            ch = 0;
            continue;
        } else if (input[curpos] == '\n') {
            argc++;
            execute_command(input, params, argc);
            line++;
            argc = 0;
            ch = 0;
            memset(params, 0, sizeof(params));
            continue;
        }
        params[argc][ch++] = input[curpos];
    }
    
    printf("\n");
    
    return 0;
}