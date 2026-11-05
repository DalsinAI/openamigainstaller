/*
 * The Installer's window on GadTools, for an Amiga without
 * gfopenlayout.library 11: the same pages as ui_olui.c's (ui_pages.c makes
 * them), laid out here in the screen's font (the one Font prefs set), with
 * the picture panel down the left quarter (oi_side, panel_amiga.c).
 *
 * The page's text is a read-only list that scrolls when it is long; one
 * choice of several is GadTools' radio buttons (a list when there are many),
 * options a list whose lines tick on and off; askstring, askdir and askfile
 * a field (with an ASL requester for the last two), asknumber a number
 * field, and the progress page a bar drawn in the screen's fill colour.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_panel.h"
#include "ui_pages.h"

#ifdef OI_AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/alib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *GadToolsBase;
extern struct Library *AslBase;
const char *oi_window_title(oi *g);

enum { ID_BTN = 1, ID_TEXT = 10, ID_MX, ID_LIST, ID_STR, ID_PICK, ID_NUM, ID_LOG, ID_SUB1, ID_SUB2 };

#define MAX_LINES 400

typedef struct gw {
    oi *g;
    struct Screen *scr;
    struct Window *win;
    APTR vi;
    struct DrawInfo *dri;
    struct TextFont *font;
    struct TextAttr ta;
    struct Gadget *glist;
    struct Gadget *str, *num, *sub1, *sub2, *list;
    oi_side side;
    oi_page *page;
    int pressed;
    /* the text's lines, the choices' lines */
    struct List text_list, item_list;
    struct Node *text_nodes, *item_nodes;
    char *text_buf;
    char (*item_text)[260];
    int item_map[PG_MAXITEMS];      /* a shown line's item */
    int nshown;
    const char *mx_labels[PG_MAXITEMS + 1];
    const char *log_labels[5];
    /* the progress bar's box */
    int bar_x, bar_y, bar_w, bar_h;
    int caption_y;
    int progress_loaded;
    char progress_text[512];
} gw;

static int font_h(gw *w)
{
    return w->font ? w->font->tf_YSize : 8;
}

static int text_w(gw *w, const char *s, int n)
{
    return (int)TextLength(w->win->RPort, (STRPTR)s, (ULONG)n);
}

/* The page's text in lines as wide as the list: at spaces where it can. */
static void wrap_text(gw *w, const char *t, int width)
{
    int nl = 0;
    size_t len = t ? strlen(t) : 0;
    char *out;
    const char *p = t ? t : "";
    free(w->text_buf);
    free(w->text_nodes);
    w->text_buf = out = malloc(len * 2 + 2);
    w->text_nodes = calloc(MAX_LINES, sizeof *w->text_nodes);
    NewList(&w->text_list);
    if (!out || !w->text_nodes)
        return;
    while (*p && nl < MAX_LINES) {
        int n = 0, brk = -1;
        while (p[n] && p[n] != '\n') {
            if (p[n] == ' ')
                brk = n;
            if (text_w(w, p, n + 1) > width) {
                if (brk > 0)
                    n = brk;
                break;
            }
            n++;
        }
        if (n == 0 && p[0] && p[0] != '\n')
            n = 1;
        memcpy(out, p, (size_t)n);
        out[n] = 0;
        w->text_nodes[nl].ln_Name = out;
        AddTail(&w->text_list, &w->text_nodes[nl]);
        nl++;
        out += n + 1;
        p += n;
        if (*p == ' ' || *p == '\n')
            p++;
    }
}

