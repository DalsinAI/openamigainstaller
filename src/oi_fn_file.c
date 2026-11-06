/*
 * What a script changes: copyfiles, copylib, makedir, delete, rename,
 * protect, startup, textfile, tooltype, execute, run, rexx, makeassign
 * and reboot. While pretending, nothing is changed unless the call has
 * (safe); each change goes to the log.
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

/* May the call change things? Not while pretending, unless (safe). */
static int may_change(oi *g, oi_args *a)
{
    return !oi_pretend(g) || a->p[GP_SAFE].set;
}

/* (confirm): asked when the user's level calls for it. Returns 1 to go
 * on, 0 to skip this part (or the user aborted). */
static int confirmed(oi *g, oi_args *a, const char *what)
{
    char *pr, *hp;
    int r;
    if (!oi_confirm_needed(g, a) || !g->ui)
        return 1;
    pr = oi_param_str(a, GP_PROMPT);
    hp = oi_param_str(a, GP_HELP);
    r = g->ui->ops->confirm(g->ui, pr && *pr ? pr : what, hp ? hp : "");
    free(pr);
    free(hp);
    if (r == OI_UI_ABORT) {
        oi_abort(g, "");
        return 0;
    }
    return r == OI_UI_OK;
}

/* What (optional ...) says about errors. */
static int optional(oi_args *a, const char *word)
{
    oi_param *p = &a->p[GP_OPTIONAL];
    int i;
    for (i = 0; i < p->n; i++)
        if (p->v[i].type == GV_STR && !oi_stricmp(p->v[i].str, word))
            return 1;
    return a->p[oi_param_id(word)].set;
}

static int fails(oi_args *a)
{
    return !optional(a, "nofail");
}

/* An error of the file system: an error, unless (optional "nofail"). */
static void dos_failed(oi *g, oi_args *a, oi_node *c, const char *fmt, const char *x, const char *y)
{
    char msg[600];
    snprintf(msg, sizeof msg, fmt, x, y ? y : "");
    oi_log(g, "Problem: %s", msg);
    oi_set_num(g, "@ioerr", 1);
    if (fails(a))
        oi_error(g, OI_ERR_DOS, "Line %d: %s", c->line, msg);
}

/* ---------------------------------------------------------------- copying */

typedef struct copy_ctx {
    oi *g;
    oi_args *a;
    oi_node *c;
    int gauge;
    const char *name;
    long files;
} copy_ctx;

static int on_progress(void *ud, long long now, long long total)
{
    copy_ctx *k = ud;
    if (!k->gauge || !k->g->ui)
        return 0;
    return k->g->ui->ops->gauge(k->g->ui, k->name, now, total) == OI_UI_ABORT ? -1 : 0;
}

static int copy_one(copy_ctx *k, const char *from, const char *to)
{
    oi *g = k->g;
    int r;
    k->name = from;
    oi_log(g, "Copy file \"%s\" to \"%s\"", from, to);
    if (!may_change(g, k->a))
        return 0;
    if (k->a->p[GP_FORCE].set || optional(k->a, "force")) {
        long bits = gs_get_protect(to);
        if (bits >= 0 && (bits & 0x05))     /* delete or write protected */
            gs_set_protect(to, bits & ~0x0fL);
    }
    r = gs_copy(from, to, on_progress, k);
    if (r == -2) {
        oi_abort(g, "");
        return -1;
    }
    if (r) {
        dos_failed(g, k->a, k->c, "Can't copy \"%s\" to \"%s\"", from, to);
        return g->unwind != GU_NONE ? -1 : 0;
    }
    k->files++;
    if (k->a->p[GP_NOPOSITION].set) {
        size_t n = strlen(to);
        if (n > 5 && !oi_stricmp(to + n - 5, ".info"))
            gs_icon_noposition(to);
    }
    return 0;
}

static int copy_tree(copy_ctx *k, const char *from, const char *to);

