/*
 * The interpreter: values, variables, calls, parameters, errors, and the
 * run of a whole script with its first and last pages.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_ui.h"
#include "oi_sys.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- values */

oi_val oi_num(long n)
{
    oi_val v;
    v.type = GV_NUM;
    v.num = n;
    v.str = NULL;
    return v;
}

oi_val oi_strn(const char *s, size_t n)
{
    oi_val v;
    v.type = GV_STR;
    v.num = 0;
    v.str = malloc(n + 1);
    if (v.str) {
        if (n)
            memcpy(v.str, s, n);
        v.str[n] = 0;
    } else {
        v.type = GV_NUM;
    }
    return v;
}

oi_val oi_str(const char *s)
{
    return oi_strn(s ? s : "", s ? strlen(s) : 0);
}

oi_val oi_str_own(char *s)
{
    oi_val v;
    if (!s)
        return oi_str("");
    v.type = GV_STR;
    v.num = 0;
    v.str = s;
    return v;
}

void oi_free(oi_val *v)
{
    if (v->type == GV_STR)
        free(v->str);
    v->type = GV_NUM;
    v->num = 0;
    v->str = NULL;
}

oi_val oi_copy(const oi_val *v)
{
    if (v->type == GV_STR)
        return oi_str(v->str);
    return oi_num(v->num);
}

/* A string as a number: leading spaces, a sign, then decimal, $hex or
 * %binary digits; what follows is left out. */
static long str_to_num(const char *s)
{
    unsigned long v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    if (*s == '$') {
        for (s++;; s++) {
            int c = *s, h;
            if (c >= '0' && c <= '9') h = c - '0';
            else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
            else break;
            v = v * 16 + (unsigned long)h;
        }
    } else if (*s == '%') {
        for (s++; *s == '0' || *s == '1'; s++)
            v = v * 2 + (unsigned long)(*s - '0');
    } else {
        for (; *s >= '0' && *s <= '9'; s++)
            v = v * 10 + (unsigned long)(*s - '0');
    }
    return neg ? -(long)v : (long)v;
}

long oi_tonum(const oi_val *v)
{
    return v->type == GV_NUM ? v->num : str_to_num(v->str);
}

char *oi_tostr(const oi_val *v)
{
    if (v->type == GV_STR)
        return oi_strdup(v->str);
    return oi_xprintf("%ld", v->num);
}

int oi_true(const oi_val *v)
{
    if (v->type == GV_NUM)
        return v->num != 0;
    return v->str[0] != 0;
}

/* ---------------------------------------------------------------- helpers */

char *oi_strdup(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *d = malloc(n + 1);
    if (d)
        memcpy(d, s ? s : "", n + 1);
    return d;
}

char *oi_xprintf(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *s;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return oi_strdup("");
    s = malloc((size_t)n + 1);
    if (!s)
        return NULL;
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int oi_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = lower((unsigned char)*a), y = lower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
}

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++)
        h = h * 33 + (unsigned)lower((unsigned char)*s);
    return h % OI_HASH;
}

/* ---------------------------------------------------------------- variables */

oi_var *oi_lookup(oi *g, const char *name)
{
    oi_var *v;
    for (v = g->vars[hash(name)]; v; v = v->next)
        if (!oi_stricmp(v->name, name))
            return v;
    return NULL;
}

void oi_set(oi *g, const char *name, oi_val val)
{
    oi_var *v = oi_lookup(g, name);
    if (!v) {
        unsigned h = hash(name);
        v = calloc(1, sizeof *v);
        if (!v) {
            oi_free(&val);
            return;
        }
        v->name = oi_strdup(name);
        v->next = g->vars[h];
        g->vars[h] = v;
    } else {
        oi_free(&v->val);
    }
    v->val = val;
}

void oi_set_num(oi *g, const char *name, long n)
{
    oi_set(g, name, oi_num(n));
}

void oi_set_str(oi *g, const char *name, const char *s)
{
    oi_set(g, name, oi_str(s));
}

long oi_get_num(oi *g, const char *name)
{
    oi_var *v = oi_lookup(g, name);
    return v ? oi_tonum(&v->val) : 0;
}

const char *oi_get_str(oi *g, const char *name)
{
    oi_var *v = oi_lookup(g, name);
    return (v && v->val.type == GV_STR) ? v->val.str : "";
}

/* ---------------------------------------------------------------- errors */