/* Options' lines, ticked or not; or many choices' lines. */
static void make_items(gw *w, oi_page *p)
{
    int i;
    free(w->item_nodes);
    free(w->item_text);
    w->item_nodes = calloc(PG_MAXITEMS, sizeof *w->item_nodes);
    w->item_text = calloc(PG_MAXITEMS, sizeof *w->item_text);
    NewList(&w->item_list);
    w->nshown = 0;
    if (!w->item_nodes || !w->item_text)
        return;
    for (i = 0; i < p->nitems; i++) {
        if (((p->disabled >> i) & 1))
            continue;
        if (p->kind == PG_CHECK)
            snprintf(w->item_text[w->nshown], 260, "[%c] %s", ((p->value >> i) & 1) ? 'x' : ' ', p->items[i]);
        else
            snprintf(w->item_text[w->nshown], 260, "%s", p->items[i]);
        w->item_nodes[w->nshown].ln_Name = w->item_text[w->nshown];
        AddTail(&w->item_list, &w->item_nodes[w->nshown]);
        w->item_map[w->nshown] = i;
        w->nshown++;
    }
}

static int shown_of(gw *w, long item)
{
    int i;
    for (i = 0; i < w->nshown; i++)
        if (w->item_map[i] == item)
            return i;
    return 0;
}

static void free_gadgets(gw *w)
{
    if (w->glist) {
        RemoveGList(w->win, w->glist, -1);
        FreeGadgets(w->glist);
        w->glist = NULL;
    }
    w->str = w->num = w->sub1 = w->sub2 = w->list = NULL;
}

static void draw_bar(gw *w, long percent)
{
    struct RastPort *rp = w->win->RPort;
    int fill;
    if (w->bar_w <= 4)
        return;
    DrawBevelBox(rp, w->bar_x, w->bar_y, w->bar_w, w->bar_h, GT_VisualInfo, (ULONG)w->vi, GTBB_Recessed, TRUE,
                 TAG_DONE);
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    fill = (int)((long)(w->bar_w - 4) * percent / 100);
    SetAPen(rp, w->dri->dri_Pens[FILLPEN]);
    if (fill > 0)
        RectFill(rp, w->bar_x + 2, w->bar_y + 1, w->bar_x + 1 + fill, w->bar_y + w->bar_h - 2);
    SetAPen(rp, w->dri->dri_Pens[BACKGROUNDPEN]);
    if (fill < w->bar_w - 4)
        RectFill(rp, w->bar_x + 2 + fill, w->bar_y + 1, w->bar_x + w->bar_w - 3, w->bar_y + w->bar_h - 2);
}

/* What isn't a gadget: the background, the title, the panel and its caption. */
static void draw_rest(gw *w, oi_page *p, int clear)
{
    struct Window *win = w->win;
    struct RastPort *rp = win->RPort;
    int L = win->BorderLeft, T = win->BorderTop;
    int iw = win->Width - L - win->BorderRight, ih = win->Height - T - win->BorderBottom;
    int side = iw / 4, fh = font_h(w);
    oi_panel *pn = oi_panel_get(w->g);
    SetFont(rp, w->font);
    if (clear) {
        SetAPen(rp, w->dri->dri_Pens[BACKGROUNDPEN]);
        RectFill(rp, L, T, L + iw - 1, T + ih - 1);
    }
    oi_side_draw(&w->side, rp, L + 6, T + 6, side - 6, w->caption_y - T - 10);
    if (pn->caption[0]) {
        int n = (int)strlen(pn->caption);
        struct TextExtent te;
        n = (int)TextFit(rp, (STRPTR)pn->caption, (ULONG)n, &te, NULL, 1, (ULONG)(side - 6), (ULONG)fh + 1);
        SetAPen(rp, w->dri->dri_Pens[TEXTPEN]);
        Move(rp, L + 6 + (side - 6 - te.te_Width) / 2, w->caption_y + rp->TxBaseline);
        Text(rp, (STRPTR)pn->caption, (ULONG)n);
    }
    if (p && p->title) {
        SetAPen(rp, w->dri->dri_Pens[HIGHLIGHTTEXTPEN]);
        Move(rp, L + side + 6, T + 6 + rp->TxBaseline);
        Text(rp, (STRPTR)p->title, (ULONG)strlen(p->title));
    }
}

