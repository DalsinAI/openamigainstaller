/*
 * The Installer's window on gfopenlayout.library 11 (GFOpenLayout UI 0.4):
 * each page is markup, laid out and drawn in the theme GFOpenPrefs Look
 * chose, in the screen's font (the one Font prefs set). Down the left a
 * quarter of the window is the picture panel (oi_side, panel_amiga.c),
 * drawn by us in a box olui leaves to us (OLUI_AmigaOwn).
 *
 * The library is opened here and called through its vectors (the OLUI_*
 * calls), not through libgfopenlayout_link.a, so without it the program
 * still starts and main.c opens the GadTools window instead.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_panel.h"
#include "ui_pages.h"

#ifdef OI_AMIGA
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/asl.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <proto/gfopenlayout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *OpenLayoutBase;
struct Library *AslBase;

#define PANEL_ARGB 0xff1e2430UL

typedef struct ow {
    oi *g;
    struct Screen *scr;
    struct Window *win;
    olui *ui;
    olui_amiga *a;
    oi_side side;
    int pressed;            /* a button's index, -1 none yet */
    int pick;               /* the requester's button was pressed */
    const oi_page *page;    /* the page loaded */
    int progress_loaded;
    char progress_text[512];
    int enter_btn;
    int file_button;
    const olui_el *pic;     /* the panel's box as handed to OLUI_AmigaOwn */
} ow;

/* ---- Markup ---------------------------------------------------------------------- */

typedef struct sb {
    char *s;
    size_t n, cap;
} sb;

static void sb_add(sb *b, const char *t)
{
    size_t l = strlen(t);
    if (!b->s && b->cap)
        return;     /* out of memory before */
    if (b->n + l + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        char *s;
        while (c < b->n + l + 1)
            c *= 2;
        s = realloc(b->s, c);
        if (!s) {
            free(b->s);
            b->s = NULL;
            b->cap = 1;
            return;
        }
        b->s = s;
        b->cap = c;
    }
    memcpy(b->s + b->n, t, l + 1);
    b->n += l;
}

static void sb_addf(sb *b, const char *fmt, long a, long c)
{
    char t[128];
    snprintf(t, sizeof t, fmt, a, c);
    sb_add(b, t);
}

/* Text as markup: <, > and & as entities, a newline a line break. */
static void sb_text(sb *b, const char *t)
{
    char one[2];
    one[1] = 0;
    for (; t && *t; t++) {
        switch (*t) {
        case '<': sb_add(b, "&lt;"); break;
        case '>': sb_add(b, "&gt;"); break;
        case '&': sb_add(b, "&amp;"); break;
        case '"': sb_add(b, "&quot;"); break;
        case '\n': sb_add(b, "<br>"); break;
        case '\r': break;
        default: one[0] = *t; sb_add(b, one); break;
        }
    }
}

static const char css[] =
    "#side{width:160px;gap:4px;padding:6px}"
    "#pic{flex:1;background:#1e2430}"
    "#cap{text-align:center}"
    "#main{flex:1;padding:8px;gap:6px}"
    "#scroll{flex:1;overflow:auto}"
    "#btns{gap:6px;justify-content:end}"
    "#btns button{padding-left:10px;padding-right:10px}"
    "h2{margin-bottom:4px}"
    "progress{height:18px}";

