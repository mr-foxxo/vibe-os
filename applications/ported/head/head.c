/*
 * head - VibeOS Ported Application
 * GNU coreutils head implementation
 * 
 * Usage: head [-n NUM] [FILE]...
 * 
 * Print the first NUM lines of each FILE (default: 10).
 *
 * -n NUM         print first NUM lines
 */

#include "compat/include/compat.h"

static int head_file(const char *filename, int num_lines) {
    const char *data;
    int size;
    
    int rc = vibe_app_read_file(filename, &data, &size);
    if (rc != 0 || !data || size < 0) {
        printf("head: %s: No such file or directory\n", filename);
        return 1;
    }
    
    int lines_printed = 0;
    for (int i = 0; i < size && lines_printed < num_lines; i++) {
        putchar((unsigned char)data[i]);
        if (data[i] == '\n') {
            lines_printed++;
        }
    }
    
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    int num_lines = 10;  /* default */
    int file_count = 0;
    int error = 0;
    
    argc--;
    argv++;
    
    /* Parse options */
    while (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0') {
        if (argv[0][1] == 'n') {
            /* -n NUM */
            if (argv[0][2] != '\0') {
                /* -nNUM format */
                num_lines = atoi(argv[0] + 2);
            } else if (argc > 1) {
                /* -n NUM format */
                argc--;
                argv++;
                num_lines = atoi(argv[0]);
            }
        }
        argc--;
        argv++;
    }
    
    if (num_lines <= 0) num_lines = 10;
    
    /* Process files */
    if (argc <= 0) {
        printf("head: no files specified\n");
        return 1;
    }
    
    while (argc > 0) {
        if (head_file(argv[0], num_lines) != 0) {
            error = 1;
        }
        
        file_count++;
        argc--;
        argv++;
    }
    
    return error;
}
