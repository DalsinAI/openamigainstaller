/*
 * The pages a script shows: welcome, message, working, complete and the
 * ask functions. At the Novice level the ask functions are not shown and
 * give their defaults, and messages are shown only with (all), as with
 * the Installer.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_sys.h"
#include "oi_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN(name) static oi_val name(oi *g, oi_node *c, oi_args *a)

static int novice(oi *g)
{
    return oi_get_num(g, "@user-level") == OI_NOVICE;
}

/* What the page answered: on Back the script's (back ...) runs, on Abort
 * the installation stops. Returns 1 when the answer stands. */
static int answered(oi *g, oi_args *a, int r)
{
    if (r == OI_UI_ABORT) {
        oi_abort(g, "");
        return 0;
    }
    if (r == OI_UI_BACK) {
        oi_param *p = &a->p[GP_BACK];
        if (p->set && p->node) {
            oi_val v = oi_eval_seq(g, p->node->kid + 1, p->node->nkid - 1);
            oi_free(&v);
        }
        return 0;
    }
    return 1;
}

static void fill(oi *g, oi_args *a, oi_ask *q, char **pr, char **hp, const char *helpvar)
{
    memset(q, 0, sizeof *q);
    *pr = oi_param_str(a, GP_PROMPT);
    *hp = oi_param_str(a, GP_HELP);
    q->prompt = *pr ? *pr : "";
    q->help = *hp ? *hp : oi_get_str(g, helpvar);
    q->can_back = a->p[GP_BACK].set;
}

FN(f_welcome)
{
    char *s = oi_cat_args(g, a, 0);
    (void)c;
    oi_welcome_page(g, s);
    free(s);
    return oi_num(g->unwind == GU_NONE);
}

FN(f_message)
{
    char *s;
    int r;
    (void)c;
    if (novice(g) && !a->p[GP_ALL].set)
        return oi_num(0);
    s = oi_cat_args(g, a, 0);
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui) {
        free(s);
        return oi_num(0);
    }
    r = g->ui->ops->message(g->ui, s ? s : "", a->p[GP_BACK].set);
    free(s);
    answered(g, a, r);
    return oi_num(r == OI_UI_OK);
}

FN(f_working)
{
    char *s = oi_cat_args(g, a, 0);
    (void)c;
    oi_ensure_welcome(g);
    if (g->ui && g->unwind == GU_NONE)
        g->ui->ops->working(g->ui, s ? s : "");
    free(s);
    return oi_num(1);
}

FN(f_complete)
{
    long n = oi_argnum(g, a, 0);
    (void)c;
    if (n < 0)
        n = 0;
    if (n > 100)
        n = 100;
    g->complete = (int)n;
    if (g->ui && g->welcomed)
        g->ui->ops->complete(g->ui, (int)n);
    return oi_num(n);
}

FN(f_askbool)
{
    oi_ask q;
    char *pr, *hp, *yes = NULL, *no = NULL;
    long ans = oi_param_num(a, GP_DEFAULT, 0, 0) ? 1 : 0;
    int r;
    (void)c;
    if (novice(g))
        return oi_num(ans);
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui)
        return oi_num(ans);
    fill(g, a, &q, &pr, &hp, "@askbool-help");
    if (a->p[GP_CHOICES].n > 0)
        yes = oi_tostr(&a->p[GP_CHOICES].v[0]);
    if (a->p[GP_CHOICES].n > 1)
        no = oi_tostr(&a->p[GP_CHOICES].v[1]);
    q.yes = yes ? yes : "Yes";
    q.no = no ? no : "No";
    q.num = ans;
    r = g->ui->ops->askbool(g->ui, &q);
    if (answered(g, a, r))
        ans = q.num ? 1 : 0;
    free(pr);
    free(hp);
    free(yes);
    free(no);
    return oi_num(ans);
}

static int choices(oi_args *a, oi_ask *q, char ***store)
{
    oi_param *p = &a->p[GP_CHOICES];
    int i;
    *store = calloc((size_t)(p->n + 1), sizeof **store);
    if (!*store)
        return -1;
    for (i = 0; i < p->n; i++)
        (*store)[i] = oi_tostr(&p->v[i]);
    q->choices = (const char **)*store;
    q->nchoices = p->n;
    return 0;
}

static void free_choices(char **s, int n)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < n; i++)
        free(s[i]);
    free(s);
}

FN(f_askchoice)
{
    oi_ask q;
    char *pr, *hp, **ch = NULL;
    long ans = oi_param_num(a, GP_DEFAULT, 0, 0);
    int r, n = a->p[GP_CHOICES].n;
    (void)c;
    if (n == 0) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (askchoice) wants (choices)", c->line);
        return oi_num(0);
    }
    if (ans < 0 || ans >= n)
        ans = 0;
    if (novice(g))
        return oi_num(ans);
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui)
        return oi_num(ans);
    fill(g, a, &q, &pr, &hp, "@askchoice-help");
    if (choices(a, &q, &ch) == 0) {
        q.num = ans;
        r = g->ui->ops->askchoice(g->ui, &q);
        if (answered(g, a, r) && q.num >= 0 && q.num < n)
            ans = q.num;
    }
    free_choices(ch, n);
    free(pr);
    free(hp);
    return oi_num(ans);
}

