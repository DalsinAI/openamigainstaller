/*
 * The pages of the Installer for the window front ends (ui_pages.h): the
 * welcome's two pages, messages, the ask pages, confirmations, the
 * progress page that working, copying and downloading share, problems and
 * the last page. Help shows its text on a page of its own and comes back;
 * Abort asks first, as the Installer does.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"
#include "oi_panel.h"
#include "ui_pages.h"
#include "oi_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pages {
    oi *g;
    const oi_backend *be;
    void *b;
    char working[512];
    int percent;
    long long last_now;
} pages;

/* What a button does. */
enum { B_PROCEED = 1, B_YES, B_NO, B_SKIP, B_BACK, B_HELP, B_ABORT, B_OK, B_RETRY, B_STOP };

typedef struct btns {
    int code[PG_MAXBTN];
} btns;

static void add_btn(oi_page *p, btns *k, const char *label, int code)
{
    if (p->nbtn >= PG_MAXBTN)
        return;
    k->code[p->nbtn] = code;
    p->btn[p->nbtn++] = label;
}

static const char *abort_label(pages *s)
{
    const char *l = oi_get_str(s->g, "@abort-button");
    return *l ? l : "Abort Install";
}

/* The page with its Help and Abort; returns the code of the button that
 * stands (B_ABORT only when the user said yes to stopping). */
static int run_page(pages *s, oi_page *p, btns *k, const char *help)
{
    for (;;) {
        int i = s->be->run(s->b, p);
        int code = (i >= 0 && i < p->nbtn) ? k->code[i] : B_ABORT;
        if (code == B_HELP) {
            oi_page h;
            btns hk;
            memset(&h, 0, sizeof h);
            memset(&hk, 0, sizeof hk);
            h.title = "Help";
            h.text = help && *help ? help : "There is no help for this part.";
            h.kind = PG_TEXT;
            add_btn(&h, &hk, "OK", B_OK);
            h.enter_btn = 0;
            s->be->run(s->b, &h);
            continue;
        }
        if (code == B_ABORT) {
            oi_page q;
            btns qk;
            if (i < 0)
                return B_ABORT;     /* the window was closed */
            memset(&q, 0, sizeof q);
            memset(&qk, 0, sizeof qk);
            q.text = "Are you sure you want to stop the installation?";
            q.kind = PG_TEXT;
            add_btn(&q, &qk, "Yes, stop it", B_YES);
            add_btn(&q, &qk, "No, go on", B_NO);
            q.enter_btn = 1;
            if (s->be->run(s->b, &q) != 0)
                continue;
            return B_ABORT;
        }
        return code;
    }
}

static int code_to_ui(int code)
{
    switch (code) {
    case B_ABORT: return OI_UI_ABORT;
    case B_BACK: return OI_UI_BACK;
    case B_SKIP: return OI_UI_SKIP;
    case B_NO: return OI_UI_NO;
    default: return OI_UI_OK;
    }
}

/* Proceed [Back] Help Abort, the ask pages' row */
static void ask_buttons(oi_page *p, btns *k, pages *s, int can_back, int help)
{
    add_btn(p, k, "Proceed", B_PROCEED);
    if (can_back)
        add_btn(p, k, "Back", B_BACK);
    if (help)
        add_btn(p, k, "Help...", B_HELP);
    add_btn(p, k, abort_label(s), B_ABORT);
    p->enter_btn = 0;
}

static int p_welcome(oi_ui *u, const char *text, int *level, int min_level,
                     int *pretend, int no_pretend, int *log, int no_log)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code, i;
    static const char *const levels[] = { "Novice User: all the choices are made for you",
                                          "Intermediate User: you're asked about some choices",
                                          "Expert User: you're asked about each choice" };
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.title = "Welcome";
    p.text = text;
    p.kind = PG_RADIO;
    for (i = 0; i < 3; i++)
        p.items[i] = levels[i];
    p.nitems = 3;
    for (i = 0; i < min_level && i < 3; i++)
        p.disabled |= 1UL << i;
    p.value = *level;
    add_btn(&p, &k, "Proceed", B_PROCEED);
    add_btn(&p, &k, abort_label(s), B_ABORT);
    p.enter_btn = 0;
    code = run_page(s, &p, &k, NULL);
    if (code == B_ABORT)
        return OI_UI_ABORT;
    *level = (int)p.value < min_level ? min_level : (int)p.value;
    if (*level == OI_NOVICE)
        return OI_UI_OK;
    /* the second page: install or pretend, and the log */
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.title = "Installation options";
    p.text = "Install for real, or pretend: pretending shows each step and changes nothing.";
    p.kind = PG_RADIO;
    p.items[0] = "Install for real";
    p.items[1] = "Pretend to install";
    p.nitems = 2;
    if (no_pretend)
        p.disabled = 2;
    p.value = *pretend ? 1 : 0;
    p.label2 = "Log of the installation:";
    p.items2[0] = "None";
    p.items2[1] = "To the printer";
    p.items2[2] = "To a log file";
    p.nitems2 = no_log ? 1 : 3;
    p.value2 = no_log ? 0 : *log;
    add_btn(&p, &k, "Proceed", B_PROCEED);
    add_btn(&p, &k, abort_label(s), B_ABORT);
    p.enter_btn = 0;
    code = run_page(s, &p, &k, NULL);
    if (code == B_ABORT)
        return OI_UI_ABORT;
    *pretend = !no_pretend && p.value == 1;
    *log = no_log ? 0 : (int)p.value2;
    return OI_UI_OK;
}

