#ifndef _FMTMSG_H
#define _FMTMSG_H
#define MM_HARD 1
#define MM_SOFT 2
#define MM_FIRM 4
#define MM_APPL 8
#define MM_UTIL 16
#define MM_OPSYS 32
#define MM_RECOVER 64
#define MM_NRECOV 128
#define MM_PRINT 256
#define MM_CONSOLE 512
#define MM_NULLMC 0
#define MM_OK 0
#define MM_NOMSG 1
#define MM_NOCON 4
#define MM_NOTOK (-1)
extern int fmtmsg(long classification, const char *label, int severity, const char *text, const char *action, const char *tag);
#endif