void oi_error(oi *g, int code, const char *fmt, ...)
{
    va_list ap;
    if (g->unwind != GU_NONE)
        return;
    va_start(ap, fmt);
    vsnprintf(g->err_msg, sizeof g->err_msg, fmt, ap);
    va_end(ap);
    g->unwind = GU_ERROR;
    g->err_code = code;
    oi_set_str(g, "@error-msg", g->err_msg);
}

void oi_abort(oi *g, const char *msg)
{
    if (g->unwind != GU_NONE)
        return;
    snprintf(g->err_msg, sizeof g->err_msg, "%s", msg ? msg : "");
    g->unwind = GU_ABORT;
    g->err_code = OI_ERR_ABORT;
}

/* ---------------------------------------------------------------- the log */

void oi_log(oi *g, const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    int n;
    if (!g->log_mode || oi_get_num(g, "@no-log"))
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof buf - 3)
        n = (int)sizeof buf - 3;
    buf[n++] = '\n';
    buf[n] = 0;
    gs_write_file(g->log_name, buf, (size_t)n, 1);
}

int oi_pretend(oi *g)
{
    return oi_get_num(g, "@pretend") != 0;
}

/* (confirm [level]): asked when the user's level is at least that level
 * (expert when none is given). */
int oi_confirm_needed(oi *g, oi_args *a)
{
    long level = OI_EXPERT;
    if (!a->p[GP_CONFIRM].set)
        return 0;
    if (oi_get_num(g, "@yes"))
        return 0;
    if (a->p[GP_CONFIRM].n > 0) {
        const oi_val *v = &a->p[GP_CONFIRM].v[0];
        /* a level by number, or by its name ("expert", "average", "novice") */
        if (v->type == GV_STR && !oi_stricmp(v->str, "expert"))
            level = OI_EXPERT;
        else if (v->type == GV_STR && (!oi_stricmp(v->str, "average") || !oi_stricmp(v->str, "intermediate")))
            level = OI_AVERAGE;
        else if (v->type == GV_STR && !oi_stricmp(v->str, "novice"))
            level = OI_NOVICE;
        else
            level = oi_tonum(v);
    }
    return oi_get_num(g, "@user-level") >= level;
}

/* ---------------------------------------------------------------- calls */

oi_val oi_eval_seq(oi *g, oi_node **n, int count)
{
    oi_val r = oi_num(0);
    int i;
    for (i = 0; i < count; i++) {
        oi_free(&r);
        r = oi_eval(g, n[i]);
        if (g->unwind != GU_NONE)
            break;
    }
    return r;
}

static void args_free(oi_args *a)
{
    int i, j;
    for (i = 0; i < GP_COUNT; i++)
        for (j = 0; j < a->p[i].n; j++)
            oi_free(&a->p[i].v[j]);
    free(a->node);
}

oi_val oi_arg(oi *g, oi_args *a, int i)
{
    if (i < 0 || i >= a->n)
        return oi_num(0);
    return oi_eval(g, a->node[i]);
}

long oi_argnum(oi *g, oi_args *a, int i)
{
    oi_val v = oi_arg(g, a, i);
    long n = oi_tonum(&v);
    oi_free(&v);
    return n;
}

char *oi_argstr(oi *g, oi_args *a, int i)
{
    oi_val v = oi_arg(g, a, i);
    char *s = oi_tostr(&v);
    oi_free(&v);
    return s;
}

static int sb_add(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 64;
        char *nb;
        while (nc < *len + n + 1)
            nc *= 2;
        nb = realloc(*buf, nc);
        if (!nb)
            return -1;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
    return 0;
}

char *oi_cat_args(oi *g, oi_args *a, int from)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int i;
    sb_add(&buf, &len, &cap, "", 0);
    for (i = from; i < a->n && g->unwind == GU_NONE; i++) {
        oi_val v = oi_eval(g, a->node[i]);
        char *s = oi_tostr(&v);
        if (s)
            sb_add(&buf, &len, &cap, s, strlen(s));
        free(s);
        oi_free(&v);
    }
    return buf ? buf : oi_strdup("");
}

char *oi_param_str(oi_args *a, int p)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int i;
    if (!a->p[p].set)
        return NULL;
    sb_add(&buf, &len, &cap, "", 0);
    for (i = 0; i < a->p[p].n; i++) {
        char *s = oi_tostr(&a->p[p].v[i]);
        if (s)
            sb_add(&buf, &len, &cap, s, strlen(s));
        free(s);
    }
    return buf ? buf : oi_strdup("");
}

