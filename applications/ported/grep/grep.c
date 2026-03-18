/*
 * grep - VibeOS Ported Application
 * GNU coreutils grep implementation (simplified - literal matching)
 * 
 * Usage: grep [OPTION]... PATTERN [FILE]...
 * 
 * Search for PATTERN in each FILE.
 * By default, grep prints lines containing the pattern.
 *
 * -c             print only count of matching lines
 * -i             ignore case distinctions
 * -n             print line number with output lines
 * -v             invert match (show non-matching lines)
 */

#include "compat/include/compat.h"

typedef struct {
    int count_only;
    int ignore_case;
    int show_line_num;
    int invert_match;
} grep_options_t;

static int tolower_compat(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int strstr_case(const char *haystack, const char *needle, int ignore_case) {
    if (!haystack || !needle || needle[0] == '\0') return 0;
    
    if (ignore_case) {
        for (int i = 0; haystack[i]; i++) {
            int j = 0;
            while (needle[j] && haystack[i + j]) {
                if (tolower_compat(haystack[i + j]) != tolower_compat(needle[j])) {
                    break;
                }
                j++;
            }
            if (needle[j] == '\0') {
                return 1;
            }
        }
    } else {
        const char *result = strstr(haystack, needle);
        return result != NULL ? 1 : 0;
    }
    
    return 0;
}

static int grep_file(const char *filename, const char *pattern, 
                     const grep_options_t *opts) {
    const char *data;
    int size;
    
    int rc = vibe_app_read_file(filename, &data, &size);
    if (rc != 0 || !data || size < 0) {
        printf("grep: %s: No such file or directory\n", filename);
        return 1;
    }
    
    int matches = 0;
    int line_num = 1;
    int i = 0;
    
    while (i < size) {
        /* Extract line */
        int line_start = i;
        while (i < size && data[i] != '\n') i++;
        int line_end = i;
        if (i < size && data[i] == '\n') i++;
        
        /* Check if line matches pattern */
        int found = 0;
        
        /* Search in line */
        for (int j = line_start; j < line_end; j++) {
            found = strstr_case(&data[j], pattern, opts->ignore_case);
            if (found) break;
        }
        
        /* Apply logic */
        int should_print = (found && !opts->invert_match) || 
                          (!found && opts->invert_match);
        
        if (should_print) {
            matches++;
            if (!opts->count_only) {
                if (opts->show_line_num) {
                    printf("%d: ", line_num);
                }
                /* Print line content */
                for (int j = line_start; j < line_end; j++) {
                    putchar((unsigned char)data[j]);
                }
                printf("\n");
            }
        }
        
        line_num++;
    }
    
    if (opts->count_only) {
        printf("%d\n", matches);
    }
    
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    grep_options_t opts = {0, 0, 0, 0};
    int error = 0;
    
    argc--;
    argv++;
    
    /* Parse options */
    while (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0') {
        const char *p = argv[0] + 1;
        
        while (*p) {
            if (*p == 'c') {
                opts.count_only = 1;
            } else if (*p == 'i') {
                opts.ignore_case = 1;
            } else if (*p == 'n') {
                opts.show_line_num = 1;
            } else if (*p == 'v') {
                opts.invert_match = 1;
            }
            p++;
        }
        argc--;
        argv++;
    }
    
    /* Get pattern */
    if (argc <= 0) {
        printf("grep: no pattern specified\n");
        return 1;
    }
    
    const char *pattern = argv[0];
    argc--;
    argv++;
    
    /* Process files */
    if (argc <= 0) {
        printf("grep: no files specified\n");
        return 1;
    }
    
    while (argc > 0) {
        if (grep_file(argv[0], pattern, &opts) != 0) {
            error = 1;
        }
        argc--;
        argv++;
    }
    
    return error;
}
