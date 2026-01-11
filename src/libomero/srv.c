#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <b.h>
#include <omero.h>
#include <auth.h>


typedef struct Evh	Evh;

struct Evh {
	char*		ev;
	void	(*f)(Oev*);
};

static Channel*	uieventc;
static QLock	uilock;
int	omerodebug;
char*	appluiaddress;

#define dprint if(omerodebug)fprint

Channel*
omeroeventchan(Panel* g)
{
	if (g){
		qlock(&g->lk);
		assert(!g->evc);
		g->evc = chancreate(sizeof(Oev), 5);
		qunlock(&g->lk);
		return g->evc;
	}
	qlock(&uilock);
	if (uieventc == nil)
		uieventc = chancreate(sizeof(Oev), 5);
	qunlock(&uilock);
	return uieventc;
}

static int
unpackevent(Oev* e, char* s)
{
	char*		p;
	char*		ev;
	char*		a;
	char*		name;
	char*		ns;

	p = s;
	s = strchr(s, ' ');
	if (s == nil)
		return -1;
	*s++ = 0;
	ev = s;
	a = strchr(s, ' ');
	if (a)
		*a++ = 0;
	e->ev = strdup(ev);
	e->arg = a ? strdup(a) : nil;
	ns = getns();
	e->path = smprint("%s/omero%s", ns, p);
	e->panel = nil;
	name = strrchr(e->path, '/');
	if (name){
		name++;
		e->panel = findpanel(name, 0);
	}
	return 0;
}

void
clearoev(Oev* e)
{
	if (e->panel != nil && decref(&e->panel->r) <= 0)
		if (e->panel->gone){
			dprint(2, "clearov: removepanel %s\n", e->panel->name);
			removepanel(e->panel);
		}
	e->panel = nil;
	free(e->path);
	e->path = nil;
	free(e->ev);
	e->ev = nil;
	free(e->arg);
	e->arg = nil;
}

static void

deliver(Oev* e)

{

	Oev ne;



	if (e->panel)

	if (e->panel->evc != nil || uieventc != nil){

		memset(&ne, 0, sizeof(ne));

		if (e->path)

			ne.path = strdup(e->path);

		if (e->ev)

			ne.ev = strdup(e->ev);

		if (e->arg)

			ne.arg  = strdup(e->arg);

		ne.panel = e->panel;

		if (ne.panel != nil)

			incref(&ne.panel->r);

		if (e->panel->evc)

			send(e->panel->evc, &ne);

		else

			send(uieventc, &ne);

	}

}



static int

pathnames(char mnt[], int mntsz, char sys[], int syssz, char* path)

{

	char*	ea;

	int	n;

	char*	ns;



	ns = getns();

	n = strlen(ns);

	if (strncmp(path, ns, n))

		return 0;

	if (strncmp(path+n, "/omero/", 7))

		return 0;

	

	ea = strstr(path+n+7, "ui/");

	if (!ea)

		return 0;

	

	strecpy(sys, sys+syssz, path+n+7);

	sys[ea-(path+n+7)] = 0;



	strecpy(mnt, mnt+mntsz, path);

	mnt[ea+2-(path)] = 0;



	return 1;

}



static int

mountui(char* gdir, char* oaddr)

{

	char	addr[128];

	char	sys[50];

	char	mnt[128];

	char	ctl[128];

	char	cmd[256];



	dprint(2, "checking %s for access\n", gdir);

	if (access(gdir, AEXIST) < 0){

		dprint(2, "mountui for %s (addr %s)\n", gdir, oaddr);

		if (!pathnames(mnt, sizeof(mnt), sys, sizeof(sys), gdir))

			goto fail;


		dprint(2, "mountui: sys %s mnt %s\n", sys, mnt);
		snprint(ctl, sizeof(ctl), "%s/ctl", mnt);
		if (access(ctl, AEXIST) >= 0)	// omero ok. gdir not there.
			goto fail;
		if (!oaddr) {
			seprint(addr, addr+sizeof(addr), "tcp!%s!11007", sys);
			oaddr = addr;
		}

		/* Use 9pfuse to mount */
		dprint(2, "mountui: 9pfuse %s %s\n", oaddr, mnt);
		snprint(cmd, sizeof(cmd), "mkdir -p '%s' && 9pfuse '%s' '%s'", mnt, oaddr, mnt);
		if(system(cmd) != 0){
			dprint(2, "mountui: 9pfuse failed\n");
			goto fail;
		}
		
		sleep(200);

		if (access(gdir, AEXIST) < 0){
			dprint(2, "mountui: still cannot access %s\n", gdir);
			goto fail;
		}
	}
	return 1;
fail:
	dprint(2, "cannot access: %s\n", gdir);
	werrstr("cannot access: %s\n", gdir);
	return 0;
}

