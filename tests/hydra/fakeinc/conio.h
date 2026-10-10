/* stand-ins for Turbo C's console calls (HydraCom on Linux, a test
 * reference): nothing is drawn */
#define cprintf(...)   ((void)0)
#define gotoxy(x, y)   ((void)0)
#define window(a,b,c,d) ((void)0)
#define textattr(a)    ((void)0)
#define textcolor(a)   ((void)0)
#define textbackground(a) ((void)0)
#define clrscr()       ((void)0)
#define clreol()       ((void)0)
#define wherex()       1
#define wherey()       1
struct text_info { unsigned char winleft, wintop, winright, winbottom,
    attribute, normattr, currmode, screenheight, screenwidth, curx, cury; };
#define gettextinfo(t) ((t)->screenheight = 25, (t)->screenwidth = 80, \
    (t)->curx = (t)->cury = 1, (t)->attribute = 7, (void)0)
#define gettext(a,b,c,d,e) 1
#define puttext(a,b,c,d,e) 1
#define _setcursortype(a) ((void)0)
#define highvideo() ((void)0)
#define lowvideo() ((void)0)
#define normvideo() ((void)0)
#define putch(c) ((void)0)
