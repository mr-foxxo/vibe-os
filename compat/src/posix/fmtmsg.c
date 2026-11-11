#include <fmtmsg.h>
int fmtmsg(long classification, const char *label, int severity, const char *text, const char *action, const char *tag) {
    (void)classification; (void)label; (void)severity; (void)text; (void)action; (void)tag;
    return MM_OK;
}