static void
addrh(Oev* e)
{
	Panel* p;

	dprint(2, "addrh: %s: %s\n", e->path, e->arg);
	p = newpanel(e->path, 0);
	if (p != nil){
		if (e->panel != nil){
			assert(e->panel == p);
			decref(&p->r);
		} else
			e->panel = p;
	} else
		dprint(2, "addrh: no panel: %s\n", e->path);
	if (omerodebug > 1)
		paneldump(2);
}

static void
pathh(Oev* e)
{
	char*	to;
	char*	ns;

	if (e->panel != nil){
		ns = getns();
		to = smprint("%s/omero%s", ns, e->arg);
		if (strcmp(e->path, to)){
			movepanel(e->path, to);
			deliver(e);
		}
		free(to);
	}
}

static void
exith(Oev* e)
{
	Panel*	g;
	Repl**	l;
	Repl*	gr;
	int	nr;

	if (!e->panel)
		return;
	g = e->panel;
	qlock(&g->lk);
	for(l = &g->repl; gr = *l; l = &(*l)->next)
			if (!strcmp(gr->path, e->path))
				break;
	if (gr != nil){
		*l = gr->next;
		dprint(2, "exith rmrepl: %s: %r\n", gr->path);
		g->nrepl--;
		if (gr->fd[0] >= 0)
			close(gr->fd[0]);
		if (gr->fd[1] >= 0)
			close(gr->fd[1]);
		free(gr->path);
		free(gr);
	}
	nr = g->nrepl;
	qunlock(&g->lk);
	if (nr == 0)
		deliver(e);
}

static void
datah(Oev* e)
{
	Repl*	r;

	if (e->panel){
		r = findrepl(e->panel, e->path, 0);
		rpaneldata(e->panel, r);
		deliver(e);
	} else
		dprint(2, "datah: no panel\n");
}

static void
txth(Oev* e)
{
	char*	s;
	Repl*	r;

	if (e->panel && e->panel->nrepl > 1){
		s = smprint("%s %s\n", e->ev, e->arg);
		r = findrepl(e->panel, e->path, 0);
		wpanelexcl(e->panel, "ctl", s, strlen(s), r);
		free(s);
	}
	if (strcmp(e->ev, "dirty") == 0 || strcmp(e->ev, "clean") == 0)
		deliver(e);
}

static Evh evs[] = {
	{ "addr",	addrh },
	{ "path",	pathh },
	{ "exit",	exith },

	{ "data",	datah },	// Used for replication
	{ "ins",	txth },		// Could think of a better way
	{ "del",	txth },
	{ "dirty",	txth },
	{ "clean",	txth },
};

static Lock plock;
static int procs[100];
static int nprocs;

static void
addproc(int pid)
{
	int	i;

	lock(&plock);
	for (i = 0; i < nelem(procs); i++)
		if (!procs[i]){
			procs[i] = pid;
			break;
		}
	if (i == nprocs)
		nprocs++;
	unlock(&plock);
}

static int
delproc(int pid)
{
	int	i;

	lock(&plock);
	for (i = 0; i < nelem(procs); i++)
		if (procs[i] == pid){
			procs[i] = 0;
			break;
		}
	if (i + 1 == nprocs)
		while(--nprocs > 0 && !procs[nprocs-1])
			;
	unlock(&plock);
	return nprocs;
}

static void
kill(int pid)
{
	char	fn[40];


seprint(fn, fn+40, "/proc/%d/ctl", pid);
	writefstr(fn, "kill");
}

