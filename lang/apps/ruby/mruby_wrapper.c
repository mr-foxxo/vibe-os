#include <lang/include/vibe_app_runtime.h>
#include <mruby.h>
#include <mruby/compile.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vibe_app_main(int argc, char **argv) {
    mrb_state *mrb = mrb_open();
    if (!mrb) {
        fprintf(stderr, "Failed to initialize mruby\n");
        return 1;
    }

    if (argc > 1) {
        // File execution mode
        FILE *script_file = fopen(argv[1], "r");
        if (!script_file) {
            fprintf(stderr, "Failed to open script file: %s\n", argv[1]);
            mrb_close(mrb);
            return 1;
        }

        mrb_load_file(mrb, script_file);
        fclose(script_file);
    } else {
        // REPL mode
        const char *repl_script = "puts 'mruby REPL not implemented yet'";
        mrb_load_string(mrb, repl_script);
    }

    if (mrb->exc) {
        fprintf(stderr, "Ruby execution error\n");
    }

    mrb_close(mrb);
    return 0;
}