static int p_message(oi_ui *u, const char *text, int can_back)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = text;
    p.kind = PG_TEXT;
    ask_buttons(&p, &k, s, can_back, 0);
    return code_to_ui(run_page(s, &p, &k, NULL));
}

static int p_askbool(oi_ui *u, oi_ask *q)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = q->prompt;
    p.kind = PG_TEXT;
    add_btn(&p, &k, q->yes, B_YES);
    add_btn(&p, &k, q->no, B_NO);
    if (q->can_back)
        add_btn(&p, &k, "Back", B_BACK);
    add_btn(&p, &k, "Help...", B_HELP);
    add_btn(&p, &k, abort_label(s), B_ABORT);
    p.enter_btn = q->num ? 0 : 1;
    code = run_page(s, &p, &k, q->help);
    if (code == B_YES || code == B_NO) {
        q->num = code == B_YES;
        return OI_UI_OK;
    }
    return code_to_ui(code);
}

static int p_choices(oi_ui *u, oi_ask *q, int kind)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code, i;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = q->prompt;
    p.kind = kind;
    /* an empty choice isn't shown (OS 3.5's rule), its number kept */
    for (i = 0; i < q->nchoices && i < PG_MAXITEMS; i++) {
        p.items[i] = q->choices[i];
        if (!q->choices[i][0])
            p.disabled |= 1UL << i;
    }
    p.nitems = i;
    p.value = q->num;
    ask_buttons(&p, &k, s, q->can_back, 1);
    code = run_page(s, &p, &k, q->help);
    if (code == B_PROCEED)
        q->num = p.value;
    return code_to_ui(code);
}

static int p_askchoice(oi_ui *u, oi_ask *q)  { return p_choices(u, q, PG_RADIO); }
static int p_askoptions(oi_ui *u, oi_ask *q) { return p_choices(u, q, PG_CHECK); }

static int p_asknumber(oi_ui *u, oi_ask *q)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code;
    char range[128];
    char *text;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    snprintf(range, sizeof range, "(%ld to %ld)", q->min, q->max);
    text = oi_xprintf("%s\n%s", q->prompt, range);
    p.text = text ? text : q->prompt;
    p.kind = PG_NUMBER;
    p.min = q->min;
    p.max = q->max;
    p.value = q->num;
    ask_buttons(&p, &k, s, q->can_back, 1);
    code = run_page(s, &p, &k, q->help);
    if (code == B_PROCEED)
        q->num = p.value < q->min ? q->min : p.value > q->max ? q->max : p.value;
    free(text);
    return code_to_ui(code);
}

static int p_text(oi_ui *u, oi_ask *q, int file_button)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = q->prompt;
    p.kind = PG_STRING;
    p.file_button = file_button;
    snprintf(p.str, sizeof p.str, "%s", q->dflt ? q->dflt : "");
    ask_buttons(&p, &k, s, q->can_back, 1);
    code = run_page(s, &p, &k, q->help);
    if (code == B_PROCEED)
        q->answer = oi_strdup(p.str);
    return code_to_ui(code);
}

static int p_askstring(oi_ui *u, oi_ask *q) { return p_text(u, q, 0); }
static int p_askdir(oi_ui *u, oi_ask *q)    { return p_text(u, q, 1); }
static int p_askfile(oi_ui *u, oi_ask *q)   { return p_text(u, q, 2); }



static int p_askdisk(oi_ui *u, oi_ask *q, const char *volume)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    char *text = oi_xprintf("%s%sPut the disk %s in any drive.", q->prompt, *q->prompt ? "\n\n" : "", volume);
    int r = OI_UI_ABORT;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = text ? text : volume;
    p.kind = PG_TEXT;
    ask_buttons(&p, &k, s, q->can_back, 1);
    for (;;) {
        int code = run_page(s, &p, &k, q->help);
        if (code != B_PROCEED) {
            r = code_to_ui(code);
            break;
        }
        if (gs_volume_mounted(volume)) {
            r = OI_UI_OK;
            break;
        }
    }
    free(text);
    return r;
}

static int p_confirm(oi_ui *u, const char *prompt, const char *help)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.text = prompt;
    p.kind = PG_TEXT;
    add_btn(&p, &k, "Proceed", B_PROCEED);
    add_btn(&p, &k, "Skip This Part", B_SKIP);
    add_btn(&p, &k, "Help...", B_HELP);
    add_btn(&p, &k, abort_label(s), B_ABORT);
    p.enter_btn = 0;
    return code_to_ui(run_page(s, &p, &k, help));
}

