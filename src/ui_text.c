/*
 * The front end with no window, for the tests (and the HEADLESS switch):
 * each page is printed as one line, and its answer is the next line of
 * the file OI_ANSWERS names (the page's default when there is no file or
 * no line left). An answer is a number, yes/no, a string, or one of ok,
 * back, skip and abort; the welcome's is "LEVEL PRETEND LOG" (0-2, 0-1,
 * 0-2).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_panel.h"
#include "oi_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tui {
    FILE *answers;
    char line[1024];
} tui;

/* One line of a page's text, its newlines shown as \n. */
static void show(const char *kind, const char *text)
{
    const char *p;
    printf("[%s]", kind);
    if (text && *text) {
        printf(" ");
        for (p = text; *p; p++) {
            if (*p == '\n')
                printf("\\n");
            else
                putchar(*p);
        }
    }
    printf("\n");
}

static const char *answer(oi_ui *u)
{
    tui *t = u->priv;
    size_t n;
    if (!t->answers || !fgets(t->line, sizeof t->line, t->answers))
        return NULL;
    n = strlen(t->line);
    while (n && (t->line[n - 1] == '\n' || t->line[n - 1] == '\r'))
        t->line[--n] = 0;
    if (!strcmp(t->line, "-"))
        return NULL;    /* the default */
    printf("  > %s\n", t->line);
    return t->line;
}

/* ok, back, skip, abort: what such an answer means, or 0 for none */
static int verb(const char *a)
{
    if (!a)
        return 0;
    if (!oi_stricmp(a, "abort"))
        return OI_UI_ABORT;
    if (!oi_stricmp(a, "back"))
        return OI_UI_BACK;
    if (!oi_stricmp(a, "skip"))
        return OI_UI_SKIP;
    if (!oi_stricmp(a, "ok") || !oi_stricmp(a, "proceed"))
        return OI_UI_OK;
    return 0;
}

static int t_welcome(oi_ui *u, const char *text, int *level, int min_level,
                     int *pretend, int no_pretend, int *log, int no_log)
{
    const char *a;
    int l, p, g;
    show("welcome", text);
    a = answer(u);
    if (verb(a) == OI_UI_ABORT)
        return OI_UI_ABORT;
    if (a && sscanf(a, "%d %d %d", &l, &p, &g) == 3) {
        if (l >= min_level && l <= OI_EXPERT)
            *level = l;
        if (!no_pretend)
            *pretend = p != 0;
        if (!no_log && g >= 0 && g <= 2)
            *log = g;
    }
    printf("  level %d, %s, log %d\n", *level, *pretend ? "pretend" : "for real", *log);
    return OI_UI_OK;
}

static int t_message(oi_ui *u, const char *text, int can_back)
{
    int v;
    (void)can_back;
    show("message", text);
    v = verb(answer(u));
    return v ? v : OI_UI_OK;
}

static int t_askbool(oi_ui *u, oi_ask *q)
{
    const char *a;
    int v;
    printf("[askbool] %s (%s/%s, default %s)\n", q->prompt, q->yes, q->no, q->num ? q->yes : q->no);
    a = answer(u);
    if ((v = verb(a)) != 0 && v != OI_UI_OK)
        return v;
    if (a && v == 0)
        q->num = (!oi_stricmp(a, "yes") || !oi_stricmp(a, "1") || !oi_stricmp(a, q->yes));
    return OI_UI_OK;
}

static int t_askchoice(oi_ui *u, oi_ask *q)
{
    const char *a;
    int i, v;
    printf("[askchoice] %s:", q->prompt);
    for (i = 0; i < q->nchoices; i++)
        if (q->choices[i][0])
            printf(" %d=%s", i, q->choices[i]);
    printf(" (default %ld)\n", q->num);
    a = answer(u);
    if ((v = verb(a)) != 0 && v != OI_UI_OK)
        return v;
    if (a && v == 0)
        q->num = strtol(a, NULL, 0);
    return OI_UI_OK;
}

