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

int framedebug;

void
framedump(Panel* p)
{
	if (!framedebug)
		return;
	print("froff %d chr %d ln %d max %d, llf %d full %d\n",
		p->frame.froff, p->frame.f.nchars, p->frame.f.nlines,
		p->frame.f.maxlines, p->frame.f.lastlinefull, p->frame.f.lastlinefull);
}

/*
 * The routines below update both the Tblock and the Frame,
 * they do nothing else. No events, no resizes, no flushimages.
 */

void
fillframe(Panel* p)
{
	Tblock*	b;
	int	n;
	int	pos;
	Point	pt;
	Rectangle	r;

	if (p->frame.fimage == nil || p->frame.f.lastlinefull)
		return;

	b = blockseek(p->frame.blks, &n, p->frame.froff + p->frame.f.nchars);
	
	for(; b && !p->frame.f.lastlinefull; b = b->next){
		pos = p->frame.f.nchars;
		if (b->nr){
			frinsert(&p->frame.f, b->r+n, b->r+b->nr, pos);
		}
		n = 0;
	}
	pt = frptofchar(&p->frame.f, p->frame.f.nchars);
	r = p->frame.fimage->r;
	r.min.y = pt.y;
	if(pt.x > r.min.x) {
		Rectangle r1 = r;
		r1.min.x = pt.x;
		r1.max.y = pt.y + p->font->height;
		draw(p->frame.fimage, r1, cols[BACK], nil, ZP);
		r.min.y += p->font->height;
	}
	draw(p->frame.fimage, r, cols[BACK], nil, ZP);
}

static void
newsel(Frame* f, int p0, int p1, int showtick)
{
	int	e;

	if (showtick)
		frdrawsel(f, frptofchar(f, f->p0), f->p0, f->p1, 0);
	if (!f->lastlinefull || !f->nchars)
		e = f->nchars;
	else
		e = f->nchars - 1;
	if (p0 > e)
		p0 = e;
	if (p1 > e)
		p1 = e;
	if (p0 < 0)
		p0 = 0;
	if (p1 < 0)
		p1 = 0;
	f->p0 = p0;
	f->p1 = p1;
	if (showtick)
		frdrawsel(f, frptofchar(f, f->p0), f->p0, f->p1, 1);
}

void
reloadframe(Panel* p, int resized)
{
	Rectangle r;

	if (p->frame.fimage == nil)
		return;
	r = Rpt(ZP, Pt(Dx(p->rect), Dy(p->rect)));
	frclear(&p->frame.f, resized);
	if (resized){
		freeimage(p->frame.fimage);
		p->frame.fimage = allocimage(display, r, screen->chan, 0, CBack);
		if (p->frame.fimage == nil)
			sysfatal("reloadframe: allocimage failed");
		frinit(&p->frame.f, r, p->font, p->frame.fimage, cols);
	}
	fillframe(p);
	newsel(&p->frame.f, p->frame.ss - p->frame.froff, p->frame.se - p->frame.froff, (p->flags&Pedit));
}

void
setframefont(Panel* p, Font* f)
{
	if (f != p->font){
		p->font = f;
		reloadframe(p, 0);
	}
}

/* Returns 0 if size did not change.
 * Returns 1 if #rows/#cols change
 * Returns 2 if we want a resize
 */
int
setframesizes(Panel* p)
{
	int	oncols, onrows;
	int	res;
	Point	omin;
	int	n;

	res = p->maxsz.y = 0;
	if (p->flags&Pline){
		omin = p->minsz;
		packblock(p->frame.blks);
		p->minsz.y = fontheight(p->font);
		if (!(p->flags&Pedit) && p->frame.blks->nr > 0){
			n = runestringnwidth(p->font, p->frame.blks->r, p->frame.blks->nr);
			p->minsz.x = n;
		}
		res = !eqpt(omin, p->minsz);
	} else {
		p->minsz = Pt(p->font->height*2,p->font->height*2);
		if (p->frame.nlines == 0)
			p->maxsz.y = 0;
		else {
			n = p->frame.nlines + 1;
			if (n < 3)
				n = 3;
			p->maxsz.y =  p->font->height * n;
		}
	}
	oncols = p->frame.ncols;
	onrows = p->frame.nrows;
	p->frame.ncols = gettextwid(p);
	p->frame.nrows = gettextht(p);
	fdprint("setframesizes: %s: sz %P min %P max [0 %d] txt %dx%d wx %d wy %d\n",
		p->name, p->size, p->minsz, p->maxsz.y, p->frame.ncols, p->frame.nrows,
		p->wants.x, p->wants.y);
	if (!res)
		res =  oncols != p->frame.ncols || onrows != p->frame.nrows;
	return res;
}

