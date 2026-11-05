/*
 * The language's own functions: control, variables, arithmetic, logic,
 * strings, and what a script asks about the machine.
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
#define UNUSED (void)c; (void)a

/* ---------------------------------------------------------------- control */

FN(f_if)
{
    oi_val v;
    int t;
    (void)a;
    if (c->nkid < 2)
        return oi_num(0);
    v = oi_eval(g, c->kid[1]);
    t = oi_true(&v);
    oi_free(&v);
    if (g->unwind != GU_NONE)
        return oi_num(0);
    if (t)
        return c->nkid > 2 ? oi_eval(g, c->kid[2]) : oi_num(0);
    return c->nkid > 3 ? oi_eval(g, c->kid[3]) : oi_num(0);
}

static int cond(oi *g, oi_node *n)
{
    oi_val v = oi_eval(g, n);
    int t = oi_true(&v);
    oi_free(&v);
    return t && g->unwind == GU_NONE;
}

FN(f_while)
{
    oi_val r = oi_num(0);
    (void)a;
    if (c->nkid < 2)
        return r;
    while (cond(g, c->kid[1])) {
        oi_free(&r);
        r = oi_eval_seq(g, c->kid + 2, c->nkid - 2);
        if (g->unwind != GU_NONE)
            break;
        if (g->ui && g->ui->ops->poll && g->ui->ops->poll(g->ui) == OI_UI_ABORT)
            oi_abort(g, "");
    }
    return r;
}

FN(f_until)
{
    oi_val r = oi_num(0);
    (void)a;
    if (c->nkid < 2)
        return r;
    do {
        oi_free(&r);
        r = oi_eval_seq(g, c->kid + 2, c->nkid - 2);
        if (g->unwind != GU_NONE)
            break;
        if (g->ui && g->ui->ops->poll && g->ui->ops->poll(g->ui) == OI_UI_ABORT) {
            oi_abort(g, "");
            break;
        }
    } while (!cond(g, c->kid[1]) && g->unwind == GU_NONE);
    return r;
}

FN(f_select)
{
    oi_val v;
    long i;
    (void)a;
    if (c->nkid < 2)
        return oi_num(0);
    v = oi_eval(g, c->kid[1]);
    i = oi_tonum(&v);
    oi_free(&v);
    if (g->unwind != GU_NONE || i < 0 || i + 2 >= c->nkid)
        return oi_num(0);
    return oi_eval(g, c->kid[i + 2]);
}

FN(f_foreach)
{
    char *dir, *pat;
    gs_dir *d;
    const char *name;
    int type;
    oi_val r = oi_num(0);
    oi_val v;
    (void)a;
    if (c->nkid < 3)
        return r;
    v = oi_eval(g, c->kid[1]);
    dir = oi_tostr(&v);
    oi_free(&v);
    v = oi_eval(g, c->kid[2]);
    pat = oi_tostr(&v);
    oi_free(&v);
    d = (dir && pat && g->unwind == GU_NONE) ? gs_opendir(dir) : NULL;
    if (d) {
        while ((name = gs_readdir(d, &type)) != NULL && g->unwind == GU_NONE) {
            if (!gs_patmatch(*pat ? pat : "#?", name))
                continue;
            oi_set_str(g, "@each-name", name);
            /* fib_DirEntryType: 2 for a drawer, -3 for a file */
            oi_set_num(g, "@each-type", type == GS_DIR ? 2 : -3);
            oi_free(&r);
            r = oi_eval_seq(g, c->kid + 3, c->nkid - 3);
        }
        gs_closedir(d);
    }
    free(dir);
    free(pat);
    return r;
}

FN(f_procedure)
{
    UNUSED;
    (void)g;
    return oi_num(0);   /* oi_load found every one before the run */
}