static int t_askoptions(oi_ui *u, oi_ask *q)
{
    const char *a;
    int i, v;
    printf("[askoptions] %s:", q->prompt);
    for (i = 0; i < q->nchoices; i++)
        if (q->choices[i][0])
            printf(" %d=%s", i, q->choices[i]);
    printf(" (default %ld)\n", q->num);
    a = answer(u);
    if ((v = verb(a)) != 0 && v != OI_UI_OK)
        return v;
    if (a && v == 0)
        q->num = strtol(a, NULL, 0);
    return OI_UI_OK;
}

static int t_asknumber(oi_ui *u, oi_ask *q)
{
    const char *a;
    int v;
    printf("[asknumber] %s (%ld to %ld, default %ld)\n", q->prompt, q->min, q->max, q->num);
    a = answer(u);
    if ((v = verb(a)) != 0 && v != OI_UI_OK)
        return v;
    if (a && v == 0)
        q->num = strtol(a, NULL, 0);
    return OI_UI_OK;
}

static int t_text(oi_ui *u, oi_ask *q, const char *kind)
{
    const char *a;
    int v;
    printf("[%s] %s (default \"%s\")\n", kind, q->prompt, q->dflt);
    a = answer(u);
    if ((v = verb(a)) != 0 && v != OI_UI_OK)
        return v;
    q->answer = oi_strdup(a && v == 0 ? a : q->dflt);
    return OI_UI_OK;
}

static int t_askstring(oi_ui *u, oi_ask *q) { return t_text(u, q, "askstring"); }
static int t_askdir(oi_ui *u, oi_ask *q)    { return t_text(u, q, "askdir"); }
static int t_askfile(oi_ui *u, oi_ask *q)   { return t_text(u, q, "askfile"); }

static int t_askdisk(oi_ui *u, oi_ask *q, const char *volume)
{
    int v;
    printf("[askdisk] %s (%s)\n", q->prompt, volume);
    v = verb(answer(u));
    return v ? v : OI_UI_OK;
}

static int t_confirm(oi_ui *u, const char *prompt, const char *help)
{
    int v;
    (void)help;
    show("confirm", prompt);
    v = verb(answer(u));
    return v ? v : OI_UI_OK;
}

static void t_working(oi_ui *u, const char *text)
{
    (void)u;
    show("working", text);
}

static void t_complete(oi_ui *u, int percent)
{
    (void)u;
    printf("[complete] %d%%\n", percent);
}

static int t_gauge(oi_ui *u, const char *what, long long now, long long total)
{
    (void)u; (void)what; (void)now; (void)total;
    return OI_UI_OK;
}

static int t_problem(oi_ui *u, const char *text, int retry)
{
    int v;
    show(retry ? "problem-retry" : "problem", text);
    v = verb(answer(u));
    return v ? v : (retry ? OI_UI_ABORT : OI_UI_OK);
}

static void t_final(oi_ui *u, const char *text, int aborted)
{
    (void)u;
    show(aborted ? "final-aborted" : "final", text);
}

static void t_panel(oi_ui *u)
{
    oi_panel *p = oi_panel_get(u->g);
    printf("[panel] %s%s%s\n", p->path[0] ? p->path : "(none)",
           p->caption[0] ? " - " : "", p->caption);
}

static int t_poll(oi_ui *u)
{
    (void)u;
    return OI_UI_OK;
}

static void t_close(oi_ui *u)
{
    tui *t = u->priv;
    if (t->answers)
        fclose(t->answers);
    free(t);
    free(u);
}

static const oi_ui_ops text_ops = {
    "text",
    t_welcome, t_message, t_askbool, t_askchoice, t_askoptions, t_asknumber,
    t_askstring, t_askdir, t_askfile, t_askdisk, t_confirm, t_working,
    t_complete, t_gauge, t_problem, t_final, t_panel, t_poll, t_close,
};

oi_ui *oi_ui_text_open(oi *g)
{
    oi_ui *u = calloc(1, sizeof *u);
    tui *t = calloc(1, sizeof *t);
    const char *f = getenv("OI_ANSWERS");
    if (!u || !t) {
        free(u);
        free(t);
        return NULL;
    }
    if (f && *f)
        t->answers = fopen(f, "r");
    u->ops = &text_ops;
    u->g = g;
    u->priv = t;
    return u;
}
