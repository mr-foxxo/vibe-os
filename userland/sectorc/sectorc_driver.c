#include "sectorc_internal.h"

#define SECTORC_MAX_TOKENS 768

enum sectorc_token_kind {
    SECTORC_TOKEN_EOF = 0,
    SECTORC_TOKEN_IDENT,
    SECTORC_TOKEN_NUMBER,
    SECTORC_TOKEN_STRING,
    SECTORC_TOKEN_INT,
    SECTORC_TOKEN_VOID,
    SECTORC_TOKEN_IF,
    SECTORC_TOKEN_WHILE,
    SECTORC_TOKEN_RETURN,
    SECTORC_TOKEN_ASM,
    SECTORC_TOKEN_ELSE,
    SECTORC_TOKEN_LPAREN,
    SECTORC_TOKEN_RPAREN,
    SECTORC_TOKEN_LBRACE,
    SECTORC_TOKEN_RBRACE,
    SECTORC_TOKEN_SEMI,
    SECTORC_TOKEN_COMMA,
    SECTORC_TOKEN_ASSIGN,
    SECTORC_TOKEN_PLUS,
    SECTORC_TOKEN_MINUS,
    SECTORC_TOKEN_STAR,
    SECTORC_TOKEN_AMP,
    SECTORC_TOKEN_PIPE,
    SECTORC_TOKEN_CARET,
    SECTORC_TOKEN_SHL,
    SECTORC_TOKEN_SHR,
    SECTORC_TOKEN_EQ,
    SECTORC_TOKEN_NE,
    SECTORC_TOKEN_LT,
    SECTORC_TOKEN_GT,
    SECTORC_TOKEN_LE,
    SECTORC_TOKEN_GE
};

struct sectorc_token {
    int kind;
    int value;
    int line;
    char text[128];
};

struct sectorc_compiler {
    const char *path;
    const char *source;
    int pos;
    int line;
    struct sectorc_token tokens[SECTORC_MAX_TOKENS];
    int token_count;
    int current;
    struct sectorc_program *program;
    int failed;
};

static int is_alpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int is_alnum(int c) {
    return is_alpha(c) || is_digit(c);
}

static void compiler_error(struct sectorc_compiler *compiler, const char *message, int line) {
    compiler->failed = 1;
    sectorc_set_error(compiler->program->error,
                      (int)sizeof(compiler->program->error),
                      message,
                      line);
}

static void add_token(struct sectorc_compiler *compiler, int kind, const char *text, int value, int line) {
    struct sectorc_token *token;
    int index = compiler->token_count;

    if (index >= SECTORC_MAX_TOKENS) {
        compiler_error(compiler, "codigo fonte grande demais", line);
        return;
    }

    token = &compiler->tokens[index];
    token->kind = kind;
    token->value = value;
    token->line = line;
    token->text[0] = '\0';
    if (text) {
        sectorc_copy_string(token->text, text, (int)sizeof(token->text));
    }
    compiler->token_count = index + 1;
}

static void skip_comment(struct sectorc_compiler *compiler) {
    if (compiler->source[compiler->pos] == '/' && compiler->source[compiler->pos + 1] == '/') {
        compiler->pos += 2;
        while (compiler->source[compiler->pos] != '\0' && compiler->source[compiler->pos] != '\n') {
            ++compiler->pos;
        }
        return;
    }

    if (compiler->source[compiler->pos] == '/' && compiler->source[compiler->pos + 1] == '*') {
        compiler->pos += 2;
        while (compiler->source[compiler->pos] != '\0') {
            if (compiler->source[compiler->pos] == '\n') {
                ++compiler->line;
            }
            if (compiler->source[compiler->pos] == '*' && compiler->source[compiler->pos + 1] == '/') {
                compiler->pos += 2;
                return;
            }
            ++compiler->pos;
        }
        compiler_error(compiler, "comentario sem fechamento", compiler->line);
    }
}