FN(f_askoptions)
{
    oi_ask q;
    char *pr, *hp, **ch = NULL;
    long ans = oi_param_num(a, GP_DEFAULT, 0, -1);
    int r, n = a->p[GP_CHOICES].n;
    (void)c;
    if (n == 0) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (askoptions) wants (choices)", c->line);
        return oi_num(0);
    }
    if (n < 32)
        ans &= (1L << n) - 1;
    if (novice(g))
        return oi_num(ans);
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui)
        return oi_num(ans);
    fill(g, a, &q, &pr, &hp, "@askoptions-help");
    if (choices(a, &q, &ch) == 0) {
        q.num = ans;
        r = g->ui->ops->askoptions(g->ui, &q);
        if (answered(g, a, r))
            ans = q.num;
    }
    free_choices(ch, n);
    free(pr);
    free(hp);
    return oi_num(ans);
}

FN(f_asknumber)
{
    oi_ask q;
    char *pr, *hp;
    long lo = oi_param_num(a, GP_RANGE, 0, 0), hi = oi_param_num(a, GP_RANGE, 1, 100);
    long ans = oi_param_num(a, GP_DEFAULT, 0, lo);
    int r;
    (void)c;
    if (!a->p[GP_RANGE].set) {
        lo = 0;
        hi = 0x7fffffffL;
        if (ans < 0)
            lo = ans;
    }
    if (ans < lo)
        ans = lo;
    if (ans > hi)
        ans = hi;
    if (novice(g))
        return oi_num(ans);
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui)
        return oi_num(ans);
    fill(g, a, &q, &pr, &hp, "@asknumber-help");
    q.min = lo;
    q.max = hi;
    q.num = ans;
    r = g->ui->ops->asknumber(g->ui, &q);
    if (answered(g, a, r) && q.num >= lo && q.num <= hi)
        ans = q.num;
    free(pr);
    free(hp);
    return oi_num(ans);
}

/* askstring, askdir and askfile: the answer is a string */
static oi_val ask_text(oi *g, oi_node *c, oi_args *a, int kind)
{
    static const char *const helps[] = { "@askstring-help", "@askdir-help", "@askfile-help" };
    oi_ask q;
    char *pr, *hp, *dflt = oi_param_str(a, GP_DEFAULT);
    oi_val ans;
    int r;
    (void)c;
    if (!dflt)
        dflt = oi_strdup(kind ? oi_get_str(g, "@default-dest") : "");
    if (kind && !a->p[GP_DEFAULT].set) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (%s) wants (default)", c->line, c->kid[0]->text);
        free(dflt);
        return oi_str("");
    }
    ans = oi_str(dflt ? dflt : "");
    if (novice(g)) {
        free(dflt);
        return ans;
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !g->ui) {
        free(dflt);
        return ans;
    }
    fill(g, a, &q, &pr, &hp, helps[kind]);
    q.dflt = dflt ? dflt : "";
    q.newpath = a->p[GP_NEWPATH].set;
    q.disk = a->p[GP_DISK].set;
    q.assigns = a->p[GP_ASSIGNS].set;
    r = kind == 0 ? g->ui->ops->askstring(g->ui, &q)
      : kind == 1 ? g->ui->ops->askdir(g->ui, &q)
                  : g->ui->ops->askfile(g->ui, &q);
    if (answered(g, a, r) && q.answer) {
        oi_free(&ans);
        ans = oi_str(q.answer);
    }
    free(q.answer);
    free(pr);
    free(hp);
    free(dflt);
    return ans;
}

FN(f_askstring) { return ask_text(g, c, a, 0); }
FN(f_askdir)    { return ask_text(g, c, a, 1); }
FN(f_askfile)   { return ask_text(g, c, a, 2); }

FN(f_askdisk)
{
    oi_ask q;
    char *pr, *hp, *dest = oi_param_str(a, GP_DEST), *vol;
    int r = OI_UI_OK;
    size_t n;
    (void)c;
    if (!dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (askdisk) wants (dest)", c->line);
        return oi_num(0);
    }
    /* the volume's name, with its colon */
    n = strlen(dest);
    vol = malloc(n + 2);
    if (!vol) {
        free(dest);
        return oi_num(0);
    }
    strcpy(vol, dest);
    if (!n || vol[n - 1] != ':')
        strcat(vol, ":");
    if (gs_volume_mounted(vol)) {
        free(dest);
        free(vol);
        return oi_num(1);
    }
    oi_ensure_welcome(g);
    if (g->unwind == GU_NONE && g->ui) {
        fill(g, a, &q, &pr, &hp, "@askdisk-help");
        r = g->ui->ops->askdisk(g->ui, &q, vol);
        answered(g, a, r);
        if (r == OI_UI_OK && a->p[GP_NEWNAME].set) {
            /* (newname): an assign to the disk under another name */
            char *nn = oi_param_str(a, GP_NEWNAME);
            if (nn)
                gs_makeassign(nn, vol, 0);
            free(nn);
        }
        free(pr);
        free(hp);
    }
    free(dest);
    free(vol);
    return oi_num(r == OI_UI_OK);
}

const oi_fn oi_fn_ask[] = {
    { "welcome", f_welcome, 0, GF_NOPARAM },
    { "message", f_message, 0, 0 },
    { "working", f_working, 0, GF_NOPARAM },
    { "complete", f_complete, 1, GF_NOPARAM },
    { "askbool", f_askbool, 0, 0 },
    { "askchoice", f_askchoice, 0, 0 },
    { "askoptions", f_askoptions, 0, 0 },
    { "asknumber", f_asknumber, 0, 0 },
    { "askstring", f_askstring, 0, 0 },
    { "askdir", f_askdir, 0, 0 },
    { "askfile", f_askfile, 0, 0 },
    { "askdisk", f_askdisk, 0, 0 },
    { NULL, NULL, 0, 0 }
};
