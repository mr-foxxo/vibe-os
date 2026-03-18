#ifndef SECTORC_H
#define SECTORC_H

#define SECTORC_MAX_CODE 4096
#define SECTORC_MAX_GLOBALS 64
#define SECTORC_MAX_FUNCTIONS 32
#define SECTORC_MAX_STRINGS 32

enum sectorc_opcode {
    SECTORC_OP_NOP = 0,
    SECTORC_OP_PUSH_IMM,
    SECTORC_OP_LOAD_GLOBAL,
    SECTORC_OP_STORE_GLOBAL,
    SECTORC_OP_LOAD_INDIRECT,
    SECTORC_OP_STORE_INDIRECT,
    SECTORC_OP_ADD,
    SECTORC_OP_SUB,
    SECTORC_OP_MUL,
    SECTORC_OP_BAND,
    SECTORC_OP_BOR,
    SECTORC_OP_BXOR,
    SECTORC_OP_SHL,
    SECTORC_OP_SHR,
    SECTORC_OP_EQ,
    SECTORC_OP_NE,
    SECTORC_OP_LT,
    SECTORC_OP_GT,
    SECTORC_OP_LE,
    SECTORC_OP_GE,
    SECTORC_OP_NEG,
    SECTORC_OP_JUMP,
    SECTORC_OP_JUMP_IF_ZERO,
    SECTORC_OP_CALL,
    SECTORC_OP_RET,
    SECTORC_OP_POP,
    SECTORC_OP_PRINT_STR,
    SECTORC_OP_PRINT_INT
};

struct sectorc_global {
    char name[32];
};

struct sectorc_function {
    char name[32];
    int entry;
    int declared;
    int returns_value;
};

struct sectorc_string {
    char value[128];
};

struct sectorc_program {
    int code[SECTORC_MAX_CODE];
    int code_count;
    struct sectorc_global globals[SECTORC_MAX_GLOBALS];
    int global_count;
    struct sectorc_function functions[SECTORC_MAX_FUNCTIONS];
    int function_count;
    struct sectorc_string strings[SECTORC_MAX_STRINGS];
    int string_count;
    int main_function;
    char error[160];
};

int sectorc_main(int argc, char **argv);
int sectorc_compile(const char *path, const char *source, struct sectorc_program *program);
int sectorc_execute(struct sectorc_program *program);

#endif // SECTORC_H