static int keyword_kind(const char *text) {
    if (sectorc_string_equal(text, "int")) return SECTORC_TOKEN_INT;
    if (sectorc_string_equal(text, "void")) return SECTORC_TOKEN_VOID;
    if (sectorc_string_equal(text, "if")) return SECTORC_TOKEN_IF;
    if (sectorc_string_equal(text, "while")) return SECTORC_TOKEN_WHILE;
    if (sectorc_string_equal(text, "return")) return SECTORC_TOKEN_RETURN;
    if (sectorc_string_equal(text, "asm")) return SECTORC_TOKEN_ASM;
    if (sectorc_string_equal(text, "else")) return SECTORC_TOKEN_ELSE;
    return SECTORC_TOKEN_IDENT;
}

static void tokenize(struct sectorc_compiler *compiler) {
    compiler->pos = 0;
    compiler->line = 1;
    compiler->token_count = 0;

    while (compiler->source[compiler->pos] != '\0' && !compiler->failed) {
        char c = compiler->source[compiler->pos];
        int line = compiler->line;

        if (c == ' ' || c == '\t' || c == '\r') {
            ++compiler->pos;
            continue;
        }
        if (c == '\n') {
            ++compiler->line;
            ++compiler->pos;
            continue;
        }
        if (c == '/' && (compiler->source[compiler->pos + 1] == '/' ||
                         compiler->source[compiler->pos + 1] == '*')) {
            skip_comment(compiler);
            continue;
        }
        if (is_alpha(c)) {
            char buf[128];
            int len = 0;

            while (is_alnum(compiler->source[compiler->pos]) && len < (int)sizeof(buf) - 1) {
                buf[len++] = compiler->source[compiler->pos++];
            }
            buf[len] = '\0';
            add_token(compiler, keyword_kind(buf), buf, 0, line);
            continue;
        }
        if (is_digit(c)) {
            int value = 0;

            while (is_digit(compiler->source[compiler->pos])) {
                value = value * 10 + (compiler->source[compiler->pos] - '0');
                ++compiler->pos;
            }
            add_token(compiler, SECTORC_TOKEN_NUMBER, 0, value, line);
            continue;
        }
        if (c == '"') {
            char buf[128];
            int len = 0;

            ++compiler->pos;
            while (compiler->source[compiler->pos] != '\0' &&
                   compiler->source[compiler->pos] != '"' &&
                   len < (int)sizeof(buf) - 1) {
                if (compiler->source[compiler->pos] == '\\') {
                    ++compiler->pos;
                    if (compiler->source[compiler->pos] == 'n') {
                        buf[len++] = '\n';
                    } else if (compiler->source[compiler->pos] == 't') {
                        buf[len++] = '\t';
                    } else if (compiler->source[compiler->pos] == '"' ||
                               compiler->source[compiler->pos] == '\\') {
                        buf[len++] = compiler->source[compiler->pos];
                    } else {
                        compiler_error(compiler, "escape invalido em string", line);
                        return;
                    }
                } else {
                    buf[len++] = compiler->source[compiler->pos];
                }
                ++compiler->pos;
            }

            if (compiler->source[compiler->pos] != '"') {
                compiler_error(compiler, "string sem fechamento", line);
                return;
            }

            ++compiler->pos;
            buf[len] = '\0';
            add_token(compiler, SECTORC_TOKEN_STRING, buf, 0, line);
            continue;
        }

        if (c == '<' && compiler->source[compiler->pos + 1] == '<') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_SHL, 0, 0, line);
            continue;
        }
        if (c == '>' && compiler->source[compiler->pos + 1] == '>') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_SHR, 0, 0, line);
            continue;
        }
        if (c == '=' && compiler->source[compiler->pos + 1] == '=') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_EQ, 0, 0, line);
            continue;
        }
        if (c == '!' && compiler->source[compiler->pos + 1] == '=') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_NE, 0, 0, line);
            continue;
        }
        if (c == '<' && compiler->source[compiler->pos + 1] == '=') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_LE, 0, 0, line);
            continue;
        }
        if (c == '>' && compiler->source[compiler->pos + 1] == '=') {
            compiler->pos += 2;
            add_token(compiler, SECTORC_TOKEN_GE, 0, 0, line);
            continue;
        }

        ++compiler->pos;
        switch (c) {
        case '(': add_token(compiler, SECTORC_TOKEN_LPAREN, 0, 0, line); break;
        case ')': add_token(compiler, SECTORC_TOKEN_RPAREN, 0, 0, line); break;
        case '{': add_token(compiler, SECTORC_TOKEN_LBRACE, 0, 0, line); break;
        case '}': add_token(compiler, SECTORC_TOKEN_RBRACE, 0, 0, line); break;
        case ';': add_token(compiler, SECTORC_TOKEN_SEMI, 0, 0, line); break;
        case ',': add_token(compiler, SECTORC_TOKEN_COMMA, 0, 0, line); break;
        case '=': add_token(compiler, SECTORC_TOKEN_ASSIGN, 0, 0, line); break;
        case '+': add_token(compiler, SECTORC_TOKEN_PLUS, 0, 0, line); break;
        case '-': add_token(compiler, SECTORC_TOKEN_MINUS, 0, 0, line); break;
        case '*': add_token(compiler, SECTORC_TOKEN_STAR, 0, 0, line); break;
        case '&': add_token(compiler, SECTORC_TOKEN_AMP, 0, 0, line); break;
        case '|': add_token(compiler, SECTORC_TOKEN_PIPE, 0, 0, line); break;
        case '^': add_token(compiler, SECTORC_TOKEN_CARET, 0, 0, line); break;
        case '<': add_token(compiler, SECTORC_TOKEN_LT, 0, 0, line); break;
        case '>': add_token(compiler, SECTORC_TOKEN_GT, 0, 0, line); break;
        default:
            compiler_error(compiler, "token invalido", line);
            return;
        }
    }

    add_token(compiler, SECTORC_TOKEN_EOF, 0, 0, compiler->line);
}