FN(f_set)
{
    oi_val r = oi_num(0);
    int i;
    (void)a;
    for (i = 1; i + 1 < c->nkid; i += 2) {
        oi_node *name = c->kid[i];
        oi_val v;
        if (name->type != GN_SYM) {
            oi_error(g, OI_ERR_SCRIPT, "Line %d: (set) wants a name", c->line);
            break;
        }
        v = oi_eval(g, c->kid[i + 1]);
        if (g->unwind != GU_NONE) {
            oi_free(&v);
            break;
        }
        oi_free(&r);
        r = oi_copy(&v);
        oi_set(g, name->text, v);
    }
    return r;
}

FN(f_symbolset)
{
    oi_val r = oi_num(0);
    int i;
    (void)a;
    for (i = 1; i + 1 < c->nkid; i += 2) {
        oi_val n = oi_eval(g, c->kid[i]);
        oi_val v = oi_eval(g, c->kid[i + 1]);
        char *name = oi_tostr(&n);
        oi_free(&n);
        if (g->unwind != GU_NONE || !name) {
            free(name);
            oi_free(&v);
            break;
        }
        oi_free(&r);
        r = oi_copy(&v);
        oi_set(g, name, v);
        free(name);
    }
    return r;
}

FN(f_symbolval)
{
    char *name = oi_argstr(g, a, 0);
    oi_var *v = name ? oi_lookup(g, name) : NULL;
    oi_val r = v ? oi_copy(&v->val) : oi_num(0);
    (void)c;
    free(name);
    return r;
}

FN(f_trap)
{
    oi_val v;
    long flags;
    oi_val r;
    (void)a;
    if (c->nkid < 2)
        return oi_num(0);
    v = oi_eval(g, c->kid[1]);
    flags = oi_tonum(&v);
    oi_free(&v);
    r = oi_eval_seq(g, c->kid + 2, c->nkid - 2);
    if ((g->unwind == GU_ERROR || g->unwind == GU_ABORT) && g->err_code > 0 &&
        (flags & (1L << (g->err_code - 1)))) {
        g->unwind = GU_NONE;
        oi_free(&r);
        r = oi_num(g->err_code);
    }
    return r;
}

FN(f_onerror)
{
    (void)a;
    (void)g;
    g->onerror = c;
    return oi_num(0);
}

FN(f_exit)
{
    char *msg = oi_cat_args(g, a, 0);
    (void)c;
    if (g->unwind != GU_NONE) {
        free(msg);
        return oi_num(0);
    }
    snprintf(g->exit_msg, sizeof g->exit_msg, "%s", msg ? msg : "");
    free(msg);
    g->exit_quiet = a->p[GP_QUIET].set;
    g->unwind = GU_EXIT;
    return oi_num(0);
}

FN(f_abort)
{
    char *msg = oi_cat_args(g, a, 0);
    (void)c;
    if (g->unwind == GU_NONE)
        oi_abort(g, msg);
    free(msg);
    return oi_num(0);
}

FN(f_trace)
{
    UNUSED;
    g->trace_on = 1;
    return oi_num(0);
}

FN(f_retrace)
{
    UNUSED;
    (void)g;
    return oi_num(0);
}

FN(f_debug)
{
    /* the values are worked out first, then printed on one line, so a
     * page shown while working them out doesn't break the line */
    char *line = oi_strdup("");
    int i;
    (void)c;
    for (i = 0; i < a->n && g->unwind == GU_NONE && line; i++) {
        oi_val v = oi_eval(g, a->node[i]);
        char *s, *n;
        if (g->unwind != GU_NONE) {
            oi_free(&v);
            break;
        }
        s = oi_tostr(&v);
        n = oi_xprintf("%s%s%s", line, i ? " " : "", s ? s : "");
        free(s);
        free(line);
        line = n;
        oi_free(&v);
    }
    printf("%s\n", line ? line : "");
    fflush(stdout);
    free(line);
    return oi_num(1);
}

/* ---------------------------------------------------------------- arithmetic */

static long nth(oi *g, oi_args *a, int i)
{
    return oi_argnum(g, a, i);
}