long oi_param_num(oi_args *a, int p, int i, long dflt)
{
    if (!a->p[p].set || i >= a->p[p].n)
        return dflt;
    return oi_tonum(&a->p[p].v[i]);
}

/* printf as the Installer's strings have it: %s, %ld, %lu, %lx, %lX, %ld
 * with flags and widths (RawDoFmt's), %c and %%. A missing argument is
 * left out. */
char *oi_format(oi *g, const char *fmt, oi_args *a, int from)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int next = from;
    const char *p = fmt;
    sb_add(&buf, &len, &cap, "", 0);
    while (*p && g->unwind == GU_NONE) {
        const char *start = p;
        char spec[32];
        int k = 0, conv;
        if (*p != '%') {
            while (*p && *p != '%')
                p++;
            sb_add(&buf, &len, &cap, start, (size_t)(p - start));
            continue;
        }
        p++;
        if (*p == '%') {
            sb_add(&buf, &len, &cap, "%", 1);
            p++;
            continue;
        }
        spec[k++] = '%';
        while (*p && strchr("-+ 0#", *p) && k < 20)
            spec[k++] = *p++;
        while (*p >= '0' && *p <= '9' && k < 24)
            spec[k++] = *p++;
        if (*p == '.') {
            spec[k++] = *p++;
            while (*p >= '0' && *p <= '9' && k < 28)
                spec[k++] = *p++;
        }
        while (*p == 'l' || *p == 'h')
            p++;
        conv = *p;
        if (!conv)
            break;
        p++;
        if (!strchr("sduxXc", conv)) {
            /* not a conversion we know: as it was written */
            sb_add(&buf, &len, &cap, start, (size_t)(p - start));
            continue;
        }
        if (next >= a->n) {
            continue;   /* no argument for it */
        } else {
            oi_val v = oi_eval(g, a->node[next++]);
            char out[64];
            char *s = NULL;
            if (conv == 's') {
                s = oi_tostr(&v);
                spec[k++] = 's';
                spec[k] = 0;
                if (s) {
                    int n = snprintf(NULL, 0, spec, s);
                    char *o = n >= 0 ? malloc((size_t)n + 1) : NULL;
                    if (o) {
                        snprintf(o, (size_t)n + 1, spec, s);
                        sb_add(&buf, &len, &cap, o, (size_t)n);
                        free(o);
                    }
                    free(s);
                }
            } else {
                long n = v.type == GV_STR ? str_to_num(v.str) : v.num;
                spec[k++] = 'l';
                spec[k++] = (char)(conv == 'c' ? 'd' : conv);
                spec[k] = 0;
                if (conv == 'c') {
                    out[0] = (char)n;
                    out[1] = 0;
                } else if (conv == 'd') {
                    snprintf(out, sizeof out, spec, n);
                } else {
                    snprintf(out, sizeof out, spec, (unsigned long)n);
                }
                sb_add(&buf, &len, &cap, out, strlen(out));
            }
            oi_free(&v);
        }
    }
    return buf ? buf : oi_strdup("");
}

static oi_val call_proc(oi *g, oi_proc *pr, oi_node *call)
{
    oi_val r;
    oi_val saved[32];
    int had[32];
    int i, n = pr->nargs < 32 ? pr->nargs : 32;
    /* arguments are evaluated first, then given to the procedure's names,
     * whose earlier values come back when it returns */
    oi_val got[32];
    for (i = 0; i < n; i++)
        got[i] = (i + 1 < call->nkid) ? oi_eval(g, call->kid[i + 1]) : oi_num(0);
    for (i = 0; i < n; i++) {
        const char *name = pr->def->kid[pr->first_arg + i]->text;
        oi_var *v = oi_lookup(g, name);
        had[i] = v != NULL;
        saved[i] = v ? oi_copy(&v->val) : oi_num(0);
        oi_set(g, name, got[i]);
    }
    if (g->unwind != GU_NONE) {
        r = oi_num(0);
    } else if (++g->depth > 400) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: procedures nested too deeply", call->line);
        r = oi_num(0);
    } else {
        r = oi_eval_seq(g, pr->def->kid + pr->first_body, pr->def->nkid - pr->first_body);
    }
    g->depth--;
    for (i = 0; i < n; i++) {
        const char *name = pr->def->kid[pr->first_arg + i]->text;
        if (had[i]) {
            oi_set(g, name, saved[i]);
        } else {
            oi_free(&saved[i]);
            oi_set(g, name, oi_num(0));
        }
    }
    return r;
}

