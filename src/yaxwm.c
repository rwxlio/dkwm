/* yet another X window manager
* see license file for copyright and license details
* vim:ft=c:fdm=syntax:ts=4:sts=4:sw=4
*/

/* signal.h sigaction */
#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xproto.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_keysyms.h>

#ifdef DEBUG
#define DBG(fmt, ...) print("yaxwm:%s:%d - " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
static void print(const char *fmt, ...);
#else
#define DBG(fmt, ...)
#endif

#ifndef VERSION
#define VERSION "0.1"
#endif

#define UNSET       (INT_MAX)
#define STICKY      (0xFFFFFFFF)
#define W(x)        ((x)->w + 2 * (x)->bw)
#define H(x)        ((x)->h + 2 * (x)->bw)
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define LEN(x)      (sizeof(x) / sizeof(x[0]))
#define CLNMOD(mod) (mod & ~(numlockmask | XCB_MOD_MASK_LOCK))
#define BWMASK      (XCB_CONFIG_WINDOW_BORDER_WIDTH)
#define XYMASK      (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y)
#define WHMASK      (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)
#define BUTTONMASK  (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE)
#define GRABMASK    (XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION\
		| XCB_EVENT_MASK_POINTER_MOTION_HINT)
#define CLIENTMASK  (XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE\
		| XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY)
#define ROOTMASK    (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT\
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS\
		| XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW\
		| XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY\
		| XCB_EVENT_MASK_PROPERTY_CHANGE)

#define FOR_EACH(v, list)    for ((v) = (list); (v); (v) = (v)->next)
#define FOR_STACK(v, list)   for ((v) = (list); (v); (v) = (v)->snext)
#define FOR_CLIENTS(c, ws)   FOR_EACH((ws), workspaces) FOR_EACH((c), (ws)->clients)

#define FIND_TAIL(v, list)\
	for ((v) = (list); (v) && (v)->next; (v) = (v)->next)
#define FIND_TILETAIL(v, list)\
	for ((v) = nexttiled((list)); (v) && nexttiled((v)->next); (v) = nexttiled((v)->next))

#define FIND_PREV(v, cur, list)\
	for ((v) = (list); (v) && (v)->next && (v)->next != (cur); (v) = (v)->next)
#define FIND_PREVTILED(v, cur, list)\
	for ((v) = nexttiled((list)); (v) && nexttiled((v)->next)\
			&& nexttiled((v)->next) != (cur); (v) = nexttiled((v)->next))

#define PROP_APPEND(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_APPEND, (win), (atom),\
			(type), (membsize), (nmemb), (value))
#define PROP_REPLACE(win, atom, type, membsize, nmemb, value)\
	xcb_change_property(con, XCB_PROP_MODE_REPLACE, (win), (atom),\
			(type), (membsize), (nmemb), (value))

#define MOVE(win, x, y) xcb_configure_window(con, (win), XYMASK, (uint []){(x), (y)})
#define RESIZE(win, w, h) xcb_configure_window(con, (win), WHMASK, (uint []){(w), (h)})
#define MOVERESIZE(win, x, y, w, h) xcb_configure_window(con, (win), XYMASK | WHMASK, (uint []){(x), (y), (w), (h)})

typedef unsigned int uint;
typedef unsigned char uchar;
typedef struct Panel Panel;
typedef struct Client Client;
typedef struct Layout Layout;
typedef struct Monitor Monitor;
typedef struct Keyword Keyword;
typedef struct Command Command;
typedef struct Workspace Workspace;
typedef struct ClientRule ClientRule;
typedef struct WorkspaceRule WorkspaceRule;

enum Borders {
	Width, Smart, Focus, Unfocus
};

enum Gravity {
	Left, Right, Center, Top, Bottom,
};

struct Panel {
	int x, y, w, h, strut_l, strut_r, strut_t, strut_b;
	Panel *next;
	Monitor *mon;
	xcb_window_t win;
};

struct Client {
	int x, y, w, h, bw;
	int old_x, old_y, old_w, old_h, old_bw;
	int max_w, max_h, min_w, min_h;
	int base_w, base_h, increment_w, increment_h;
	float min_aspect, max_aspect;
	int sticky, fixed, floating, fullscreen, urgent, nofocus, oldstate;
	Client *next, *snext;
	Workspace *ws;
	xcb_window_t win;
};

struct Monitor {
	char *name;
	xcb_randr_output_t id;
	int x, y, w, h, winarea_x, winarea_y, winarea_w, winarea_h;
	Monitor *next;
	Workspace *ws;
};

struct Keyword {
	char *name;
	void (*func)(char **);
};

struct Workspace {
	int num;
	char *name;
	uint nmaster, nstack, gappx;
	float split;
	Layout *layout;
	Monitor *mon;
	Workspace *next;
	Client *sel, *stack, *clients, *hidden;
};

struct ClientRule {
	char *regex, *monitor;
	int workspace, floating;
	void (*cbfunc)(Client *c);
	regex_t regcomp;
};

struct WorkspaceRule {
	char *name;
	uint nmaster, nstack, gappx;
	float split;
	Layout *layout;
};

struct Layout {
	char *name;
	void (*fn)(Workspace *);
};

struct Command {
	char *name;
	void (*fn)(int);
};

static int adjbdorgap(int i, char *opt, int changing, int other);
static void adjmvfocus(char **argv, void (*fn)(int));
static void *applyclientrule(Client *c, xcb_window_t *trans);
static void applypanelstrut(Panel *p);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int usermotion);
static void assignworkspaces(void);
static void attach(Client *c, int tohead);
static void attachpanel(Panel *p);
static void attachstack(Client *c);
static void changews(Workspace *ws, int usermotion);
static void checkerror(char *msg, xcb_generic_error_t *e);
static void cmdborder(char **argv);
static void cmdfloat(char **argv);
static void cmdfocus(char **argv);
static void cmdgappx(char **argv);
static void cmdkill(char **argv);
static void cmdlayout(char **argv);
static void cmdmove(char **argv);
static void cmdmouse(char **argv);
static void cmdnmaster(char **argv);
static void cmdnstack(char **argv);
static void cmdparse(char *buf);
static void cmdset(char **argv);
static void cmdsplit(char **argv);
static void cmdswap(char **argv);
static void cmdwin(char **argv);
static void cmdwm(char **argv);
static void cmdws(char **argv);
static void configure(Client *c);
static Monitor *coordtomon(int x, int y);
static void detach(Client *c, int reattach);
static void detachstack(Client *c);
static void *ecalloc(size_t elems, size_t size);
static void eventhandle(xcb_generic_event_t *ev);
static void eventignore(uint8_t type);
static void eventloop(void);
static void execcfg(void);
static void fixupworkspaces(void);
static void focus(Client *c);
static void follow(int num);
static void freeclient(Client *c, int destroyed);
static void freemon(Monitor *m);
static void freepanel(Panel *panel, int destroyed);
static void freewm(void);
static void freews(Workspace *ws);
static void grabbuttons(Client *c, int focused);
static int grabpointer(xcb_cursor_t cursor);
static void gravitate(Client *c, int vert, int horz, int matchgap);
static void initatoms(xcb_atom_t *atoms, const char **names, int num);
static void initclient(xcb_window_t win, xcb_window_t trans);
static Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h);
static void initpanel(xcb_window_t win);
static int initrandr(void);
static void initscan(void);
static void initwm(void);
static void initworkspaces(void);
static Workspace *initws(int num, WorkspaceRule *r);
static char *itoa(int n, char *s);
static Workspace *itows(int num);
static void layoutws(Workspace *ws);
static void monocle(Workspace *ws);
static void mousemvr(int move);
static void movefocus(int direction);
static void movestack(int direction);
static Client *nexttiled(Client *c);
static char *optparse(char **argv, char **opts, int *argi, float *argf, int hex);
static Monitor *outputtomon(xcb_randr_output_t id);
static int querypointer(int *x, int *y);
static Monitor *randrclone(xcb_randr_output_t id, int x, int y);
static void resize(Client *c, int x, int y, int w, int h, int bw);
static void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion);
static void restack(Workspace *ws);
static int rulecmp(regex_t *r, char *class, char *inst);
static int sendevent(Client *c, int wmproto);
static void send(int num);
static void setclientws(Client *c, uint num);
static void setfullscreen(Client *c, int fullscreen);
static void setnetworkareavp(void);
static void setstackmode(xcb_window_t win, uint mode);
static void seturgency(Client *c, int urg);
static void setwinstate(xcb_window_t win, uint32_t state);
static void showhide(Client *c);
static void sighandle(int);
static void sizehints(Client *c);
static size_t strlcat(char *dst, const char *src, size_t size);
static size_t strlcpy(char *dst, const char *src, size_t size);
static void takefocus(Client *c);
static void tile(Workspace *ws);
static void unfocus(Client *c, int focusroot);
static void ungrabpointer(void);
static void updatenumws(int needed);
static int updateoutputs(xcb_randr_output_t *outputs, int len, xcb_timestamp_t timestamp);
static int updaterandr(void);
static void updatestruts(Panel *p, int apply);
static void view(int num);
static xcb_get_window_attributes_reply_t *winattr(xcb_window_t win);
static void winhints(Client *c);
static xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop);
static int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size);
static Client *wintoclient(xcb_window_t win);
static Panel *wintopanel(xcb_window_t win);
static Workspace *wintows(xcb_window_t win);
static xcb_window_t wintrans(xcb_window_t win);
static void wintype(Client *c);

/* options available for various commands */
enum Opt {
	Std, Min, Ws, List, Col, Wm, Bdr
};
static const char *parseropts[][6] = {
	[Min]  = { "absolute", NULL },
	[List] = { "next",     "prev",     NULL },
	[Std]  = { "reset",    "absolute", NULL },
	[Wm]   = { "reload",   "restart",  "exit",    NULL },
	[Col]  = { "reset",    "focus",    "unfocus", NULL },
	[Bdr]  = { "absolute", "width",    "colour",  "color", "smart", NULL },
};

/* fifo command keywords and functions */
static Keyword keywords[] = {
	{ "win", cmdwin },
	{ "set", cmdset },
	{ "ws",  cmdws  },
	{ "wm",  cmdwm  },
};

/* "set" keyword options, used by cmdset() to parse arguments */
static Keyword setcmds[] = {
	{ "gap",     cmdgappx   },
	{ "border",  cmdborder  },
	{ "split",   cmdsplit   },
	{ "stack",   cmdnstack  },
	{ "master",  cmdnmaster },
	{ "layout",  cmdlayout  },
	{ "mouse",   cmdmouse   },
};

/* "win" keyword options, used by cmdwin() to parse arguments */
static Keyword wincmds[] = {
	{ "float",  cmdfloat },
	{ "focus",  cmdfocus },
	{ "kill",   cmdkill  },
	{ "move",   cmdmove  },
	{ "swap",   cmdswap  },
};

/* cursors used for normal operation, moving, and resizing */
enum Cursors {
	Normal, Move, Resize
};
static const char *cursors[] = {
	[Move] = "fleur",
	[Normal] = "arrow",
	[Resize] = "sizing"
};

/* supported WM_* atoms */
enum WMAtoms {
	Protocols, Delete, WMState, TakeFocus, Utf8Str
};
static const char *wmatomnames[] = {
	[Delete] = "WM_DELETE_WINDOW",
	[Protocols] = "WM_PROTOCOLS",
	[TakeFocus] = "WM_TAKE_FOCUS",
	[Utf8Str] = "UTF8_STRING",
	[WMState] = "WM_STATE",
};