FN(f_add)
{
    long s = 0;
    int i;
    (void)c;
    for (i = 0; i < a->n && g->unwind == GU_NONE; i++)
        s += nth(g, a, i);
    return oi_num(s);
}

FN(f_sub)
{
    long s;
    int i;
    (void)c;
    if (a->n == 1)
        return oi_num(-nth(g, a, 0));
    s = nth(g, a, 0);
    for (i = 1; i < a->n && g->unwind == GU_NONE; i++)
        s -= nth(g, a, i);
    return oi_num(s);
}

FN(f_mul)
{
    long s = 1;
    int i;
    (void)c;
    for (i = 0; i < a->n && g->unwind == GU_NONE; i++)
        s *= nth(g, a, i);
    return oi_num(s);
}

FN(f_div)
{
    long x = nth(g, a, 0), y = nth(g, a, 1);
    if (y == 0) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: division by zero", c->line);
        return oi_num(0);
    }
    return oi_num(x / y);
}

/* Comparing: numbers as numbers, anything with a string in it as text
 * (exactly, as the Installer does). */
static int compare(oi *g, oi_args *a, int *res)
{
    oi_val x = oi_arg(g, a, 0), y = oi_arg(g, a, 1);
    if (x.type == GV_NUM && y.type == GV_NUM) {
        *res = x.num < y.num ? -1 : x.num > y.num;
    } else {
        char *s = oi_tostr(&x), *t = oi_tostr(&y);
        int r = (s && t) ? strcmp(s, t) : 0;
        *res = r < 0 ? -1 : r > 0;
        free(s);
        free(t);
    }
    oi_free(&x);
    oi_free(&y);
    return g->unwind == GU_NONE;
}

FN(f_eq)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r == 0); }
FN(f_ne)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r != 0); }
FN(f_gt)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r > 0); }
FN(f_ge)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r >= 0); }
FN(f_lt)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r < 0); }
FN(f_le)  { int r = 0; (void)c; compare(g, a, &r); return oi_num(r <= 0); }

FN(f_and)
{
    int i, r = 1;
    (void)c;
    for (i = 0; i < a->n && g->unwind == GU_NONE; i++) {
        oi_val v = oi_arg(g, a, i);
        if (!oi_true(&v))
            r = 0;
        oi_free(&v);
    }
    return oi_num(r);
}

FN(f_or)
{
    int i, r = 0;
    (void)c;
    for (i = 0; i < a->n && g->unwind == GU_NONE; i++) {
        oi_val v = oi_arg(g, a, i);
        if (oi_true(&v))
            r = 1;
        oi_free(&v);
    }
    return oi_num(r);
}

FN(f_xor)
{
    oi_val x = oi_arg(g, a, 0), y = oi_arg(g, a, 1);
    int r = oi_true(&x) != oi_true(&y);
    (void)c;
    oi_free(&x);
    oi_free(&y);
    return oi_num(r);
}

FN(f_not)
{
    oi_val x = oi_arg(g, a, 0);
    int r = !oi_true(&x);
    (void)c;
    oi_free(&x);
    return oi_num(r);
}

FN(f_bitand) { (void)c; return oi_num(nth(g, a, 0) & nth(g, a, 1)); }
FN(f_bitor)  { (void)c; return oi_num(nth(g, a, 0) | nth(g, a, 1)); }
FN(f_bitxor) { (void)c; return oi_num(nth(g, a, 0) ^ nth(g, a, 1)); }
FN(f_bitnot) { (void)c; return oi_num(~nth(g, a, 0)); }

FN(f_shiftleft)
{
    unsigned long x = (unsigned long)nth(g, a, 0);
    long n = nth(g, a, 1);
    (void)c;
    return oi_num(n >= 32 || n < 0 ? 0 : (long)(x << n));
}