static oi_proc *find_proc(oi *g, const char *name)
{
    oi_proc *p;
    for (p = g->procs; p; p = p->next)
        if (!oi_stricmp(p->name, name))
            return p;
    return NULL;
}

/* A format string at a list's head: ("%s is %ld" a b). */
static oi_val call_format(oi *g, const char *fmt, oi_node *call)
{
    oi_args a;
    oi_val r;
    memset(&a, 0, sizeof a);
    a.n = call->nkid - 1;
    a.node = call->kid + 1;
    r = oi_str_own(oi_format(g, fmt, &a, 0));
    return r;
}

static oi_val call_builtin(oi *g, const oi_fn *fn, oi_node *call)
{
    oi_args a;
    oi_val r;
    int i;
    memset(&a, 0, sizeof a);
    a.node = malloc((size_t)(call->nkid + 1) * sizeof *a.node);
    if (!a.node) {
        oi_error(g, OI_ERR_NOMEM, "out of memory");
        return oi_num(0);
    }
    for (i = 1; i < call->nkid; i++) {
        oi_node *k = call->kid[i];
        int pid = -1;
        if (!(fn->flags & (GF_SPECIAL | GF_NOPARAM)) && k->type == GN_LIST && k->nkid > 0 &&
            k->kid[0]->type == GN_SYM)
            pid = oi_param_id(k->kid[0]->text);
        if (pid >= 0) {
            oi_param *p = &a.p[pid];
            int j;
            p->set = 1;
            p->node = k;
            if (pid == GP_BACK)
                continue;   /* run only when the user goes back */
            for (j = 1; j < k->nkid && p->n < OI_MAXPARAM; j++) {
                p->v[p->n++] = oi_eval(g, k->kid[j]);
                if (g->unwind != GU_NONE)
                    break;
            }
        } else {
            a.node[a.n++] = k;
        }
        if (g->unwind != GU_NONE)
            break;
    }
    if (g->unwind != GU_NONE) {
        args_free(&a);
        return oi_num(0);
    }
    if (a.n < fn->min_args) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (%s) needs more arguments", call->line, fn->name);
        args_free(&a);
        return oi_num(0);
    }
    if (g->trace_on)
        fprintf(stderr, "trace: line %d (%s)\n", call->line, fn->name);
    r = fn->fn(g, call, &a);
    args_free(&a);
    return r;
}

oi_val oi_eval(oi *g, oi_node *n)
{
    if (g->unwind != GU_NONE)
        return oi_num(0);
    switch (n->type) {
    case GN_NUM:
        return oi_num(n->num);
    case GN_STR:
        return oi_strn(n->text, (size_t)n->num);
    case GN_SYM: {
        oi_var *v = oi_lookup(g, n->text);
        if (v)
            return oi_copy(&v->val);
        /* an unset variable is 0, as with the Installer */
        return oi_num(0);
    }
    case GN_LIST: {
        oi_node *head;
        if (n->nkid == 0)
            return oi_num(0);
        head = n->kid[0];
        if (head->type == GN_LIST)
            return oi_eval_seq(g, n->kid, n->nkid);    /* ((a) (b)): a block */
        if (head->type == GN_STR)
            return call_format(g, head->text, n);
        if (head->type == GN_NUM) {
            oi_error(g, OI_ERR_SCRIPT, "Line %d: a number can't be called", n->line);
            return oi_num(0);
        }
        if (!n->fn) {
            n->fn = oi_find_fn(head->text);
        }
        if (n->fn)
            return call_builtin(g, n->fn, n);
        {
            oi_proc *pr = find_proc(g, head->text);
            oi_var *v;
            if (pr)
                return call_proc(g, pr, n);
            v = oi_lookup(g, head->text);
            if (v && v->val.type == GV_STR) {
                char *fmt = oi_strdup(v->val.str);
                oi_val r = call_format(g, fmt ? fmt : "", n);
                free(fmt);
                return r;
            }
        }
        oi_error(g, OI_ERR_SCRIPT, "Line %d: undefined function '%s'", n->line, head->text);
        return oi_num(0);
    }
    }
    return oi_num(0);
}

/* ---------------------------------------------------------------- setup */