static int make_dest(copy_ctx *k, const char *dir)
{
    if (gs_exists(dir) == GS_DIR)
        return 0;
    oi_log(k->g, "Make drawer \"%s\"", dir);
    if (!may_change(k->g, k->a))
        return 0;
    if (gs_makedir(dir)) {
        dos_failed(k->g, k->a, k->c, "Can't make the drawer \"%s\"%s", dir, NULL);
        return -1;
    }
    return 0;
}

/* One entry of a source drawer, file or drawer. */
static int copy_entry(copy_ctx *k, const char *from_dir, const char *name, int type, const char *to_dir)
{
    char *from = oi_tackon(from_dir, name), *to = oi_tackon(to_dir, name);
    int r = 0;
    if (!from || !to) {
        r = -1;
    } else if (type == GS_DIR) {
        if (!k->a->p[GP_FILES].set) {
            r = make_dest(k, to);
            if (!r)
                r = copy_tree(k, from, to);
        }
    } else {
        r = copy_one(k, from, to);
    }
    if (!r && k->a->p[GP_INFOS].set) {
        size_t n = strlen(name);
        if (n < 5 || oi_stricmp(name + n - 5, ".info")) {
            char *fi = oi_xprintf("%s.info", from), *ti = oi_xprintf("%s.info", to);
            if (fi && ti && gs_exists(fi) == GS_FILE && !(type == GS_DIR && k->a->p[GP_FILES].set))
                r = copy_one(k, fi, ti);
            free(fi);
            free(ti);
        }
    }
    free(from);
    free(to);
    return r;
}

static int copy_tree(copy_ctx *k, const char *from, const char *to)
{
    gs_dir *d = gs_opendir(from);
    const char *name;
    int type, r = 0;
    if (!d) {
        dos_failed(k->g, k->a, k->c, "Can't read the drawer \"%s\"%s", from, NULL);
        return -1;
    }
    while (r == 0 && (name = gs_readdir(d, &type)) != NULL) {
        size_t n = strlen(name);
        /* icons go with what they belong to when (infos) is given */
        if (k->a->p[GP_INFOS].set && n > 5 && !oi_stricmp(name + n - 5, ".info")) {
            char *base = oi_strdup(name), *p;
            base[n - 5] = 0;
            p = oi_tackon(from, base);
            if (p && gs_exists(p) != GS_NONE) {
                free(p);
                free(base);
                continue;
            }
            free(p);
            free(base);
        }
        r = copy_entry(k, from, name, type, to);
    }
    gs_closedir(d);
    return r;
}

FN(f_copyfiles)
{
    char *src = oi_param_str(a, GP_SOURCE), *dest = oi_param_str(a, GP_DEST);
    char *newname = oi_param_str(a, GP_NEWNAME);
    copy_ctx k;
    int st;
    memset(&k, 0, sizeof k);
    k.g = g;
    k.a = a;
    k.c = c;
    k.gauge = !a->p[GP_NOGAUGE].set;
    if (!src || !dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (copyfiles) wants (source) and (dest)", c->line);
        goto out;
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !confirmed(g, a, "Copy the files?"))
        goto out;
    st = gs_exists(src);
    if (st == GS_NONE) {
        dos_failed(g, a, c, "\"%s\" isn't there%s", src, NULL);
        goto out;
    }
    if (g->ui)
        g->ui->ops->working(g->ui, "Copying files...");
    if (make_dest(&k, dest))
        goto out;
    if (st == GS_FILE) {
        const char *base = strrchr(src, '/');
        char *to;
        if (!base)
            base = strrchr(src, ':');
        base = base ? base + 1 : src;
        to = oi_tackon(dest, newname && *newname ? newname : base);
        if (to)
            copy_one(&k, src, to);
        if (to && a->p[GP_INFOS].set && g->unwind == GU_NONE) {
            char *fi = oi_xprintf("%s.info", src), *ti = oi_xprintf("%s.info", to);
            if (fi && ti && gs_exists(fi) == GS_FILE)
                copy_one(&k, fi, ti);
            free(fi);
            free(ti);
        }
        free(to);
    } else if (a->p[GP_CHOICES].set) {
        int i;
        for (i = 0; i < a->p[GP_CHOICES].n && g->unwind == GU_NONE; i++) {
            char *name = oi_tostr(&a->p[GP_CHOICES].v[i]);
            char *full = name ? oi_tackon(src, name) : NULL;
            int t = full ? gs_exists(full) : GS_NONE;
            if (t == GS_NONE)
                dos_failed(g, a, c, "\"%s\" isn't there%s", full ? full : "", NULL);
            else
                copy_entry(&k, src, name, t, dest);
            free(name);
            free(full);
        }
    } else if (a->p[GP_PATTERN].set) {
        char *pat = oi_param_str(a, GP_PATTERN);
        gs_dir *d = gs_opendir(src);
        const char *name;
        int type;
        if (!d) {
            dos_failed(g, a, c, "Can't read the drawer \"%s\"%s", src, NULL);
        } else {
            while ((name = gs_readdir(d, &type)) != NULL && g->unwind == GU_NONE)
                if (gs_patmatch(pat, name))
                    copy_entry(&k, src, name, type, dest);
            gs_closedir(d);
        }
        free(pat);
    } else {
        /* (all), or a drawer with nothing more said: all of it */
        copy_tree(&k, src, dest);
    }
out:
    free(src);
    free(dest);
    free(newname);
    return oi_num(g->unwind == GU_NONE);
}