FN(f_shiftright)
{
    unsigned long x = (unsigned long)nth(g, a, 0);
    long n = nth(g, a, 1);
    (void)c;
    return oi_num(n >= 32 || n < 0 ? 0 : (long)(x >> n));
}

/* (in value bit ...): value with only those bits */
FN(f_in)
{
    long v = nth(g, a, 0), mask = 0;
    int i;
    (void)c;
    for (i = 1; i < a->n && g->unwind == GU_NONE; i++) {
        long b = nth(g, a, i);
        if (b >= 0 && b < 32)
            mask |= 1L << b;
    }
    return oi_num(v & mask);
}

/* ---------------------------------------------------------------- strings */

FN(f_cat)
{
    (void)c;
    return oi_str_own(oi_cat_args(g, a, 0));
}

FN(f_substr)
{
    char *s = oi_argstr(g, a, 0);
    long len = s ? (long)strlen(s) : 0, start = nth(g, a, 1), n;
    oi_val r;
    (void)c;
    n = a->n > 2 ? nth(g, a, 2) : len;
    if (start < 0)
        start = 0;
    if (start > len)
        start = len;
    if (n < 0)
        n = 0;
    if (start + n > len)
        n = len - start;
    r = oi_strn(s ? s + start : "", (size_t)n);
    free(s);
    return r;
}

FN(f_strlen)
{
    char *s = oi_argstr(g, a, 0);
    long n = s ? (long)strlen(s) : 0;
    (void)c;
    free(s);
    return oi_num(n);
}

/* AddPart() of dos.library: a name with a colon replaces the path, and
 * each leading / goes up one drawer. */
char *oi_tackon(const char *path, const char *file)
{
    size_t pl;
    char *out;
    if (strchr(file, ':') || !*path)
        return oi_strdup(file);
    out = malloc(strlen(path) + strlen(file) + 2);
    if (!out)
        return NULL;
    strcpy(out, path);
    pl = strlen(out);
    while (*file == '/') {
        /* up one: take off the last part of the path */
        char *colon = strrchr(out, ':'), *slash;
        if (pl && out[pl - 1] == '/')
            out[--pl] = 0;
        slash = strrchr(out, '/');
        if (slash && (!colon || slash > colon)) {
            *slash = 0;
        } else if (colon) {
            if (colon[1] == 0) {
                /* already at the volume: AddPart keeps the / */
                break;
            }
            colon[1] = 0;
        } else {
            if (!*out)
                break;
            out[0] = 0;
        }
        pl = strlen(out);
        file++;
    }
    if (pl && out[pl - 1] != ':' && out[pl - 1] != '/' && *file)
        strcat(out, "/");
    strcat(out, file);
    return out;
}

FN(f_tackon)
{
    char *p = oi_argstr(g, a, 0), *f = oi_argstr(g, a, 1);
    char *r = (p && f) ? oi_tackon(p, f) : NULL;
    (void)c;
    free(p);
    free(f);
    return oi_str_own(r);
}

FN(f_fileonly)
{
    char *p = oi_argstr(g, a, 0), *s, *r;
    (void)c;
    if (!p)
        return oi_str("");
    s = strrchr(p, '/');
    if (!s)
        s = strrchr(p, ':');
    r = oi_strdup(s ? s + 1 : p);
    free(p);
    return oi_str_own(r);
}

FN(f_pathonly)
{
    char *p = oi_argstr(g, a, 0), *slash, *colon;
    (void)c;
    if (!p)
        return oi_str("");
    slash = strrchr(p, '/');
    colon = strrchr(p, ':');
    if (slash && (!colon || slash > colon))
        *slash = 0;
    else if (colon)
        colon[1] = 0;
    else
        p[0] = 0;
    return oi_str_own(p);
}

FN(f_patmatch)
{
    char *pat = oi_argstr(g, a, 0), *s = oi_argstr(g, a, 1);
    int r = (pat && s) ? gs_patmatch(pat, s) : 0;
    (void)c;
    free(pat);
    free(s);
    return oi_num(r);
}