/* The markup of a page; the side panel's box (#pic) stays the same in all. */
static char *page_markup(ow *w, const oi_page *p, const char *caption)
{
    sb b;
    int i;
    memset(&b, 0, sizeof b);
    sb_add(&b, "<row style='flex:1'><column id=side><div id=pic></div><p id=cap>");
    sb_text(&b, caption);
    sb_add(&b, "</p></column><column id=main>");
    if (p->title) {
        sb_add(&b, "<h2>");
        sb_text(&b, p->title);
        sb_add(&b, "</h2>");
    }
    sb_add(&b, "<div id=scroll><p id=text>");
    sb_text(&b, p->text);
    sb_add(&b, "</p>");
    switch (p->kind) {
    case PG_RADIO:
    case PG_CHECK:
        for (i = 0; i < p->nitems; i++) {
            int off = (p->disabled >> i) & 1;
            int on = p->kind == PG_RADIO ? p->value == i : (int)((p->value >> i) & 1);
            if (off && !p->items[i][0])
                continue;   /* an empty choice: not shown */
            if (p->kind == PG_RADIO)
                sb_addf(&b, "<radio name=r id=r%ld value=%ld", i, i);
            else
                sb_addf(&b, "<checkbox id=c%ld value=%ld", i, i);
            if (on)
                sb_add(&b, " checked");
            if (off)
                sb_add(&b, " disabled");
            sb_add(&b, ">");
            sb_text(&b, p->items[i]);
            sb_add(&b, p->kind == PG_RADIO ? "</radio>" : "</checkbox>");
        }
        break;
    case PG_STRING:
        sb_add(&b, "<row style='gap:6px'><input id=str style='flex:1' value=\"");
        sb_text(&b, p->str);
        sb_add(&b, "\">");
        if (p->file_button)
            sb_add(&b, "<button id=pick>Choose...</button>");
        sb_add(&b, "</row>");
        break;
    case PG_NUMBER:
        sb_addf(&b, "<spinner id=num min=%ld max=%ld", p->min, p->max);
        sb_addf(&b, " value=%ld>", p->value, 0);
        break;
    case PG_PROGRESS:
        sb_add(&b, "<progress id=bar max=100></progress><p id=sub1></p><p id=sub2></p>");
        break;
    }
    if (p->nitems2) {
        sb_add(&b, "<p>");
        sb_text(&b, p->label2);
        sb_add(&b, "</p>");
        for (i = 0; i < p->nitems2; i++) {
            sb_addf(&b, "<radio name=l id=l%ld value=%ld", i, i);
            if (p->value2 == i)
                sb_add(&b, " checked");
            sb_add(&b, ">");
            sb_text(&b, p->items2[i]);
            sb_add(&b, "</radio>");
        }
    }
    sb_add(&b, "</div><row id=btns>");
    for (i = 0; i < p->nbtn; i++) {
        sb_addf(&b, "<button id=b%ld>", i, 0);
        sb_text(&b, p->btn[i]);
        sb_add(&b, "</button>");
    }
    sb_add(&b, "</row></column></row>");
    (void)w;
    return b.s;
}

/* ---- The window --------------------------------------------------------------- */

static int inner_width(ow *w)
{
    return w->win->Width - w->win->BorderLeft - w->win->BorderRight;
}

/* The panel a quarter of the window wide (CSS has no percentages here). */
static void size_side(ow *w)
{
    char st[48];
    olui_el *side = OLUI_Find(w->ui, "side");
    if (side) {
        snprintf(st, sizeof st, "width:%dpx", inner_width(w) / 4);
        OLUI_SetStyle(w->ui, side, st);
    }
}

static void own_pic(void *userdata, const olui_el *el, int x, int y, int wd, int ht)
{
    ow *w = userdata;
    (void)el;
    oi_side_draw(&w->side, w->win->RPort, x, y, wd, ht);
}

static void on_event(olui *ui, const olui_event *ev, void *userdata)
{
    ow *w = userdata;
    (void)ui;
    if (ev->type == OLUI_CLICK && ev->id[0] == 'b' && ev->id[1] >= '0' && ev->id[1] <= '9' && !ev->id[2])
        w->pressed = ev->id[1] - '0';
    else if (ev->type == OLUI_CLICK && !strcmp(ev->id, "pick"))
        w->pick = 1;
    else if (ev->type == OLUI_SUBMIT && w->enter_btn >= 0)
        w->pressed = w->enter_btn;
}

static int load(ow *w, const oi_page *p)
{
    char err[160];
    char *m;
    olui_el *pic, *focus;
    oi_panel *pn = oi_panel_get(w->g);
    m = page_markup(w, p, pn->caption);
    if (!m)
        return 0;
    /* The last page's box goes before its elements do. olui_amiga keeps
     * four drawn-by-us boxes and matches them by pointer: left there, the
     * table filled after four pages (the panel then went undrawn), and an
     * element of a later page put where the old box had been (a help
     * page's text) was drawn over with the picture. */
    if (w->pic) {
        OLUI_AmigaOwn(w->a, w->pic, NULL, NULL);
        w->pic = NULL;
    }
    if (!OLUI_Load(w->ui, m, NULL, err, sizeof err)) {
        free(m);
        return 0;
    }
    free(m);
    w->page = p;
    w->enter_btn = p->enter_btn;
    w->file_button = p->file_button;
    w->progress_loaded = p->kind == PG_PROGRESS;
    size_side(w);
    pic = OLUI_Find(w->ui, "pic");
    if (pic && OLUI_AmigaOwn(w->a, pic, own_pic, w))
        w->pic = pic;
    OLUI_AmigaLayout(w->a);
    focus = NULL;
    if (p->kind == PG_STRING)
        focus = OLUI_Find(w->ui, "str");
    else if (p->kind == PG_NUMBER)
        focus = OLUI_Find(w->ui, "num");
    else if (p->enter_btn >= 0) {
        char id[16];
        snprintf(id, sizeof id, "b%d", p->enter_btn);
        focus = OLUI_Find(w->ui, id);
    }
    if (focus)
        OLUI_Focus(w->ui, focus);
    OLUI_AmigaDraw(w->a);
    return 1;
}

