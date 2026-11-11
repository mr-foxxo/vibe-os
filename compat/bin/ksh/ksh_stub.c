int ksh_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    extern int busybox_main(int, char **);
    char *args[] = {"ksh", "-i", NULL};
    return busybox_main(2, args);
}
