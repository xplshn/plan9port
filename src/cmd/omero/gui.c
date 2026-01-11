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
#include "gui.h"
#include "cook.h"

Image*	cols[MAXCOL];
Image*	bord[NBORD];
Font*	fonts[NFONT];

static Cursor whitearrow = {
	{0, 0},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFC, 
	 0xFF, 0xF0, 0xFF, 0xF0, 0xFF, 0xF8, 0xFF, 0xFC, 
	 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFC, 
	 0xF3, 0xF8, 0xF1, 0xF0, 0xE0, 0xE0, 0xC0, 0x40, },
	{0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x06, 0xC0, 0x1C, 
	 0xC0, 0x30, 0xC0, 0x30, 0xC0, 0x38, 0xC0, 0x1C, 
	 0xC0, 0x0E, 0xC0, 0x07, 0xCE, 0x0E, 0xDF, 0x1C, 
	 0xD3, 0xB8, 0xF1, 0xF0, 0xE0, 0xE0, 0xC0, 0x40, }
};

Cursor nocursor; 

Point	lastxy;	// click to type
static	Mousectl* mctl;
File*	focus;

Channel*resizec;
int 	eventdebug;

void
incwin(File* f)
{
	Panel*	p;

	p = f->aux;
	if (p->colrow.nwins == 0)
		flagsons(f, Phide, 0, ++p->colrow.nwins, 1024);
	else
		flagsons(f, 0, Phide, p->colrow.nwins, p->colrow.nwins); p->colrow.nwins++;
	if (hassons(f, Phide))
		p->flags |= Pmore;
	else {
		p->colrow.nwins = 0;
		p->flags &= ~Pmore;
	}
}

void
minwin(File* f)
{
	Panel*	p;

	p = f->aux;
	p->colrow.nwins = 0;
	incwin(f);
}

void
fullwin(File* f)
{
	Panel*	p;

	p = f->aux;
	p->colrow.nwins = 0;
	p->flags |= Predraw;
	flagsons(f, 0, Phide, 0, 1024);
	if (p->flags&Pmore)
		p->flags &= ~Pmore;
}

void
maxwin(File* f)
{
	Panel*	p;

	if (f->parent != f){
		p = f->parent->aux;
		p->flags |= Predraw;
		flagothersons(f->parent, Phide, 0, f);
		if (hassons(f->parent, Phide))
			p->flags |= Pmore;
		else {
			p->colrow.nwins = 0;
			p->flags &= ~Pmore;
		}
	}
}

static void
setatime(File* f)
{
	long	now;
	Panel*	p;

	now = time(nil);
	for(;;){
		p = f->aux;
		p->atime = now;
		if (p->flags&Ptop)
			break;
		if (f == f->parent)
			return;
		f = f->parent;
	}
}

void
genkeyboard(Panel* p, Rune r)
{
	if (r == Kdel)
		event(p, "interrupt");
	else if (p->flags&Pedit)
		event(p, "keys %C", r);
}

static void
hidecursor(void)
{
	setcursor(mctl,&nocursor);
}

void
argcursor(int yes)
{
	setcursor(mctl, yes ? &whitearrow : nil);
}


static void
redraw(void)
{
	showtree(slash, 0);
	flushimage(display, 1);
}

Channel* sc;
static void
putscreenproc(void*)
{
	while(recvul(sc) > 0){
		while(nbrecvul(sc))
			;
		flushimage(display, 1);
	}
}


void
putscreen(void)
{
	if (sc == nil){
		sc = chancreate(sizeof(ulong), 8);
		proccreate(putscreenproc, nil, 256*1024);
	}
	sendul(sc, 1);
}

void
resize(void)
{
	sendul(resizec, 1);
}


static void
resizethread(void* arg)
{
	int	fd;
	ulong	dummy;
	Alt	a[] = {
		{arg, &dummy, CHANRCV},
		{resizec, &dummy, CHANRCV},
		{nil, nil, CHANEND}};

		threadsetname("resizethread");

		for(;;){

			switch(alt(a)){

			case 0:

				if (getwindow(display, Refnone) <= 0){

					fprint(2, "omero: getwindow failed: %r\n");

				}

				/* fall through */

			case 1:

				layout(slash);

				showtree(slash, 1);

				flushimage(display, 1);

				break;

			}

		}

	}

static char* terms[5];

void
setterm(char* term)
{
	int	i;

	for (i = 0; i < nelem(terms); i++)
		if (terms[i] == nil){
			terms[i] = smprint("call %s", term);
			return;
		}
}


static void
setfocus(File *f)
{
	if(f == focus)
		return;
	if(focus)
		closefile(focus);
	focus = f;
	if(focus)
		incref(&focus->ref);
}

