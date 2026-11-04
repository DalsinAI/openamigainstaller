/*
 * OpenInstaller's own commands, beyond the OS 3.2 Installer's:
 *
 *   (download (source "url") (dest "file or drawer") [(ifmissing)]
 *             [(checksum "sha256")] [(prompt ..)] [(help ..)] [(confirm)]
 *             [(optional "nofail")] [(safe)])
 *       Downloads a file through the built-in libcurl (HTTP and HTTPS).
 *       With (ifmissing) nothing is downloaded when the file is there
 *       already. Returns 1 when the file is there afterwards.
 *
 *   (netavailable)
 *       1 when there is a network to download from.
 *
 *   (panel "picture" [(caption "text")])
 *       Shows a picture in the panel a quarter of the window wide: IFF,
 *       PNG, GIF (animated ones play) or any picture a datatype reads.
 *       (panel "") shows the AmigaChrome logo again, as the panel does
 *       before a script names a picture.
 *
 * Scripts that must also run on Commodore's Installer can test for these
 * with (if @openinstaller ...): that variable is unset (0) there.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"
#include "oi_net.h"
#include "oi_panel.h"
#include "oi_sys.h"
#include "oi_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN(name) static oi_val name(oi *g, oi_node *c, oi_args *a)

static oi_panel the_panel;

oi_panel *oi_panel_get(oi *g)
{
    (void)g;
    return &the_panel;
}

typedef struct dl_ctx {
    oi *g;
    const char *name;
} dl_ctx;

static int dl_progress(void *ud, long long now, long long total)
{
    dl_ctx *k = ud;
    if (!k->g->ui)
        return 0;
    return k->g->ui->ops->gauge(k->g->ui, k->name, now, total) == OI_UI_ABORT;
}

/* The file part of an address: what follows its last /, without ? or #. */
static char *url_file(const char *url)
{
    const char *s = strstr(url, "://"), *p, *e;
    char *out;
    s = s ? s + 3 : url;
    p = strchr(s, '/');
    if (!p)
        return oi_strdup("index.html");
    e = p + strcspn(p, "?#");
    while (e > p && e[-1] == '/')
        e--;
    s = e;
    while (s > p && s[-1] != '/')
        s--;
    if (s == e)
        return oi_strdup("index.html");
    out = malloc((size_t)(e - s) + 1);
    if (out) {
        memcpy(out, s, (size_t)(e - s));
        out[e - s] = 0;
    }
    return out;
}

static int optional_nofail(oi_args *a)
{
    int i;
    if (a->p[GP_NOFAIL].set)
        return 1;
    for (i = 0; i < a->p[GP_OPTIONAL].n; i++)
        if (a->p[GP_OPTIONAL].v[i].type == GV_STR && !oi_stricmp(a->p[GP_OPTIONAL].v[i].str, "nofail"))
            return 1;
    return 0;
}