static int build(gw *w, oi_page *p)
{
    struct Window *win = w->win;
    struct NewGadget ng;
    struct Gadget *gad;
    int L = win->BorderLeft, T = win->BorderTop;
    int iw = win->Width - L - win->BorderRight, ih = win->Height - T - win->BorderBottom;
    int side = iw / 4, fh = font_h(w), row = fh + 6;
    int mx = L + side + 6, mw = iw - side - 12;
    int top = T + 6 + (p->title ? fh + 6 : 0);
    int bottom = T + ih - 6 - row;     /* the buttons' row */
    int ctl = 0, i, x;
    free_gadgets(w);
    w->bar_w = 0;
    w->caption_y = oi_panel_get(w->g)->caption[0] ? T + ih - 6 - fh : T + ih - 2;
    /* the controls' height, under the text */
    switch (p->kind) {
    case PG_RADIO:
        make_items(w, p);
        ctl = w->nshown <= 8 ? w->nshown * (fh + 3) : 6 * (fh + 2) + 4;
        break;
    case PG_CHECK:
        make_items(w, p);
        ctl = (w->nshown < 6 ? w->nshown : 6) * (fh + 2) + 4;
        break;
    case PG_STRING:
    case PG_NUMBER:
        ctl = row;
        break;
    case PG_PROGRESS:
        ctl = row + 2 * (fh + 4);
        break;
    }
    if (p->nitems2)
        ctl += row + 4;
    if (ctl)
        ctl += 6;
    gad = CreateContext(&w->glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &w->ta;
    ng.ng_VisualInfo = w->vi;
    /* the text */
    wrap_text(w, p->text, mw - 24);
    ng.ng_LeftEdge = mx;
    ng.ng_TopEdge = top;
    ng.ng_Width = mw;
    ng.ng_Height = bottom - 6 - ctl - top;
    if (ng.ng_Height < fh + 6)
        ng.ng_Height = fh + 6;
    ng.ng_GadgetID = ID_TEXT;
    gad = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&w->text_list, GTLV_ReadOnly, TRUE, TAG_DONE);
    x = bottom - ctl;
    ng.ng_TopEdge = x;
    switch (p->kind) {
    case PG_RADIO:
        if (w->nshown <= 8) {
            for (i = 0; i < w->nshown; i++)
                w->mx_labels[i] = w->item_text[i];
            w->mx_labels[i] = NULL;
            ng.ng_Width = 17;
            ng.ng_Height = fh + 1;
            ng.ng_Flags = PLACETEXT_RIGHT;
            ng.ng_GadgetID = ID_MX;
            gad = CreateGadget(MX_KIND, gad, &ng, GTMX_Labels, (ULONG)w->mx_labels,
                               GTMX_Active, shown_of(w, p->value), GTMX_Spacing, 2, GTMX_Scaled, TRUE, TAG_DONE);
        } else {
            ng.ng_Height = 6 * (fh + 2) + 4;
            ng.ng_GadgetID = ID_LIST;
            gad = w->list = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&w->item_list,
                                         GTLV_ShowSelected, 0, GTLV_Selected, shown_of(w, p->value),
                                         GTLV_MakeVisible, shown_of(w, p->value), TAG_DONE);
        }
        ng.ng_Flags = 0;
        break;
    case PG_CHECK:
        ng.ng_Height = (w->nshown < 6 ? w->nshown : 6) * (fh + 2) + 4;
        ng.ng_GadgetID = ID_LIST;
        gad = w->list = CreateGadget(LISTVIEW_KIND, gad, &ng, GTLV_Labels, (ULONG)&w->item_list, TAG_DONE);
        break;
    case PG_STRING: {
        int bw = p->file_button ? text_w(w, "Choose...", 9) + 16 : 0;
        ng.ng_Width = mw - (bw ? bw + 6 : 0);
        ng.ng_Height = row;
        ng.ng_GadgetID = ID_STR;
        gad = w->str = CreateGadget(STRING_KIND, gad, &ng, GTST_String, (ULONG)p->str, GTST_MaxChars,
                                    sizeof p->str - 1, TAG_DONE);
        if (bw) {
            ng.ng_LeftEdge = mx + mw - bw;
            ng.ng_Width = bw;
            ng.ng_GadgetText = (UBYTE *)"Choose...";
            ng.ng_GadgetID = ID_PICK;
            gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
            ng.ng_GadgetText = NULL;
            ng.ng_LeftEdge = mx;
        }
        break;
    }
    case PG_NUMBER:
        ng.ng_Width = mw / 2;
        ng.ng_Height = row;
        ng.ng_GadgetID = ID_NUM;
        gad = w->num = CreateGadget(INTEGER_KIND, gad, &ng, GTIN_Number, p->value, GTIN_MaxChars, 11, TAG_DONE);
        break;
    case PG_PROGRESS:
        w->bar_x = mx;
        w->bar_y = x;
        w->bar_w = mw;
        w->bar_h = row;
        ng.ng_TopEdge = x + row + 4;
        ng.ng_Height = fh + 2;
        ng.ng_GadgetID = ID_SUB1;
        gad = w->sub1 = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)"", GTTX_CopyText, TRUE, TAG_DONE);
        ng.ng_TopEdge += fh + 4;
        ng.ng_GadgetID = ID_SUB2;
        gad = w->sub2 = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)"", GTTX_CopyText, TRUE, TAG_DONE);
        break;
    }
    if (p->nitems2) {
        for (i = 0; i < p->nitems2 && i < 4; i++)
            w->log_labels[i] = p->items2[i];
        w->log_labels[i] = NULL;
        ng.ng_LeftEdge = mx + text_w(w, p->label2, (int)strlen(p->label2)) + 8;
        ng.ng_TopEdge = bottom - 6 - row;
        ng.ng_Width = mw - (ng.ng_LeftEdge - mx);
        ng.ng_Height = row;
        ng.ng_GadgetText = (UBYTE *)p->label2;
        ng.ng_Flags = PLACETEXT_LEFT;
        ng.ng_GadgetID = ID_LOG;
        gad = CreateGadget(CYCLE_KIND, gad, &ng, GTCY_Labels, (ULONG)w->log_labels, GTCY_Active, p->value2,
                           TAG_DONE);
        ng.ng_GadgetText = NULL;
        ng.ng_Flags = 0;
    }
    /* the buttons, from the right */
    x = mx + mw;
    for (i = p->nbtn - 1; i >= 0; i--) {
        int bw = text_w(w, p->btn[i], (int)strlen(p->btn[i])) + 20;
        x -= bw;
        ng.ng_LeftEdge = x;
        ng.ng_TopEdge = bottom;
        ng.ng_Width = bw;
        ng.ng_Height = row;
        ng.ng_GadgetText = (UBYTE *)p->btn[i];
        ng.ng_Flags = PLACETEXT_IN;
        ng.ng_GadgetID = ID_BTN + i;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);
        x -= 6;
    }
    if (!gad) {
        FreeGadgets(w->glist);
        w->glist = NULL;
        return 0;
    }
    draw_rest(w, p, 1);
    AddGList(win, w->glist, (UWORD)-1, -1, NULL);
    RefreshGList(w->glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    if (p->kind == PG_PROGRESS)
        draw_bar(w, p->value);
    if (w->str)
        ActivateGadget(w->str, win, NULL);
    else if (w->num)
        ActivateGadget(w->num, win, NULL);
    w->page = p;
    return 1;
}