static void register_procs(oi *g, oi_node *list)
{
    int i;
    /* procedures may be called before their definition is reached: the
     * whole script is searched for them first, as the Installer does */
    for (i = 0; i < list->nkid; i++) {
        oi_node *k = list->kid[i];
        if (k->type != GN_LIST || k->nkid == 0)
            continue;
        if (k->kid[0]->type == GN_SYM && !oi_stricmp(k->kid[0]->text, "procedure") &&
            k->nkid >= 2 && k->kid[1]->type == GN_SYM) {
            oi_proc *p = calloc(1, sizeof *p);
            int j = 2;
            if (!p)
                continue;
            p->name = oi_strdup(k->kid[1]->text);
            p->def = k;
            p->first_arg = 2;
            while (j < k->nkid && k->kid[j]->type == GN_SYM)
                j++;
            p->nargs = j - 2;
            p->first_body = j;
            p->next = g->procs;
            g->procs = p;
        }
        register_procs(g, k);
    }
}

static const char *const help_texts[][2] = {
    { "@askoptions-help", "Choose any or all of the options by clicking them. A ticked option will be installed." },
    { "@askchoice-help", "Choose one of the options by clicking it." },
    { "@asknumber-help", "Type a number in the field, within the range shown." },
    { "@askstring-help", "Type the text asked for in the field." },
    { "@askdisk-help", "Put the disk asked for in any drive. The installation carries on when it is in." },
    { "@askfile-help", "Choose a file with the file requester, or type its name." },
    { "@askdir-help", "Choose a drawer with the requester, or type its name." },
    { "@copylib-help", "A library or device is being copied. When one is already there, the newer one is kept unless you choose otherwise." },
    { "@copyfiles-help", "Files are being copied to the drawer shown." },
    { "@makedir-help", "A new drawer is being made." },
    { "@startup-help", "Lines are being added to S:User-Startup, so the program has what it needs each time the Amiga starts." },
};

static void init_vars(oi *g)
{
    size_t i;
    const char *dest = "Work:";
    if (gs_exists("Work:") != GS_DIR)
        dest = "SYS:";
    oi_set_str(g, "@abort-button", "Abort Install");
    oi_set_str(g, "@app-name", g->app_name ? g->app_name : "");
    oi_set_str(g, "@default-dest", dest);
    oi_set_str(g, "@error-msg", "");
    oi_set_str(g, "@special-msg", "");
    oi_set_num(g, "@ioerr", 0);
    oi_set_str(g, "@language", g->language ? g->language : "english");
    oi_set_num(g, "@pretend", 0);
    oi_set_num(g, "@user-level", g->def_user);
    oi_set_num(g, "@installer-version", ((long)OI_COMPAT_VER << 16) | OI_COMPAT_REV);
    oi_set_str(g, "@each-name", "");
    oi_set_num(g, "@each-type", 0);
    oi_set_str(g, "@execute-dir", "");
    oi_set_str(g, "@icon", "");
    oi_set_str(g, "@user-startup", "S:User-Startup");
    oi_set_str(g, "@log-file", g->log_name);
    oi_set_num(g, "@no-log", 0);
    /* ours: which Installer this is, for scripts that use the extras */
    oi_set_num(g, "@openinstaller", ((long)OI_VERSION << 16) | OI_REVISION);
    for (i = 0; i < sizeof help_texts / sizeof help_texts[0]; i++)
        oi_set_str(g, help_texts[i][0], help_texts[i][1]);
    oi_set_num(g, "TRUE", 1);
    oi_set_num(g, "FALSE", 0);
}

oi *oi_new(void)
{
    oi *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    snprintf(g->log_name, sizeof g->log_name, "install_log_file");
    g->def_user = OI_AVERAGE;
    g->min_user = OI_NOVICE;
    return g;
}

void oi_delete(oi *g)
{
    int i;
    if (!g)
        return;
    for (i = 0; i < OI_HASH; i++) {
        oi_var *v = g->vars[i];
        while (v) {
            oi_var *n = v->next;
            oi_free(&v->val);
            free(v->name);
            free(v);
            v = n;
        }
    }
    while (g->procs) {
        oi_proc *n = g->procs->next;
        free(g->procs->name);
        free(g->procs);
        g->procs = n;
    }
    oi_node_free(g->script);
    free(g);
}

int oi_load(oi *g, const char *text, size_t len, const char *name, char *err, size_t errlen)
{
    g->script = oi_parse(text, len, err, errlen);
    g->script_name = name;
    if (!g->script)
        return -1;
    register_procs(g, g->script);
    return 0;
}

/* The first pages, shown once: at (welcome), or before the first page or
 * change of any other kind. */
void oi_ensure_welcome(oi *g)
{
    oi_welcome_page(g, NULL);
}