/* supported _NET_* atoms */
enum NetAtoms {
	Supported,       Name,             State,            Check,           Fullscreen,
	ActiveWindow,    WindowType,       WindowTypeDialog, WindowTypeDock,  FrameExtents,
	Desktop,         CurrentDesktop,   NumDesktops,      DesktopViewport, DesktopGeometry,
	DesktopNames,    ClientList,       Strut,            StrutPartial,    WorkArea,
};
static const char *netatomnames[] = {
	[ActiveWindow] = "_NET_ACTIVE_WINDOW",
	[Check] = "_NET_SUPPORTING_WM_CHECK",
	[ClientList] = "_NET_CLIENT_LIST",
	[CurrentDesktop] = "_NET_CURRENT_DESKTOP",
	[DesktopGeometry] = "_NET_DESKTOP_GEOMETRY",
	[DesktopNames] = "_NET_DESKTOP_NAMES",
	[DesktopViewport] = "_NET_DESKTOP_VIEWPORT",
	[Desktop] = "_NET_WM_DESKTOP",
	[FrameExtents] = "_NET_FRAME_EXTENTS",
	[Fullscreen] = "_NET_WM_STATE_FULLSCREEN",
	[Name] = "_NET_WM_NAME",
	[NumDesktops] = "_NET_NUMBER_OF_DESKTOPS",
	[State] = "_NET_WM_STATE",
	[StrutPartial] = "_NET_WM_STRUT_PARTIAL",
	[Strut] = "_NET_WM_STRUT",
	[Supported] = "_NET_SUPPORTED",
	[WindowTypeDialog] = "_NET_WM_WINDOW_TYPE_DIALOG",
	[WindowTypeDock] = "_NET_WM_WINDOW_TYPE_DOCK",
	[WindowType] = "_NET_WM_WINDOW_TYPE",
	[WorkArea] = "_NET_WORKAREA",
};

#include "config.h"

extern char **environ;            /* environment variables */

static char *argv0;               /* program name */
static char *fifo;                /* path to fifo file, loaded from YAXWM_FIFO env */
static int fifofd;                /* fifo pipe file descriptor */
static int numws = 0;             /* number of workspaces currently allocated */
static int scr_w, scr_h;          /* root window size */
static uint running = 1;          /* continue handling events */
static int randrbase = -1;        /* randr extension response */
static uint numlockmask = 0;      /* numlock modifier bit mask */
static int dborder[LEN(borders)]; /* default border values used for resetting */

static Panel *panels;         /* panel linked list head */
static Workspace *selws;      /* selected workspace */
static Monitor *primary;      /* primary monitor detected with RANDR */
static Monitor *monitors;     /* monitor linked list head */
static Workspace *workspaces; /* workspace linked list head */

static xcb_screen_t *scr;                      /* the X screen */
static xcb_connection_t *con;                  /* xcb connection to the X server */
static xcb_window_t root, wmcheck;             /* root window and _NET_SUPPORTING_WM_CHECK window */
static xcb_key_symbols_t *keysyms;             /* current keymap symbols */
static xcb_cursor_t cursor[LEN(cursors)];      /* cursors for moving, resizing, and normal */
static xcb_atom_t wmatoms[LEN(wmatomnames)];   /* _WM atoms used mostly internally */
static xcb_atom_t netatoms[LEN(netatomnames)]; /* _NET atoms used both internally and by other clients */

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	xcb_void_cookie_t c;
	struct sigaction sa;
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGCHLD };
	uint mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	if (argc > 1) {
		fprintf(stderr, !strcmp(argv[1], "-v") ? "%s "VERSION"\n" : "usage: %s [-v]\n", argv0);
		exit(1);
	}
	if (!setlocale(LC_CTYPE, ""))
		err(1, "no locale support");
	if (xcb_connection_has_error((con = xcb_connect(NULL, NULL))))
		err(1, "error connecting to X");

	/* cleanly quit when exit(3) is called */
	atexit(freewm);

	/* setup root screen */
	if (!(scr = xcb_setup_roots_iterator(xcb_get_setup(con)).data))
		errx(1, "error getting default screen from X connection");
	root = scr->root;
	scr_w = scr->width_in_pixels;
	scr_h = scr->height_in_pixels;
	DBG("initialized root window: 0x%08x - size: %dx%d", root, scr_w, scr_h)

	/* check that we can grab SubstructureRedirect events on the root window */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK, &mask);
	if (xcb_request_check(con, c))
		errx(1, "is another window manager already running?");

	/* setup signal handlers (atexit(3) doesn't handle process exiting via signals) */
	sa.sa_handler = sighandle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	for (uint i = 0; i < LEN(sigs); i++)
		if (sigaction(sigs[i], &sa, NULL) < 0)
			err(1, "unable to setup handler for signal: %d", sigs[i]);

	/* setup the wm and existing windows before entering the event loop */
	initwm();
	initscan();
	layoutws(NULL);
	focus(NULL);
	execcfg();
	eventloop();

	return 0;
}

int adjbdorgap(int i, char *opt, int changing, int other)
{
	int r;
	/* if opt is NULL or empty we assume absolute sizes, however we still use
	 * a relative calculation so we can just call the set* function and save code repetition */
	if (opt && *opt) {
		if (!strcmp("reset", opt)) {
			return 0;
		} else if (!strcmp("absolute", opt)) {
			if (!(r = MAX(MIN(i, (selws->mon->winarea_h / 6) - other), 0) - changing))
				return UNSET;
		} else
			return UNSET;
	} else if (!(r = i))
		return UNSET;
	return r;
}

void adjmstack(int i, char *opt, int master)
{
	uint n = UNSET;

	if (i == UNSET)
		return;
	if (opt)
		i -= master ? (int)selws->nmaster : (int)selws->nstack;
	if (master && (n = MAX(selws->nmaster + i, 0)) != selws->nmaster)
		selws->nmaster = n;
	else if (!master && (n = MAX(selws->nstack + i, 0)) != selws->nstack)
		selws->nstack = n;
	if (n != UNSET)
		layoutws(selws);
}

void adjmvfocus(char **argv, void (*fn)(int))
{
	int i;
	char *opt;

	if (!(opt = optparse(argv, (char **)parseropts[List], &i, NULL, 0)) && i == UNSET)
		return;
	else if ((opt && !strcmp(opt, "next")) || (i > 0 && i != UNSET))
		i = opt ? +1 : i;
	else if ((opt && !strcmp(opt, "prev")) || i < 0)
		i = opt ? -1 : i;
	while (i) {
		fn(i);
		i += i > 0 ? -1 : 1;
	}
}

void *applyclientrule(Client *c, xcb_window_t *trans)
{ /* apply user specified rules to client, try using _NET atoms otherwise
   * returns a pointer to the callback function if any otherwise NULL */
	uint i;
	Client *t;
	Monitor *m;
	int ws, n, num = -1;
	char name[NAME_MAX];
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;
	xcb_icccm_get_wm_class_reply_t prop;
	void (*rulecbfunc)(Client *c) = NULL;

	if ((*trans != XCB_WINDOW_NONE || (*trans = wintrans(c->win)) != XCB_WINDOW_NONE)
			&& (t = wintoclient(*trans)))
	{
		c->ws = t->ws;
		c->floating = 1;
		ws = c->ws->num;
		goto done;
	}

	if (!wintextprop(c->win, netatoms[Name], name, sizeof(name))
			&& !wintextprop(c->win, XCB_ATOM_WM_NAME, name, sizeof(name)))
		strlcpy(name, "broken", sizeof(name));
	DBG("window title: %s", name);

	pc = xcb_icccm_get_wm_class(con, c->win);
	c->floating = 0;
	if ((ws = winprop(c->win, netatoms[Desktop])) < 0)
		ws = selws->num;
	if (xcb_icccm_get_wm_class_reply(con, pc, &prop, &e)) {
		DBG("window class: %s - instance: %s", prop.class_name, prop.instance_name)
		for (i = 0; i < LEN(clientrules); i++) {
			if (!rulecmp(&clientrules[i].regcomp, prop.class_name, prop.instance_name))
				continue;
			DBG("client matched rule regex: %s", clientrules[i].regex)
			c->floating = clientrules[i].floating;
			if (clientrules[i].workspace >= 0)
				ws = clientrules[i].workspace;
			else if (clientrules[i].monitor) {
				if (strtol(clientrules[i].monitor, NULL, 0) || clientrules[i].monitor[0] == '0')
					num = strtol(clientrules[i].monitor, NULL, 0);
				for (n = 0, m = monitors; m; m = m->next, n++) {
					if ((num >= 0 && num == n) || !strcmp(clientrules[i].monitor, m->name)) {
						ws = m->ws->num;
						break;
					}
				}
			}
			rulecbfunc = clientrules[i].cbfunc;
		}
		xcb_icccm_get_wm_class_reply_wipe(&prop);
	} else {
		checkerror("failed to get window class", e);
	}
done:
	setclientws(c, ws);
	DBG("set client values - workspace: %d, monitor: %s, floating: %d",
			c->ws->num, c->ws->mon->name, c->floating)
	return rulecbfunc;
}