static void
useriothread(void*)
{
	Cmouse  m;
	Channel*mc;
	Panel*  p;
	Keyboardctl*kctl;
	int     hide;
	File*   f;
	int     fd, n;
	Rune    r;
	Alt     a[] = {
		{nil, &m, CHANRCV},
		{nil, &r, CHANRCV},
		{destroyc, &p, CHANRCV},
		{nil, nil, CHANEND}};

	threadsetname("useriothread");
	mc = cookmouse(mctl->c);
	if (mc == nil)
		sysfatal("cookmouse failed");
	kctl = initkeyboard(nil);
	if (kctl == nil)
		sysfatal("initkeyboard failed");
	a[0].c = mc;
	a[1].c = kctl->c;
	setfocus(nil);
	hide = 0;
	for(;;){
		switch(alt(a)){
		case 0:
			if (hide){
				hide = 0;
				argcursor(0);
			}
			f = pointinpanel(m.mouse.xy, 1);
			if (f == nil)
				continue;
			setfocus(f);
			lastxy = m.mouse.xy;
			p = f->aux;
			if (!p || (p->flags&Pdead)){
				setfocus(nil);
				continue;
			}
			panelok(p);
			if (m.mouse.buttons){
				setatime(f);
				if (mousecmdarg(f, &m, mc))
					argcursor(0);
				else if (hastag(f) && intag(p, m.mouse.xy))
					tagmousecmd(f, &m, mc);
				else
					panels[p->type]->mouse(p, &m, mc);
			}
			break;
		case 1:
			if ((r&KF) == KF){
				n = (r & ~KF) - 1;
				if (n >= 0 && n < nelem(terms) && terms[n]){
					fd = open("/dev/mousectl", OWRITE);
					if(fd >= 0){
						write(fd, terms[n], strlen(terms[n]));
						close(fd);
					}
					continue;
				}
			}
			if (r == Kup || r == Kdown || focus == nil)
				setfocus(pointinpanel(lastxy, 1));
			if (focus == nil)
				continue;
			f = focus;
			if (!hide){
				hide = 1;
				hidecursor();
			}
			p = f->aux;
			if (!p || (p->flags&Pdead)){
				setfocus(nil);
				continue;
			}
			setatime(f);
			do {
				if (f->parent == nil || f->aux == nil)
					break;
				panelok(p);
				panels[p->type]->keyboard(p, r);
			} while (nbrecv(kctl->c,&r) > 0 && focus == f && !(p->flags&Pdead));
			break;
		case 2:
			if(focus && focus->aux == p)
				setfocus(nil);
			panelok(p);
			closepanel(p);
			resize();
			break;
		}
	}
}

/* R and B fonts may vary slightly. Adjust them to
 * the same height, so that we do not resize because
 * of R/B/T <-> R/B/T changes
 */
static int maxfontht;

int
fontheight(Font* f)
{
	if (f == fonts[FL])
		return f->height;
	else
		return maxfontht;
}

static void
loadfonts(void)
{
	if (smallomero){
		fonts[FR] = openfont(display, "/lib/font/bit/Vera/Vera.10.font");
		fonts[FB] = openfont(display, "/lib/font/bit/VeraBd/VeraBd.10.font");
		fonts[FT] = openfont(display, "/lib/font/bit/VeraMono/VeraMono.10.font");
		fonts[FL] = openfont(display, "/lib/font/bit/VeraMono/VeraMono.12.font");
	} else {
		fonts[FR] = openfont(display, "/lib/font/bit/Vera/Vera.12.font");
		fonts[FB] = openfont(display, "/lib/font/bit/VeraBd/VeraBd.12.font");
		fonts[FT] = openfont(display, "/lib/font/bit/VeraMono/VeraMono.12.font");
		fonts[FL] = openfont(display, "/lib/font/bit/VeraMono/VeraMono.20.font");
	}
	fonts[FS] = openfont(display, "/lib/font/bit/VeraMono/VeraMono.10.font");

	if(fonts[FR] == nil) fonts[FR] = font;
	if(fonts[FB] == nil) fonts[FB] = font;
	if(fonts[FT] == nil) fonts[FT] = font;
	if(fonts[FL] == nil) fonts[FL] = font;
	if(fonts[FS] == nil) fonts[FS] = font;

	maxfontht = fonts[FR]->height;
	if (fonts[FB]->height > maxfontht)
		maxfontht = fonts[FB]->height;
	if (fonts[FT]->height > maxfontht)
		maxfontht = fonts[FT]->height;
	
}

