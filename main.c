#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 1024
#define STRING_MAX 255

#define ctoi(c) (c - '0')
#define string_compare(x, y) (0==strcmp(x, y))

typedef unsigned char  u8;
typedef unsigned short u16;

typedef struct {
    char name[64];
    u16 pos; // Position of function.
} Function;

u8 memory[65536] = {0};
u8 registers[32] = {0};

////////////////////////////////
// Special registers:
// REG[31] = used for CMP flag.
// REG[30] = used for string lengths.
// REG[29] = used for print mode.
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

static char *read_file(const char *fp, unsigned *l) {
    FILE *f = fopen(fp, "r");
    if (!f) {
        fprintf(stderr, "Couldn't open %s!\n", fp);
        exit(1);
    }
    char *str;
    
    long p;
    size_t length;
    
    p = ftell(f);
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, p, SEEK_SET);
    
    str = malloc(length+1);
    length = fread(str, 1, length, f);
    str[length] = 0;
    
    if (l) *l = length;
    
    return str;
}

static unsigned cleanup_source(char *input, char **output) {
    // Removing double spaces, comments, tabs, and whitespace from input.
    // Also, add a newline at the end of the file.
    char *dest = input;
    char *start = input;
    int start_line = 0;
    int comment = 0;
    int trailing_space = 0;
    while (*input) {
        if (*input == '/' && *(input+1) == '/') comment = 1;
        while (comment) {
            ++input;
            if (*input == '\n') comment = 0;
        }
        
        while (*input == '\t') ++input;
        if (start_line) {
            while (*input == ' ') ++input;
            start_line = 0;
        } else {
            while (*input == ' ' && *(input+1) == ' ') ++input;
        }
        
        if (*input == '\n') start_line = 1;
        
        *dest++ = *input++;
    }
    *dest = 0;
    
    input = start;
    dest = start;
    
    unsigned length = 0;
    
    // Remove trailing whitespace pass; the previous pass got rid of all multiple spaces in a row, so this is trivial.
    while (*input) {
        if (*input == ' ' && *(input+1) == '\n') ++input;
        *dest++ = *input++;
        length++;
    }
    input = start;
    input = realloc(input, length+2);
    input[length] = '\n';
    input[length+1] = 0;
    
    *output = input;
    
    return length+1;
}

static int recalculate_line(const char *input) {
    const u16 len = strlen(input);
    line = 1;
    for (int i = 0; i < curpos; ++i) {
        if (input[i] == '\n') line++;
    }
}