void applypanelstrut(Panel *p)
{
	DBG("%s window area before: %d,%d @ %dx%d", p->mon->name, p->mon->winarea_x,
			p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
	if (p->mon->x + p->strut_l > p->mon->winarea_x)
		p->mon->winarea_x = p->strut_l;
	if (p->mon->y + p->strut_t > p->mon->winarea_y)
		p->mon->winarea_y = p->strut_t;
	if (p->mon->w - (p->strut_r + p->strut_l) < p->mon->winarea_w)
		p->mon->winarea_w = p->mon->w - (p->strut_r + p->strut_l);
	if (p->mon->h - (p->strut_b + p->strut_t) < p->mon->winarea_h)
		p->mon->winarea_h = p->mon->h - (p->strut_b + p->strut_t);
	DBG("%s window area after: %d,%d @ %dx%d", p->mon->name, p->mon->winarea_x,
			p->mon->winarea_y, p->mon->winarea_w, p->mon->winarea_h);
}

int applysizehints(Client *c, int *x, int *y, int *w, int *h, int usermotion)
{
	int baseismin;
	Monitor *m = c->ws->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (usermotion) { /* don't confine */
		if (*x > scr_w)
			*x = scr_w - W(c);
		if (*y > scr_h)
			*y = scr_h - H(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else { /* confine to monitor */
		if (*x > m->winarea_x + m->winarea_w)
			*x = m->winarea_x + m->winarea_w - W(c);
		if (*y > m->winarea_y + m->winarea_h)
			*y = m->winarea_y + m->winarea_h - H(c);
		if (*x + *w + 2 * c->bw < m->winarea_x)
			*x = m->winarea_x;
		if (*y + *h + 2 * c->bw < m->winarea_y)
			*y = m->winarea_y;
	}
	if (c->floating || !c->ws->layout->fn) {
		if (!(baseismin = c->base_w == c->min_w && c->base_h == c->min_h)) {
			/* temporarily remove base dimensions */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		if (c->min_aspect > 0 && c->max_aspect > 0) { /* adjust for aspect limits */
			if (c->max_aspect < (float)*w / *h)
				*w = *h * c->max_aspect + 0.5;
			else if (c->min_aspect < (float)*h / *w)
				*h = *w * c->min_aspect + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->base_w;
			*h -= c->base_h;
		}
		/* adjust for increment value */
		if (c->increment_w)
			*w -= *w % c->increment_w;
		if (c->increment_h)
			*h -= *h % c->increment_h;
		/* restore base dimensions */
		*w += c->base_w;
		*h += c->base_h;
		*w = MAX(*w, c->min_w);
		*h = MAX(*h, c->min_h);
		if (c->max_w)
			*w = MIN(*w, c->max_w);
		if (c->max_h)
			*h = MIN(*h, c->max_h);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void attach(Client *c, int tohead)
{ /* attach client to it's workspaces client list */
	Client *t = NULL;

	if (!c->ws)
		c->ws = selws;
	if (!tohead)
		FIND_TAIL(t, c->ws->clients);
	if (t) { /* attach to tail */
		c->next = t->next;
		t->next = c;
	} else { /* attach to head */
		c->next = c->ws->clients;
		c->ws->clients = c;
	}
}

void attachpanel(Panel *p)
{
	p->next = panels;
	panels = p;
}

void attachstack(Client *c)
{ /* attach client to it's workspaces focus stack list */
	c->snext = c->ws->stack;
	c->ws->stack = c;
}

void assignworkspaces(void)
{ /* map workspaces to monitors, create more if needed */
	int i, j, n = 0;
	Monitor *m;
	Workspace *ws;

	FOR_EACH(m, monitors)
		n++;
	updatenumws(n);
	j = numws / MAX(1, n);
	ws = workspaces;
	DBG("%d workspaces - %d per monitor", numws, j)

	FOR_EACH(m, monitors)
		for (i = 0; ws && i < j; i++, ws = ws->next) {
			ws->mon = m;
			DBG("workspace: %d - monitor: %s", ws->num, m->name)
			if (!i || ws == selws || ws->mon->ws == ws)
				m->ws = ws;
		}
	if (j * n != numws) {
		DBG("leftovers after dividing between monitors, assigning one per monitor until exhausted")
		for (m = monitors; ws; m = monitors)
			while (ws && m) {
				DBG("workspace: %d - monitor: %s", ws->num, m->name)
				ws->mon = m;
				ws = ws->next;
				m = m->next;
			}
	}
}

void changews(Workspace *ws, int usermotion)
{ /* change the currently active workspace and warp the mouse if needed */
	int diffmon = selws ? selws->mon != ws->mon : 1;

	selws = ws;
	selws->mon->ws = ws;
	PROP_REPLACE(root, netatoms[CurrentDesktop], XCB_ATOM_CARDINAL, 32, 1, &ws->num);
	if (diffmon && !usermotion)
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				ws->mon->x + (ws->mon->w / 2), ws->mon->y + (ws->mon->h / 2));
}

void checkerror(char *msg, xcb_generic_error_t *e)
{ /* if e is non-null print a warning with error code and name to stderr and free(3) e */
	if (!e)
		return;
	warnx("%s -- X11 error: %d: %s", msg, e->error_code,
			xcb_event_get_error_label(e->error_code));
	free(e);
}

void cmdborder(char **argv)
{
	char *opt;
	Client *c;
	Workspace *ws;
	int i, n, newborder, col = UNSET;
	int f = borders[Focus], u = borders[Unfocus];

	opt = optparse(argv, (char **)parseropts[Bdr], &i, NULL, 0);

	if (!opt)
		return;
	if (opt && !strcmp(opt, "smart")) {
		if (i != UNSET)
			borders[Smart] = i;
	} else if (!strcmp(opt, "colour") || !strcmp(opt, "color")) {
		opt = optparse(argv + 1, (char **)parseropts[Col], &i, NULL, 1);
		if (!opt)
			return;
		if (!strcmp("reset", opt)) {
			borders[Focus] = dborder[Focus];
			borders[Unfocus] = dborder[Unfocus];
		} else if (col <= 0xffffff || col >= 0x000000) {
			if (!strcmp("focus", opt)) {
				borders[Focus] = col;
				xcb_change_window_attributes(con, selws->sel->win,
						XCB_CW_BORDER_PIXEL, &borders[Focus]);
				return;
			} else if (!strcmp("unfocus", opt)) {
				borders[Unfocus] = col;
			}
		}
		if (f != borders[Focus] || u != borders[Unfocus])
			FOR_CLIENTS(c, ws)
				xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL,
						&borders[c == c->ws->sel ? Focus : Unfocus]);
	} else if (!strcmp(opt, "width")) {
		opt = optparse(argv + 1, (char **)parseropts[Std], &i, NULL, 0);
		if (!opt && i == UNSET)
			return;
		if ((n = adjbdorgap(i, opt, selws->gappx, borders[Width])) != UNSET) {
			if (n == 0)
				newborder = dborder[Width];
			else /* if we allow borders to be huge things would get silly, so we limit
				  * them to 1/6 screen height - gap size, we also don't allow the global
				  * border to be <1 so we can preserve borderless windows */
				newborder = MAX(MIN((int)((selws->mon->winarea_h / 6) - selws->gappx),
							borders[Width] + n), 1);
			if (newborder != borders[Width]) {
				/* update border width on clients that have borders matching the current global */
				FOR_CLIENTS(c, ws)
					if (c->bw && c->bw == borders[Width])
						c->bw = newborder;
				borders[Width] = newborder;
				layoutws(NULL);
			}
		}
	}
}

void cmdfloat(char **argv)
{
	Client *c;

	if (!(c = selws->sel) || c->fullscreen)
		return;
	if ((c->floating = !c->floating || c->fixed)) {
		c->w = c->old_w, c->h = c->old_h;
		c->x = c->old_x ? c->old_x : (c->ws->mon->winarea_x + c->ws->mon->winarea_w - W(c)) / 2;
		c->y = c->old_y ? c->old_y : (c->ws->mon->winarea_y + c->ws->mon->winarea_h - H(c)) / 2;
		resize(c, c->x, c->y, c->w, c->h, c->bw);
	} else {
		c->old_x = c->x, c->old_y = c->y, c->old_w = c->w, c->old_h = c->h;
	}
	layoutws(selws);
	(void)(argv);
}

void cmdfocus(char **argv)
{
	if (!selws->sel || selws->sel->fullscreen)
		return;
	adjmvfocus(argv, movefocus);
}

void cmdgappx(char **argv)
{
	int i, n;
	char *opt;
	uint newgap;

	opt = optparse(argv, (char **)parseropts[Std], &i, NULL, 0);

	if (!opt && i == UNSET)
		return;
	if ((n = adjbdorgap(i, opt, borders[Width], selws->gappx)) == UNSET)
		return;
	if (n == 0)
		newgap = workspacerules[selws->num].gappx;
	else /* if we allow gaps or borders to be huge things would get silly,
		  * so we limit them to 1/6 screen height - border size */
		newgap = MAX(MIN((int)selws->gappx + n,
					(selws->mon->winarea_h / 6) - borders[Width]), 0);
	if (newgap != selws->gappx) {
		selws->gappx = newgap;
		layoutws(selws);
	}
}

void cmdkill(char **argv)
{ /* close currently active client and free it */
	if (!selws->sel)
		return;
	DBG("user requested kill current client")
	(void)(argv);
	if (!sendevent(selws->sel, Delete)) {
		xcb_grab_server(con);
		xcb_set_close_down_mode(con, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(con, selws->sel->win);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	xcb_flush(con);
}

void cmdlayout(char **argv)
{
	uint i;

	if (!argv || !*argv)
		return;
	while (*argv) {
		for (i = 0; i < LEN(layouts); i++)
			if (!strcmp(layouts[i].name, *argv)) {
				if (&layouts[i] != selws->layout) {
					selws->layout = &layouts[i];
					layoutws(selws);
				}
				return;
			}
		argv++;
	}
}

void cmdmove(char **argv)
{
	if (!selws->sel || selws->sel->fullscreen || selws->sel->floating)
		return;
	adjmvfocus(argv, movestack);
}

void cmdmouse(char **argv)
{
	if (!argv || !*argv)
		return;
	while (*argv) {
		if (!strcmp("mod", *argv)) {
			argv++;
			if (!strcmp("alt", *argv) || !strcmp("mod1", *argv))
				mousemod = XCB_MOD_MASK_1;
			else if (!strcmp("super", *argv) || !strcmp("mod4", *argv))
				mousemod = XCB_MOD_MASK_4;
			else if (!strcmp("ctrl", *argv) || !strcmp("control", *argv))
				mousemod = XCB_MOD_MASK_CONTROL;
		} else if (!strcmp("move", *argv)) {
			argv++;
			if (!strcmp("button1", *argv))
				mousemove = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				mousemove = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				mousemove = XCB_BUTTON_INDEX_3;
		} else if (!strcmp("resize", *argv)) {
			argv++;
			if (!strcmp("button1", *argv))
				mouseresize = XCB_BUTTON_INDEX_1;
			else if (!strcmp("button2", *argv))
				mouseresize = XCB_BUTTON_INDEX_2;
			else if (!strcmp("button3", *argv))
				mouseresize = XCB_BUTTON_INDEX_3;
		}
		argv++;
	}
	if (selws->sel)
		grabbuttons(selws->sel, 1);
}

void cmdnmaster(char **argv)
{
	int i;
	char *opt;

	opt = optparse(argv, (char **)parseropts[Min], &i, NULL, 0);
	adjmstack(i, opt, 1);
}

void cmdnstack(char **argv)
{
	int i;
	char *opt;

	opt = optparse(argv, (char **)parseropts[Min], &i, NULL, 0);
	adjmstack(i, opt, 0);
}

void cmdparse(char *buf)
{
	uint i, n = 0;
	char *k, *args[10], *dbuf, *delim = " \t\n\r";

	dbuf = strdup(buf);
	if (!(k = strtok(dbuf, delim)))
		goto out;
	for (i = 0; i < LEN(keywords); i++)
		if (!strcmp(keywords[i].name, k)) {
			while (n < sizeof(args) && (args[n++] = strtok(NULL, delim)))
				;
			if (*args)
				keywords[i].func((char **)args);
			break;
		}
out:
	free(dbuf);
}

void cmdset(char **argv)
{
	uint i;
	char *s, **r;

	if (!(s = argv[0]))
		return;
	r = argv + 1;
	for (i = 0; i < LEN(setcmds); i++)
		if (!strcmp(setcmds[i].name, s)) {
			setcmds[i].func(r);
			return;
		}
}

void cmdsplit(char **argv)
{
	char *opt;
	float f, nf;

	if (!selws->layout->fn)
		return;
	opt = optparse(argv, (char **)parseropts[Min], NULL, &f, 0);
	if (f == 0.0 || (opt && (f > 0.9 || f < 0.1 || !(f -= selws->split))))
		return;
	if ((nf = f < 1.0 ? f + selws->split : f - 1.0) < 0.1 || nf > 0.9)
		return;
	selws->split = nf;
	layoutws(selws);
}

void cmdswap(char **argv)
{
	Client *c;

	(void)(argv);
	if (!(c = selws->sel) || c->floating || !selws->layout->fn)
		return;
	if (c == nexttiled(selws->clients) && !(c = nexttiled(c->next)))
		return;
	DBG("swapping current client window: 0x%08x", c->win)
	detach(c, 1);
	focus(NULL);
	layoutws(c->ws);
}

void cmdwin(char **argv)
{
	uint i;
	char *s, **r;

	if (!argv || !argv[0])
		return;
	s = argv[0];
	r = argv + 1;
	for (i = 0; i < LEN(wincmds); i++)
		if (!strcmp(wincmds[i].name, s)) {
			wincmds[i].func(r);
			return;
		}
}

void cmdwm(char **argv)
{
	char *opt;
	char *const arg[] = { argv0, NULL };

	if ((opt = optparse(argv, (char **)parseropts[Wm], NULL, NULL, 0))) {
		if (!strcmp(opt, "reload"))
			execcfg();
		else if (!strcmp(opt, "restart"))
			execvp(arg[0], arg);
		else
			running = 0;
	}
}

void cmdws(char **argv)
{
	uint j;
	int i = UNSET, n;
	void (*fn)(int) = view; /* assume view so `ws 1` is the same as `ws view 1` */

	if (!argv || !*argv)
		return;
	while (*argv) {
		if ((n = strtol(*argv, NULL, 0)) || **argv == '0')
			i = n;
		else for (j = 0; j < LEN(wscommands); j++)
			if (wscommands[j].fn && !strcmp(wscommands[j].name, *argv))
				fn = wscommands[j].fn;
		argv++;
	}
	if (i < (int)numws && i >= 0)
		fn(i);
}

void configure(Client *c)
{ /* send client a configure notify event */
	xcb_void_cookie_t vc;
	xcb_generic_error_t *e;
	xcb_configure_notify_event_t ce;

	ce.event = c->win;
	ce.window = c->win;
	ce.response_type = XCB_CONFIGURE_NOTIFY;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above_sibling = XCB_NONE;
	ce.override_redirect = 0;
	vc = xcb_send_event_checked(con, 0, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *)&ce);
	if ((e = xcb_request_check(con, vc))) {
		free(e);
		warnx("failed sending configure notify event to client window: 0x%x", c->win);
	}
}

Client *coordtoclient(int x, int y)
{
	Client *c;

	FOR_EACH(c, selws->clients)
		if (x > c->x && x < c->x + W(c) && y > c->y && y < c->y + H(c))
			break;
	return c;
}

Monitor *coordtomon(int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
			return m;
	return selws->mon;
}

void detach(Client *c, int reattach)
{ /* detach client from it's workspaces client list, can reattach to save calling attach() */
	Client **tc = &c->ws->clients;

	while (*tc && *tc != c)
		tc = &(*tc)->next;
	*tc = c->next;
	if (reattach)
		attach(c, 1);
}

void detachpanel(Panel *p)
{
	Panel **pp = &panels;

	while (*pp && *pp != p)
		pp = &(*pp)->next;
	*pp = p->next;
}

void detachstack(Client *c)
{ /* detach client from it's workspaces focus stack list */
	Client **tc = &c->ws->stack;

	while (*tc && *tc != c)
		tc = &(*tc)->snext;
	*tc = c->snext;
	if (c == c->ws->sel)
		c->ws->sel = c->ws->stack;
}

void *ecalloc(size_t elems, size_t size)
{ /* calloc(3) elems elements of size size, exit with message on error */
	void *p;

	if (!(p = calloc(elems, size)))
		err(1, "unable to allocate space");
	return p;
}

void eventhandle(xcb_generic_event_t *ev)
{
	Client *c;
	Monitor *m;
	Workspace *ws;
	Panel *p = NULL;
	static xcb_timestamp_t lasttime = 0;

	switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_FOCUS_IN:
		{
			xcb_focus_in_event_t *e = (xcb_focus_in_event_t *)ev;

			if (e->mode == XCB_NOTIFY_MODE_GRAB
					|| e->mode == XCB_NOTIFY_MODE_UNGRAB
					|| e->detail == XCB_NOTIFY_DETAIL_POINTER
					|| e->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
					|| e->detail == XCB_NOTIFY_DETAIL_NONE) {
				return;
			}

			if (selws->sel && e->event != selws->sel->win)
				takefocus(selws->sel);
			return;
		}
		case XCB_CONFIGURE_NOTIFY:
		{
			xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)ev;

			if (e->window == root && (scr_h != e->height || scr_w != e->width)) {
				scr_w = e->width;
				scr_h = e->height;
				if (randrbase < 0) {
					monitors->w = monitors->winarea_w = scr_w;
					monitors->h = monitors->winarea_h = scr_h;
					fixupworkspaces();
				}
			}
			return;
		}
		case XCB_CONFIGURE_REQUEST:
		{
			xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;

			if ((c = wintoclient(e->window))) {
				DBG("configure request event for managed window: 0x%x", e->window)
				if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
					c->bw = e->border_width;
				else if (c->floating || !selws->layout->fn) {
					m = c->ws->mon;
					if (e->value_mask & XCB_CONFIG_WINDOW_X) {
						c->old_x = c->x;
						c->x = m->x + e->x;
					}
					if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
						c->old_y = c->y;
						c->y = m->y + e->y;
					}
					if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
						c->old_w = c->w;
						c->w = e->width;
					}
					if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
						c->old_h = c->h;
						c->h = e->height;
					}
					if ((c->x + W(c)) > m->x + m->w)
						c->x = (m->winarea_x + m->winarea_w - W(c)) / 2;
					if ((c->y + H(c)) > m->y + m->h)
						c->y = (m->winarea_y + m->winarea_h - H(c)) / 2;
					if ((e->value_mask & XYMASK) && !(e->value_mask & WHMASK))
						configure(c);
					if (c->ws == c->ws->mon->ws)
						resize(c, c->x, c->y, c->w, c->h, c->bw);
				} else {
					configure(c);
				}
			} else {
				DBG("configure request event for unmanaged window: 0x%x", e->window)
				xcb_params_configure_window_t wc;
				wc.x = e->x;
				wc.y = e->y;
				wc.width = e->width;
				wc.height = e->height;
				wc.sibling = e->sibling;
				wc.stack_mode = e->stack_mode;
				wc.border_width = e->border_width;
				xcb_configure_window(con, e->window, e->value_mask, &wc);
			}
			xcb_flush(con);
			return;
		}
		case XCB_DESTROY_NOTIFY:
		{
			xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

			if ((c = wintoclient(e->window))) {
				DBG("destroy notify event for managed client window: 0x%08x -- freeing", e->window)
				freeclient(c, 1);
			} else if ((p = wintopanel(e->window))) {
				DBG("destroy notify event for managed panel window: 0x%08x -- freeing", e->window)
				freepanel(p, 1);
			}
			return;
		}
		case XCB_ENTER_NOTIFY:
		{
			xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;

			if (e->mode != XCB_NOTIFY_MODE_NORMAL || e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
				return;
			DBG("enter notify event - window: 0x%08x", e->event)
			if ((ws = (c = wintoclient(e->event)) ? c->ws : wintows(e->event)) != selws) {
				unfocus(selws->sel, 1);
				changews(ws, 1);
			} else if (!focusmouse || !c || c == selws->sel)
				return;
			focus(c);
			return;
		}
		case XCB_BUTTON_PRESS:
		{
			xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

			if (!(c = wintoclient(e->event)))
				return;
			DBG("button press event - mouse button %d - window: 0x%08x", e->detail, e->event)
			focus(c);
			restack(c->ws);
			if (CLNMOD(e->state) == XCB_NONE) {
				xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, e->time);
				xcb_flush(con);
			} else if (CLNMOD(e->state) == CLNMOD(mousemod)
					&& (e->detail == mousemove || e->detail == mouseresize))
				mousemvr(e->detail == mousemove);
			return;
		}
		case XCB_MOTION_NOTIFY:
		{
			xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

			if ((e->time - lasttime) < (1000 / 60)) /* not too frequently */
				return;
			lasttime = e->time;
			if (e->event == root && (m = coordtomon(e->root_x, e->root_y)) != selws->mon) {
				unfocus(selws->sel, 1);
				changews(m->ws, 1);
				focus(NULL);
			}
			return;
		}
		case XCB_MAP_REQUEST:
		{
			xcb_get_window_attributes_reply_t *wa;
			xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;

			if (!e->window || !(wa = winattr(e->window))
					|| (c = wintoclient(e->window)) || (p = wintopanel(e->window)))
				return;
			DBG("map request event for unmanaged window: 0x%08x", e->window)
			if (winprop(e->window, netatoms[WindowType]) == netatoms[WindowTypeDock])
				initpanel(e->window);
			else if (!wa->override_redirect)
				initclient(e->window, XCB_WINDOW_NONE);
			free(wa);
			return;
		}
		case XCB_UNMAP_NOTIFY:
		{
			xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

			if (XCB_EVENT_SENT(ev))
				setwinstate(e->window, XCB_ICCCM_WM_STATE_WITHDRAWN);
			else if ((c = wintoclient(e->window)))
				freeclient(c, 0);
			else if ((p = wintopanel(e->window)))
				freepanel(p, 0);
			return;
		}
		case XCB_CLIENT_MESSAGE:
		{
			xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
			xcb_atom_t fs = netatoms[Fullscreen];
			uint32_t *d = e->data.data32;

			if (e->window == root && e->type == netatoms[CurrentDesktop]) {
				view(d[0]);
			} else if ((c = wintoclient(e->window))) {
				if (e->type == netatoms[State] && (d[1] == fs || d[2] == fs))
					setfullscreen(c, (d[0] == 1 || (d[0] == 2 && !c->fullscreen)));
				else if (e->type == netatoms[ActiveWindow]) {
					unfocus(selws->sel, 1);
					view(c->ws->num);
					focus(c);
					restack(selws);
				}
			}
			return;
		}
		case XCB_PROPERTY_NOTIFY:
		{
			xcb_window_t trans;
			xcb_property_notify_event_t *e = (xcb_property_notify_event_t *)ev;

			if (e->atom == netatoms[StrutPartial] && (p = wintopanel(e->window))) {
				updatestruts(p, 1);
				layoutws(NULL);
			} else if (e->state != XCB_PROPERTY_DELETE && (c = wintoclient(e->window))) {
				switch (e->atom) {
				case XCB_ATOM_WM_TRANSIENT_FOR:
					if (c->floating || (trans = wintrans(c->win)) == XCB_NONE)
						return;
					if ((c->floating = (wintoclient(trans) != NULL)))
						layoutws(c->ws);
					break;
				case XCB_ATOM_WM_NORMAL_HINTS:
					sizehints(c);
					break;
				case XCB_ATOM_WM_HINTS:
					winhints(c);
					break;
				}
				if (e->atom == netatoms[WindowType])
					wintype(c);
			}
			return;
		}
		default:
		{
			if (randrbase != -1 && ev->response_type == randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
				DBG("RANDR screen change notify, updating monitors")
				if (updaterandr() > 0)
					fixupworkspaces();
				return;
			}

			xcb_request_error_t *e = (xcb_request_error_t *)ev;

			/* BadWindow
			 * or BadMatch & SetInputFocus/ConfigureWindow
			 * or BadAccess & GrabButton/GrabKey */
			if (ev->response_type || e->error_code == 3
					|| (e->error_code == 8  && (e->major_opcode == 42 || e->major_opcode == 12))
					|| (e->error_code == 10 && (e->major_opcode == 28 || e->major_opcode == 33)))
				return;

			/* TODO: some kind of error handling for those we don't want to ignore */
			warnx("failed request: %s - %s: 0x%08x", xcb_event_get_request_label(e->major_opcode),
					xcb_event_get_error_label(e->error_code), e->bad_value);
			return;
		}
	}
}