void oi_welcome_page(oi *g, const char *extra)
{
    int level, pretend, log;
    char *text;
    int r;
    if (g->welcomed || !g->ui || g->unwind != GU_NONE)
        return;
    g->welcomed = 1;
    level = (int)oi_get_num(g, "@user-level");
    pretend = g->start_pretend && !g->no_pretend;
    log = g->no_log ? 0 : 2;
    if (g->min_user > level)
        level = g->min_user;
    {
        const char *app = g->app_name && *g->app_name ? g->app_name : oi_get_str(g, "@app-name");
        text = oi_xprintf("Welcome to the %s%sinstallation utility.%s%s", app, *app ? " " : "",
                          extra && *extra ? "\n\n" : "", extra ? extra : "");
    }
    r = g->ui->ops->welcome(g->ui, text, &level, g->min_user, &pretend, g->no_pretend, &log, g->no_log);
    free(text);
    if (r == OI_UI_ABORT) {
        oi_abort(g, "");
        return;
    }
    oi_set_num(g, "@user-level", level);
    oi_set_num(g, "@pretend", g->no_pretend ? 0 : pretend);
    g->log_mode = log;
    if (log == 1)
        snprintf(g->log_name, sizeof g->log_name, "PRT:");
    oi_set_str(g, "@log-file", g->log_name);
    if (g->log_mode) {
        char *head = oi_xprintf("Installation log for %s, by OpenInstaller %s%s\n",
                                oi_get_str(g, "@app-name"), OI_VERSTR,
                                pretend ? " (pretending)" : "");
        if (head) {
            gs_write_file(g->log_name, head, strlen(head), 0);
            free(head);
        }
    }
}

int oi_run(oi *g)
{
    oi_val r;
    int rc = 0, failed = 0;
    init_vars(g);
    if (g->app_name)
        oi_set_str(g, "@app-name", g->app_name);
    oi_set_str(g, "@icon", g->icon);
    r = oi_eval_seq(g, g->script->kid, g->script->nkid);
    oi_free(&r);
    if (g->unwind == GU_ERROR || g->unwind == GU_ABORT) {
        int was = g->unwind;
        char msg[512];
        snprintf(msg, sizeof msg, "%s", g->err_msg);
        if (was == GU_ERROR)
            oi_log(g, "Error: %s", msg);
        else
            oi_log(g, "Aborted: %s", msg);
        /* (onerror ...) runs once, with nothing unwinding */
        if (g->onerror) {
            oi_node *oe = g->onerror;
            g->onerror = NULL;
            g->unwind = GU_NONE;
            r = oi_eval_seq(g, oe->kid + 1, oe->nkid - 1);
            oi_free(&r);
            if (g->unwind == GU_EXIT)
                was = GU_EXIT;
        }
        if (was != GU_EXIT && g->ui) {
            char *t;
            if (was == GU_ERROR)
                t = oi_xprintf("%s\n\nThe installation could not be finished.", msg);
            else if (*msg)
                t = oi_xprintf("%s\n\nThe installation was stopped.", msg);
            else
                t = oi_xprintf("The installation was stopped.");
            g->ui->ops->final(g->ui, t ? t : "", 1);
            free(t);
        }
        rc = was == GU_ERROR ? 10 : (was == GU_EXIT ? 0 : 5);
        failed = was != GU_EXIT;
    }
    if (!failed) {
        if (g->ui && !g->exit_quiet) {
            char *t;
            const char *dest = oi_get_str(g, "@default-dest");
            oi_ensure_welcome(g);
            if (g->exit_msg[0])
                t = oi_xprintf("%s\n\nInstallation complete.", g->exit_msg);
            else if (*oi_get_str(g, "@app-name"))
                t = oi_xprintf("Installation complete.\n\n%s has been installed in %s.",
                               oi_get_str(g, "@app-name"), dest);
            else
                t = oi_xprintf("Installation complete.");
            if (g->log_mode == 2 && t) {
                char *t2 = oi_xprintf("%s\n\nThe log of what was done is in %s.", t, g->log_name);
                free(t);
                t = t2;
            }
            g->ui->ops->final(g->ui, t ? t : "", 0);
            free(t);
        }
        oi_log(g, "Installation complete.");
    }
    return rc;
}

int oi_run_with(oi *g, const char *icon, int pretend)
{
    snprintf(g->icon, sizeof g->icon, "%s", icon ? icon : "");
    g->start_pretend = pretend;
    return oi_run(g);
}