FN(f_download)
{
    char *url = oi_param_str(a, GP_SOURCE), *dest = oi_param_str(a, GP_DEST);
    char *path = NULL, *part = NULL, *sum = NULL;
    char err[256];
    int r = 0;
    if (!url && a->n > 0)
        url = oi_argstr(g, a, 0);
    if (!dest && a->n > 1)
        dest = oi_argstr(g, a, 1);
    if (!url || !dest || !*url || !*dest) {
        oi_error(g, OI_ERR_SCRIPT, "Line %d: (download) wants (source) and (dest)", c->line);
        goto out;
    }
    if (gs_exists(dest) == GS_DIR) {
        char *f = url_file(url);
        path = f ? oi_tackon(dest, f) : NULL;
        free(f);
    } else {
        path = oi_strdup(dest);
    }
    if (!path)
        goto out;
    sum = oi_param_str(a, GP_CHECKSUM);
    if (a->p[GP_IFMISSING].set && gs_exists(path) == GS_FILE) {
        char have[65];
        /* there already: kept, unless its checksum is given and differs */
        if (!sum || !*sum || (gn_sha256_file(path, have) == 0 && !oi_stricmp(have, sum))) {
            oi_log(g, "\"%s\" is there already", path);
            r = 1;
            goto out;
        }
    }
    oi_ensure_welcome(g);
    if (g->unwind != GU_NONE)
        goto out;
    if (oi_confirm_needed(g, a) && g->ui) {
        char *pr = oi_param_str(a, GP_PROMPT), *hp = oi_param_str(a, GP_HELP);
        char *q = (pr && *pr) ? NULL : oi_xprintf("Download %s?", url);
        int k = g->ui->ops->confirm(g->ui, (pr && *pr) ? pr : (q ? q : ""), hp ? hp : "");
        free(pr);
        free(hp);
        free(q);
        if (k == OI_UI_ABORT) {
            oi_abort(g, "");
            goto out;
        }
        if (k != OI_UI_OK)
            goto out;
    }
    oi_log(g, "Download \"%s\" to \"%s\"", url, path);
    if (oi_pretend(g) && !a->p[GP_SAFE].set) {
        r = 1;
        goto out;
    }
    if (g->ui) {
        char *w = oi_xprintf("Downloading %s...", url);
        g->ui->ops->working(g->ui, w ? w : "");
        free(w);
    }
    /* to NAME.part first, renamed when it is whole and checked */
    part = oi_xprintf("%s.part", path);
    if (!part)
        goto out;
    {
        dl_ctx k;
        int rc;
        k.g = g;
        k.name = url;
        rc = gn_download(url, part, "OpenInstaller/" OI_VERSTR, dl_progress, &k, err, sizeof err);
        if (rc == -2) {
            gs_delete(part);
            oi_abort(g, "");
            goto out;
        }
        if (rc) {
            gs_delete(part);
            oi_set_num(g, "@ioerr", 1);
            oi_log(g, "Problem: the download of \"%s\" failed: %s", url, err);
            if (!optional_nofail(a))
                oi_error(g, OI_ERR_DOS, "Line %d: The download of %s failed: %s", c->line, url, err);
            goto out;
        }
    }
    if (sum && *sum) {
        char have[65];
        if (gn_sha256_file(part, have) || oi_stricmp(have, sum)) {
            gs_delete(part);
            oi_set_num(g, "@ioerr", 1);
            oi_log(g, "Problem: \"%s\" is not the file expected (its checksum differs)", url);
            if (!optional_nofail(a))
                oi_error(g, OI_ERR_DOS, "Line %d: %s is not the file expected: its checksum differs",
                         c->line, url);
            goto out;
        }
    }
    if (gs_exists(path) == GS_FILE)
        gs_delete(path);
    if (gs_rename(part, path)) {
        gs_delete(part);
        oi_set_num(g, "@ioerr", 1);
        if (!optional_nofail(a))
            oi_error(g, OI_ERR_DOS, "Line %d: Can't write \"%s\"", c->line, path);
        goto out;
    }
    oi_set_num(g, "@ioerr", 0);
    r = 1;
out:
    free(url);
    free(dest);
    free(path);
    free(part);
    free(sum);
    return oi_num(r);
}

FN(f_netavailable)
{
    (void)g;
    (void)c;
    (void)a;
    return oi_num(gn_available());
}

FN(f_panel)
{
    oi_panel *p = oi_panel_get(g);
    char *file = a->n > 0 ? oi_argstr(g, a, 0) : oi_strdup("");
    char *cap = oi_param_str(a, GP_CAPTION);
    (void)c;
    if (!file) {
        free(cap);
        return oi_num(0);
    }
    if (*file && gs_exists(file) != GS_FILE) {
        oi_log(g, "Problem: the picture \"%s\" isn't there", file);
        free(file);
        free(cap);
        return oi_num(0);
    }
    snprintf(p->path, sizeof p->path, "%s", file);
    snprintf(p->caption, sizeof p->caption, "%s", cap ? cap : "");
    p->fit = 1;
    p->generation++;
    if (g->ui && g->ui->ops->panel_changed)
        g->ui->ops->panel_changed(g->ui);
    free(file);
    free(cap);
    return oi_num(1);
}

const oi_fn oi_fn_extra[] = {
    { "download", f_download, 0, 0 },
    { "netavailable", f_netavailable, 0, GF_NOPARAM },
    { "panel", f_panel, 0, 0 },
    { NULL, NULL, 0, 0 }
};