static void pick(gw *w, int files)
{
    char cur[512], drawer[512], file[256];
    struct FileRequester *fr;
    if (!w->str)
        return;
    if (!AslBase)
        AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (!AslBase)
        return;
    snprintf(cur, sizeof cur, "%s", (const char *)((struct StringInfo *)w->str->SpecialInfo)->Buffer);
    snprintf(drawer, sizeof drawer, "%s", cur);
    file[0] = 0;
    if (files) {
        *PathPart((STRPTR)drawer) = 0;
        snprintf(file, sizeof file, "%s", (const char *)FilePart((STRPTR)cur));
    }
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)w->win, ASLFR_SleepWindow, TRUE,
                             ASLFR_InitialDrawer, (ULONG)drawer, ASLFR_InitialFile, (ULONG)file,
                             ASLFR_DrawersOnly, !files, ASLFR_RejectIcons, TRUE, TAG_DONE);
    if (!fr)
        return;
    if (AslRequestTags(fr, TAG_DONE)) {
        char out[512];
        snprintf(out, sizeof out, "%s", (const char *)fr->fr_Drawer);
        if (files)
            AddPart((STRPTR)out, fr->fr_File, sizeof out);
        GT_SetGadgetAttrs(w->str, w->win, NULL, GTST_String, (ULONG)out, TAG_DONE);
    }
    FreeAslRequest(fr);
}