void
setframesel(Panel* p, int ss, int se, int setmpos)
{
	int	sw;

	if (ss == se){
		p->frame.sdir = 0;
		p->frame.s0 = ss;
	}
	if (ss > se){
		sw = ss;
		ss = se;
		se = sw;
	}
	if (setmpos)
		p->frame.mpos = p->frame.s0;
	if (ss < p->frame.s0)
		p->frame.sdir = -1;
	if (se > p->frame.s0)
		p->frame.sdir = 1;
	p->frame.ss = ss;
	p->frame.se = se;
	if (p->frame.fimage != nil)
		newsel(&p->frame.f, p->frame.ss-p->frame.froff, p->frame.se-p->frame.froff, (p->flags&Pedit));
}

void
addframesel(Panel* p, int pos)
{
	setframesel(p, p->frame.s0, pos, 0);
}


void
jumpframepos(Panel* p, int pos)
{
	if (p->frame.fimage)
	if (pos < p->frame.froff || pos >= p->frame.froff + p->frame.f.nchars){
		p->frame.froff = pos - 10;
		if (p->frame.froff < 0)
			p->frame.froff = 0;
		else if (p->frame.froff > 0)
			scrollframe(p, -1);
		else
			reloadframe(p, 0);
	}
}

void
jumpframesel(Panel* p)
{
	jumpframepos(p, p->frame.ss);
}

void
setframemark(Panel* p, int pos)
{
	int	l;

	l = blocklen(p->frame.blks);
	p->frame.mark = pos;
	if (p->frame.mark < 0)
		p->frame.mark = 0;
	if (p->frame.mark > l)
		p->frame.mark = l;
}

int
framehassel(Panel* p)
{
	return p->frame.sdir;
}

int
posselcmp(Panel* p, int pos)
{
	if (pos < p->frame.ss)
		return -1;
	if (pos >= p->frame.se)
		return 1;
	return 0;
}


int
findln(Tblock* b, int* pp)
{
	int	i;
	int	pos;

	pos = *pp;
	for(i = 0; i < 128 && pos < b->nr; i++, pos++)
		if (b->r[pos] == '\n')
			break;
	*pp = pos;
	return (i == 128 || b->r[pos] == '\n');
}

int
findrln(Tblock* b, int* pp)
{
	int	i;
	int	pos;

	pos = *pp;
	for(i = 0; i < 128 && pos > 0; i++, pos--)
		if (b->r[pos] == '\n')
			break;
	*pp = pos;
	return (i == 128 || b->r[pos] == '\n' || pos == 0);
}

int
scrollframe(Panel* p, int nscroll)
{
	Tblock*	b;
	int	n;
	int	pos;
	int	nlines;
	int	l;

	fdprint("scroll %d\n", nscroll);
	nlines = abs(nscroll);

	assert(p->frame.froff >= 0);
	if (nscroll == 0)
		return 0;
	packblock(p->frame.blks);
	b = p->frame.blks;
	if (nscroll > 0) {
		l = blocklen(b);
		if (p->frame.froff + p->frame.f.nchars >= l)
			return 0;
		n = p->frame.froff;
		while(nlines-- && findln(b, &n))
			if (n < b->nr)
				n++;
	} else {
		if (p->frame.froff <= 0){
			if (blockdebug)
				blockdump(b);
			return 0;
		}
		n = p->frame.froff;
		while(nlines-- && findrln(b, &n))
			if (n > 0)
				n--;
		if (n > 0 && n < b->nr) // advance to skip last \n
			n++;
	}
	if (!b)
		return 0;
	pos = n;
	if (b->r[pos] == '\n')
		pos++;
	frclear(&p->frame.f, 0);
	p->frame.froff = pos;
	reloadframe(p, 0);

	newsel(&p->frame.f, p->frame.ss - p->frame.froff, p->frame.se - p->frame.froff, (p->flags&Pedit));
	framedump(p);
	return 1;
}


int
frameins(Panel* p, Rune* r, int nr, int pos)
{
	Tblock*	b;
	int	n;
	int	old;
	int	full;

	assert(pos <= blocklen(p->frame.blks));
	fdprint("ins %d (p0 %uld) %d runes [%.*S]\n", pos, p->frame.f.p0, nr, nr, r);
	b = blockseek(p->frame.blks, &n, pos);
	blockins(b, n, r, nr);
	p->dfile->dir.length += runenlen(r, nr);

	full = 0;
	if (p->frame.fimage != nil)
		if (pos >= p->frame.froff){
			old = p->frame.f.nchars;
			frinsert(&p->frame.f, r, r +nr, pos - p->frame.froff);
			fdprint("old %d nchars %d\n", old, p->frame.f.nchars);
			if (old == p->frame.f.nchars)
				full = 1;
		} else
			p->frame.froff += nr;
	if (pos < p->frame.ss)
		p->frame.ss += nr;
	if (pos < p->frame.se)
		p->frame.se += nr;
	if (pos < p->frame.s0)
		p->frame.s0 += nr;
	if (pos < p->frame.mark)
		p->frame.mark += nr;
	return full;
}

static int
fixpos(int pos, int x, int n)
{
	if (x < pos){
		if (x + n > pos)
			n = pos - x;
		pos -= n;
	}
	return pos;
}

