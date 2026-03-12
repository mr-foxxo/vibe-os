#include "sectorc_internal.h"

int sectorc_main(int argc, char **argv) {
    static struct sectorc_program program;
    const char *source = 0;
    const char *path;
    int compile_status;
    int exec_status;

    if (argc <= 1) {
        sectorc_write_line("uso: sectorc <arquivo.c>");
        sectorc_write_line("subset: globais int, funcoes sem argumentos, if, while, atribuicao, print()");
        return 0;
    }

    path = argv[1];
    if (argc > 2) {
        sectorc_write_line("sectorc: argumentos extras ignorados");
    }

    sectorc_write("compilando ");
    sectorc_write(path);
    sectorc_write_line("...");

    if (sectorc_read_source(path, &source) != 0) {
        sectorc_write_line("sectorc: arquivo nao encontrado");
        return 0;
    }

    compile_status = sectorc_compile(path, source, &program);
    if (compile_status != 0) {
        sectorc_write_line(program.error);
        return 0;
    }

    sectorc_write_line("ok");
    sectorc_write("bytecode: ");
    sectorc_write_int(program.code_count / 2);
    sectorc_write_line(" instrucoes");
    sectorc_write_line("executando...");

    exec_status = sectorc_execute(&program);
    if (exec_status != 0) {
        sectorc_write_line(program.error);
    }
    return 0;
}