FN(f_copylib)
{
    char *src = oi_param_str(a, GP_SOURCE), *dest = oi_param_str(a, GP_DEST);
    char *newname = oi_param_str(a, GP_NEWNAME), *to = NULL;
    copy_ctx k;
    const char *base;
    long vs, vd;
    int copy = 1;
    memset(&k, 0, sizeof k);
    k.g = g;
    k.a = a;
    k.c = c;
    k.gauge = !a->p[GP_NOGAUGE].set;
    if (!src || !dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (copylib) wants (source) and (dest)", c->line);
        goto out;
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE)
        goto out;
    if (gs_exists(src) != GS_FILE) {
        dos_failed(g, a, c, "\"%s\" isn't there%s", src, NULL);
        goto out;
    }
    base = strrchr(src, '/');
    if (!base)
        base = strrchr(src, ':');
    base = base ? base + 1 : src;
    to = oi_tackon(dest, newname && *newname ? newname : base);
    if (!to)
        goto out;
    vs = gs_getversion(src, 0);
    vd = gs_exists(to) == GS_FILE ? gs_getversion(to, 0) : -1;
    if (vd >= 0 && vs >= 0 && vs <= vd) {
        /* the one there is as new or newer: kept, unless an expert asked
         * for it to be replaced */
        copy = 0;
        if (oi_confirm_needed(g, a) && g->ui) {
            char *q = oi_xprintf("The %s there is version %ld.%ld; this one is %ld.%ld. Replace it?",
                                 base, vd >> 16, vd & 0xffff, vs >> 16, vs & 0xffff);
            int r = g->ui->ops->confirm(g->ui, q ? q : "", oi_get_str(g, "@copylib-help"));
            free(q);
            if (r == OI_UI_ABORT) {
                oi_abort(g, "");
                goto out;
            }
            copy = r == OI_UI_OK;
        }
        oi_log(g, "Kept \"%s\" (version %ld.%ld; the new one is %ld.%ld)", to,
               vd >> 16, vd & 0xffff, vs >> 16, vs & 0xffff);
    } else if (!confirmed(g, a, "Copy the library?")) {
        copy = 0;
    }
    if (copy && g->unwind == GU_NONE) {
        if (g->ui)
            g->ui->ops->working(g->ui, "Copying a library...");
        if (!make_dest(&k, dest)) {
            copy_one(&k, src, to);
            if (a->p[GP_INFOS].set && g->unwind == GU_NONE) {
                char *fi = oi_xprintf("%s.info", src), *ti = oi_xprintf("%s.info", to);
                if (fi && ti && gs_exists(fi) == GS_FILE)
                    copy_one(&k, fi, ti);
                free(fi);
                free(ti);
            }
        }
    }
out:
    free(src);
    free(dest);
    free(newname);
    free(to);
    return oi_num(g->unwind == GU_NONE);
}