int
framedel(Panel* p, Rune* r, int nr, int pos)
{
	Tblock*	b;
	int	n;
	Rune	nl[1];

	nl[0] = L'\n';
	assert(r);
	b = blockseek(p->frame.blks, &n, pos);
	if (b == nil)
		return 0;
	blockdel(b, n, nr, r);
	p->dfile->dir.length -= runenlen(r, nr);
	if (pos >= p->frame.froff){
		if (p->frame.fimage != nil){
			frdelete(&p->frame.f, pos - p->frame.froff, pos - p->frame.froff + nr);
			/* Deleting on the last line leaves part of the tick
			 * in the screen. The rune is blanked but not the tick.
			 * We add a fake \n for a while just to clear the line.
			 * To me, it seems that it should be libframe the one
			 * clearing the right part of the last line. BUG?
			 */
			if (!p->frame.f.lastlinefull){
				frinsert(&p->frame.f, nl, nl+1, p->frame.f.nchars);
				frdelete(&p->frame.f, p->frame.f.nchars-1, p->frame.f.nchars);
			}
		}
	} else
		p->frame.froff = fixpos(p->frame.froff, pos, nr);
	// BUG: cut with a mark in the middle;
	p->frame.ss = fixpos(p->frame.ss, pos, nr);
	p->frame.se = fixpos(p->frame.se, pos, nr);
	p->frame.s0 = fixpos(p->frame.s0, pos, nr);
	p->frame.mark = fixpos(p->frame.mark, pos, nr);
	return nr;
}

static int
iswordchar(Rune r)
{
	return isalpharune(r) || runestrchr((Rune*)L"0123456789|&?=._-+/:", r);
}

static wchar_t lparen[] = L"{[(«<“";
static wchar_t rparen[] = L"}])»>”";
static wchar_t paren[] = L"\"'`";

static int
isparen(Rune* set, Rune r)
{
	Rune* p;
	p = runestrchr(set, r);
	if (p)
		return p - set + 1;
	else
		return 0;
}

/* Returns the word at pos.

 * The word is the selection when it exists.
 * It is the longest set of <wordchar>s if pos at <wordchar>
 * It is the text between {} [] '' "" () if pos is at delim.
 * It is the current line otherwise (if pos at blank)
 */
Rune* 
framegetword(Panel* p, int pos, int* ss, int* se)
{
	Rune*	r;
	Tblock*	b;
	int	spos, epos;
	int	nr;
	int	pi;
	int	nparen;

	b = p->frame.blks;
	packblock(b);
	assert(pos <= b->nr);
	spos = epos = pos;
	if (b->nr > 0)
	if (framehassel(p) && !posselcmp(p, pos)){
		spos = p->frame.ss;
		epos = p->frame.se;
	} else if (iswordchar(b->r[pos])){
		while(spos > 0 && iswordchar(b->r[spos]))
			spos--;
		if (spos > 0)
			spos++;
		while(epos < b->nr && iswordchar(b->r[epos]))
			epos++;
	} else if (pi = isparen((Rune*)paren, b->r[pos])){
		spos++;
		for(epos = spos; epos < b->nr; epos++)
			if (isparen((Rune*)paren, b->r[epos]) == pi)
				break;
	} else if (pi = isparen((Rune*)lparen, b->r[pos])){
		nparen = 1;
		spos++;
		for(epos = spos; epos < b->nr; epos++){
			if (isparen((Rune*)lparen, b->r[epos]) == pi)
				nparen++;
			if (isparen((Rune*)rparen, b->r[epos]) == pi)
				nparen--;
			if (nparen <= 0){
				break;
			}
		}
	} else if (pi = isparen((Rune*)rparen, b->r[pos])){
		nparen = 1;
		if (spos > 0)
		for(spos--; spos > 0; spos--){
			if (isparen((Rune*)rparen, b->r[spos]) == pi)
				nparen++;
			if (isparen((Rune*)lparen, b->r[spos]) == pi)
				nparen--;
			if (nparen <= 0){
				spos++;
				break;
			}
		}
	} else { // pos at blank
		if (b->r[spos] == '\n' && spos > 0 && b->r[spos-1] != '\n'){
			// click at right part of line; step back
			// so that expanding leads to previous line
			spos--;
		}
		while(spos > 0 && b->r[spos-1] != '\n')
			spos--;
		while(epos < b->nr && b->r[epos] != '\n')
			epos++;
		if (epos < b->nr)
			epos++;	// include \n
	}
	if (epos > b->nr)
		fprint(2, "epos bug: %d %d %d\n", spos, epos, b->nr);
	assert(spos >= 0);
	assert(epos >= 0);
	assert(epos >= spos);
	assert(epos <= b->nr);
	if (ss){
		*ss = spos;
		*se = epos;
	}
	nr = epos - spos;
	r = emalloc9p((nr  + 1) * sizeof(Rune));
	if (nr > 0)
		blockget(b, spos, nr, r);
	r[nr] = 0;
	edprint("framegetword %.*S %d %d\n", nr, r, spos, epos);
	return r;
}
