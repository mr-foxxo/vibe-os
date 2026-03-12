#include "sectorc_internal.h"

#define SECTORC_VM_STACK_MAX 256
#define SECTORC_VM_CALL_MAX 64

static int vm_pop(int *stack, int *sp, struct sectorc_program *program) {
    if (*sp <= 0) {
        sectorc_set_error(program->error, (int)sizeof(program->error), "stack underflow", 0);
        return 0;
    }
    return stack[--(*sp)];
}

static int vm_push(int *stack, int *sp, int value, struct sectorc_program *program) {
    if (*sp >= SECTORC_VM_STACK_MAX) {
        sectorc_set_error(program->error, (int)sizeof(program->error), "stack overflow", 0);
        return -1;
    }
    stack[(*sp)++] = value;
    return 0;
}

static int vm_binary(struct sectorc_program *program,
                     int *stack,
                     int *sp,
                     int opcode) {
    int rhs = vm_pop(stack, sp, program);
    int lhs = vm_pop(stack, sp, program);
    int result = 0;

    if (program->error[0] != '\0') {
        return -1;
    }

    switch (opcode) {
    case SECTORC_OP_ADD: result = lhs + rhs; break;
    case SECTORC_OP_SUB: result = lhs - rhs; break;
    case SECTORC_OP_MUL: result = lhs * rhs; break;
    case SECTORC_OP_BAND: result = lhs & rhs; break;
    case SECTORC_OP_BOR: result = lhs | rhs; break;
    case SECTORC_OP_BXOR: result = lhs ^ rhs; break;
    case SECTORC_OP_SHL: result = lhs << rhs; break;
    case SECTORC_OP_SHR: result = lhs >> rhs; break;
    case SECTORC_OP_EQ: result = lhs == rhs; break;
    case SECTORC_OP_NE: result = lhs != rhs; break;
    case SECTORC_OP_LT: result = lhs < rhs; break;
    case SECTORC_OP_GT: result = lhs > rhs; break;
    case SECTORC_OP_LE: result = lhs <= rhs; break;
    case SECTORC_OP_GE: result = lhs >= rhs; break;
    default:
        sectorc_set_error(program->error, (int)sizeof(program->error), "opcode binario invalido", 0);
        return -1;
    }

    return vm_push(stack, sp, result, program);
}

int sectorc_execute(struct sectorc_program *program) {
    int globals[SECTORC_MAX_GLOBALS];
    int stack[SECTORC_VM_STACK_MAX];
    int call_stack[SECTORC_VM_CALL_MAX];
    int sp = 0;
    int call_sp = 0;
    int ip;

    for (int i = 0; i < SECTORC_MAX_GLOBALS; ++i) {
        globals[i] = 0;
    }
    program->error[0] = '\0';

    if (program->main_function < 0 || program->main_function >= program->function_count) {
        sectorc_set_error(program->error, (int)sizeof(program->error), "funcao main nao encontrada", 0);
        return -1;
    }

    ip = program->functions[program->main_function].entry;
    while (ip >= 0 && ip + 1 < program->code_count) {
        int opcode = program->code[ip++];
        int arg = program->code[ip++];

        switch (opcode) {
        case SECTORC_OP_NOP:
            break;
        case SECTORC_OP_PUSH_IMM:
            if (vm_push(stack, &sp, arg, program) != 0) {
                return -1;
            }
            break;
        case SECTORC_OP_LOAD_GLOBAL:
            if (arg < 0 || arg >= program->global_count) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "global invalida", 0);
                return -1;
            }
            if (vm_push(stack, &sp, globals[arg], program) != 0) {
                return -1;
            }
            break;
        case SECTORC_OP_STORE_GLOBAL: {
            int value = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            if (arg < 0 || arg >= program->global_count) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "global invalida", 0);
                return -1;
            }
            globals[arg] = value;
            break;
        }
        case SECTORC_OP_LOAD_INDIRECT: {
            int addr = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            if (addr < 0 || addr >= program->global_count) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "endereco invalido", 0);
                return -1;
            }
            if (vm_push(stack, &sp, globals[addr], program) != 0) {
                return -1;
            }
            break;
        }
        case SECTORC_OP_STORE_INDIRECT: {
            int value = vm_pop(stack, &sp, program);
            int addr = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            if (addr < 0 || addr >= program->global_count) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "endereco invalido", 0);
                return -1;
            }
            globals[addr] = value;
            break;
        }
        case SECTORC_OP_ADD:
        case SECTORC_OP_SUB:
        case SECTORC_OP_MUL:
        case SECTORC_OP_BAND:
        case SECTORC_OP_BOR:
        case SECTORC_OP_BXOR:
        case SECTORC_OP_SHL:
        case SECTORC_OP_SHR:
        case SECTORC_OP_EQ:
        case SECTORC_OP_NE:
        case SECTORC_OP_LT:
        case SECTORC_OP_GT:
        case SECTORC_OP_LE:
        case SECTORC_OP_GE:
            if (vm_binary(program, stack, &sp, opcode) != 0) {
                return -1;
            }
            break;
        case SECTORC_OP_NEG: {
            int value = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            if (vm_push(stack, &sp, -value, program) != 0) {
                return -1;
            }
            break;
        }
        case SECTORC_OP_JUMP:
            ip = arg;
            break;
        case SECTORC_OP_JUMP_IF_ZERO: {
            int value = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            if (value == 0) {
                ip = arg;
            }
            break;
        }
        case SECTORC_OP_CALL:
            if (arg < 0 || arg >= program->function_count || !program->functions[arg].declared) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "chamada para funcao indefinida", 0);
                return -1;
            }
            if (call_sp >= SECTORC_VM_CALL_MAX) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "pilha de chamadas cheia", 0);
                return -1;
            }
            call_stack[call_sp++] = ip;
            ip = program->functions[arg].entry;
            break;
        case SECTORC_OP_RET: {
            int value = 0;

            if (sp > 0) {
                value = vm_pop(stack, &sp, program);
                if (program->error[0] != '\0') {
                    return -1;
                }
            }

            if (call_sp == 0) {
                return 0;
            }

            ip = call_stack[--call_sp];
            if (vm_push(stack, &sp, value, program) != 0) {
                return -1;
            }
            break;
        }
        case SECTORC_OP_POP:
            (void)vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            break;
        case SECTORC_OP_PRINT_STR:
            if (arg < 0 || arg >= program->string_count) {
                sectorc_set_error(program->error, (int)sizeof(program->error), "string invalida", 0);
                return -1;
            }
            sectorc_write_line(program->strings[arg].value);
            break;
        case SECTORC_OP_PRINT_INT: {
            int value = vm_pop(stack, &sp, program);
            if (program->error[0] != '\0') {
                return -1;
            }
            sectorc_write_int(value);
            sectorc_putc('\n');
            break;
        }
        default:
            sectorc_set_error(program->error, (int)sizeof(program->error), "opcode invalido", 0);
            return -1;
        }
    }

    sectorc_set_error(program->error, (int)sizeof(program->error), "fluxo de execucao invalido", 0);
    return -1;
}