/* The window's messages; 1 when the user wants out (Ctrl-C). */
static int events(gw *w)
{
    struct IntuiMessage *m;
    oi_page *p = w->page;
    while ((m = GT_GetIMsg(w->win->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        struct Gadget *gad = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        switch (cls) {
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(w->win);
            if (p) {
                draw_rest(w, p, 0);
                if (p->kind == PG_PROGRESS)
                    draw_bar(w, p->value);
            }
            GT_EndRefresh(w->win, TRUE);
            break;
        case IDCMP_NEWSIZE:
            if (p) {
                w->progress_loaded = 0;
                build(w, p);
                if (p->kind == PG_PROGRESS)
                    w->progress_loaded = 1;
            }
            break;
        case IDCMP_VANILLAKEY:
            if ((code == '\r' || code == '\n') && p && p->enter_btn >= 0)
                w->pressed = p->enter_btn;
            break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (!p)
                break;
            switch (gad->GadgetID) {
            case ID_MX:
                if (code < w->nshown)
                    p->value = w->item_map[code];
                break;
            case ID_LIST:
                if (code >= w->nshown)
                    break;
                if (p->kind == PG_CHECK) {
                    int item = w->item_map[code];
                    p->value ^= 1L << item;
                    snprintf(w->item_text[code], 260, "[%c] %s", ((p->value >> item) & 1) ? 'x' : ' ',
                             p->items[item]);
                    GT_SetGadgetAttrs(w->list, w->win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
                    GT_SetGadgetAttrs(w->list, w->win, NULL, GTLV_Labels, (ULONG)&w->item_list, TAG_DONE);
                } else {
                    p->value = w->item_map[code];
                }
                break;
            case ID_LOG:
                p->value2 = code;
                break;
            case ID_PICK:
                pick(w, p->file_button == 2);
                break;
            case ID_STR:
            case ID_NUM:
                /* Return in the field */
                if (cls == IDCMP_GADGETUP && code != 9 && p->enter_btn >= 0)
                    w->pressed = p->enter_btn;
                break;
            default:
                if (gad->GadgetID >= ID_BTN && gad->GadgetID < ID_BTN + PG_MAXBTN)
                    w->pressed = gad->GadgetID - ID_BTN;
                break;
            }
            break;
        }
    }
    oi_side_tick(&w->side);
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}

static void read_back(gw *w, oi_page *p)
{
    if (w->str)
        snprintf(p->str, sizeof p->str, "%s", (const char *)((struct StringInfo *)w->str->SpecialInfo)->Buffer);
    if (w->num)
        p->value = ((struct StringInfo *)w->num->SpecialInfo)->LongInt;
}

static int b_run(void *b, oi_page *p)
{
    gw *w = b;
    w->progress_loaded = 0;
    if (!build(w, p))
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

static int b_progress(void *b, oi_page *p)
{
    gw *w = b;
    char one[256];
    const char *nl = strchr(p->sub, '\n');
    size_t n = nl ? (size_t)(nl - p->sub) : strlen(p->sub);
    if (!w->progress_loaded || strcmp(w->progress_text, p->text ? p->text : "")) {
        if (!build(w, p))
            return -1;
        w->progress_loaded = 1;
        snprintf(w->progress_text, sizeof w->progress_text, "%s", p->text ? p->text : "");
        w->pressed = -1;
    }
    w->page = p;
    if (n >= sizeof one)
        n = sizeof one - 1;
    memcpy(one, p->sub, n);
    one[n] = 0;
    if (w->sub1)
        GT_SetGadgetAttrs(w->sub1, w->win, NULL, GTTX_Text, (ULONG)one, TAG_DONE);
    if (w->sub2)
        GT_SetGadgetAttrs(w->sub2, w->win, NULL, GTTX_Text, (ULONG)(nl ? nl + 1 : ""), TAG_DONE);
    draw_bar(w, p->value);
    if (events(w))
        return 0;
    if (w->pressed >= 0) {
        int i = w->pressed;
        w->pressed = -1;
        w->progress_loaded = 0;
        return i;
    }
    return -1;
}

static void b_panel(void *b)
{
    gw *w = b;
    oi_side_update(&w->side, w->g, w->scr);
    if (w->page)
        draw_rest(w, w->page, 0);
    if (w->glist)
        RefreshGList(w->glist, w->win, NULL, -1);
}

static void b_idle(void *b)
{
    events((gw *)b);
}

static void b_close(void *b)
{
    gw *w = b;
    oi_side_free(&w->side);
    if (w->win) {
        free_gadgets(w);
        CloseWindow(w->win);
    }
    if (w->font)
        CloseFont(w->font);
    if (w->dri)
        FreeScreenDrawInfo(w->scr, w->dri);
    if (w->vi)
        FreeVisualInfo(w->vi);
    if (w->scr)
        UnlockPubScreen(NULL, w->scr);
    free(w->text_buf);
    free(w->text_nodes);
    free(w->item_nodes);
    free(w->item_text);
    if (AslBase) {
        CloseLibrary(AslBase);
        AslBase = NULL;
    }
    if (GadToolsBase) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    free(w);
}

static const oi_backend gadtools_backend = { b_run, b_progress, b_panel, b_idle, b_close };

oi_ui *oi_ui_gadtools_open(oi *g)
{
    gw *w;
    int wd, ht;
    oi_ui *u;
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
    if (!GadToolsBase)
        return NULL;
    w = calloc(1, sizeof *w);
    if (!w) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
        return NULL;
    }
    w->g = g;
    NewList(&w->text_list);
    NewList(&w->item_list);
    w->scr = LockPubScreen(NULL);
    if (!w->scr)
        goto fail;
    w->vi = GetVisualInfo(w->scr, TAG_DONE);
    w->dri = GetScreenDrawInfo(w->scr);
    if (!w->vi || !w->dri)
        goto fail;
    /* the screen's font, as Font prefs set it */
    w->ta = *w->scr->Font;
    w->font = OpenFont(&w->ta);
    wd = w->scr->Width < 640 ? w->scr->Width : 640;
    ht = w->scr->Height - w->scr->BarHeight - 1;
    if (ht > 440)
        ht = 440;
    w->win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)w->scr, WA_Title, (ULONG)oi_window_title(g),
                            WA_Left, (w->scr->Width - wd) / 2, WA_Top, (w->scr->Height - ht) / 2,
                            WA_Width, wd, WA_Height, ht, WA_MinWidth, 400, WA_MinHeight, 260,
                            WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                            WA_SizeGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
                            WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                                          IDCMP_VANILLAKEY | IDCMP_INTUITICKS | IDCMP_MOUSEBUTTONS |
                                          LISTVIEWIDCMP | MXIDCMP,
                            TAG_DONE);
    if (!w->win)
        goto fail;
    if (w->font)
        SetFont(w->win->RPort, w->font);
    oi_side_init(&w->side, 0xff000000UL, w->dri->dri_Pens[SHADOWPEN]);
    oi_side_update(&w->side, g, w->scr);
    u = oi_pages_open(g, &gadtools_backend, w);
    if (u)
        return u;
fail:
    b_close(w);
    return NULL;
}
#else
oi_ui *oi_ui_gadtools_open(oi *g)
{
    (void)g;
    return NULL;
}
#endif