/* ---------------------------------------------------------------- the machine */

FN(f_getenv)
{
    char *n = oi_argstr(g, a, 0);
    char *v = n ? gs_getenv(n) : NULL;
    (void)c;
    free(n);
    return oi_str_own(v ? v : oi_strdup(""));
}

FN(f_getassign)
{
    char *n = oi_argstr(g, a, 0);
    char *o = a->n > 1 ? oi_argstr(g, a, 1) : oi_strdup("a");
    char *v = (n && o) ? gs_getassign(n, o) : NULL;
    (void)c;
    free(n);
    free(o);
    return oi_str_own(v ? v : oi_strdup(""));
}

FN(f_getdevice)
{
    char *p = oi_argstr(g, a, 0);
    char *v = p ? gs_getdevice(p) : NULL;
    (void)c;
    free(p);
    return oi_str_own(v ? v : oi_strdup(""));
}

FN(f_getdiskspace)
{
    char *p = oi_argstr(g, a, 0);
    char *u = a->n > 1 ? oi_argstr(g, a, 1) : NULL;
    long long b = p ? gs_diskspace(p) : -1;
    long r;
    (void)c;
    if (b < 0) {
        r = -1;
    } else if (u && (*u == 'K' || *u == 'k')) {
        r = (long)(b >> 10);
    } else if (u && (*u == 'M' || *u == 'm')) {
        r = (long)(b >> 20);
    } else if (u && (*u == 'G' || *u == 'g')) {
        r = (long)(b >> 30);
    } else {
        /* bytes, held at the most a long can say */
        r = b > 0x7fffffffLL ? 0x7fffffffL : (long)b;
    }
    free(p);
    free(u);
    return oi_num(r);
}

FN(f_getsize)
{
    char *p = oi_argstr(g, a, 0);
    long long n = p ? gs_size(p) : -1;
    (void)c;
    free(p);
    return oi_num(n < 0 ? 0 : (n > 0x7fffffffLL ? 0x7fffffffL : (long)n));
}

FN(f_getsum)
{
    char *p = oi_argstr(g, a, 0);
    int ok = 0;
    unsigned long s = p ? gs_getsum(p, &ok) : 0;
    (void)c;
    free(p);
    return oi_num(ok ? (long)s : 0);
}

FN(f_getversion)
{
    char *p = a->n > 0 ? oi_argstr(g, a, 0) : NULL;
    long v = gs_getversion(p, a->p[GP_RESIDENT].set);
    (void)c;
    free(p);
    return oi_num(v < 0 ? 0 : v);
}

FN(f_exists)
{
    char *p = oi_argstr(g, a, 0);
    int r = 0;
    (void)c;
    if (p) {
        /* a volume that isn't mounted is not asked for: (noreq) or not,
         * the answer is 0 (dos.library's requester is turned off) */
        r = gs_exists(p);
    }
    free(p);
    return oi_num(r);
}

FN(f_earlier)
{
    char *x = oi_argstr(g, a, 0), *y = oi_argstr(g, a, 1);
    int res = 0, r = 0;
    if (x && y) {
        if (gs_date_cmp(x, y, &res) == 0)
            r = res < 0;
        else
            oi_error(g, OI_ERR_DOS, "Line %d: can't read the dates of %s and %s", c->line, x, y);
    }
    free(x);
    free(y);
    return oi_num(r);
}

FN(f_expandpath)
{
    char *p = oi_argstr(g, a, 0);
    char *r = p ? gs_expandpath(p) : NULL;
    (void)c;
    free(p);
    return oi_str_own(r ? r : oi_strdup(""));
}

