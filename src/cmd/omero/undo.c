#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <draw.h>
#include <mouse.h>
#include <cursor.h>
#include <keyboard.h>
#include <frame.h>
#include <9p.h>
#include <ctype.h>
#include "gui.h"

int textdebug;

static void
dumpop(char* pref, Edit* e)
{
	int	nr;

	nr = e->nr > 3 ? 3 : e->nr;
	fprint(2, "%s%c%s %3d #%d\t [%.*S]\n", pref,
		e->end ? '*' : ' ',
		e->op == Ins ? "ins" : "del",
		e->pos, e->nr, nr, e->r);
}

static void
dumpedits(Panel* p)
{
	Edit*	e;

	fprint(2, "%s edits:\n", p->name);
	for (e = p->frame.undos; e && e < p->frame.undo; e++)
		dumpop("\t", e);
	fprint(2, "\n");
}

Edit*
addedit(Panel* p, int op, Rune* r, int nr, int pos)
{
	Edit*	e;
	int	nlen;
	Edit*	last;

	/* merge to succesive inserts into a single one, if feasible.
	 */
	if (op == Ins && p->frame.undos && p->frame.undo > p->frame.undos){
		last = p->frame.undo - 1;
		if (last->op == Ins && !last->end && pos == last->pos + last->nr){
			nlen = (nr + last->nr) * sizeof(Rune);
			last->r = erealloc9p(last->r, nlen);
			memmove(last->r + last->nr, r, nr*sizeof(Rune));
			last->nr += nr;
			return last;
		}
	}
	if (p->frame.undo == p->frame.undos+p->frame.nundos){
		if (!(p->frame.nundos%16))
			p->frame.undos = erealloc9p(p->frame.undos, (p->frame.nundos+16)*sizeof(Edit));
		e = p->frame.undos + p->frame.nundos++;
		p->frame.undo = p->frame.undos + p->frame.nundos;
	} else {
		assert(p->frame.undo && p->frame.undo < p->frame.undos+p->frame.nundos);
		e = p->frame.undo++;
		free(e->r);
	}
	e->r = emalloc9p(nr * sizeof(Rune));
	memmove(e->r, r, nr*sizeof(Rune));
	e->nr = nr;
	e->pos= pos;
	e->op= op;
	e->end = 0;
	if (textdebug)
		dumpedits(p);
	return e;
}


void
newedit(Panel* p)
{
	Edit*	e;

	if (p->frame.undo && p->frame.undo > p->frame.undos){
		e = p->frame.undo-1;
		e->end = 1;
	}
}

int
hasedits(Panel* p)
{
	if (!p->frame.undo)
		return 0;
	return (p->frame.undo - p->frame.undos) != p->frame.cundo;
}

void
setnoedits(Panel* p)
{
	if (!p->frame.undo)
		p->frame.cundo = 0;
	else
		p->frame.cundo = p->frame.undo - p->frame.undos;
}

void
cleanedits(Panel* p)
{
	int	i;

	for (i = 0; i < p->frame.nundos; i++){
		free(p->frame.undos[i].r);
	}
	free(p->frame.undos);
	p->frame.undos = p->frame.undo = nil;
	p->frame.nundos = 0;
	p->frame.cundo = 0;
	if (textdebug)
		dumpedits(p);
}


static void
undo1(Panel* p)
{
	p->frame.undo--;
	if (p->frame.undo->op == Ins){
		textdel(p, p->frame.undo->r, p->frame.undo->nr, p->frame.undo->pos);
		fillframe(p);
	} else
		textins(p, p->frame.undo->r, p->frame.undo->nr, p->frame.undo->pos);
}

int
undo(Panel* p)
{
	Edit*	e;
	int	some;

	some = 0;
	while(p->frame.undo && p->frame.undo > p->frame.undos){
		undo1(p);
		some++;
		if (p->frame.undo > p->frame.undos){
			e = p->frame.undo - 1;
			if (e->end)
				break;
		}
	}
	return some;
}

static void
redo1(Panel* p)
{
	if (p->frame.undo->op == Ins)
		textins(p, p->frame.undo->r, p->frame.undo->nr, p->frame.undo->pos);
	else {
		textdel(p, p->frame.undo->r, p->frame.undo->nr, p->frame.undo->pos);
		fillframe(p);
	}
	p->frame.undo++;
}

int
redo(Panel* p)
{
	Edit*	e;
	int	last;
	int	some;

	e = p->frame.undos + p->frame.nundos;
	for(last = some = 0; !last && p->frame.undo && p->frame.undo < e; some++){
		last = p->frame.undo->end;
		redo1(p);
	}
	return some;
}