/* What the page's widgets hold now, into the page. */
static void read_back(ow *w, oi_page *p)
{
    int i;
    char id[16];
    olui_el *el;
    switch (p->kind) {
    case PG_RADIO:
        for (i = 0; i < p->nitems; i++) {
            snprintf(id, sizeof id, "r%d", i);
            if ((el = OLUI_Find(w->ui, id)) && OLUI_GetChecked(el))
                p->value = i;
        }
        break;
    case PG_CHECK:
        p->value = 0;
        for (i = 0; i < p->nitems; i++) {
            snprintf(id, sizeof id, "c%d", i);
            if ((el = OLUI_Find(w->ui, id)) && OLUI_GetChecked(el))
                p->value |= 1L << i;
        }
        break;
    case PG_STRING:
        if ((el = OLUI_Find(w->ui, "str")))
            snprintf(p->str, sizeof p->str, "%s", OLUI_GetText(el));
        break;
    case PG_NUMBER:
        if ((el = OLUI_Find(w->ui, "num")))
            p->value = OLUI_NumberValue(el);
        break;
    }
    for (i = 0; i < p->nitems2; i++) {
        snprintf(id, sizeof id, "l%d", i);
        if ((el = OLUI_Find(w->ui, id)) && OLUI_GetChecked(el))
            p->value2 = i;
    }
}

/* The drawer or file requester for askdir and askfile's Choose... */
static void pick(ow *w)
{
    olui_el *el = OLUI_Find(w->ui, "str");
    char cur[512], drawer[512], file[256];
    struct FileRequester *fr;
    if (!el)
        return;
    if (!AslBase)
        AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (!AslBase)
        return;
    snprintf(cur, sizeof cur, "%s", OLUI_GetText(el));
    if (w->file_button == 2) {
        snprintf(drawer, sizeof drawer, "%s", cur);
        *PathPart((STRPTR)drawer) = 0;
        snprintf(file, sizeof file, "%s", (const char *)FilePart((STRPTR)cur));
    } else {
        snprintf(drawer, sizeof drawer, "%s", cur);
        file[0] = 0;
    }
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)w->win, ASLFR_SleepWindow, TRUE,
                             ASLFR_InitialDrawer, (ULONG)drawer, ASLFR_InitialFile, (ULONG)file,
                             ASLFR_DrawersOnly, w->file_button == 1, ASLFR_RejectIcons, TRUE, TAG_DONE);
    if (!fr)
        return;
    if (AslRequestTags(fr, TAG_DONE)) {
        char out[512];
        snprintf(out, sizeof out, "%s", (const char *)fr->fr_Drawer);
        if (w->file_button == 2)
            AddPart((STRPTR)out, fr->fr_File, sizeof out);
        OLUI_SetText(w->ui, el, out);
    }
    FreeAslRequest(fr);
}

/* The window's messages; 1 when the user wants out (Ctrl-C). */
static int events(ow *w)
{
    struct IntuiMessage *m;
    int quit = 0;
    while ((m = (struct IntuiMessage *)GetMsg(w->win->UserPort))) {
        if (m->Class == IDCMP_NEWSIZE)
            size_side(w);
        OLUI_AmigaEvent(w->a, m);
        ReplyMsg(&m->ExecMessage);
    }
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        quit = 1;
    if (w->pick) {
        w->pick = 0;
        pick(w);
    }
    oi_side_tick(&w->side);
    OLUI_AmigaDraw(w->a);
    return quit;
}