static struct sectorc_token *peek_token(struct sectorc_compiler *compiler) {
    return &compiler->tokens[compiler->current];
}

static struct sectorc_token *previous_token(struct sectorc_compiler *compiler) {
    return &compiler->tokens[compiler->current - 1];
}

static int match_token(struct sectorc_compiler *compiler, int kind) {
    if (peek_token(compiler)->kind != kind) {
        return 0;
    }
    ++compiler->current;
    return 1;
}

static int expect_token(struct sectorc_compiler *compiler, int kind, const char *message) {
    if (peek_token(compiler)->kind == kind) {
        ++compiler->current;
        return 1;
    }
    compiler_error(compiler, message, peek_token(compiler)->line);
    return 0;
}

static int emit(struct sectorc_compiler *compiler, int opcode, int arg) {
    int index = compiler->program->code_count;

    if (index + 1 >= SECTORC_MAX_CODE) {
        compiler_error(compiler, "bytecode excedeu o limite", peek_token(compiler)->line);
        return -1;
    }
    compiler->program->code[index] = opcode;
    compiler->program->code[index + 1] = arg;
    compiler->program->code_count += 2;
    return index;
}

static void patch_jump(struct sectorc_compiler *compiler, int instruction_index, int target) {
    if (instruction_index < 0 || instruction_index + 1 >= compiler->program->code_count) {
        compiler_error(compiler, "patch de salto invalido", peek_token(compiler)->line);
        return;
    }
    compiler->program->code[instruction_index + 1] = target;
}