void
omeroterm(void)
{
	int	i;
	Oev	e;

	lock(&plock);
	for (i = 0; i < nelem(procs); i++)
		if (procs[i]){
			kill(procs[i]);
			procs[i] = 0;
		}
	nprocs = 0;
	unlock(&plock);
	if (uieventc){
		memset(&e, 0, sizeof(e));
		e.ev = strdup("exit");
		send(uieventc, &e);
	}
}

extern int omerogone(void);

static void
eventproc(void*a)
{
	int	fd;
	Oev	e;
	int	i;
	Biobuf	bin;
	char*	ln;
	int	pid;
	int	np;
	int	mounted;
	char*	s;

	threadsetname("eventproc");
	fd = (int)(uintptr)a;
	memset(&e, 0, sizeof(e));
	pid = getpid();
	addproc(pid);
	Binit(&bin, fd, OREAD);
	mounted = 0;
	while(ln = Brdstr(&bin, '\001', 1)){
		if (!strcmp(ln, "bye"))
			break;
		if (unpackevent(&e, ln) < 0){
			clearoev(&e);
			free(ln);
			continue;
		}
		if (!mounted){
			s = strchr(e.arg, ' ');
			if (s)
				s++;
			if (!mountui(e.path, s)){
				fprint(2, "mounting omero %s: %r\n", e.path);
				free(ln);
				break;
			} else
				mounted = 1;
		}
		for(i = 0; i < nelem(evs); i++)
			if (!strcmp(e.ev, evs[i].ev)){
				evs[i].f(&e);
				break;
			}
		if (e.panel && i == nelem(evs))
			deliver(&e);
		free(ln);
		clearoev(&e);
	}
	clearoev(&e);
	Bterm(&bin);
	np = delproc(pid);
	if (np == 1 && omerogone()){
		assert(procs[0]);
		kill(procs[0]);
	}
	threadexits(nil);
}


static void
srvproc(void* a)
{
	ulong id = (ulong)(uintptr)a;
	int	afd, lfd;
	char	adir[40];
	char	ldir[40];
	int	dfd;
	NetConnInfo* ni;
	int	pid;

	threadsetname("uiproc");
	pid = getpid();
	addproc(pid);
	afd = announce("tcp!*!0", adir);
	if (afd < 0)
		sysfatal("can't announce: %r");
	ni = getnetconninfo(adir, afd);
	if (ni == nil)
		sysfatal("can't get conninfo");
	rendezvous((ulong)id, (ulong)atoi(ni->lserv));
	freenetconninfo(ni);
	for(;;){
		lfd = listen(adir, ldir);
		if (lfd < 0)
			sysfatal("can't listen: %r");
		dfd = accept(lfd, ldir);
		close(lfd);
		proccreate(eventproc, (void*)(uintptr)dfd, 256*1024);
	}
}

static char*
setupdir(char* gdir)
{
	static char* omero = nil;
	char *ns;

	if (gdir == nil){
		if (omero == nil)
			omero = getenv("omero");
		if (omero == nil){
			ns = getns();
			omero = smprint("%s/omero/%sui/row:wins/col:1", ns, sysname());
		}
		gdir = omero;
	}
	if (gdir != nil && !mountui(gdir, nil))
		return nil;
	return gdir;
}

Panel*
createpanel(char* name, char* type, char* omero)
{
	static int	initted;
	static long	port;
	ulong	id;
	char*	path;
	Panel*	g;

	assert(type);
	omero = setupdir(omero);
	if (omero == nil)
		return nil;
	if (!initted++){
		id = getpid();
		proccreate(srvproc, (void*)(uintptr)id, 256*1024);
		port = (long)rendezvous((ulong)id, 0);
		appluiaddress = smprint("tcp!%s!%ld", sysname(), port);
	} else
		while(port == 0){
			yield();
			sleep(10);
		}
	path = smprint("%s/%s:%s.%uld", omero, type, name, truerand()%10000);
	g = mkpanel(path);
	if (g){
		openpanelctl(g);
		panelctl(g, "hold\naddr %s", appluiaddress);
	}
	free(path);
	return g;
}