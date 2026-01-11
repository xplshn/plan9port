#include <thread.h>
#pragma	lib	"libb.a"
#pragma	src	"/sys/src/libb"

extern	void*	readf(char*f, void* buf, long l, long *ol);
extern	char*	readfstr(char*f);
extern	long	writef(char* f, void* buf, long len);
extern	long	writefstr(char* f, char* s);
extern	long	createf(char* f, void* buf, long len, ulong mode);
extern	int	announcevol(int afd, char* addr, char* name, char* cnstr);
extern	long	cmdoutput(char* cmd, char* out, long sz);
extern	long	tcmdoutput(char* cmd, char* out, long sz);

int procrfork(void (*fn)(void*), void *arg, long stack, int flags);
int procexec(Channel *pidc, char *prog, char **argv);
int procexecl(Channel *pidc, char *prog, ...);