static int find_global(struct sectorc_program *program, const char *name) {
    for (int i = 0; i < program->global_count; ++i) {
        if (sectorc_string_equal(program->globals[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int add_global(struct sectorc_compiler *compiler, const char *name) {
    int index;

    if (find_global(compiler->program, name) >= 0) {
        compiler_error(compiler, "global redeclarada", previous_token(compiler)->line);
        return -1;
    }
    index = compiler->program->global_count;
    if (index >= SECTORC_MAX_GLOBALS) {
        compiler_error(compiler, "globais demais", previous_token(compiler)->line);
        return -1;
    }
    sectorc_copy_string(compiler->program->globals[index].name,
                        name,
                        (int)sizeof(compiler->program->globals[index].name));
    compiler->program->global_count = index + 1;
    return index;
}

static int find_function(struct sectorc_program *program, const char *name) {
    for (int i = 0; i < program->function_count; ++i) {
        if (sectorc_string_equal(program->functions[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int get_function(struct sectorc_compiler *compiler, const char *name) {
    int index = find_function(compiler->program, name);

    if (index >= 0) {
        return index;
    }

    index = compiler->program->function_count;
    if (index >= SECTORC_MAX_FUNCTIONS) {
        compiler_error(compiler, "funcoes demais", previous_token(compiler)->line);
        return -1;
    }

    sectorc_copy_string(compiler->program->functions[index].name,
                        name,
                        (int)sizeof(compiler->program->functions[index].name));
    compiler->program->functions[index].entry = -1;
    compiler->program->functions[index].declared = 0;
    compiler->program->functions[index].returns_value = 0;
    compiler->program->function_count = index + 1;
    return index;
}

static int add_string(struct sectorc_compiler *compiler, const char *text) {
    int index = compiler->program->string_count;

    for (int i = 0; i < index; ++i) {
        if (sectorc_string_equal(compiler->program->strings[i].value, text)) {
            return i;
        }
    }

    if (index >= SECTORC_MAX_STRINGS) {
        compiler_error(compiler, "strings demais", previous_token(compiler)->line);
        return -1;
    }

    sectorc_copy_string(compiler->program->strings[index].value,
                        text,
                        (int)sizeof(compiler->program->strings[index].value));
    compiler->program->string_count = index + 1;
    return index;
}

static void parse_expression(struct sectorc_compiler *compiler);

static int maybe_parse_pointer_cast(struct sectorc_compiler *compiler) {
    int saved = compiler->current;

    if (!match_token(compiler, SECTORC_TOKEN_LPAREN)) {
        return 0;
    }
    if (!match_token(compiler, SECTORC_TOKEN_INT)) {
        compiler->current = saved;
        return 0;
    }
    if (!match_token(compiler, SECTORC_TOKEN_STAR)) {
        compiler->current = saved;
        return 0;
    }
    if (!match_token(compiler, SECTORC_TOKEN_RPAREN)) {
        compiler->current = saved;
        return 0;
    }
    return 1;
}

static void parse_primary(struct sectorc_compiler *compiler) {
    struct sectorc_token *token = peek_token(compiler);

    if (match_token(compiler, SECTORC_TOKEN_NUMBER)) {
        (void)emit(compiler, SECTORC_OP_PUSH_IMM, previous_token(compiler)->value);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_LPAREN)) {
        parse_expression(compiler);
        (void)expect_token(compiler, SECTORC_TOKEN_RPAREN, "faltou )");
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_IDENT)) {
        struct sectorc_token *name = previous_token(compiler);
        int global_index;
        int func_index;

        if (match_token(compiler, SECTORC_TOKEN_LPAREN)) {
            if (!expect_token(compiler, SECTORC_TOKEN_RPAREN, "funcoes do subset nao recebem argumentos")) {
                return;
            }
            func_index = get_function(compiler, name->text);
            if (func_index >= 0) {
                (void)emit(compiler, SECTORC_OP_CALL, func_index);
            }
            return;
        }

        global_index = find_global(compiler->program, name->text);
        if (global_index < 0) {
            compiler_error(compiler, "identificador nao declarado", token->line);
            return;
        }
        (void)emit(compiler, SECTORC_OP_LOAD_GLOBAL, global_index);
        return;
    }

    compiler_error(compiler, "expressao invalida", token->line);
}

static void parse_unary(struct sectorc_compiler *compiler) {
    if (match_token(compiler, SECTORC_TOKEN_MINUS)) {
        parse_unary(compiler);
        (void)emit(compiler, SECTORC_OP_NEG, 0);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_AMP)) {
        struct sectorc_token *token = peek_token(compiler);
        int global_index;

        if (!expect_token(compiler, SECTORC_TOKEN_IDENT, "faltou identificador apos &")) {
            return;
        }
        global_index = find_global(compiler->program, previous_token(compiler)->text);
        if (global_index < 0) {
            compiler_error(compiler, "identificador nao declarado", token->line);
            return;
        }
        (void)emit(compiler, SECTORC_OP_PUSH_IMM, global_index);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_STAR)) {
        (void)maybe_parse_pointer_cast(compiler);
        parse_unary(compiler);
        (void)emit(compiler, SECTORC_OP_LOAD_INDIRECT, 0);
        return;
    }
    parse_primary(compiler);
}

static void parse_term(struct sectorc_compiler *compiler) {
    parse_unary(compiler);
    while (match_token(compiler, SECTORC_TOKEN_STAR)) {
        parse_unary(compiler);
        (void)emit(compiler, SECTORC_OP_MUL, 0);
    }
}

static void parse_additive(struct sectorc_compiler *compiler) {
    parse_term(compiler);
    while (peek_token(compiler)->kind == SECTORC_TOKEN_PLUS ||
           peek_token(compiler)->kind == SECTORC_TOKEN_MINUS) {
        int opcode;

        if (match_token(compiler, SECTORC_TOKEN_PLUS)) opcode = SECTORC_OP_ADD;
        else {
            (void)match_token(compiler, SECTORC_TOKEN_MINUS);
            opcode = SECTORC_OP_SUB;
        }
        parse_term(compiler);
        (void)emit(compiler, opcode, 0);
    }
}

static void parse_shift(struct sectorc_compiler *compiler) {
    parse_additive(compiler);
    while (peek_token(compiler)->kind == SECTORC_TOKEN_SHL ||
           peek_token(compiler)->kind == SECTORC_TOKEN_SHR) {
        int opcode;

        if (match_token(compiler, SECTORC_TOKEN_SHL)) opcode = SECTORC_OP_SHL;
        else {
            (void)match_token(compiler, SECTORC_TOKEN_SHR);
            opcode = SECTORC_OP_SHR;
        }
        parse_additive(compiler);
        (void)emit(compiler, opcode, 0);
    }
}

static void parse_relational(struct sectorc_compiler *compiler) {
    parse_shift(compiler);
    while (peek_token(compiler)->kind == SECTORC_TOKEN_LT ||
           peek_token(compiler)->kind == SECTORC_TOKEN_GT ||
           peek_token(compiler)->kind == SECTORC_TOKEN_LE ||
           peek_token(compiler)->kind == SECTORC_TOKEN_GE) {
        int opcode = SECTORC_OP_LT;

        if (match_token(compiler, SECTORC_TOKEN_LT)) opcode = SECTORC_OP_LT;
        else if (match_token(compiler, SECTORC_TOKEN_GT)) opcode = SECTORC_OP_GT;
        else if (match_token(compiler, SECTORC_TOKEN_LE)) opcode = SECTORC_OP_LE;
        else {
            (void)match_token(compiler, SECTORC_TOKEN_GE);
            opcode = SECTORC_OP_GE;
        }

        parse_shift(compiler);
        (void)emit(compiler, opcode, 0);
    }
}

static void parse_equality(struct sectorc_compiler *compiler) {
    parse_relational(compiler);
    while (peek_token(compiler)->kind == SECTORC_TOKEN_EQ ||
           peek_token(compiler)->kind == SECTORC_TOKEN_NE) {
        int opcode;

        if (match_token(compiler, SECTORC_TOKEN_EQ)) opcode = SECTORC_OP_EQ;
        else {
            (void)match_token(compiler, SECTORC_TOKEN_NE);
            opcode = SECTORC_OP_NE;
        }
        parse_relational(compiler);
        (void)emit(compiler, opcode, 0);
    }
}

static void parse_bitand(struct sectorc_compiler *compiler) {
    parse_equality(compiler);
    while (match_token(compiler, SECTORC_TOKEN_AMP)) {
        parse_equality(compiler);
        (void)emit(compiler, SECTORC_OP_BAND, 0);
    }
}

static void parse_bitxor(struct sectorc_compiler *compiler) {
    parse_bitand(compiler);
    while (match_token(compiler, SECTORC_TOKEN_CARET)) {
        parse_bitand(compiler);
        (void)emit(compiler, SECTORC_OP_BXOR, 0);
    }
}

static void parse_expression(struct sectorc_compiler *compiler) {
    parse_bitxor(compiler);
    while (match_token(compiler, SECTORC_TOKEN_PIPE)) {
        parse_bitxor(compiler);
        (void)emit(compiler, SECTORC_OP_BOR, 0);
    }
}

static void parse_statement(struct sectorc_compiler *compiler);

static void parse_block(struct sectorc_compiler *compiler) {
    if (!expect_token(compiler, SECTORC_TOKEN_LBRACE, "faltou {")) {
        return;
    }

    while (!compiler->failed && peek_token(compiler)->kind != SECTORC_TOKEN_RBRACE) {
        parse_statement(compiler);
    }

    (void)expect_token(compiler, SECTORC_TOKEN_RBRACE, "faltou }");
}

static void parse_print_call(struct sectorc_compiler *compiler) {
    if (!expect_token(compiler, SECTORC_TOKEN_LPAREN, "faltou (")) {
        return;
    }

    if (match_token(compiler, SECTORC_TOKEN_STRING)) {
        int string_index = add_string(compiler, previous_token(compiler)->text);
        (void)expect_token(compiler, SECTORC_TOKEN_RPAREN, "faltou ) em print");
        (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos print");
        if (string_index >= 0) {
            (void)emit(compiler, SECTORC_OP_PRINT_STR, string_index);
        }
        return;
    }

    parse_expression(compiler);
    (void)expect_token(compiler, SECTORC_TOKEN_RPAREN, "faltou ) em print");
    (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos print");
    (void)emit(compiler, SECTORC_OP_PRINT_INT, 0);
}

static void parse_assignment_or_call(struct sectorc_compiler *compiler) {
    int saved = compiler->current;

    if (match_token(compiler, SECTORC_TOKEN_IDENT)) {
        struct sectorc_token *name = previous_token(compiler);

        if (sectorc_string_equal(name->text, "print")) {
            parse_print_call(compiler);
            return;
        }

        if (match_token(compiler, SECTORC_TOKEN_LPAREN)) {
            int func_index;

            if (!expect_token(compiler, SECTORC_TOKEN_RPAREN, "funcoes do subset nao recebem argumentos")) {
                return;
            }
            if (!expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos chamada")) {
                return;
            }
            func_index = get_function(compiler, name->text);
            if (func_index >= 0) {
                (void)emit(compiler, SECTORC_OP_CALL, func_index);
                (void)emit(compiler, SECTORC_OP_POP, 0);
            }
            return;
        }

        if (match_token(compiler, SECTORC_TOKEN_ASSIGN)) {
            int global_index = find_global(compiler->program, name->text);

            if (global_index < 0) {
                compiler_error(compiler, "identificador nao declarado", name->line);
                return;
            }

            parse_expression(compiler);
            (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos atribuicao");
            (void)emit(compiler, SECTORC_OP_STORE_GLOBAL, global_index);
            return;
        }
    }

    compiler->current = saved;
    if (match_token(compiler, SECTORC_TOKEN_STAR)) {
        (void)maybe_parse_pointer_cast(compiler);
        parse_expression(compiler);
        if (!expect_token(compiler, SECTORC_TOKEN_ASSIGN, "faltou = na atribuicao indireta")) {
            return;
        }
        parse_expression(compiler);
        (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos atribuicao");
        (void)emit(compiler, SECTORC_OP_STORE_INDIRECT, 0);
        return;
    }

    compiler_error(compiler, "statement invalido", peek_token(compiler)->line);
}

static void parse_statement(struct sectorc_compiler *compiler) {
    if (compiler->failed) {
        return;
    }

    if (match_token(compiler, SECTORC_TOKEN_SEMI)) {
        return;
    }
    if (peek_token(compiler)->kind == SECTORC_TOKEN_LBRACE) {
        parse_block(compiler);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_IF)) {
        int jump_index;

        (void)expect_token(compiler, SECTORC_TOKEN_LPAREN, "faltou ( apos if");
        parse_expression(compiler);
        (void)expect_token(compiler, SECTORC_TOKEN_RPAREN, "faltou ) apos if");
        jump_index = emit(compiler, SECTORC_OP_JUMP_IF_ZERO, -1);
        parse_statement(compiler);
        if (match_token(compiler, SECTORC_TOKEN_ELSE)) {
            int end_jump = emit(compiler, SECTORC_OP_JUMP, -1);
            patch_jump(compiler, jump_index, compiler->program->code_count);
            parse_statement(compiler);
            patch_jump(compiler, end_jump, compiler->program->code_count);
        } else {
            patch_jump(compiler, jump_index, compiler->program->code_count);
        }
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_WHILE)) {
        int loop_start = compiler->program->code_count;
        int jump_index;

        (void)expect_token(compiler, SECTORC_TOKEN_LPAREN, "faltou ( apos while");
        parse_expression(compiler);
        (void)expect_token(compiler, SECTORC_TOKEN_RPAREN, "faltou ) apos while");
        jump_index = emit(compiler, SECTORC_OP_JUMP_IF_ZERO, -1);
        parse_statement(compiler);
        (void)emit(compiler, SECTORC_OP_JUMP, loop_start);
        patch_jump(compiler, jump_index, compiler->program->code_count);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_RETURN)) {
        if (!match_token(compiler, SECTORC_TOKEN_SEMI)) {
            parse_expression(compiler);
            (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos return");
        } else {
            (void)emit(compiler, SECTORC_OP_PUSH_IMM, 0);
        }
        (void)emit(compiler, SECTORC_OP_RET, 0);
        return;
    }
    if (match_token(compiler, SECTORC_TOKEN_ASM)) {
        compiler_error(compiler, "asm do SectorC original ainda nao foi portado para o backend do VibeOS", previous_token(compiler)->line);
        return;
    }

    parse_assignment_or_call(compiler);
}

static void parse_global_decl(struct sectorc_compiler *compiler, const char *name) {
    (void)add_global(compiler, name);
    (void)expect_token(compiler, SECTORC_TOKEN_SEMI, "faltou ; apos global");
}

static void parse_function_decl(struct sectorc_compiler *compiler, int returns_value, const char *name) {
    int func_index = get_function(compiler, name);
    int saw_return = 0;

    if (func_index < 0) {
        return;
    }
    if (compiler->program->functions[func_index].declared) {
        compiler_error(compiler, "funcao redeclarada", previous_token(compiler)->line);
        return;
    }

    compiler->program->functions[func_index].declared = 1;
    compiler->program->functions[func_index].returns_value = returns_value;
    compiler->program->functions[func_index].entry = compiler->program->code_count;

    if (sectorc_string_equal(name, "main")) {
        compiler->program->main_function = func_index;
    }

    parse_block(compiler);

    if (compiler->program->code_count >= 2 &&
        compiler->program->code[compiler->program->code_count - 2] == SECTORC_OP_RET) {
        saw_return = 1;
    }

    if (!saw_return) {
        (void)emit(compiler, SECTORC_OP_PUSH_IMM, 0);
        (void)emit(compiler, SECTORC_OP_RET, 0);
    }
}

static void parse_program(struct sectorc_compiler *compiler) {
    compiler->current = 0;

    while (!compiler->failed && peek_token(compiler)->kind != SECTORC_TOKEN_EOF) {
        int returns_value;
        struct sectorc_token *name;

        if (match_token(compiler, SECTORC_TOKEN_INT)) {
            returns_value = 1;
        } else if (match_token(compiler, SECTORC_TOKEN_VOID)) {
            returns_value = 0;
        } else {
            compiler_error(compiler, "declaracao de topo invalida", peek_token(compiler)->line);
            return;
        }

        name = peek_token(compiler);
        if (!expect_token(compiler, SECTORC_TOKEN_IDENT, "faltou identificador de topo")) {
            return;
        }

        if (match_token(compiler, SECTORC_TOKEN_LPAREN)) {
            if (!expect_token(compiler, SECTORC_TOKEN_RPAREN, "funcoes do subset nao recebem argumentos")) {
                return;
            }
            parse_function_decl(compiler, returns_value, name->text);
        } else if (returns_value) {
            parse_global_decl(compiler, name->text);
        } else {
            compiler_error(compiler, "somente globais int sao suportadas", name->line);
            return;
        }
    }
}

int sectorc_compile(const char *path, const char *source, struct sectorc_program *program) {
    struct sectorc_compiler compiler;

    (void)path;
    compiler.path = path;
    compiler.source = source;
    compiler.program = program;
    compiler.failed = 0;

    program->code_count = 0;
    program->global_count = 0;
    program->function_count = 0;
    program->string_count = 0;
    program->main_function = -1;
    program->error[0] = '\0';

    tokenize(&compiler);
    if (compiler.failed) {
        return -1;
    }

    parse_program(&compiler);
    if (compiler.failed) {
        return -1;
    }

    if (program->main_function < 0) {
        sectorc_set_error(program->error, (int)sizeof(program->error), "funcao main() obrigatoria", 0);
        return -1;
    }

    for (int i = 0; i < program->function_count; ++i) {
        if (!program->functions[i].declared) {
            sectorc_set_error(program->error, (int)sizeof(program->error), "existe chamada para funcao sem definicao", 0);
            return -1;
        }
    }

    return 0;
}
