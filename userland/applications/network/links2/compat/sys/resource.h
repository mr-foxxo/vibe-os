#ifndef SYS_RESOURCE_H
#define SYS_RESOURCE_H
struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
};
extern int getrusage(int, struct rusage *);
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN -1
#endif
