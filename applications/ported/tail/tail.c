/*
 * tail - VibeOS Ported Application
 * GNU coreutils tail implementation
 * 
 * Usage: tail [-n NUM] [FILE]...
 * 
 * Print the last NUM lines of each FILE (default: 10).
 *
 * -n NUM         print last NUM lines
 */

#include "compat/include/compat.h"

static int tail_file(const char *filename, int num_lines) {
    const char *data;
    int size;
    
    int rc = vibe_app_read_file(filename, &data, &size);
    if (rc != 0 || !data || size < 0) {
        printf("tail: %s: No such file or directory\n", filename);
        return 1;
    }
    
    /* Count total lines to find start position */
    int total_lines = 0;
    for (int i = 0; i < size; i++) {
        if (data[i] == '\n') {
            total_lines++;
        }
    }
    
    /* If content doesn't end with newline, count as partial line */
    if (size > 0 && data[size - 1] != '\n') {
        total_lines++;
    }
    
    /* Calculate which line to start printing from */
    int start_line = (total_lines > num_lines) ? (total_lines - num_lines) : 0;
    int current_line = 0;
    int printing = (start_line == 0);
    
    /* Print last N lines */
    for (int i = 0; i < size; i++) {
        if (!printing && current_line == start_line) {
            printing = 1;
        }
        
        if (printing) {
            putchar((unsigned char)data[i]);
        }
        
        if (data[i] == '\n') {
            current_line++;
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
        printf("tail: no files specified\n");
        return 1;
    }
    
    while (argc > 0) {
        if (tail_file(argv[0], num_lines) != 0) {
            error = 1;
        }
        
        file_count++;
        argc--;
        argv++;
    }
    
    return error;
}
