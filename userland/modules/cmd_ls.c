#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

int cmd_ls_main(int argc, char *argv[])
{
    DIR *dirp;
    struct dirent *dp;
    const char *path;
    int i;
    int show_all = 0;
    int longfmt = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            const char *p = argv[i] + 1;
            while (*p) {
                if (*p == 'a') show_all = 1;
                if (*p == 'l') longfmt = 1;
                p++;
            }
        }
    }

    path = (argc > 1 && argv[argc - 1][0] != '-') ? argv[argc - 1] : ".";
    dirp = opendir(path);
    if (dirp == NULL) {
        warn("%s", path);
        return 1;
    }
    while ((dp = readdir(dirp)) != NULL) {
        if (!show_all && dp->d_name[0] == '.')
            continue;
        puts(dp->d_name);
    }
    closedir(dirp);
    return 0;
}
