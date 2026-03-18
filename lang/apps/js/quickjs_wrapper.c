#include <lang/include/vibe_app_runtime.h>
#include "quickjs.h"

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vibe_app_main(int argc, char **argv) {
    JSRuntime *runtime;
    JSContext *context;
    JSValue result;
    int eval_flags = JS_EVAL_TYPE_GLOBAL;

    // Initialize QuickJS runtime and context
    runtime = JS_NewRuntime();
    if (!runtime) {
        fprintf(stderr, "Failed to initialize QuickJS runtime\n");
        return 1;
    }

    context = JS_NewContext(runtime);
    if (!context) {
        fprintf(stderr, "Failed to create QuickJS context\n");
        JS_FreeRuntime(runtime);
        return 1;
    }

    if (argc > 1) {
        // File execution mode
        FILE *script_file = fopen(argv[1], "r");
        if (!script_file) {
            fprintf(stderr, "Failed to open script file: %s\n", argv[1]);
            JS_FreeContext(context);
            JS_FreeRuntime(runtime);
            return 1;
        }

        fseek(script_file, 0, SEEK_END);
        long script_size = ftell(script_file);
        fseek(script_file, 0, SEEK_SET);

        char *script = malloc(script_size + 1);
        if (!script) {
            fprintf(stderr, "Failed to allocate memory for script\n");
            fclose(script_file);
            JS_FreeContext(context);
            JS_FreeRuntime(runtime);
            return 1;
        }

        fread(script, 1, script_size, script_file);
        script[script_size] = '\0';
        fclose(script_file);

        result = JS_Eval(context, script, script_size, argv[1], eval_flags);
        free(script);
    } else {
        // REPL mode
        const char *repl_script = "console.log('QuickJS REPL not implemented yet');";
        result = JS_Eval(context, repl_script, strlen(repl_script), "<repl>", eval_flags);
    }

    if (JS_IsException(result)) {
        fprintf(stderr, "JavaScript execution error\n");
    }

    JS_FreeValue(context, result);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);

    return 0;
}