/* ---------------------------------------------------------------- drawers */

FN(f_makedir)
{
    char *dir = oi_argstr(g, a, 0);
    int r = 0;
    oi_ensure_welcome(g);
    if (!dir || g->unwind != GU_NONE)
        goto out;
    if (gs_exists(dir) == GS_DIR) {
        r = 1;
        goto out;
    }
    if (!confirmed(g, a, "Make a new drawer?"))
        goto out;
    oi_log(g, "Make drawer \"%s\"", dir);
    if (!may_change(g, a)) {
        r = 1;
        goto out;
    }
    if (gs_makedir(dir)) {
        dos_failed(g, a, c, "Can't make the drawer \"%s\"%s", dir, NULL);
        goto out;
    }
    if (a->p[GP_INFOS].set)
        gs_icon_tooltype(dir, NULL, NULL, -1);  /* -1: give it a drawer icon */
    r = 1;
out:
    free(dir);
    return oi_num(r);
}

static int delete_one(oi *g, oi_args *a, oi_node *c, const char *path)
{
    long bits;
    oi_log(g, "Delete \"%s\"", path);
    if (!may_change(g, a))
        return 0;
    bits = gs_get_protect(path);
    if (bits >= 0 && (bits & 0x01)) {
        /* delete protected: (optional "force") takes it off, "askuser"
         * asks first, "oknodelete" leaves it */
        int go = optional(a, "force");
        if (!go && optional(a, "askuser") && g->ui) {
            char *q = oi_xprintf("\"%s\" is protected from deletion. Delete it anyway?", path);
            int r = g->ui->ops->confirm(g->ui, q ? q : "", "");
            free(q);
            if (r == OI_UI_ABORT) {
                oi_abort(g, "");
                return -1;
            }
            go = r == OI_UI_OK;
        }
        if (!go) {
            if (optional(a, "oknodelete"))
                return 0;
            dos_failed(g, a, c, "\"%s\" is protected from deletion%s", path, NULL);
            return -1;
        }
        gs_set_protect(path, bits & ~0x0fL);
    }
    if (gs_delete(path)) {
        dos_failed(g, a, c, "Can't delete \"%s\"%s", path, NULL);
        return -1;
    }
    return 0;
}

static int delete_tree(oi *g, oi_args *a, oi_node *c, const char *path)
{
    gs_dir *d = gs_opendir(path);
    const char *name;
    int type, r = 0;
    char **names = NULL;
    int *types = NULL, n = 0, i;
    if (!d)
        return delete_one(g, a, c, path);
    /* the names first: a drawer isn't changed while it is read */
    while ((name = gs_readdir(d, &type)) != NULL) {
        char **nn = realloc(names, (size_t)(n + 1) * sizeof *nn);
        int *nt = realloc(types, (size_t)(n + 1) * sizeof *nt);
        if (!nn || !nt) {
            free(nn ? nn : names);
            free(nt ? nt : types);
            gs_closedir(d);
            return -1;
        }
        names = nn;
        types = nt;
        names[n] = oi_tackon(path, name);
        types[n++] = type;
    }
    gs_closedir(d);
    for (i = 0; i < n; i++) {
        if (!r && names[i])
            r = types[i] == GS_DIR ? delete_tree(g, a, c, names[i]) : delete_one(g, a, c, names[i]);
        free(names[i]);
    }
    free(names);
    free(types);
    return r ? r : delete_one(g, a, c, path);
}