/* The progress page: (working)'s text, the bar of (complete) or of the
 * file being copied or downloaded, and the Abort button. */
static int show_progress(pages *s, const char *sub, int percent)
{
    oi_page p;
    int i;
    memset(&p, 0, sizeof p);
    p.title = "Working on the installation";
    p.text = s->working[0] ? s->working : "Working on the installation...";
    p.kind = PG_PROGRESS;
    p.value = percent;
    snprintf(p.sub, sizeof p.sub, "%s", sub ? sub : "");
    p.btn[0] = abort_label(s);
    p.nbtn = 1;
    p.enter_btn = -1;
    i = s->be->progress(s->b, &p);
    if (i == 0) {
        /* asked, as Abort is everywhere */
        oi_page q;
        btns qk;
        memset(&q, 0, sizeof q);
        memset(&qk, 0, sizeof qk);
        q.text = "Are you sure you want to stop the installation?";
        q.kind = PG_TEXT;
        add_btn(&q, &qk, "Yes, stop it", B_YES);
        add_btn(&q, &qk, "No, go on", B_NO);
        q.enter_btn = 1;
        if (s->be->run(s->b, &q) == 0)
            return OI_UI_ABORT;
        s->be->progress(s->b, &p);
    }
    return OI_UI_OK;
}

static void p_working(oi_ui *u, const char *text)
{
    pages *s = u->priv;
    snprintf(s->working, sizeof s->working, "%s", text);
    show_progress(s, "", s->percent);
}

static void p_complete(oi_ui *u, int percent)
{
    pages *s = u->priv;
    s->percent = percent;
    show_progress(s, "", percent);
}

static int p_gauge(oi_ui *u, const char *what, long long now, long long total)
{
    pages *s = u->priv;
    char sub[256];
    int pc = -1;
    /* not more often than each 16 KB, but always at the end */
    if (now - s->last_now < 16384 && now != total && now != 0)
        return OI_UI_OK;
    s->last_now = now;
    if (total > 0) {
        pc = (int)(now * 100 / total);
        if (total >= 10LL * 1024 * 1024)
            snprintf(sub, sizeof sub, "%s\n%lld of %lld MB", what, now >> 20, total >> 20);
        else
            snprintf(sub, sizeof sub, "%s\n%lld of %lld KB", what, now >> 10, total >> 10);
    } else {
        snprintf(sub, sizeof sub, "%s\n%lld KB", what, now >> 10);
    }
    if (now == total)
        s->last_now = 0;
    return show_progress(s, sub, pc);
}

static int p_problem(oi_ui *u, const char *text, int retry)
{
    pages *s = u->priv;
    oi_page p;
    btns k;
    int code;
    memset(&p, 0, sizeof p);
    memset(&k, 0, sizeof k);
    p.title = "Problem";
    p.text = text;
    p.kind = PG_TEXT;
    if (retry) {
        add_btn(&p, &k, "Retry", B_RETRY);
        add_btn(&p, &k, abort_label(s), B_ABORT);
    } else {
        add_btn(&p, &k, "OK", B_OK);
    }
    p.enter_btn = 0;
    code = run_page(s, &p, &k, NULL);
    return code == B_RETRY || code == B_OK ? OI_UI_OK : OI_UI_ABORT;
}

static void p_final(oi_ui *u, const char *text, int aborted)
{
    pages *s = u->priv;
    oi_page p;
    memset(&p, 0, sizeof p);
    p.title = aborted ? "The installation stopped" : "Installation complete";
    p.text = text;
    p.kind = PG_TEXT;
    p.btn[0] = "Proceed";
    p.nbtn = 1;
    p.enter_btn = 0;
    s->be->run(s->b, &p);
}

static void p_panel(oi_ui *u)
{
    pages *s = u->priv;
    s->be->panel(s->b);
}

static int p_poll(oi_ui *u)
{
    pages *s = u->priv;
    s->be->idle(s->b);
    return OI_UI_OK;
}

static void p_close(oi_ui *u)
{
    pages *s = u->priv;
    s->be->close(s->b);
    free(s);
    free(u);
}

static const oi_ui_ops page_ops = {
    "window",
    p_welcome, p_message, p_askbool, p_askchoice, p_askoptions, p_asknumber,
    p_askstring, p_askdir, p_askfile, p_askdisk, p_confirm, p_working,
    p_complete, p_gauge, p_problem, p_final, p_panel, p_poll, p_close,
};

oi_ui *oi_pages_open(oi *g, const oi_backend *be, void *b)
{
    oi_ui *u = calloc(1, sizeof *u);
    pages *s = calloc(1, sizeof *s);
    if (!u || !s) {
        free(u);
        free(s);
        return NULL;
    }
    s->g = g;
    s->be = be;
    s->b = b;
    u->ops = &page_ops;
    u->g = g;
    u->priv = s;
    return u;
}