static int b_run(void *b, oi_page *p)
{
    ow *w = b;
    if (!load(w, p))
        return -1;
    w->pressed = -1;
    while (w->pressed < 0) {
        Wait((1UL << w->win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (events(w))
            return -1;
    }
    read_back(w, p);
    return w->pressed;
}

static void set_sub(ow *w, const char *sub)
{
    char one[256];
    const char *nl = strchr(sub, '\n');
    olui_el *a = OLUI_Find(w->ui, "sub1"), *c = OLUI_Find(w->ui, "sub2");
    size_t n = nl ? (size_t)(nl - sub) : strlen(sub);
    if (n >= sizeof one)
        n = sizeof one - 1;
    memcpy(one, sub, n);
    one[n] = 0;
    if (a)
        OLUI_SetText(w->ui, a, one);
    if (c)
        OLUI_SetText(w->ui, c, nl ? nl + 1 : "");
}

static int b_progress(void *b, oi_page *p)
{
    ow *w = b;
    olui_el *bar;
    if (!w->progress_loaded || strcmp(w->progress_text, p->text ? p->text : "")) {
        if (!load(w, p))
            return -1;
        snprintf(w->progress_text, sizeof w->progress_text, "%s", p->text ? p->text : "");
        w->pressed = -1;
    }
    set_sub(w, p->sub);
    if ((bar = OLUI_Find(w->ui, "bar")))
        OLUI_SetProgress(w->ui, bar, p->value < 0 ? 0 : p->value > 100 ? 100 : (int)p->value);
    if (events(w))
        return 0;
    if (w->pressed >= 0) {
        int i = w->pressed;
        w->pressed = -1;
        w->progress_loaded = 0;     /* a question comes over it: loaded again after */
        return i;
    }
    return -1;
}

static void b_panel(void *b)
{
    ow *w = b;
    olui_el *cap = OLUI_Find(w->ui, "cap");
    oi_side_update(&w->side, w->g, w->scr);
    if (cap)
        OLUI_SetText(w->ui, cap, oi_panel_get(w->g)->caption);
    OLUI_AmigaDraw(w->a);
}

static void b_idle(void *b)
{
    events((ow *)b);
}

static void b_close(void *b)
{
    ow *w = b;
    oi_side_free(&w->side);
    if (w->a)
        OLUI_AmigaFree(w->a);
    if (w->ui)
        OLUI_Free(w->ui);
    if (w->win)
        CloseWindow(w->win);
    if (w->scr)
        UnlockPubScreen(NULL, w->scr);
    if (AslBase) {
        CloseLibrary(AslBase);
        AslBase = NULL;
    }
    if (OpenLayoutBase) {
        CloseLibrary(OpenLayoutBase);
        OpenLayoutBase = NULL;
    }
    free(w);
}

static const oi_backend olui_backend = { b_run, b_progress, b_panel, b_idle, b_close };

/* The window's title: the program being installed, when the script says. */
const char *oi_window_title(oi *g)
{
    static char t[160];
    const char *app = g->app_name ? g->app_name : oi_get_str(g, "@app-name");
    if (app && *app)
        snprintf(t, sizeof t, "%s Installation", app);
    else
        snprintf(t, sizeof t, "OpenInstaller");
    return t;
}

oi_ui *oi_ui_olui_open(oi *g)
{
    ow *w;
    int wd, ht;
    oi_ui *u;
    OpenLayoutBase = OpenLibrary((CONST_STRPTR)"gfopenlayout.library", OPENLAYOUTLIB_DESKTOP_VERSION);
    if (!OpenLayoutBase)
        return NULL;
    w = calloc(1, sizeof *w);
    if (!w)
        goto fail;
    w->g = g;
    w->pressed = -1;
    w->scr = LockPubScreen(NULL);
    if (!w->scr)
        goto fail;
    wd = w->scr->Width < 640 ? w->scr->Width : 640;
    ht = w->scr->Height - w->scr->BarHeight - 1;
    if (ht > 440)
        ht = 440;
    w->win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)w->scr, WA_Title, (ULONG)oi_window_title(g),
                            WA_Left, (w->scr->Width - wd) / 2, WA_Top, (w->scr->Height - ht) / 2,
                            WA_Width, wd, WA_Height, ht, WA_MinWidth, 400, WA_MinHeight, 260,
                            WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                            WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
                            WA_ReportMouse, TRUE, WA_IDCMP, OLUI_AmigaIdcmp() & ~IDCMP_CLOSEWINDOW, TAG_DONE);
    if (!w->win)
        goto fail;
    w->ui = OLUI_New();
    w->a = w->ui ? OLUI_AmigaNew(w->ui, w->win) : NULL;
    if (!w->a)
        goto fail;
    OLUI_AmigaTheme(w->a, NULL);
    OLUI_AddCss(w->ui, css);
    OLUI_On(w->ui, NULL, 0, on_event, w);
    oi_side_init(&w->side, PANEL_ARGB, 1);
    oi_side_update(&w->side, g, w->scr);
    u = oi_pages_open(g, &olui_backend, w);
    if (u)
        return u;
fail:
    if (w)
        b_close(w);
    else if (OpenLayoutBase) {
        CloseLibrary(OpenLayoutBase);
        OpenLayoutBase = NULL;
    }
    return NULL;
}
#else
oi_ui *oi_ui_olui_open(oi *g)
{
    (void)g;
    return NULL;
}
#endif