FN(f_delete)
{
    char *path = oi_argstr(g, a, 0);
    int r = 0;
    oi_ensure_welcome(g);
    if (!path || g->unwind != GU_NONE)
        goto out;
    if (!confirmed(g, a, "Delete this?"))
        goto out;
    if (strpbrk(path, "#?*~[|(")) {
        /* a pattern: what matches it in its drawer */
        char *dir = oi_strdup(path), *slash, *colon, *pat;
        gs_dir *d;
        slash = strrchr(dir, '/');
        colon = strrchr(dir, ':');
        if (slash && (!colon || slash > colon)) {
            *slash = 0;
            pat = slash + 1;
        } else if (colon) {
            pat = oi_strdup(colon + 1);
            colon[1] = 0;
            d = gs_opendir(dir);
            goto have_dir;
        } else {
            pat = oi_strdup(dir);
            dir[0] = 0;
            d = gs_opendir("");
            goto have_dir;
        }
        pat = oi_strdup(pat);
        d = gs_opendir(dir);
have_dir:
        if (d) {
            const char *name;
            int type;
            char **list = NULL;
            int *types = NULL, n = 0, i;
            while ((name = gs_readdir(d, &type)) != NULL) {
                if (gs_patmatch(pat, name)) {
                    char **nl = realloc(list, (size_t)(n + 1) * sizeof *nl);
                    int *nt = realloc(types, (size_t)(n + 1) * sizeof *nt);
                    if (!nl || !nt) {
                        free(nl ? nl : list);
                        free(nt ? nt : types);
                        list = NULL;
                        types = NULL;
                        n = 0;
                        break;
                    }
                    list = nl;
                    types = nt;
                    list[n] = oi_tackon(dir, name);
                    types[n++] = type;
                }
            }
            gs_closedir(d);
            for (i = 0; i < n; i++) {
                if (g->unwind == GU_NONE && list[i]) {
                    if (types[i] == GS_DIR && a->p[GP_ALL].set)
                        delete_tree(g, a, c, list[i]);
                    else
                        delete_one(g, a, c, list[i]);
                }
                free(list[i]);
            }
            free(list);
            free(types);
        }
        free(pat);
        free(dir);
        r = g->unwind == GU_NONE;
        goto out;
    }
    if (gs_exists(path) == GS_NONE) {
        r = 1;      /* nothing to delete */
        goto out;
    }
    if (gs_exists(path) == GS_DIR && a->p[GP_ALL].set)
        r = delete_tree(g, a, c, path) == 0;
    else
        r = delete_one(g, a, c, path) == 0;
    if (r && a->p[GP_INFOS].set) {
        char *info = oi_xprintf("%s.info", path);
        if (info && gs_exists(info) == GS_FILE)
            delete_one(g, a, c, info);
        free(info);
    }
out:
    free(path);
    return oi_num(r);
}

FN(f_rename)
{
    char *from = oi_argstr(g, a, 0), *to = oi_argstr(g, a, 1);
    int r = 0;
    oi_ensure_welcome(g);
    if (!from || !to || g->unwind != GU_NONE)
        goto out;
    if (!confirmed(g, a, "Rename this?"))
        goto out;
    if (a->p[GP_DISK].set) {
        oi_log(g, "Rename the disk \"%s\" to \"%s\"", from, to);
        if (may_change(g, a)) {
            char *cmd = oi_xprintf("Relabel DRIVE \"%s\" NAME \"%s\"", from, to);
            r = cmd && gs_run(cmd, NULL, 1) == 0;
            free(cmd);
        } else {
            r = 1;
        }
        goto out;
    }
    oi_log(g, "Rename \"%s\" to \"%s\"", from, to);
    if (!may_change(g, a)) {
        r = 1;
        goto out;
    }
    if (gs_rename(from, to))
        dos_failed(g, a, c, "Can't rename \"%s\" to \"%s\"", from, to);
    else
        r = 1;
out:
    free(from);
    free(to);
    return oi_num(r);
}

/* Protection bits from a string: "+r -w", "rwed", "-d". In the Amiga's
 * bits, r w e d (bits 3 to 0) are set to forbid; h s p a are set to have. */
