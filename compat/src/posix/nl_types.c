#include <nl_types.h>

nl_catd catopen(const char *name, int flag) {
    (void)name;
    (void)flag;
    return (nl_catd)-1;
}

char *catgets(nl_catd catd, int set_id, int msg_id, const char *s) {
    (void)catd;
    (void)set_id;
    (void)msg_id;
    return (char *)s;
}

int catclose(nl_catd catd) {
    (void)catd;
    return 0;
}
