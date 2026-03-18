#include <lang/include/vibe_app_runtime.h>
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/stackctrl.h"

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vibe_app_main(int argc, char **argv) {
    mp_stack_ctrl_init();
    mp_stack_ctrl_init();
    mp_stack_set_limit(40000 * (BYTES_PER_WORD / 4));

    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_init(mp_sys_argv, argc);

    for (int i = 0; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
    }

    if (argc > 1) {
        // File execution mode
        FILE *script_file = fopen(argv[1], "r");
        if (!script_file) {
            fprintf(stderr, "Failed to open script file: %s\n", argv[1]);
            mp_deinit();
            return 1;
        }

        fseek(script_file, 0, SEEK_END);
        long script_size = ftell(script_file);
        fseek(script_file, 0, SEEK_SET);

        char *script = malloc(script_size + 1);
        if (!script) {
            fprintf(stderr, "Failed to allocate memory for script\n");
            fclose(script_file);
            mp_deinit();
            return 1;
        }

        fread(script, 1, script_size, script_file);
        script[script_size] = '\0';
        fclose(script_file);

        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_lexer_t *lex = mp_lexer_new_from_str_len(0, script, script_size, false);
            mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
            mp_obj_t module_fun = mp_compile(&parse_tree, lex->source_name, MP_EMIT_OPT_NONE, false);
            mp_call_function_0(module_fun);
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        }

        free(script);
    } else {
        // REPL mode
        mp_hal_stdout_tx_str("MicroPython REPL not implemented yet\n");
    }

    mp_deinit();
    return 0;
}