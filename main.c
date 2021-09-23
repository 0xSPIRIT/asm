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
    TYPE_INT,
    TYPE_STRING
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

// Gets the index of any [] operation. Figures out any recursive indexing inside the [] if needed, eg. REG[MEM[REG[15]]].
static int get_index(char parameter[32]) {
    int last_index = 0;
    int reg = 0;
    int count = 0;
    int current_bracket = 0;
    for (int i = 32; i > 0; --i) {
        if (parameter[i] == '[') {
            if (parameter[i-1] == 'G') reg = 1;
            if (current_bracket == 0) { // Getting the base value.
                char integer[32];
                strcpy(integer, parameter + i + 1);
                int j;
                for (j = 0; integer[j] == ']'; ++j); // Clipping off the ]
                integer[j+1] = 0;
                last_index = atoi(integer);
            } else { // Get the next index in the chain.
                if (reg) {
                    last_index = registers[last_index];
                } else {
                    last_index = memory[last_index];
                }
            }
            current_bracket++;
        }
    }
    return last_index;
}

static int get_type(char parameter[32]) {
    switch (parameter[0]) {
        case 'R':  return TYPE_REGISTER;
        case 'M':  return TYPE_MEMORY;
        case '\'': return TYPE_CHAR;
        case '"':  return TYPE_STRING;
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
            putchar(registers[index]);
            //printf("%d\n", registers[index]);
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
    } else if (0==strcmp(params[0], "SKI")) { // Skip next command if REG[31] == 0
        if (registers[31] == 0) {
            while (input[++curpos] != '\n');
            recalculate_line(input);
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
            fprintf(stderr, "Error (Line %d): Maths operations work with registers only.\n", line);
        }
    } else if (0==strcmp(params[0], "STR")) { // Sets memory starting at position to null-terminated string data.
        int pos = 0;
        switch (get_type(params[1])) {
            case TYPE_INT: {
                pos = atoi(params[1]);
            } break;
            case TYPE_REGISTER: {
                pos = registers[get_index(params[1])];
            } break;
            default: {
                fprintf(stderr, "Error (Line %d): \"STR\" reqiures an integer or register for the position.\n", line);
                return;
            } break;
        }
        if (get_type(params[2]) != TYPE_STRING) {
            fprintf(stderr, "Error (Line %d): \"STR\" requires a string for the third parameter.\n", line);
            return;
        }
        char string[128] = {0};
        strcpy(string, params[2]+1);
        int length = strlen(string)-1; // -1 to remove the closing quote.
        string[length] = 0;
        for (int i = 0; i <= length; ++i) // Include the null-terminator.
            memory[pos+i] = string[i];
    } else if (0==strcmp(params[0], "OSR")) { // Output a string.
        switch (get_type(params[1])) {
            case TYPE_REGISTER: {
                int pos = registers[get_index(params[1])];
                printf("%s", memory+pos);
            } break;
            case TYPE_STRING: {
                char string[128] = {0};
                strcpy(string, params[1]+1);
                string[strlen(string)-1] = 0;
                printf("%s", string);
            } break;
            default: {
                fprintf(stderr, "Error (Line %d): \"OSR\" reqiures an integer or register for the position.\n", line);
                return;
            } break;
        }
    }
}

int main(void) {
    char tokens[32][MAX_TOKENS];
    
    const char *input =
        "STR 0 \"Hello, World!\"\n" // Puts the array of characters sequentially in memory starting at 0.
        "SET REG[3] 12\n" // Set a total count of the characters.
        "JSR print\n" // Jump into to print function.
        "FUN print:\n"
        "SET REG[2] MEM[REG[1]]\n" // set REG[2] to the current character, with REG[1] as a coutner
        "OUT REG[2]\n" // Output the character
        "INC REG[1]\n" // Increment the counter.
        "CMP REG[3] REG[1]\n" // if REG[1] > REG[3], REG[31] = 0
        "SKI\n" // Skips next command (and ends the program) if REG[31] == 0
        "JSR print\n";
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
    int string = 0;
    
    ch = 0;
    for (curpos = 0; curpos < input_length; curpos += 1) {
        if (input[curpos] == '"') {
            string = !string;
        }
        if (!string && input[curpos] == ' ') {
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