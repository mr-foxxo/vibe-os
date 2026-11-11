#ifndef SETJMP_H
#define SETJMP_H
#include <sys/types.h>
typedef long jmp_buf[10];
typedef struct { jmp_buf jb; int saved_mask; } sigjmp_buf[11];
extern int setjmp(jmp_buf);
extern void longjmp(jmp_buf, int);
extern int sigsetjmp(sigjmp_buf, int);
extern void siglongjmp(sigjmp_buf, int);
#endif