static long protect_from_string(const char *s, long cur)
{
    static const char letters[] = "dewrapsh";
    long bits = cur;
    int mode = 0;   /* 0 absolute, 1 add, -1 take away */
    int absolute_started = 0;
    for (; *s; s++) {
        const char *l;
        long b;
        if (*s == '+') { mode = 1; continue; }
        if (*s == '-') { mode = -1; continue; }
        if (*s == ' ' || *s == ',') continue;
        l = strchr(letters, (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s);
        if (!l)
            continue;
        b = 1L << (l - letters);
        if (mode == 0 && !absolute_started) {
            bits = 0x0f;        /* r w e d all forbidden, the rest off */
            absolute_started = 1;
        }
        if (b <= 8) {
            /* r w e d: inverted */
            if (mode >= 0) bits &= ~b; else bits |= b;
        } else {
            if (mode >= 0) bits |= b; else bits &= ~b;
        }
    }
    return bits;
}

FN(f_protect)
{
    char *path = oi_argstr(g, a, 0);
    long cur, bits;
    int r = 0;
    if (!path)
        return oi_num(0);
    cur = gs_get_protect(path);
    if (a->n < 2) {
        /* (protect file): its bits, -1 if it isn't there */
        free(path);
        return oi_num(cur);
    }
    oi_ensure_welcome(g);
    {
        oi_val v = oi_arg(g, a, 1);
        bits = v.type == GV_NUM ? v.num : protect_from_string(v.str, cur < 0 ? 0 : cur);
        oi_free(&v);
    }
    if (g->unwind == GU_NONE && confirmed(g, a, "Change the protection bits?")) {
        oi_log(g, "Protect \"%s\" with $%lx", path, (unsigned long)bits);
        if (!may_change(g, a))
            r = 1;
        else if (gs_set_protect(path, bits))
            dos_failed(g, a, c, "Can't change the protection bits of \"%s\"%s", path, NULL);
        else
            r = 1;
    }
    free(path);
    return oi_num(r);
}

/* ---------------------------------------------------------------- startup, textfile */

FN(f_startup)
{
    char *app = oi_argstr(g, a, 0), *cmd = oi_param_str(a, GP_COMMAND);
    const char *us = oi_get_str(g, "@user-startup");
    char *old = NULL, *begin = NULL, *end = NULL, *out = NULL;
    size_t len = 0;
    int r = 0;
    oi_ensure_welcome(g);
    if (!app || g->unwind != GU_NONE)
        goto done;
    if (!*app && g->app_name)
        app = (free(app), oi_strdup(g->app_name));
    if (!confirmed(g, a, "Change S:User-Startup?"))
        goto done;
    oi_log(g, "Add to \"%s\" for %s: %s", us, app, cmd ? cmd : "");
    if (!may_change(g, a)) {
        r = 1;
        goto done;
    }
    old = gs_read_file(us, &len);
    begin = oi_xprintf(";BEGIN %s", app);
    end = oi_xprintf(";END %s", app);
    if (!begin || !end)
        goto done;
    {
        /* the program's lines between ;BEGIN and ;END are replaced, or
         * added at the end */
        const char *s = old ? old : "";
        const char *b = NULL, *e = NULL, *p;
        size_t blen = strlen(begin), elen = strlen(end);
        for (p = s; *p; p++) {
            if ((p == s || p[-1] == '\n') && !b && !strncmp(p, begin, blen) &&
                (p[blen] == '\n' || p[blen] == '\r' || !p[blen]))
                b = p;
            if (b && (p == s || p[-1] == '\n') && !strncmp(p, end, elen) &&
                (p[elen] == '\n' || p[elen] == '\r' || !p[elen])) {
                e = p + elen;
                if (*e == '\r') e++;
                if (*e == '\n') e++;
                break;
            }
        }
        if (b && e) {
            out = oi_xprintf("%.*s%s\n%s%s%s\n%s", (int)(b - s), s, begin, cmd ? cmd : "",
                             cmd && *cmd && cmd[strlen(cmd) - 1] != '\n' ? "\n" : "", end, e);
        } else {
            size_t sl = strlen(s);
            out = oi_xprintf("%s%s%s\n%s%s%s\n", s, sl && s[sl - 1] != '\n' ? "\n" : "",
                             begin, cmd ? cmd : "",
                             cmd && *cmd && cmd[strlen(cmd) - 1] != '\n' ? "\n" : "", end);
        }
    }
    if (!out || gs_write_file(us, out, strlen(out), 0))
        dos_failed(g, a, c, "Can't write \"%s\"%s", us, NULL);
    else
        r = 1;
done:
    free(app);
    free(cmd);
    free(old);
    free(begin);
    free(end);
    free(out);
    return oi_num(r);
}

FN(f_textfile)
{
    char *dest = oi_param_str(a, GP_DEST);
    char *buf = NULL;
    size_t len = 0;
    int r = 0, i;
    if (!dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (textfile) wants (dest)", c->line);
        return oi_num(0);
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !confirmed(g, a, "Make a text file?"))
        goto out;
    buf = oi_strdup("");
    /* (include file ...) then (append text ...), in that order */
    for (i = 0; i < a->p[GP_INCLUDE].n && buf; i++) {
        char *f = oi_tostr(&a->p[GP_INCLUDE].v[i]);
        size_t fl = 0;
        char *t = f ? gs_read_file(f, &fl) : NULL;
        if (!t) {
            dos_failed(g, a, c, "Can't read \"%s\"%s", f ? f : "", NULL);
        } else {
            char *nb = realloc(buf, len + fl + 1);
            if (nb) {
                memcpy(nb + len, t, fl);
                len += fl;
                nb[len] = 0;
                buf = nb;
            }
        }
        free(t);
        free(f);
    }
    if (a->p[GP_APPEND].set && buf) {
        char *s = oi_param_str(a, GP_APPEND);
        size_t sl = s ? strlen(s) : 0;
        char *nb = realloc(buf, len + sl + 1);
        if (nb) {
            memcpy(nb + len, s ? s : "", sl);
            len += sl;
            nb[len] = 0;
            buf = nb;
        }
        free(s);
    }
    oi_log(g, "Make the text file \"%s\"", dest);
    if (!may_change(g, a)) {
        r = 1;
        goto out;
    }
    if (g->unwind == GU_NONE) {
        if (gs_write_file(dest, buf ? buf : "", len, 0))
            dos_failed(g, a, c, "Can't write \"%s\"%s", dest, NULL);
        else
            r = 1;
    }
out:
    free(dest);
    free(buf);
    return oi_num(r);
}

FN(f_tooltype)
{
    char *dest = oi_param_str(a, GP_DEST);
    int r = 1, i;
    char *icon;
    if (!dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (tooltype) wants (dest)", c->line);
        return oi_num(0);
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE || !confirmed(g, a, "Change the icon?")) {
        free(dest);
        return oi_num(0);
    }
    /* the icon itself: the name with .info taken off */
    icon = oi_strdup(dest);
    if (icon) {
        size_t n = strlen(icon);
        if (n > 5 && !oi_stricmp(icon + n - 5, ".info"))
            icon[n - 5] = 0;
    }
    oi_log(g, "Change the icon of \"%s\"", icon ? icon : dest);
    if (may_change(g, a) && icon) {
        oi_param *p = &a->p[GP_SETTOOLTYPE];
        if (p->n > 0) {
            char *name = oi_tostr(&p->v[0]);
            char *val = p->n > 1 ? oi_tostr(&p->v[1]) : NULL;
            /* with no value the tool type goes; "" gives it with no '=' */
            if (gs_icon_tooltype(icon, name, val, p->n < 2))
                r = 0;
            free(name);
            free(val);
        }
        if (a->p[GP_SETDEFAULTTOOL].n > 0) {
            char *t = oi_tostr(&a->p[GP_SETDEFAULTTOOL].v[0]);
            if (gs_icon_default_tool(icon, t))
                r = 0;
            free(t);
        }
        if (a->p[GP_SETSTACK].n > 0 && gs_icon_stack(icon, oi_tonum(&a->p[GP_SETSTACK].v[0])))
            r = 0;
        if (a->p[GP_NOPOSITION].set && gs_icon_noposition(icon))
            r = 0;
        if (a->p[GP_SWAPCOLORS].set && gs_icon_swapcolors(icon))
            r = 0;
        if (!r)
            dos_failed(g, a, c, "Can't change the icon \"%s\"%s", icon, NULL);
    }
    (void)i;
    free(icon);
    free(dest);
    return oi_num(r);
}

/* ---------------------------------------------------------------- programs */

static oi_val run_cmd(oi *g, oi_node *c, oi_args *a, int kind)
{
    char *args = oi_cat_args(g, a, 0), *cmd = NULL;
    long rc = 0;
    oi_ensure_welcome(g);
    if (!args || g->unwind != GU_NONE)
        goto out;
    if (!confirmed(g, a, kind == 0 ? "Run a program?" : kind == 1 ? "Run a script?" : "Run an ARexx script?"))
        goto out;
    if (kind == 0) {
        /* (run): args joined with a space between them */
        int i;
        free(args);
        args = oi_strdup("");
        for (i = 0; i < a->n && args; i++) {
            char *s = oi_argstr(g, a, i), *n;
            n = oi_xprintf("%s%s%s", args, i ? " " : "", s ? s : "");
            free(s);
            free(args);
            args = n;
        }
        cmd = oi_strdup(args ? args : "");
    } else if (kind == 1) {
        cmd = oi_xprintf("Execute %s", args);
    } else {
        cmd = oi_xprintf("SYS:Rexxc/RX %s", args);
    }
    oi_log(g, "%s \"%s\"", kind == 0 ? "Run" : kind == 1 ? "Execute" : "Run the ARexx script", args);
    if (!cmd || !may_change(g, a))
        goto out;
    rc = gs_run(cmd, oi_get_str(g, "@execute-dir"), 1);
    oi_set_num(g, "@ioerr", rc);
    if (rc < 0)
        dos_failed(g, a, c, "Can't run \"%s\"%s", cmd, NULL);
out:
    free(args);
    free(cmd);
    return oi_num(rc);
}

FN(f_run)     { return run_cmd(g, c, a, 0); }
FN(f_execute) { return run_cmd(g, c, a, 1); }
FN(f_rexx)    { return run_cmd(g, c, a, 2); }

FN(f_makeassign)
{
    char *name = oi_argstr(g, a, 0), *path = a->n > 1 ? oi_argstr(g, a, 1) : NULL;
    int r = 0;
    (void)c;
    oi_ensure_welcome(g);
    if (!name || g->unwind != GU_NONE)
        goto out;
    oi_log(g, path ? "Assign %s: to \"%s\"" : "Remove the assign %s:%s", name, path ? path : "");
    if (!may_change(g, a)) {
        r = 1;
        goto out;
    }
    r = gs_makeassign(name, path, 0) == 0;
out:
    free(name);
    free(path);
    return oi_num(r);
}

FN(f_reboot)
{
    (void)c;
    (void)a;
    oi_ensure_welcome(g);
    oi_log(g, "Restart the Amiga");
    if (g->unwind != GU_NONE || oi_pretend(g))
        return oi_num(0);
    if (g->ui && g->ui->ops->confirm(g->ui, "The Amiga will now restart.", "") != OI_UI_OK)
        return oi_num(0);
    gs_run(NULL, NULL, -1);     /* -1: ColdReboot() */
    return oi_num(1);
}

const oi_fn oi_fn_file[] = {
    { "copyfiles", f_copyfiles, 0, 0 },
    { "copylib", f_copylib, 0, 0 },
    { "makedir", f_makedir, 1, 0 },
    { "delete", f_delete, 1, 0 },
    { "rename", f_rename, 2, 0 },
    { "protect", f_protect, 1, 0 },
    { "startup", f_startup, 0, 0 },
    { "textfile", f_textfile, 0, 0 },
    { "tooltype", f_tooltype, 0, 0 },
    { "run", f_run, 1, 0 },
    { "execute", f_execute, 1, 0 },
    { "rexx", f_rexx, 1, 0 },
    { "makeassign", f_makeassign, 1, 0 },
    { "reboot", f_reboot, 0, 0 },
    { NULL, NULL, 0, 0 }
};