// Gets the index of any [] operation. Figures out any recursive indexing inside the [] if needed, eg. REG[MEM[REG[15]]].
static u16 get_index(char parameter[32]) {
    u16 last_index = 0;
    int reg = 0;
    int current_bracket = 0;
    for (int i = 32; i > 0; --i) {
        if (parameter[i] == '[') {
            if (parameter[i-1] == 'G') reg = 1;
            if (current_bracket == 0) { // Getting the base value.
                char integer[32];
                strcpy(integer, parameter + i + 1);
                int j;
                for (j = 0; integer[j] != ']'; ++j); // Clipping off the ]
                integer[j] = 0;
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

static void jump_to_subroutine(u16 pos) {
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

// Gets all the functions and stores it in an array.
static void function_pass(const char *input, int input_length) {
    char curr_line[32];
    int ch = 0;
    int ln = 1;
    int string = 0;
    for (int i = 0; i < input_length; i += 1) {
        if (input[i] == '"') {
            string = !string;
        }
        if (input[i] == '\n') {
            ln++;
            ch = 0;
            memset(curr_line, 0, 32);
            string = 0;
            continue;
        }
        if (!string && input[i] == ':') {
            char str[32] = {0};
            strncpy(str, curr_line, 3);
            if (0 != strcmp(str, "SBR")) {
                fprintf(stderr, "Error(Line %d): Colon used without a subroutine declaration.\n", ln);
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
    const u16 input_length = strlen(input);
    if (string_compare(params[0], "SET")) {
        switch (get_type(params[1])) {
            case TYPE_REGISTER: {
                u16 index = get_index(params[1]);
                switch (get_type(params[2])) {
                    case TYPE_REGISTER: {
                        u16 to_index = get_index(params[2]);
                        registers[index] = registers[to_index];
                    } break;
                    case TYPE_MEMORY: {
                        u16 to_index = get_index(params[2]);
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
                u16 index = get_index(params[1]);
                switch (get_type(params[2])) {
                    case TYPE_REGISTER: {
                        u16 to_index = get_index(params[2]);
                        memory[index] = registers[to_index];
                    } break;
                    case TYPE_MEMORY: {
                        u16 to_index = get_index(params[2]);
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
    } else if (string_compare(params[0], "OUT")) {
        if (get_type(params[1]) == TYPE_REGISTER) {
            u16 index = get_index(params[1]);
            if (registers[29] == 0) {
                putchar(registers[index]);
            } else {
                printf("%d", registers[index]);
            }
        } else {
            fprintf(stderr, "Error(Line %d): \"OUT\" only takes in registers.\n", line);
        }
    } else if (string_compare(params[0], "GET")) {
        if (get_type(params[1]) == TYPE_REGISTER) {
            u16 index = get_index(params[1]);
            registers[index] = getchar();
        } else {
            fprintf(stderr, "Error(Line %d): \"GET\" only writes to a register.\n", line);
        }
    } else if (string_compare(params[0], "CMP")) {
        if (get_type(params[1]) != TYPE_REGISTER || get_type(params[2]) != TYPE_REGISTER) {
            fprintf(stderr, "Error(Line %d): \"CMP\" requires two registers.\n", line);
        } else {
            u16 from_index = get_index(params[1]);
            u16 to_index   = get_index(params[2]);
            if (registers[to_index] == registers[from_index])     registers[31] = 1;
            else if (registers[to_index] < registers[from_index]) registers[31] = 2;
            else                                                  registers[31] = 0;
        }
    } else if (string_compare(params[0], "JSR")) {
        for (int i = 0; i < function_count; i += 1) {
            if (string_compare(functions[i].name, params[1])) {
                jump_to_subroutine(functions[i].pos);
                recalculate_line(input);
            }
        }
    } else if (string_compare(params[0], "SKP")) { // Skip next command if REG[31] == 0
        if (registers[31] == 0) {
            while (input[++curpos] != '\n');
            recalculate_line(input);
        }
    } else if (string_compare(params[0], "RET")) {
        return_from_subroutine();
        recalculate_line(input);
    } else if (string_compare(params[0], "SBR")) {
        // Skip over subroutine. Only enter them when called by JSR or derivatives.
        char str[32] = {0};
        int ch = 0;
        for (; curpos < input_length; ++curpos) {
            if (input[curpos] == '\n') {
                if (string_compare(str, "RET")) {
                    return;
                }
                memset(str, 0, 32);
                ch = 0;
                continue;
            }
            str[ch++] = input[curpos];
        }
    } else if (string_compare(params[0], "ADD") || string_compare(params[0], "SUB") || string_compare(params[0], "MUL") || string_compare(params[0], "DIV")) {
        int operation;
        if (string_compare(params[0], "ADD")) {
            operation = OPERATION_ADD;
        } else if (string_compare(params[0], "SUB")) {
            operation = OPERATION_SUB;
        } else if (string_compare(params[0], "MUL")) {
            operation = OPERATION_MUL;
        } else if (string_compare(params[0], "DIV")) {
            operation = OPERATION_DIV;
        }
        if (get_type(params[1]) == TYPE_REGISTER && get_type(params[2]) == TYPE_REGISTER) {
            u16 from_index = get_index(params[2]);
            u16 to_index = get_index(params[1]);
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
    } else if (string_compare(params[0], "INC") || string_compare(params[0], "DEC")) {
        int operation;
        if (string_compare(params[0], "INC")) operation = OPERATION_ADD;
        if (string_compare(params[0], "DEC")) operation = OPERATION_SUB;
        if (get_type(params[1]) == TYPE_REGISTER) {
            u16 index = get_index(params[1]);
            if (operation == OPERATION_ADD)
                registers[index]++;
            else
                registers[index]--;
        } else {
            fprintf(stderr, "Error (Line %d): Maths operations work with registers only.\n", line);
        }
    } else if (string_compare(params[0], "STR")) { // Sets memory starting at position to null-terminated string data.
        u16 pos = 0;
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
        char string[STRING_MAX] = {0};
        strcpy(string, params[2]+1);
        u16 length = strlen(string)-1; // -1 to remove the closing quote.
        registers[30] = length;
        string[length] = 0;
        for (int i = 0; i <= length; ++i) // Include the null-terminator.
            memory[pos+i] = string[i];
    } else if (string_compare(params[0], "OSR")) { // Output a string.
        switch (get_type(params[1])) {
            case TYPE_REGISTER: {
                u16 pos = registers[get_index(params[1])];
                printf("%s", memory+pos);
            } break;
            case TYPE_INT: {
                u16 pos = atoi(params[1]);
                printf("%s", memory+pos);
            } break;
            case TYPE_STRING: {
                char string[STRING_MAX] = {0};
                strcpy(string, params[1]+1);
                string[strlen(string)-1] = 0;
                printf("%s", string);
            } break;
            default: {
                fprintf(stderr, "Error (Line %d): \"OSR\" reqiures an integer or register for the position.\n", line);
                return;
            } break;
        }
    } else if (string_compare(params[0], "LOD")) {
        char file_path[STRING_MAX];
        unsigned len;
        u16 pos;
        switch (get_type(params[1])) {
            case TYPE_REGISTER: {
                pos = registers[get_index(params[1])];
            } break;
            case TYPE_INT: {
                pos = atoi(params[1]);
            } break;
            default: {
                fprintf(stderr, "Error (Line %d): LOD requies register or integer as a start position.\n");
            } break;
        }
        strcpy(file_path, params[2]);
        file_path[strlen(file_path)-1] = 0; // Clip the ending quote.
        char *str = read_file(file_path+1, &len); // Clip first quote.
        // Load it into memory.
        for (int i = 0; i <= len; ++i) { // Include null-terminator.
            memory[i+pos] = str[i];
        }
    }
}

int main(int argcount, char **argv) {
    char file[STRING_MAX];
    if (argcount != 2) {
        strcpy(file, "file.asm");
    } else {
        strcpy(file, argv[1]);
    }
    
    char tokens[32][MAX_TOKENS];
    
    char *input;
    unsigned input_length;
    
    input = read_file(file, &input_length);
    
    input_length = cleanup_source(input, &input);
    
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
            string = 0;
            ch = 0;
            memset(params, 0, sizeof(params));
            continue;
        }
        params[argc][ch++] = input[curpos];
    }
    
    printf("\n");
    
    return 0;
}