void eventignore(uint8_t type)
{ /* ignore the event type until the queue is cleared */
	xcb_generic_event_t *ev;

	xcb_flush(con);
	while (running && (ev = xcb_poll_for_event(con))) {
		if (XCB_EVENT_RESPONSE_TYPE(ev) != type)
			eventhandle(ev);
		free(ev);
	}
}

void eventloop(void)
{ /* wait for events while the user hasn't requested quit */
	ssize_t n;
	fd_set read_fds;
	char buf[PIPE_BUF];
	int num, confd, nfds, e;
	xcb_generic_event_t *ev;

	nfds = (confd = xcb_get_file_descriptor(con)) > fifofd ? confd : fifofd;
	nfds++;
	while (running) {
		xcb_flush(con);
		FD_ZERO(&read_fds);
		FD_SET(fifofd, &read_fds);
		FD_SET(confd, &read_fds);
		if ((num = select(nfds, &read_fds, NULL, NULL, NULL)) > 0) {
			if (FD_ISSET(fifofd, &read_fds))
				if ((n = read(fifofd, buf, sizeof(buf) - 1)) > 0 && *buf != '#' && *buf != '\n') {
					if (buf[n - 1] == '\n')
						n--;
					buf[n] = '\0';
					cmdparse(buf);
				}
			if (FD_ISSET(confd, &read_fds)) {
				while ((ev = xcb_poll_for_event(con))) {
					eventhandle(ev);
					free(ev);
				}
			}
		}
		if ((e = xcb_connection_has_error(con))) {
			warnx("connection to the server was closed");
			switch (e) {
			case XCB_CONN_ERROR:                   warn("socket, pipe or stream error"); break;
			case XCB_CONN_CLOSED_EXT_NOTSUPPORTED: warn("unsupported extension"); break;
			case XCB_CONN_CLOSED_MEM_INSUFFICIENT: warn("not enough memory"); break;
			case XCB_CONN_CLOSED_REQ_LEN_EXCEED:   warn("request length exceeded"); break;
			case XCB_CONN_CLOSED_INVALID_SCREEN:   warn("invalid screen"); break;
			case XCB_CONN_CLOSED_FDPASSING_FAILED: warn("failed to pass FD"); break;
			default: warn("unknown error.\n"); break;
			}
			running = 0;
		}
	}
}

void execcfg(void)
{
	char *cfg, *home;
	char path[PATH_MAX];

	if (!(cfg = getenv("YAXWM_CONF"))) {
		if (!(home = getenv("XDG_CONFIG_HOME")) && !(home = getenv("HOME")))
			return;
		strlcpy(path, home, sizeof(path));
		strlcat(path, "/.config/yaxwm/", sizeof(path));
		strlcat(path, "yaxwmrc", sizeof(path));
		cfg = path;
	}
	if (!fork()) {
		if (con)
			close(xcb_get_file_descriptor(con));
		setsid();
		execle(cfg, cfg, (char *)NULL, environ);
		warn("unable to execute config file");
	}
}

