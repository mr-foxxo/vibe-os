/*
 * wc - VibeOS Ported Application
 * GNU coreutils wc implementation (word/line/byte counter)
 * 
 * Usage: wc [OPTION]... [FILE]...
 * 
 * Print newline, word, and byte counts for each FILE.
 *
 * -c             print the byte count
 * -l             print the line count
 * -w             print the word count
 * (default: print all three)
 */

#include "compat/include/compat.h"

typedef struct {
    int count_lines;
    int count_words;
    int count_bytes;
} wc_options_t;

typedef struct {
    int lines;
    int words;
    int bytes;
} wc_counts_t;

static void count_data(const char *data, int size, wc_counts_t *counts) {
    counts->bytes = size;
    counts->lines = 0;
    counts->words = 0;
    
    int in_word = 0;
    for (int i = 0; i < size; i++) {
        char c = data[i];
        
        if (c == '\n') {
            counts->lines++;
            in_word = 0;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            in_word = 0;
        } else {
            if (!in_word) {
                counts->words++;
                in_word = 1;
            }
        }
    }
}

static int count_file(const char *filename, const wc_options_t *opts, 
                      wc_counts_t *counts, int show_filename) {
    const char *data;
    int size;
    
    int rc = vibe_app_read_file(filename, &data, &size);
    if (rc != 0 || !data || size < 0) {
        printf("wc: %s: No such file or directory\n", filename);
        return 1;
    }
    
    count_data(data, size, counts);
    
    if (opts->count_lines) printf(" %d", counts->lines);
    if (opts->count_words) printf(" %d", counts->words);
    if (opts->count_bytes) printf(" %d", counts->bytes);
    
    if (show_filename) {
        printf(" %s\n", filename);
    } else {
        printf("\n");
    }
    
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    wc_options_t opts = {1, 1, 1};  /* default: show all */
    int file_count = 0;
    int error = 0;
    
    argc--;
    argv++;
    
    /* Parse options */
    while (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0') {
        const char *p = argv[0] + 1;
        if (*p == '-') p++;  /* Skip second dash for long options */
        
        while (*p) {
            if (*p == 'c') {
                opts.count_lines = 0;
                opts.count_words = 0;
                opts.count_bytes = 1;
            } else if (*p == 'l') {
                opts.count_lines = 1;
                opts.count_words = 0;
                opts.count_bytes = 0;
            } else if (*p == 'w') {
                opts.count_lines = 0;
                opts.count_words = 1;
                opts.count_bytes = 0;
            }
            p++;
        }
        
        argc--;
        argv++;
    }
    
    /* Count files */
    if (argc <= 0) {
        printf("wc: no files specified\n");
        return 1;
    }
    
    wc_counts_t total = {0, 0, 0};
    
    while (argc > 0) {
        wc_counts_t counts = {0, 0, 0};
        
        if (count_file(argv[0], &opts, &counts, 1) == 0) {
            total.lines += counts.lines;
            total.words += counts.words;
            total.bytes += counts.bytes;
        } else {
            error = 1;
        }
        
        file_count++;
        argc--;
        argv++;
    }
    
    /* Print total if multiple files */
    if (file_count > 1) {
        if (opts.count_lines) printf(" %d", total.lines);
        if (opts.count_words) printf(" %d", total.words);
        if (opts.count_bytes) printf(" %d", total.bytes);
        printf(" total\n");
    }
    
    return error;
}