FN(f_database)
{
    char *w = oi_argstr(g, a, 0);
    char *v;
    (void)c;
    if (!w)
        return oi_str("");
    if (!oi_stricmp(w, "installer-version") || !oi_stricmp(w, "installer"))
        v = oi_xprintf("%d.%d", OI_COMPAT_VER, OI_COMPAT_REV);
    else
        v = gs_database(w);
    free(w);
    if (a->n > 1) {
        char *want = oi_argstr(g, a, 1);
        int eq = (v && want) ? !oi_stricmp(v, want) : 0;
        free(want);
        free(v);
        return oi_num(eq);
    }
    return oi_str_own(v ? v : oi_strdup("unknown"));
}

FN(f_user)
{
    long old = oi_get_num(g, "@user-level"), n = nth(g, a, 0);
    (void)c;
    if (n < OI_NOVICE)
        n = OI_NOVICE;
    if (n > OI_EXPERT)
        n = OI_EXPERT;
    oi_set_num(g, "@user-level", n);
    return oi_num(old);
}

FN(f_transcript)
{
    char *s = oi_cat_args(g, a, 0);
    (void)c;
    oi_ensure_welcome(g);
    if (s)
        oi_log(g, "%s", s);
    free(s);
    return oi_num(1);
}

/* (iconinfo (dest "name") (gettooltype "TT" "var") (getdefaulttool "var")
 * (getstack "var") (getposition "x" "y")): an icon's settings into
 * variables. */
FN(f_iconinfo)
{
    char *dest = oi_param_str(a, GP_DEST);
    oi_param *p;
    int i;
    (void)c;
    if (!dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (iconinfo) wants (dest)", c->line);
        return oi_num(0);
    }
    p = &a->p[GP_GETTOOLTYPE];
    for (i = 0; i + 1 < p->n; i += 2) {
        char *tt = oi_tostr(&p->v[i]), *var = oi_tostr(&p->v[i + 1]);
        char *val = (tt && var) ? gs_icon_get_tooltype(dest, tt) : NULL;
        if (var)
            oi_set_str(g, var, val ? val : "");
        free(tt);
        free(var);
        free(val);
    }
    p = &a->p[GP_GETDEFAULTTOOL];
    if (p->n > 0) {
        char *var = oi_tostr(&p->v[0]);
        char *val = gs_icon_get_default_tool(dest);
        if (var)
            oi_set_str(g, var, val ? val : "");
        free(var);
        free(val);
    }
    p = &a->p[GP_GETSTACK];
    if (p->n > 0) {
        char *var = oi_tostr(&p->v[0]);
        long st = gs_icon_get_stack(dest);
        if (var)
            oi_set_num(g, var, st < 0 ? 0 : st);
        free(var);
    }
    p = &a->p[GP_GETPOSITION];
    for (i = 0; i < p->n && i < 2; i++) {
        char *var = oi_tostr(&p->v[i]);
        if (var)
            oi_set_num(g, var, 0);
        free(var);
    }
    free(dest);
    return oi_num(1);
}

/* Things of the OS 3.5 to 3.9 Installers that OS 3.2's also takes: they
 * are kept for scripts that use them and do no more than it does. */
FN(f_effect)       { UNUSED; (void)g; return oi_num(0); }
FN(f_querydisplay) { UNUSED; (void)g; return oi_num(0); }
FN(f_media)        { UNUSED; (void)g; return oi_num(0); }

FN(f_wbobject)
{
    char *name = oi_argstr(g, a, 0);
    const char *fn = c->kid[0]->text;
    int what = !oi_stricmp(fn, "openwbobject") ? 0 : !oi_stricmp(fn, "showwbobject") ? 1 : 2;
    int r = 0;
    if (name && !oi_pretend(g))
        r = gs_wb_object(name, what) == 0;
    free(name);
    return oi_num(r);
}