static void
loadborders(void)
{
	Rectangle	r;
	Rectangle	ir;
	Rectangle	rr;
	Rectangle	irr;

	rr = Rect(0, 0, Tagwid, 2*Taght);
	irr = insetrect(rr, 1);
	r  = Rect(0, 0, Tagwid, Taght);
	ir = insetrect(r, 1);
	bord[Bback] = cols[BACK];
	bord[Btag] = allocimage(display, rr, screen->chan, 0, CBack);
	bord[Bdtag] = allocimage(display, rr, screen->chan, 0, CBack);
	bord[Bmtag] = allocimage(display, rr, screen->chan, 0, CBack);
	bord[Bdmtag] = allocimage(display, rr, screen->chan, 0, CBack);

	draw(bord[Btag],  r,  cols[BORD], nil, ZP);
	draw(bord[Bmtag],  rr,  cols[BORD], nil, ZP);
	draw(bord[Bdtag], r,  cols[BORD], nil, ZP);
	draw(bord[Bdtag], ir, cols[HBORD], nil, ZP);
	draw(bord[Bdmtag],  rr,  cols[BORD], nil, ZP);
	draw(bord[Bdmtag], irr, cols[HBORD], nil, ZP);

	bord[Bws1] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bws2] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bws3] = allocimage(display, r, screen->chan, 1, CBack);
	draw(bord[Bws1],  r,  cols[WS1], nil, ZP);
	draw(bord[Bws2],  r,  cols[WS2], nil, ZP);
	draw(bord[Bws3],  r,  cols[WS3], nil, ZP);

	r = Rect(0, 0, Inset + Tagwid, Inset);
	bord[Bn] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bs] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Be] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bw] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bnw]= nil; // northwest is the tag. Don't care.
	bord[Bne] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bse] = allocimage(display, r, screen->chan, 1, CBack);
	bord[Bsw] = allocimage(display, r, screen->chan, 1, CBack);

	ir = Rect(0, 0, Inset+Tagwid, 1);
	draw(bord[Bn], ir, cols[BORD2], nil, ZP);

	ir = Rect(0, Inset-2, Inset+Tagwid, Inset);
	draw(bord[Bs], ir, cols[BORD], nil, ZP);

	ir = Rect(Inset-2, 0, Inset, Inset);
	draw(bord[Be], ir, cols[BORD], nil, ZP);

	ir = Rect(0, 0, 1, Inset);
	draw(bord[Bw], ir, cols[BORD2], nil, ZP);

	ir = Rect(0, 0, Inset-2, 1);
	draw(bord[Bne], ir, cols[BORD2], nil, ZP);
	ir = Rect(Inset-2, 0, Inset, Inset);
	draw(bord[Bne], ir, cols[BORD], nil, ZP);

	ir = Rect(0, Inset-2, Inset, Inset);
	draw(bord[Bse], ir, cols[BORD], nil, ZP);
	ir = Rect(Inset-2, 0, Inset, Inset);
	draw(bord[Bse], ir, cols[BORD], nil, ZP);

	ir = Rect(0, Inset-2, Inset+Tagwid, Inset);
	draw(bord[Bsw], ir, cols[BORD], nil, ZP);
	ir = Rect(0, 0, 1, Inset-2);
	draw(bord[Bsw], ir, cols[BORD2], nil, ZP);
}

static void
loadcols(void)
{
	cols[BACK] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, CBack);
	cols[HIGH] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, CHigh);
	cols[SET]  = allocimage(display, Rect(0,0,1,1), screen->chan, 1, CSet);
	cols[BORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DDarkblue);
	cols[BORD2]= allocimage(display, Rect(0,0,1,1), screen->chan, 1, DDarkblue);
	cols[TEXT] = display->black;
	cols[HTEXT] = display->black;
	cols[HBORD] = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DGreen);
	cols[WS1]= allocimage(display, Rect(0,0,1,1), screen->chan, 1, DRed);
	cols[WS2]= allocimage(display, Rect(0,0,1,1), screen->chan, 1, DBlue);
	cols[WS3]= allocimage(display, Rect(0,0,1,1), screen->chan, 1, DGreen);
}


void
initui(void (*derror)(Display*, char*))
{
	fmtinstall('R', Rfmt);
	fmtinstall('P', Pfmt);
	fmtinstall('T', Tfmt);
	fmtinstall('M', Mfmt);
	resizec = chancreate(sizeof(ulong), 10);
	if(initdraw(derror, nil, "omero") < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	loadcols();
	loadborders();
	loadfonts();
	mctl = initmouse(nil, screen);
	if (mctl == nil)
		sysfatal("initmouse: %r");
	threadcreate(resizethread, mctl->resizec, 16*1024);
	threadcreate(useriothread, nil, 16*1024);
	redraw();
}



