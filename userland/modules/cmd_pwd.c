#include <stdio.h>
#include <unistd.h>
#include <err.h>

int cmd_pwd_main(int argc, char *argv[])
{
    char buf[256];
    (void)argc; (void)argv;

    if (getcwd(buf, sizeof(buf)) == NULL) {
        warn("getcwd");
        return 1;
    }
    puts(buf);
    return 0;
}