const oi_fn oi_fn_core[] = {
    { "if", f_if, 0, GF_SPECIAL },
    { "while", f_while, 0, GF_SPECIAL },
    { "until", f_until, 0, GF_SPECIAL },
    { "select", f_select, 0, GF_SPECIAL },
    { "foreach", f_foreach, 0, GF_SPECIAL },
    { "procedure", f_procedure, 0, GF_SPECIAL },
    { "set", f_set, 0, GF_SPECIAL },
    { "symbolset", f_symbolset, 0, GF_SPECIAL },
    { "symbolval", f_symbolval, 1, GF_NOPARAM },
    { "trap", f_trap, 0, GF_SPECIAL },
    { "onerror", f_onerror, 0, GF_SPECIAL },
    { "exit", f_exit, 0, 0 },
    { "abort", f_abort, 0, 0 },
    { "trace", f_trace, 0, 0 },
    { "retrace", f_retrace, 0, 0 },
    { "debug", f_debug, 0, GF_NOPARAM },
    { "+", f_add, 1, GF_NOPARAM },
    { "-", f_sub, 1, GF_NOPARAM },
    { "*", f_mul, 1, GF_NOPARAM },
    { "/", f_div, 2, GF_NOPARAM },
    { "=", f_eq, 2, GF_NOPARAM },
    { "<>", f_ne, 2, GF_NOPARAM },
    { ">", f_gt, 2, GF_NOPARAM },
    { ">=", f_ge, 2, GF_NOPARAM },
    { "<", f_lt, 2, GF_NOPARAM },
    { "<=", f_le, 2, GF_NOPARAM },
    { "and", f_and, 1, GF_NOPARAM },
    { "or", f_or, 1, GF_NOPARAM },
    { "xor", f_xor, 2, GF_NOPARAM },
    { "not", f_not, 1, GF_NOPARAM },
    { "bitand", f_bitand, 2, GF_NOPARAM },
    { "bitor", f_bitor, 2, GF_NOPARAM },
    { "bitxor", f_bitxor, 2, GF_NOPARAM },
    { "bitnot", f_bitnot, 1, GF_NOPARAM },
    { "shiftleft", f_shiftleft, 2, GF_NOPARAM },
    { "shiftright", f_shiftright, 2, GF_NOPARAM },
    { "in", f_in, 1, GF_NOPARAM },
    { "cat", f_cat, 1, GF_NOPARAM },
    { "substr", f_substr, 2, GF_NOPARAM },
    { "strlen", f_strlen, 1, GF_NOPARAM },
    { "tackon", f_tackon, 2, GF_NOPARAM },
    { "fileonly", f_fileonly, 1, GF_NOPARAM },
    { "pathonly", f_pathonly, 1, GF_NOPARAM },
    { "patmatch", f_patmatch, 2, GF_NOPARAM },
    { "getenv", f_getenv, 1, GF_NOPARAM },
    { "getassign", f_getassign, 1, GF_NOPARAM },
    { "getdevice", f_getdevice, 1, GF_NOPARAM },
    { "getdiskspace", f_getdiskspace, 1, GF_NOPARAM },
    { "getsize", f_getsize, 1, GF_NOPARAM },
    { "getsum", f_getsum, 1, GF_NOPARAM },
    { "getversion", f_getversion, 0, 0 },
    { "exists", f_exists, 1, 0 },
    { "earlier", f_earlier, 2, GF_NOPARAM },
    { "expandpath", f_expandpath, 1, GF_NOPARAM },
    { "database", f_database, 1, GF_NOPARAM },
    { "user", f_user, 1, GF_NOPARAM },
    { "transcript", f_transcript, 0, GF_NOPARAM },
    { "iconinfo", f_iconinfo, 0, 0 },
    { "effect", f_effect, 0, GF_NOPARAM },
    { "querydisplay", f_querydisplay, 0, GF_NOPARAM },
    { "showmedia", f_media, 0, 0 },
    { "setmedia", f_media, 0, 0 },
    { "closemedia", f_media, 0, 0 },
    { "openwbobject", f_wbobject, 1, 0 },
    { "showwbobject", f_wbobject, 1, 0 },
    { "closewbobject", f_wbobject, 1, 0 },
    { NULL, NULL, 0, 0 }
};
