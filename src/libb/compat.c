#include <u.h>
#include <libc.h>
#include <thread.h>

void**
privalloc(void)
{
	return threaddata();
}

typedef struct Rdez Rdez;
struct Rdez {
	ulong tag;
	ulong val;
	Channel *c;
	Rdez *next;
};

static Rdez *rdez_list;
static QLock rdez_lock;

ulong
rendezvous(ulong tag, ulong val)
{
	Rdez *r, **pp;
	ulong ret;
	Channel *c;

	qlock(&rdez_lock);
	for (pp = &rdez_list; *pp; pp = &(*pp)->next) {
		if ((*pp)->tag == tag) {
			r = *pp;
			*pp = r->next;
			qunlock(&rdez_lock);

			c = r->c;
			r->val = val;
			sendp(c, r);
			recvp(c);
			ret = r->val;
			free(r);
			chanfree(c);
			return ret;
		}
	}

	r = mallocz(sizeof(Rdez), 1);
	if(r == nil)
		sysfatal("rendezvous: malloc failed");
	r->tag = tag;
	r->val = val;
	r->c = chancreate(sizeof(void*), 1);
	r->next = rdez_list;
	rdez_list = r;
	c = r->c;
	qunlock(&rdez_lock);

	Rdez *rb = recvp(c);
	if(rb == nil)
		sysfatal("rendezvous: recvp failed");
	ret = rb->val;
	rb->val = val;
	sendp(c, rb);
	
	return ret;
}

int
procrfork(void (*fn)(void*), void *arg, long stack, int flags)
{
	if (flags & RFPROC) {
		int pid;
		pid = rfork(flags);
		if (pid < 0)
			return -1;
		if (pid == 0) {
			fn(arg);
			_exit(0);
		}
		return pid;
	} else
		return proccreate(fn, arg, stack);
}

int
procexec(Channel *pidc, char *prog, char **argv)
{
	if (pidc)
		sendul(pidc, getpid());
	exec(prog, argv);
	return -1;
}

int
procexecl(Channel *pidc, char *prog, ...)
{
	va_list arg;
	char **argv;
	int n, i;

	va_start(arg, prog);
	for(n=0; va_arg(arg, char*); n++)
		;
	va_end(arg);

	argv = malloc((n+1)*sizeof(char*));
	if(argv == nil)
		return -1;

	va_start(arg, prog);
	for(i=0; i<n; i++)
		argv[i] = va_arg(arg, char*);
	argv[n] = nil;
	va_end(arg);

	procexec(pidc, prog, argv);
	free(argv);
	return -1;
}

int
amount(int fd, char *spec, int flags, char *aname)
{
	USED(fd);
	USED(spec);
	USED(flags);
	USED(aname);
	return 0;
}

int
ctlkeyboard(void *kc, char *s)
{
	USED(kc);
	USED(s);
	return 0;
}