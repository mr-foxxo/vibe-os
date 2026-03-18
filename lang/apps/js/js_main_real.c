/* JavaScript Runtime for Vibe OS - Real QuickJS Integration */

#include "vibe_app.h"
#include "vibe_app_runtime.h"
#include <string.h>

/* Forward declarations for QuickJS */
typedef struct JSRuntime JSRuntime;
typedef struct JSContext JSContext;
typedef int64_t JSValue;

#define JS_UNDEFINED ((JSValue)(uint64_t)(0x00007FF80000D960LL))
#define JS_NULL ((JSValue)(uint64_t)(0x00007FF80000D980LL))
#define JS_FALSE ((JSValue)(uint64_t)(0x00007FF80000D9A0LL))
#define JS_TRUE ((JSValue)(uint64_t)(0x00007FF80000D9B0LL))

/* QuickJS API stubs - actual impl will link with quickjs_vibe_real.o */
extern JSRuntime *JS_NewRuntime(void);
extern void JS_FreeRuntime(JSRuntime *rt);
extern JSContext *JS_NewContext(JSRuntime *rt);
extern void JS_FreeContext(JSContext *ctx);
extern JSValue JS_Eval(JSContext *ctx, const char *input, size_t input_len, const char *filename, int flags);
extern int JS_IsException(JSValue v);
extern int JS_IsUndefined(JSValue v);
extern double JS_ToFloat64(JSContext *ctx, JSValue val);
extern const char *JS_ToCString(JSContext *ctx, JSValue val);
extern void JS_FreeCString(JSContext *ctx, const char *str);
extern void JS_FreeValue(JSContext *ctx, JSValue v);
extern JSValue JS_GetGlobalObject(JSContext *ctx);
extern JSValue JS_NewObject(JSContext *ctx);
extern void JS_SetPropertyStr(JSContext *ctx, JSValue this_obj, const char *prop, JSValue val);
extern JSValue JS_NewCFunction(JSContext *ctx, JSValue (*func)(JSContext *, JSValue, int, JSValue *), const char *name, int length);
extern JSValue JS_NewString(JSContext *ctx, const char *str);
extern void JS_DupValue(JSContext *ctx, JSValue v);

static JSRuntime *g_rt = NULL;
static JSContext *g_ctx = NULL;

/* console.log binding */
static JSValue console_log(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)ctx; (void)this_val;
    for (int i = 0; i < argc; i++) {
        if (i > 0) vibe_app_console_putc(' ');
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            vibe_app_console_write((char*)str);
            JS_FreeCString(ctx, str);
        }
    }
    vibe_app_console_putc('\n');
    return JS_UNDEFINED;
}

static void setup_console(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JSValue log_func = JS_NewCFunction(ctx, console_log, "log", 1);
    JS_SetPropertyStr(ctx, console, "log", log_func);
    JS_SetPropertyStr(ctx, global, "console", console);
}

int vibe_app_main(int argc, char **argv) {
    vibe_app_console_write("QuickJS JavaScript Engine\n\n");
    
    if (!g_rt) {
        g_rt = JS_NewRuntime();
        if (!g_rt) {
            vibe_app_console_write("ERROR: JS_NewRuntime failed\n");
            return 1;
        }
    }
    
    if (!g_ctx) {
        g_ctx = JS_NewContext(g_rt);
        if (!g_ctx) {
            vibe_app_console_write("ERROR: JS_NewContext failed\n");
            JS_FreeRuntime(g_rt);
            g_rt = NULL;
            return 1;
        }
        setup_console(g_ctx);
    }
    
    if (argc > 1) {
        /* File execution mode */
        const char *file_data;
        int file_size;
        
        vibe_app_console_write("Executing: ");
        vibe_app_console_write(argv[1]);
        vibe_app_console_write("\n\n");
        
        if (vibe_app_read_file(argv[1], &file_data, &file_size) == 0) {
            JSValue result = JS_Eval(g_ctx, file_data, file_size, argv[1], 0);
            if (JS_IsException(result)) {
                vibe_app_console_write("ERROR: Exception during execution\n");
            }
            JS_FreeValue(g_ctx, result);
        } else {
            vibe_app_console_write("ERROR: File not found: ");
            vibe_app_console_write(argv[1]);
            vibe_app_console_write("\n");
        }
    } else {
        /* REPL mode */
        vibe_app_console_write("REPL Mode (type 'exit' to quit)\n");
        vibe_app_console_write("> ");
        
        char buffer[512] = {0};
        int pos = 0;
        
        while (1) {
            unsigned char ch = vibe_app_poll_key();
            if (ch == 0) continue;
            
            if (ch == '\r' || ch == '\n') {
                vibe_app_console_putc('\n');
                
                if (pos > 0) {
                    buffer[pos] = 0;
                    
                    /* Check for exit */
                    if (pos == 4 && buffer[0] == 'e' && buffer[1] == 'x' && 
                        buffer[2] == 'i' && buffer[3] == 't') {
                        break;
                    }
                    
                    /* Evaluate */
                    JSValue result = JS_Eval(g_ctx, buffer, pos, "<stdin>", 0);
                    if (!JS_IsException(result) && !JS_IsUndefined(result)) {
                        double num = JS_ToFloat64(g_ctx, result);
                        const char *str = JS_ToCString(g_ctx, result);
                        if (str) {
                            vibe_app_console_write(str);
                            JS_FreeCString(g_ctx, str);
                        } else {
                            vibe_app_console_write("[result]");
                        }
                        vibe_app_console_putc('\n');
                    }
                    JS_FreeValue(g_ctx, result);
                    pos = 0;
                }
                
                vibe_app_console_write("> ");
            } else if (ch == '\b' || ch == 127) {
                if (pos > 0) {
                    pos--;
                    vibe_app_console_putc('\b');
                    vibe_app_console_putc(' ');
                    vibe_app_console_putc('\b');
                }
            } else if (ch >= 32 && ch < 127) {
                if (pos < (int)sizeof(buffer) - 1) {
                    buffer[pos++] = ch;
                    vibe_app_console_putc(ch);
                }
            }
        }
        
        vibe_app_console_write("\nGoodbye!\n");
    }
    
    return 0;
}