void fixupworkspaces(void)
{ /* after monitor(s) change we need to reassign workspaces and resize fullscreen clients */
	Client *c;
	Workspace *ws;
	uint v[] = { scr_w, scr_h };

	assignworkspaces();
	FOR_CLIENTS(c, ws)
		if (c->fullscreen)
			resize(c, ws->mon->x, ws->mon->y, ws->mon->w, ws->mon->h, c->bw);
	PROP_REPLACE(root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, v);
	if (panels)
		updatestruts(panels, 1);
	setnetworkareavp();
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void focus(Client *c)
{ /* focus client (making it the head of the focus stack)
   * when client is NULL focus the current workspace stack head */
	if (!selws)
		return;
	if (!c || c->ws != c->ws->mon->ws)
		c = selws->stack;
	if (selws->sel && selws->sel != c)
		unfocus(selws->sel, 0);
	if (c) {
		DBG("focusing client window: 0x%08x", c->win)
		if (c->urgent)
			seturgency(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Focus]);
		takefocus(c);
	} else {
		DBG("no available clients on this workspace, focusing root window")
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
	selws->sel = c;
}

void follow(int num)
{ /* follow selected client to a workspace */
	Client *c;
	Workspace *ws;

	if (!selws->sel || num == selws->num || !(ws = itows(num)))
		return;
	if ((c = selws->sel)) {
		unfocus(c, 1);
		setclientws(c, num);
	}
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

void freeclient(Client *c, int destroyed)
{ /* detach client and free it, if !destroyed we update the state to withdrawn */
	if (!c)
		return;
	Workspace *ws, *cws = c->ws;

	DBG("freeing client window: 0x%08x - destroyed: %i", c->win, destroyed)
	detach(c, 0);
	detachstack(c);
	if (!destroyed) {
		xcb_grab_server(con);
		xcb_configure_window(con, c->win, BWMASK, &c->old_bw);
		xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_MOD_MASK_ANY);
		setwinstate(c->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	free(c);
	xcb_delete_property(con, root, netatoms[ClientList]);
	FOR_CLIENTS(c, ws)
		PROP_APPEND(root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	layoutws(cws);
	focus(NULL);
}

void freemon(Monitor *m)
{ /* detach and free a monitor and it's name */
	Monitor *mon;

	if (m == monitors)
		monitors = monitors->next;
	else {
		FIND_PREV(mon, m, monitors);
		if (mon)
			mon->next = m->next;
	}
	DBG("freeing monitor: %s", m->name)
	free(m->name);
	free(m);
}

void freepanel(Panel *p, int destroyed)
{
	DBG("freeing panel: %d", p->win)
	detachpanel(p);
	if (!destroyed) {
		xcb_grab_server(con);
		setwinstate(p->win, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(con);
		xcb_ungrab_server(con);
	}
	updatestruts(p, 0);
	free(p);
	layoutws(NULL);
}

void freewm(void)
{ /* exit yaxwm, free everything and cleanup X */
	uint i;
	Workspace *ws;

	FOR_EACH(ws, workspaces)
		while (ws->stack)
			freeclient(ws->stack, 0);
	xcb_key_symbols_free(keysyms);
	while (panels)
		freepanel(panels, 0);
	while (monitors)
		freemon(monitors);
	while (workspaces)
		freews(workspaces);
	for (i = 0; i < LEN(cursors); i++)
		xcb_free_cursor(con, cursor[i]);
	for (i = 0; i < LEN(clientrules); i++)
		regfree(&clientrules[i].regcomp);
	xcb_destroy_window(con, wmcheck);
	xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_INPUT_FOCUS_POINTER_ROOT, XCB_CURRENT_TIME);
	xcb_flush(con);
	xcb_delete_property(con, root, netatoms[ActiveWindow]);
	xcb_disconnect(con);
	close(fifofd);
	unlink(fifo);
}

void freews(Workspace *ws)
{ /* detach and free workspace */
	Workspace *sel;

	if (ws == workspaces)
		workspaces = workspaces->next;
	else {
		FIND_PREV(sel, ws, workspaces);
		if (sel)
			sel->next = ws->next;
	}
	DBG("freeing workspace: %d", ws->num)
	free(ws);
}

void grabbuttons(Client *c, int focused)
{
	xcb_generic_error_t *e;
	xcb_keysym_t nlock = 0xff7f;
	xcb_keycode_t *kc, *t = NULL;
	xcb_get_modifier_mapping_reply_t *m;
	uint mods[] = { 0, XCB_MOD_MASK_LOCK, 0, XCB_MOD_MASK_LOCK };

	numlockmask = 0;
	if ((m = xcb_get_modifier_mapping_reply(con, xcb_get_modifier_mapping(con), &e))) {
		if ((t = xcb_key_symbols_get_keycode(keysyms, nlock))
				&& (kc = xcb_get_modifier_mapping_keycodes(m)))
		{
			for (uint i = 0; i < 8; i++)
				for (uint j = 0; j < m->keycodes_per_modifier; j++)
					if (kc[i * m->keycodes_per_modifier + j] == *t)
						numlockmask = (1 << i);
		}
		free(t);
		free(m);
	} else {
		checkerror("unable to get modifier mapping for numlock", e);
	}
	/* apply the mask to search elements */
	mods[2] |= numlockmask, mods[3] |= numlockmask;
	xcb_ungrab_button(con, XCB_BUTTON_INDEX_ANY, c->win, XCB_BUTTON_MASK_ANY);
	if (!focused)
		xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
				XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	for (uint i = 0; i < LEN(mods); i++) {
			xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, mousemove, mousemod | mods[i]);
			xcb_grab_button(con, 0, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_SYNC, XCB_NONE, XCB_NONE, mouseresize, mousemod | mods[i]);
	}
}

int grabpointer(xcb_cursor_t cursor)
{ /* grab the mouse pointer on the root window with cursor passed */
	int r = 0;
	xcb_generic_error_t *e;
	xcb_grab_pointer_cookie_t pc;
	xcb_grab_pointer_reply_t *ptr;

	pc = xcb_grab_pointer(con, 0, root, GRABMASK, XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC, root, cursor, XCB_CURRENT_TIME);
	if ((ptr = xcb_grab_pointer_reply(con, pc, &e))) {
		r = ptr->status == XCB_GRAB_STATUS_SUCCESS;
		free(ptr);
	} else {
		checkerror("unable to grab pointer", e);
	}
	return r;
}

void gravitate(Client *c, int vert, int horz, int matchgap)
{
	int x, y, gap;

	if (!c || !c->ws || !c->floating)
		return;
	x = c->x, y = c->y;
	gap = matchgap ? c->ws->gappx : 0;
	switch (horz) {
	case Left: x = c->ws->mon->winarea_x + gap; break;
	case Right: x = c->ws->mon->winarea_x + c->ws->mon->winarea_w - W(c) - gap; break;
	case Center: x = (c->ws->mon->winarea_x + c->ws->mon->winarea_w - W(c)) / 2; break;
	}
	switch (vert) {
	case Top: y = c->ws->mon->winarea_y + gap; break;
	case Bottom: y = c->ws->mon->winarea_y + c->ws->mon->winarea_h - H(c) - gap; break;
	case Center: y = (c->ws->mon->winarea_y + c->ws->mon->winarea_h - H(c)) / 2; break;
	}
	resizehint(c, x, y, c->w, c->h, c->bw, 0);
}

void initatoms(xcb_atom_t *atoms, const char **names, int num)
{ /* intern atoms in bulk */
	int i;
	xcb_generic_error_t *e;
	xcb_intern_atom_reply_t *r;
	xcb_intern_atom_cookie_t c[num];

	for (i = 0; i < num; ++i)
		c[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
	for (i = 0; i < num; ++i) {
		if ((r = xcb_intern_atom_reply(con, c[i], &e))) {
			DBG("initializing atom: %s - value: %d", names[i], r->atom)
			atoms[i] = r->atom;
			free(r);
		} else {
			checkerror("unable to initialize atom", e);
		}
	}
}

void initclient(xcb_window_t win, xcb_window_t trans)
{ /* allocate and setup new client from window */
	Client *c;
	Monitor *m;
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;
	void (*clientcbfunc)(Client *c);
	xcb_get_geometry_reply_t *g = NULL;
	uint values[] = { borders[Unfocus], CLIENTMASK };
	uint frame[] = { borders[Width], borders[Width], borders[Width], borders[Width] };

	DBG("initializing new client from window: 0x%08x", win)
	gc = xcb_get_geometry(con, win);
	c = ecalloc(1, sizeof(Client));
	c->win = win;
	if ((g = xcb_get_geometry_reply(con, gc, &e))) {
		DBG("using geometry given by the window: %d,%d @ %dx%d", g->x, g->y, g->width, g->height)
		c->x = c->old_x = g->x, c->y = c->old_y = g->y;
		c->w = c->old_w = g->width, c->h = c->old_h = g->height;
		c->old_bw = g->border_width;
	} else {
		checkerror("failed to get window geometry reply", e);
	}
	free(g);
	clientcbfunc = applyclientrule(c, &trans);
	c->bw = borders[Width];
	xcb_configure_window(con, c->win, BWMASK, &c->bw);
	configure(c);
	wintype(c);
	sizehints(c);
	winhints(c);
	xcb_change_window_attributes(con, c->win, XCB_CW_EVENT_MASK | XCB_CW_BORDER_PIXEL, values);
	grabbuttons(c, 0);
	if (c->floating || (c->floating = c->oldstate = c->fixed)) {
		m = c->ws->mon;
		if (c->x <= m->winarea_x || c->x + W(c) >= m->winarea_x + m->winarea_w
				|| c->y <= m->winarea_y || c->y + H(c) >= m->winarea_y + m->winarea_h)
			gravitate(c, Center, Center, 0);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	}
	PROP_APPEND(root, netatoms[ClientList], XCB_ATOM_WINDOW, 32, 1, &c->win);
	PROP_REPLACE(c->win, netatoms[FrameExtents], XCB_ATOM_CARDINAL, 32, 4, frame);
	setwinstate(c->win, XCB_ICCCM_WM_STATE_NORMAL);
	if (c->ws == c->ws->mon->ws) {
		if (c->ws == selws)
			unfocus(selws->sel, 0);
		layoutws(c->ws);
	} else { /* hide windows on non-visible workspaces */
		MOVE(c->win, H(c) * -2, c->y);
	}
	c->ws->sel = c;
	xcb_map_window(con, c->win);
	focus(NULL);
	if (clientcbfunc)
		clientcbfunc(c);
	DBG("new client mapped on workspace %d: %d,%d @ %dx%d - floating: %d - nofocus: %d",
			c->ws->num, c->x, c->y, c->w, c->h, c->floating, c->nofocus)
}

void initpanel(xcb_window_t win)
{
	int *s;
	Panel *p;
	xcb_generic_error_t *e;
	xcb_get_geometry_cookie_t gc;
	xcb_get_property_cookie_t rc;
	xcb_get_geometry_reply_t *g = NULL;
	xcb_get_property_reply_t *r = NULL;

	DBG("initializing new panel from window: %d", win)
	gc = xcb_get_geometry(con, win);
	p = ecalloc(1, sizeof(Panel));
	p->win = win;

	if ((g = xcb_get_geometry_reply(con, gc, &e)))
		p->x = g->x, p->y = g->y, p->w = g->width, p->h = g->height;
	else
		checkerror("failed to get window geometry reply", e);
	free(g);
	p->mon = coordtomon(p->x, p->y);
	rc = xcb_get_property(con, 0, p->win, netatoms[StrutPartial], XCB_ATOM_CARDINAL, 0, 4);
	DBG("checking panel for _NET_WM_STRUT_PARTIAL or _NET_WM_STRUT")
	if (!(r = xcb_get_property_reply(con, rc, &e)) || r->type == XCB_NONE) {
		checkerror("unable to get _NET_WM_STRUT_PARTIAL from window", e);
		rc = xcb_get_property(con, 0, p->win, netatoms[Strut], XCB_ATOM_CARDINAL, 0, 4);
		if (!(r = xcb_get_property_reply(con, rc, &e)))
			checkerror("unable to get _NET_WM_STRUT or _NET_WM_STRUT_PARTIAL from window", e);
	}
	if (r) {
		if (r->value_len && (s = xcb_get_property_value(r))) {
			DBG("panel window has struts: %d, %d, %d, %d", s[0], s[1], s[2], s[3])
			p->strut_l = s[0], p->strut_r = s[1], p->strut_t = s[2], p->strut_b = s[3];
			updatestruts(p, 1);
		}
		free(r);
	}
	attachpanel(p);
	xcb_change_window_attributes(con, p->win, XCB_CW_EVENT_MASK,
			(uint []){ XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY });
	setwinstate(p->win, XCB_ICCCM_WM_STATE_NORMAL);
	xcb_map_window(con, p->win);
	layoutws(NULL);
	DBG("new panel mapped -- monitor: %s -- geometry: %d,%d @ %dx%d",
			p->mon->name, p->x, p->y, p->w, p->h)
}

Monitor *initmon(char *name, xcb_randr_output_t id, int x, int y, int w, int h)
{ /* allocate a monitor from randr output */
	Monitor *m;
	uint len = strlen(name) + 1;

	DBG("initializing new monitor: %s - %d,%d - %dx%d", name, x, y, w, h)
	m = ecalloc(1, sizeof(Monitor));
	m->x = m->winarea_x = x;
	m->y = m->winarea_y = y;
	m->w = m->winarea_w = w;
	m->h = m->winarea_h = h;
	m->id = id;
	m->name = ecalloc(1, len);
	if (len > 1)
		strlcpy(m->name, name, len);
	return m;
}

int initrandr(void)
{
	int extbase;
	const xcb_query_extension_reply_t *ext;

	DBG("checking randr extension support")
	ext = xcb_get_extension_data(con, &xcb_randr_id);
	if (!ext->present)
		return -1;
	DBG("randr extension is supported, initializing")
	updaterandr();
	extbase = ext->first_event;
	xcb_randr_select_input(con, root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE|XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE
			|XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
	DBG("RANDR extension active and monitor(s) initialized -- extension base: %d", extbase)
	return extbase;
}

void initscan(void)
{ /* walk root window tree and init existing windows */
	uint i, num;
	xcb_window_t *win;
	xcb_generic_error_t *e;
	xcb_query_tree_cookie_t c;
	xcb_query_tree_reply_t *tree;
	xcb_atom_t iconified = XCB_ICCCM_WM_STATE_ICONIC;

	c = xcb_query_tree(con, root);
	DBG("getting root window tree and initializing existing child windows")
	if ((tree = xcb_query_tree_reply(con, c, &e))) {
		num = tree->children_len;
		win = xcb_query_tree_children(tree);
		xcb_atom_t state[num];
		xcb_window_t trans[num];
		xcb_get_window_attributes_reply_t *wa[num];

		for (i = 0; i < num; i++) { /* non transient */
			trans[i] = state[i] = XCB_WINDOW_NONE;
			if (!(wa[i] = winattr(win[i]))) {
				win[i] = 0;
			} else if (winprop(win[i], netatoms[WindowType]) == netatoms[WindowTypeDock]
					&& wa[i]->map_state != XCB_MAP_STATE_UNMAPPED)
			{
				initpanel(win[i]);
				win[i] = 0;
			} else if (!wa[i]->override_redirect
					&& (trans[i] = wintrans(win[i])) == XCB_WINDOW_NONE
					&& (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE
						|| (state[i] = winprop(win[i], wmatoms[WMState])) == iconified))
			{
				initclient(win[i], XCB_WINDOW_NONE);
				win[i] = 0;
			}
		}
		for (i = 0; i < num; i++) { /* transients */
			if (win[i] && trans[i] && !wa[i]->override_redirect
					&& (wa[i]->map_state == XCB_MAP_STATE_VIEWABLE || state[i] == iconified))
				initclient(win[i], trans[i]);
			free(wa[i]);
		}
		free(tree);
	} else {
		checkerror("FATAL: unable to query tree from root window", e);
		exit(1);
	}
}

void initwm(void)
{ /* setup internals, binds, atoms, and root window event mask */
	uint i, j;
	int r, cws;
	Workspace *ws;
	size_t len = 1;
	char errbuf[NAME_MAX];
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;
	xcb_cursor_context_t *ctx;
	uint v[] = { scr_w, scr_h };

	/* monitor(s) & workspaces */
	if ((randrbase = initrandr()) < 0 || !monitors)
		monitors = initmon("default", 0, 0, 0, scr_w, scr_h);
	if (!primary)
		primary = monitors;
	initworkspaces();
	assignworkspaces();

	for (i = 0; i < LEN(dborder); i++)
		dborder[i] = borders[i];

	/* cursors */
	if (xcb_cursor_context_new(con, scr, &ctx) < 0)
		err(1, "unable to create cursor context");
	for (i = 0; i < LEN(cursors); i++)
		cursor[i] = xcb_cursor_load_cursor(ctx, cursors[i]);
	xcb_cursor_context_free(ctx);

	/* client rules regexes */
	for (i = 0; i < LEN(clientrules); i++)
		if ((r = regcomp(&clientrules[i].regcomp, clientrules[i].regex,
						REG_NOSUB | REG_EXTENDED | REG_ICASE)))
		{
			regerror(r, &clientrules[i].regcomp, errbuf, sizeof(errbuf));
			errx(1, "invalid regex clientrules[%d]: %s: %s\n", i, clientrules[i].regex, errbuf);
		}

	/* atoms */
	initatoms(wmatoms, wmatomnames, LEN(wmatomnames));
	initatoms(netatoms, netatomnames, LEN(netatomnames));

	/* create simple window for _NET_SUPPORTING_WM_CHECK and initialize it's atoms */
	wmcheck = xcb_generate_id(con);
	xcb_create_window(con, XCB_COPY_FROM_PARENT, wmcheck, root, -1, -1, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_ONLY, scr->root_visual, 0, NULL);
	PROP_REPLACE(wmcheck, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);
	PROP_REPLACE(wmcheck, netatoms[Name], wmatoms[Utf8Str], 8, 5, "yaxwm");
	PROP_REPLACE(root, netatoms[Check], XCB_ATOM_WINDOW, 32, 1, &wmcheck);

	/* set most of the root window atoms that are unlikely to change often */
	updatenumws(numws);
	PROP_REPLACE(root, netatoms[DesktopGeometry], XCB_ATOM_CARDINAL, 32, 2, v);
	PROP_REPLACE(root, netatoms[Supported], XCB_ATOM_ATOM, 32, LEN(netatoms), netatoms);
	xcb_delete_property(con, root, netatoms[ClientList]);
	setnetworkareavp();

	/* CurrentDesktop */
	cws = (r = winprop(root, netatoms[CurrentDesktop])) >= 0 ? r : 0;
	changews((ws = itows(cws)) ? ws : workspaces, 1);

	/* DesktopNames */
	FOR_EACH(ws, workspaces)
		len += strlen(ws->name) + 1;
	char names[len];
	len = 0;
	FOR_EACH(ws, workspaces)
		for (j = 0; (names[len++] = ws->name[j]); j++)
			;
	PROP_REPLACE(root, netatoms[DesktopNames], wmatoms[Utf8Str], 8, --len, names);

	/* root window event mask & cursor */
	c = xcb_change_window_attributes_checked(con, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR,
			(uint []){ ROOTMASK, cursor[Normal] });
	if ((e = xcb_request_check(con, c))) {
		free(e);
		err(1, "unable to change root window event mask and cursor");
	}
	/* binds */
	if (!(keysyms = xcb_key_symbols_alloc(con)))
		err(1, "unable to get keysyms from X connection");

	/* fifo pipe */
	if (!(fifo = getenv("YAXWM_FIFO"))) {
		fifo = "/tmp/yaxwm.fifo";
		setenv("YAXWM_FIFO", fifo, 0);
	}
	if ((mkfifo(fifo, 0666) < 0 && errno != EEXIST)
			|| (fifofd = open(fifo, O_RDONLY | O_NONBLOCK | O_DSYNC | O_SYNC | O_RSYNC)) < 0)
		err(1, "unable to open fifo: %s", fifo);
}

void initworkspaces(void)
{ /* setup default workspaces from user specified workspace rules */
	Workspace *ws;

	for (numws = 0; numws < (int)LEN(workspacerules); numws++) {
		FIND_TAIL(ws, workspaces);
		if (ws)
			ws->next = initws(numws, &workspacerules[numws]);
		else
			workspaces = initws(numws, &workspacerules[numws]);
	}
}

Workspace *initws(int num, WorkspaceRule *r)
{
	Workspace *ws;

	DBG("initializing new workspace: '%s': %d", r->name, num)
	ws = ecalloc(1, sizeof(Workspace));
	ws->num = num;
	ws->name = r->name;
	ws->nmaster = r->nmaster;
	ws->nstack = r->nstack;
	ws->gappx = r->gappx;
	ws->split = r->split;
	ws->layout = r->layout;
	return ws;
}

char *itoa(int n, char *s)
{ /* convert n to chars in s */
	char c;
	int j, i = 0, sign = n;

	if (sign < 0)
		n = -n;
	do { /* convert digits to chars in reverse */
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	for (j = i - 1, i = 0; i < j; i++, j--) { /* un-reverse s */
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s;
}

Workspace *itows(int num)
{ /* return workspace matching num, otherwise NULL */
	Workspace *ws;

	for (ws = workspaces; ws && (int)ws->num != num; ws = ws->next)
		;
	return ws;
}

void layoutws(Workspace *ws)
{ /* show currently visible clients and restack workspaces */
	if (ws)
		showhide(ws->stack);
	else FOR_EACH(ws, workspaces)
		showhide(ws->stack);
	if (ws) {
		if (ws->layout->fn)
			ws->layout->fn(ws);
		restack(ws);
	} else FOR_EACH(ws, workspaces)
		if (ws == ws->mon->ws && ws->layout->fn)
			ws->layout->fn(ws);
}

void monocle(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;

	for (c = nexttiled(ws->clients); c; c = nexttiled(c->next))
		resize(c, m->winarea_x, m->winarea_y, m->winarea_w, m->winarea_h,
				borders[Smart] ? 0 : borders[Width]);
}

void movefocus(int direction)
{ /* focus the next or previous client on the active workspace */
	Client *c, *sel = selws->sel;

	if (!sel || sel->fullscreen)
		return;
	if (direction > 0)
		c = sel->next ? sel->next : selws->clients;
	else
		FIND_PREV(c, sel, selws->clients);
	if (c) {
		focus(c);
		restack(c->ws);
	}
}

void movestack(int direction)
{
	int i = 0;
	Client *c;

	if (!selws->sel || selws->sel->floating || !nexttiled(selws->clients->next))
		return;
	if (direction > 0) { /* swap current and the next or move to the front when at the end */
		detach(selws->sel, (c = nexttiled(selws->sel->next)) ? 0 : 1);
		if (c) { /* attach within the list */
			selws->sel->next = c->next;
			c->next = selws->sel;
		}
	} else { /* swap the current and the previous or move to the end when at the front */
		if (selws->sel == nexttiled(selws->clients)) { /* attach to end */
			detach(selws->sel, 0);
			attach(selws->sel, 0);
		} else {
			FIND_PREVTILED(c, selws->sel, selws->clients);
			detach(selws->sel, (i = (c == nexttiled(selws->clients)) ? 1 : 0));
			if (!i) { /* attach within the list */
				selws->sel->next = c;
				FIND_PREV(c, selws->sel->next, selws->clients); /* find the real (non-tiled) previous to c */
				c->next = selws->sel;
			}
		}
	}
	layoutws(selws);
	focus(selws->sel);
}

void mousemvr(int move)
{
	Client *c;
	Monitor *m;
	xcb_timestamp_t last = 0;
	xcb_motion_notify_event_t *e;
	xcb_generic_event_t *ev = NULL;
	int mx, my;
	int ox, oy, ow, oh, nw, nh, nx, ny, x, y, released = 0;

	if (!(c = selws->sel) || c->fullscreen || !querypointer(&mx, &my))
		return;
	ox = nx = c->x, oy = ny = c->y, ow = nw = c->w, oh = nh = c->h;
	if (c != c->ws->sel)
		focus(c);
	restack(c->ws);
	if (!grabpointer(cursor[move ? Move : Resize]))
		return;
	while (running && !released) {
		while ((ev = xcb_wait_for_event(con)) == NULL)
			xcb_flush(con);
		switch (XCB_EVENT_RESPONSE_TYPE(ev)) {
		case XCB_MOTION_NOTIFY:
			e = (xcb_motion_notify_event_t *)ev;

			/* we shouldn't need to query the pointer and just use the event root_x, root_y
			 * but for whatever reason there is some buffering happening and this forces
			 * a flush, using xcb_flush doesn't not seem to work in this case */
			if (!querypointer(&x, &y) || (e->time - last) < (1000 / 60))
				break;
			last = e->time;
			if (move)
				nx = ox + (x - mx), ny = oy + (y - my);
			else
				nw = ow + (x - mx), nh = oh + (y - my);
			if ((nw != c->w || nh != c->h || nx != c->x || ny != c->y) && !c->floating && selws->layout->fn) {
				c->old_x = c->x, c->old_y = c->y, c->old_h = c->h, c->old_w = c->w;
				cmdfloat(NULL);
				layoutws(c->ws);
			}
			if (!c->ws->layout->fn || c->floating) {
				if (move && (m = coordtomon(x, y)) != c->ws->mon) {
					setclientws(c, m->ws->num);
					changews(m->ws, 1);
					focus(c);
				}
				resizehint(c, nx, ny, nw, nh, c->bw, 1);
			}
			break;
		case XCB_BUTTON_RELEASE:
			released = 1;
			break;
		default:
			eventhandle(ev);
			break;
		}
		free(ev);
	}
	ungrabpointer();
	if (!move)
		eventignore(XCB_ENTER_NOTIFY);
}

Client *nexttiled(Client *c)
{ /* return c if it's not floating, or walk the list until we find one that isn't */
	while (c && c->floating)
		c = c->next;
	return c;
}

char *optparse(char **argv, char **opts, int *argi, float *argf, int hex)
{ /* parses argv for arguments we're interested in */
	int n;
	float f;
	char *opt = NULL;

	if (!argv || !*argv)
		return opt;
	if (argi)
		*argi = UNSET;
	if (argf)
		*argf = 0.0;
	while (*argv) {
		if (argi && ((hex && **argv == '#' && strlen(*argv) == 7)
					|| (n = strtol(*argv, NULL, 0)) || **argv == '0'))
			*argi = hex && **argv == '#' ? strtol(++*argv, NULL, 16) : n;
		else if (argf && (f = strtof(*argv, NULL)))
			*argf = f;
		else for (; !opt && opts && *opts; opts++)
			if (!strcmp(*opts, *argv))
				opt = *argv;
		argv++;
	}
	return opt;
}

Monitor *outputtomon(xcb_randr_output_t id)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (m->id == id)
			break;
	return m;
}

#ifdef DEBUG
void print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
#endif

int querypointer(int *x, int *y)
{
	xcb_generic_error_t *e;
	xcb_query_pointer_reply_t *p;

	if ((p = xcb_query_pointer_reply(con, xcb_query_pointer(con, root), &e))) {
		*x = p->root_x, *y = p->root_y;
		free(p);
		return 1;
	} else {
		checkerror("unable to query pointer", e);
	}
	return 0;
}

Monitor *randrclone(xcb_randr_output_t id, int x, int y)
{
	Monitor *m;

	FOR_EACH(m, monitors)
		if (id != m->id && m->x == x && m->y == y)
			break;
	return m;
}

void resize(Client *c, int x, int y, int w, int h, int bw)
{
	uint v[] = { x, y, w, h, bw };

	c->old_x = c->x, c->old_y = c->y;
	c->old_w = c->w, c->old_h = c->h;
	c->x = x, c->y = y, c->w = w, c->h = h;
	xcb_configure_window(con, c->win, XYMASK | WHMASK | BWMASK, v);
	configure(c);
}

void resizehint(Client *c, int x, int y, int w, int h, int bw, int usermotion)
{
	if (applysizehints(c, &x, &y, &w, &h, usermotion))
		resize(c, x, y, w, h, bw);
}

void restack(Workspace *ws)
{
	Client *c;

	if (!ws)
		ws = selws;
	if (!ws || !(c = ws->sel))
		return;
	DBG("restacking clients on workspace: %d", ws->num)
	if (c->floating || !ws->layout->fn)
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
	if (ws->layout->fn) {
		FOR_STACK(c, ws->stack)
			if (!c->floating && c->ws == c->ws->mon->ws)
				setstackmode(c->win, XCB_STACK_MODE_BELOW);
	}
	eventignore(XCB_ENTER_NOTIFY);
}

int rulecmp(regex_t *r, char *class, char *inst)
{
	return (!regexec(r, class, 0, NULL, 0) || !regexec(r, inst, 0, NULL, 0));
}

void send(int num)
{
	Client *c;

	if (!(c = selws->sel) || num == selws->num || !itows(num))
		return;
	unfocus(c, 1);
	setclientws(c, num);
	focus(NULL);
	layoutws(NULL);
}

int sendevent(Client *c, int wmproto)
{
	int n, exists = 0;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t rpc;
	xcb_client_message_event_t cme;
	xcb_icccm_get_wm_protocols_reply_t proto;

	rpc = xcb_icccm_get_wm_protocols(con, c->win, wmatoms[Protocols]);
	if (xcb_icccm_get_wm_protocols_reply(con, rpc, &proto, &e)) {
		n = proto.atoms_len;
		while (!exists && n--)
			exists = proto.atoms[n] == wmatoms[wmproto];
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	} else {
		checkerror("unable to get wm protocol for requested send event", e);
	}

	if (exists) {
		cme.response_type = XCB_CLIENT_MESSAGE;
		cme.window = c->win;
		cme.type = wmatoms[Protocols];
		cme.format = 32;
		cme.data.data32[0] = wmatoms[wmproto];
		cme.data.data32[1] = XCB_TIME_CURRENT_TIME;
		xcb_send_event(con, 0, c->win, XCB_EVENT_MASK_NO_EVENT, (const char *)&cme);
	}
	return exists;
}

void setstackmode(xcb_window_t win, uint mode)
{
	xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, &mode);
}

void setwinstate(xcb_window_t win, uint32_t state)
{
	uint32_t s[] = { state, XCB_ATOM_NONE };
	PROP_REPLACE(win, wmatoms[WMState], wmatoms[WMState], 32, 2, s);
}

void setnetworkareavp(void)
{
	uint wa[4];
	Workspace *ws;

	xcb_delete_property(con, root, netatoms[WorkArea]);
	xcb_delete_property(con, root, netatoms[DesktopViewport]);
	FOR_EACH(ws, workspaces) {
		wa[0] = ws->mon->winarea_x, wa[1] = ws->mon->winarea_y;
		wa[2] = ws->mon->winarea_w, wa[3] = ws->mon->winarea_h;
		PROP_APPEND(root, netatoms[WorkArea], XCB_ATOM_CARDINAL, 32, 4, wa);
		PROP_APPEND(root, netatoms[DesktopViewport], XCB_ATOM_CARDINAL, 32, 2, wa);
	}
}

void setclientws(Client *c, uint num)
{
	DBG("setting client atom -- _NET_WM_DESKTOP: %d", num)
	if (c->ws) {
		detach(c, 0);
		detachstack(c);
	}
	c->ws = itows(num);
	PROP_REPLACE(c->win, netatoms[Desktop], XCB_ATOM_CARDINAL, 32, 1, &num);
	attach(c, 0);
	attachstack(c);
}

void setfullscreen(Client *c, int fullscreen)
{
	Monitor *m;

	if (!c->ws || !(m = c->ws->mon))
		m = selws->mon;
	if (fullscreen && !c->fullscreen) {
		PROP_REPLACE(c->win, netatoms[State], XCB_ATOM_ATOM, 32, 1, &netatoms[Fullscreen]);
		c->oldstate = c->floating;
		c->old_bw = c->bw;
		c->fullscreen = 1;
		c->floating = 1;
		c->bw = 0;
		resize(c, m->x, m->y, m->w, m->h, c->bw);
		setstackmode(c->win, XCB_STACK_MODE_ABOVE);
		xcb_flush(con);
	} else if (!fullscreen && c->fullscreen) {
		PROP_REPLACE(c->win, netatoms[State], XCB_ATOM_ATOM, 32, 0, (uchar *)0);
		c->floating = c->oldstate;
		c->fullscreen = 0;
		c->bw = c->old_bw;
		c->x = c->old_x;
		c->y = c->old_y;
		c->w = c->old_w;
		c->h = c->old_h;
		resize(c, c->x, c->y, c->w, c->h, c->bw);
		layoutws(c->ws);
	}
}

void seturgency(Client *c, int urg)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	c->urgent = urg;
	DBG("setting urgency hint for window: 0x%08x -- value: %d", c->win, urg)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		wmh.flags = urg ? (wmh.flags | XCB_ICCCM_WM_HINT_X_URGENCY)
			: (wmh.flags & ~XCB_ICCCM_WM_HINT_X_URGENCY);
		xcb_icccm_set_wm_hints(con, c->win, &wmh);
	} else {
		checkerror("unable to get wm window hints", e);
	}
}

void showhide(Client *c)
{
	if (!c)
		return;
	if (c->ws == c->ws->mon->ws) { /* show clients top down */
		DBG("showing window: 0x%08x - workspace: %d", c->win, c->ws->num)
		MOVE(c->win, c->x, c->y);
		if ((!c->ws->layout->fn || c->floating) && !c->fullscreen)
			resize(c, c->x, c->y, c->w, c->h, c->bw);
		showhide(c->snext);
	} else { /* hide clients bottom up */
		showhide(c->snext);
		DBG("hiding window: 0x%08x - workspace: %d", c->win, c->ws->num)
		MOVE(c->win, W(c) * -2, c->y);
	}
}

void sighandle(int sig)
{
	switch (sig) {
	case SIGINT: /* fallthrough */
	case SIGTERM: /* fallthrough */
	case SIGHUP: /* fallthrough */
		running = 0;
		break;
	case SIGCHLD:
		signal(sig, sighandle);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		break;
	}
}

void sizehints(Client *c)
{
	xcb_size_hints_t s;
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_normal_hints(con, c->win);
	DBG("checking size hints for window: 0x%08x", c->win)
	c->max_aspect = c->min_aspect = 0.0;
	c->increment_w = c->increment_h = 0;
	c->min_w = c->min_h = c->max_w = c->max_h = c->base_w = c->base_h = 0;
	if (xcb_icccm_get_wm_normal_hints_reply(con, pc, &s, &e)) {
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->min_aspect = (float)s.min_aspect_den / s.min_aspect_num;
			c->max_aspect = (float)s.max_aspect_num / s.max_aspect_den;
			DBG("set min/max aspect: min = %f, max = %f", c->min_aspect, c->max_aspect)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
			c->max_w = s.max_width, c->max_h = s.max_height;
			DBG("set max size: %dx%d", c->max_w, c->max_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
			c->increment_w = s.width_inc, c->increment_h = s.height_inc;
			DBG("set increment size: %dx%d", c->increment_w, c->increment_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->base_w = s.base_width, c->base_h = s.base_height;
			DBG("set base size: %dx%d", c->base_w, c->base_h)
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->base_w = s.min_width, c->base_h = s.min_height;
			DBG("set base size to min size: %dx%d", c->base_w, c->base_h)
		}
		if (s.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->min_w = s.min_width, c->min_h = s.min_height;
			DBG("set min size: %dx%d", c->min_w, c->min_h)
		} else if (s.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->min_w = s.base_width, c->min_h = s.base_height;
			DBG("set min size to base size: %dx%d", c->min_w, c->min_h)
		}
	} else {
		checkerror("unable to get wm normal hints", e);
	}
	c->fixed = (c->max_w && c->max_h && c->max_w == c->min_w && c->max_h == c->min_h);
	DBG("window is %s size", c->fixed ? "fixed" : "variable")
}

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t n = size, dlen;
	const char *odst = dst;
	const char *osrc = src;

	while (n-- != 0 && *dst != '\0')
		dst++;
	dlen = dst - odst;
	n = size - dlen;

	if (n-- == 0)
		return dlen + strlen(src);
	while (*src != '\0') {
		if (n != 0) {
			*dst++ = *src;
			n--;
		}
		src++;
	}
	*dst = '\0';

	return dlen + (src - osrc);
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t n = size;
	const char *osrc = src;

	if (n != 0)
		while (--n != 0)
			if ((*dst++ = *src++) == '\0')
				break;
	if (n == 0) {
		if (size != 0)
			*dst = '\0';
		while (*src++);
	}
	return src - osrc - 1;
}

void takefocus(Client *c)
{
	if (!c->nofocus) {
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
		PROP_REPLACE(root, netatoms[ActiveWindow], XCB_ATOM_WINDOW, 32, 1, &c->win);
	}
	sendevent(c, TakeFocus);
}

void tile(Workspace *ws)
{
	Client *c;
	Monitor *m = ws->mon;
	uint i, n, my, ssy, sy, nr;
	uint mw = 0, ss = 0, sw = 0, ns = 1, bw = 0;

	for (n = 0, c = nexttiled(ws->clients); c; c = nexttiled(c->next), n++)
		;
	if (!n)
		return;
	if (n > 1 || !borders[Smart])
		bw = borders[Width];
	if (n <= ws->nmaster)
		mw = m->winarea_w, ss = 1;
	else if (ws->nmaster)
		ns = 2, mw = m->winarea_w * ws->split;
	if (ws->nstack && n - ws->nmaster > ws->nstack)
		ss = 1, sw = (m->winarea_w - mw) / 2;

	DBG("workspace: %d - nmaster: %d - nstack: %d - "
			"nclients: %d - master width: %d - stack width: %d - gap width: %d",
			ws->num, ws->nmaster, ws->nstack, n, mw, sw, ws->gappx)

	for (i = 0, my = sy = ssy = ws->gappx, c = nexttiled(ws->clients); c; c = nexttiled(c->next), ++i) {
		if (i < ws->nmaster) {
			nr = MIN(n, ws->nmaster) - i;
			resize(c, m->winarea_x + ws->gappx, m->winarea_y + my,
					mw - ws->gappx * (5 - ns) / 2 - (2 * bw),
					((m->winarea_h - my) / MAX(1, nr)) - ws->gappx - (2 * bw), bw);
			my += c->h + (2 * bw) + ws->gappx;
		} else if (i - ws->nmaster < ws->nstack) {
			nr = MIN(n - ws->nmaster, ws->nstack) - (i - ws->nmaster);
			resize(c, m->winarea_x + mw + (ws->gappx / ns), m->winarea_y + sy,
					(m->winarea_w - mw - sw - ws->gappx * (5 - ns - ss) / 2) - (2 * bw),
					(m->winarea_h - sy) / MAX(1, nr) - ws->gappx - (2 * bw), bw);
			sy += c->h + (2 * bw) + ws->gappx;
		} else {
			resize(c, m->winarea_x + mw + sw + (ws->gappx / ns), m->winarea_y + ssy,
					(m->winarea_w - mw - sw - ws->gappx * (5 - ns) / 2) - (2 * bw),
					(m->winarea_h - ssy) / MAX(1, n - i) - ws->gappx - (2 * bw), c->bw);
			ssy += c->h + (2 * bw) + ws->gappx;
		}
		DBG("tiled window %d of %d: %d,%d @ %dx%d", i + 1, n, c->x, c->y, W(c), c->h + (2 * bw))
	}
}

void unfocus(Client *c, int focusroot)
{
	if (!c)
		return;
	DBG("unfocusing client window: 0x%08x", c->win)
	grabbuttons(c, 0);
	xcb_change_window_attributes(con, c->win, XCB_CW_BORDER_PIXEL, &borders[Unfocus]);
	if (focusroot) {
		DBG("focusing root window")
		xcb_set_input_focus(con, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
		xcb_delete_property(con, root, netatoms[ActiveWindow]);
	}
}

void ungrabpointer(void)
{
	xcb_void_cookie_t c;
	xcb_generic_error_t *e;

	c = xcb_ungrab_pointer_checked(con, XCB_CURRENT_TIME);
	if ((e = xcb_request_check(con, c))) {
		free(e);
		errx(1, "failed to ungrab pointer");
	}
}

void updatenumws(int needed)
{
	char name[4]; /* we're never gonna have more than 999 workspaces */
	Workspace *ws;
	WorkspaceRule r;

	DBG("more monitors than workspaces, allocating enough for each monitor")
	if (needed > 999)
		errx(1, "attempting to allocate too many workspaces");
	else if (needed > numws) {
		while (needed > numws) {
			FIND_TAIL(ws, workspaces);
			r.name = itoa(numws, name);
			r.nmaster = ws->nmaster;
			r.nstack = ws->nstack;
			r.gappx = ws->gappx;
			r.split = ws->split;
			r.layout = ws->layout;
			ws->next = initws(numws, &r);
			numws++;
		}
	}
	PROP_REPLACE(root, netatoms[NumDesktops], XCB_ATOM_CARDINAL, 32, 1, &numws);
}

int updateoutputs(xcb_randr_output_t *outs, int len, xcb_timestamp_t timestamp)
{
	uint n;
	Monitor *m;
	char name[64];
	int i, changed = 0;
	xcb_generic_error_t *e;
	xcb_randr_get_crtc_info_cookie_t c;
	xcb_randr_get_output_info_reply_t *o;
	xcb_randr_get_crtc_info_reply_t *crtc;
	xcb_randr_get_output_info_cookie_t oc[len];
	xcb_randr_get_output_primary_reply_t *po;

	DBG("%d outputs, requesting info for each")
	for (i = 0; i < len; i++)
		oc[i] = xcb_randr_get_output_info(con, outs[i], timestamp);
	for (i = 0; i < len; i++) {
		if (!(o = xcb_randr_get_output_info_reply(con, oc[i], &e))) {
			checkerror("unable to get monitor info", e);
			continue;
		}
		if (o->crtc != XCB_NONE) {
			c = xcb_randr_get_crtc_info(con, o->crtc, timestamp);
			if (!(crtc = xcb_randr_get_crtc_info_reply(con, c, &e))) {
				checkerror("crtc info for randr output was NULL", e);
				free(o);
				continue;
			}

			n = xcb_randr_get_output_info_name_length(o) + 1;
			strlcpy(name, (const char *)xcb_randr_get_output_info_name(o), MIN(sizeof(name), n));
			DBG("crtc: %s -- location: %d,%d -- size: %dx%d -- status: %d",
					name, crtc->x, crtc->y, crtc->width, crtc->height, crtc->status)

			if ((m = randrclone(outs[i], crtc->x, crtc->y))) {
				DBG("monitor %s, id %d is a clone of %s, id %d, skipping",
						name, outs[i], m->name, m->id)
			} else if ((m = outputtomon(outs[i]))) {
				DBG("previously initialized monitor: %s -- location and size: %d,%d @ %dx%d",
						m->name, m->x, m->y, m->w, m->h)
				changed = (crtc->x != m->x || crtc->y != m->y
						|| crtc->width != m->w || crtc->height != m->h);
				if (crtc->x != m->x)      m->x = m->winarea_x = crtc->x;
				if (crtc->y != m->y)      m->y = m->winarea_y = crtc->y;
				if (crtc->width != m->w)  m->w = m->winarea_w = crtc->width;
				if (crtc->height != m->h) m->h = m->winarea_h = crtc->height;
				DBG("size and location for monitor: %s -- %d,%d @ %dx%d -- %s",
						m->name, m->x, m->y, m->w, m->h, changed ? "updated" : "unchanged")
			} else {
				FIND_TAIL(m, monitors);
				if (m)
					m->next = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				else
					monitors = initmon(name, outs[i], crtc->x, crtc->y, crtc->width, crtc->height);
				changed = 1;
			}
			free(crtc);
		} else if ((m = outputtomon(outs[i])) && o->connection == XCB_RANDR_CONNECTION_DISCONNECTED) {
			DBG("previously initialized monitor is now inactive: %s -- freeing", m->name)
			freemon(m);
			changed = 1;
		}
		free(o);
	}
	if ((po = xcb_randr_get_output_primary_reply(con, xcb_randr_get_output_primary(con, root), NULL))) {
		primary = outputtomon(po->output);
		free(po);
		xcb_warp_pointer(con, root, root, 0, 0, 0, 0,
				primary->x + (primary->w / 2), primary->y + (primary->h / 2));
	}
	return changed;
}

int updaterandr(void)
{
	int len, changed;
	xcb_timestamp_t timestamp;
	xcb_generic_error_t *e;
	xcb_randr_output_t *outputs;
	xcb_randr_get_screen_resources_current_reply_t *r;
	xcb_randr_get_screen_resources_current_cookie_t rc;

	DBG("querying current randr outputs")
	rc = xcb_randr_get_screen_resources_current(con, root);
	if (!(r = xcb_randr_get_screen_resources_current_reply(con, rc, &e))) {
		checkerror("unable to get screen resources", e);
		return -1;
	}

	timestamp = r->config_timestamp;
	len = xcb_randr_get_screen_resources_current_outputs_length(r);
	outputs = xcb_randr_get_screen_resources_current_outputs(r);
	changed = updateoutputs(outputs, len, timestamp);
	free(r);
	return changed;
}

void updatestruts(Panel *p, int apply)
{
	Panel *n;
	Monitor *m;

	DBG("resetting struts for each monitor")
	FOR_EACH(m, monitors) {
		m->winarea_x = m->x, m->winarea_y = m->y, m->winarea_w = m->w, m->winarea_h = m->h;
	}
	if (!p)
		return;
	if (apply && !panels)
		applypanelstrut(p);
	DBG("applying each panel strut where needed")
	FOR_EACH(n, panels)
		if ((apply || n != p) && (n->strut_l || n->strut_r || n->strut_t || n->strut_b))
			applypanelstrut(p);
}

void view(int num)
{
	Workspace *ws;

	if (num == selws->num || !(ws = itows(num)))
		return;
	changews(ws, 0);
	focus(NULL);
	layoutws(NULL);
	restack(selws);
}

xcb_get_window_attributes_reply_t *winattr(xcb_window_t win)
{
	xcb_generic_error_t *e;
	xcb_get_window_attributes_cookie_t c;
	xcb_get_window_attributes_reply_t *wa;

	c = xcb_get_window_attributes(con, win);
	DBG("getting window attributes from window: 0x%08x", win)
	if (!(wa = xcb_get_window_attributes_reply(con, c, &e)))
		checkerror("unable to get window attributes", e);
	return wa;
}

void winhints(Client *c)
{
	xcb_generic_error_t *e;
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t pc;

	pc = xcb_icccm_get_wm_hints(con, c->win);
	DBG("checking and setting wm hints for window: 0x%08x", c->win)
	if (xcb_icccm_get_wm_hints_reply(con, pc, &wmh, &e)) {
		if (c == selws->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
			wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
			xcb_icccm_set_wm_hints(con, c->win, &wmh);
		} else {
			c->urgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? 1 : 0;
		}
		c->nofocus = (wmh.flags & XCB_ICCCM_WM_HINT_INPUT) ? !wmh.input : 0;
	} else {
		checkerror("unable to get wm window hints", e);
	}
}

xcb_atom_t winprop(xcb_window_t win, xcb_atom_t prop)
{
	xcb_atom_t ret;
	xcb_generic_error_t *e;
	xcb_get_property_reply_t *r;
	xcb_get_property_cookie_t c;

	c = xcb_get_property(con, 0, win, prop, XCB_ATOM_ANY, 0, 1);
	ret = -1;
	DBG("getting window property atom %d from window: 0x%08x", prop, win)
	if ((r = xcb_get_property_reply(con, c, &e))) {
		if (xcb_get_property_value_length(r))
			ret = *(xcb_atom_t *)xcb_get_property_value(r);
		free(r);
	} else {
		checkerror("unable to get window property", e);
	}
	return ret;
}

int wintextprop(xcb_window_t w, xcb_atom_t atom, char *text, size_t size)
{
	xcb_generic_error_t *e;
	xcb_get_property_cookie_t c;
	xcb_icccm_get_text_property_reply_t r;

	c = xcb_icccm_get_text_property(con, w, atom);
	if (!xcb_icccm_get_text_property_reply(con, c, &r, &e)) {
		checkerror("failed to get text property", e);
		return 0;
	}

	/* FIXME: encoding */

	if(!r.name || !r.name_len)
		return 0;
	strlcpy(text, r.name, size);
	xcb_icccm_get_text_property_reply_wipe(&r);
	return 1;
}

Client *wintoclient(xcb_window_t win)
{
	Client *c;
	Workspace *ws;

	if (win == root)
		return NULL;
	FOR_CLIENTS(c, ws)
		if (c->win == win)
			return c;
	return NULL;
}

Panel *wintopanel(xcb_window_t win)
{
	Panel *p;

	if (win == root)
		return NULL;
	FOR_EACH(p, panels)
		if (p->win == win)
			return p;
	return p;
}

Workspace *wintows(xcb_window_t win)
{
	int x, y;
	Client *c;
	Workspace *ws;

	if (win == root && querypointer(&x, &y))
		return coordtomon(x, y)->ws;
	FOR_CLIENTS(c, ws)
		if (c->win == win)
			return ws;
	return selws;
}

xcb_window_t wintrans(xcb_window_t win)
{
	xcb_window_t trans;
	xcb_get_property_cookie_t pc;
	xcb_generic_error_t *e = NULL;

	pc = xcb_icccm_get_wm_transient_for(con, win);
	trans = XCB_WINDOW_NONE;
	DBG("getting transient for hint - window: 0x%08x", win)
	if (!xcb_icccm_get_wm_transient_for_reply(con, pc, &trans, &e) && e) {
		warnx("unable to get wm transient for hint - X11 error: %d: %s",
				e->error_code, xcb_event_get_error_label(e->error_code));
		free(e);
	}
	return trans;
}

void wintype(Client *c)
{
	xcb_atom_t t;

	DBG("checking window type for window: 0x%08x", c->win)
	if (winprop(c->win, netatoms[State]) == netatoms[Fullscreen])
		setfullscreen(c, 1);
	else if ((t = winprop(c->win, netatoms[WindowType])) == netatoms[WindowTypeDialog])
		c->floating = 1;
}