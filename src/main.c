/*
 * OpenInstaller's start: the arguments (the Shell's, or a Workbench
 * icon's tool types, as the Installer takes them), the script, a front
 * end, and the run.
 *
 * Shell:     OpenInstaller SCRIPT/A,APPNAME/K,MINUSER/K,DEFUSER/K,
 *                          LOGFILE/K,LANGUAGE/K,NOPRETEND/S,NOLOG/S,
 *                          PRETEND/S,HEADLESS/S
 * Workbench: an icon whose default tool is OpenInstaller (or Installer,
 *            when OpenInstaller is put in its place), with the same names
 *            as tool types; without SCRIPT the icon's own file is the
 *            script.
 *
 * The window is gfopenlayout.library 11's; without that library it is
 * GadTools'. HEADLESS (or the host build) has no window: ui_text.c.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_net.h"
#include "oi_sys.h"
#include "oi_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OI_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

/* Stack: copying and TLS want room (gfopencurl asks 64 KB). */
unsigned long __stack = 65536;
static const char version[] __attribute__((used)) =
    "$VER: OpenInstaller " OI_VERSTR " (" OI_DATE ")";
extern struct WBStartup *_WBenchMsg;
struct Library *IconBase;
#endif

typedef struct opts {
    char script[512];
    char appname[128];
    char logfile[256];
    char language[64];
    char icon[512];     /* the host's tests: what @icon is */
    int minuser, defuser, nopretend, nolog, pretend, headless;
} opts;

static int level_of(const char *s, int dflt)
{
    if (!s || !*s)
        return dflt;
    if (!oi_stricmp(s, "NOVICE") || !strcmp(s, "0"))
        return OI_NOVICE;
    if (!oi_stricmp(s, "AVERAGE") || !oi_stricmp(s, "INTERMEDIATE") || !strcmp(s, "1"))
        return OI_AVERAGE;
    if (!oi_stricmp(s, "EXPERT") || !strcmp(s, "2"))
        return OI_EXPERT;
    return dflt;
}

/* NAME=value (or a switch NAME alone), as a tool type or on the host's
 * command line. */
static void take(opts *o, const char *arg)
{
    const char *eq = strchr(arg, '=');
    char key[32];
    const char *val = eq ? eq + 1 : "";
    size_t n = eq ? (size_t)(eq - arg) : strlen(arg);
    if (n >= sizeof key)
        return;
    memcpy(key, arg, n);
    key[n] = 0;
    if (!oi_stricmp(key, "SCRIPT"))
        snprintf(o->script, sizeof o->script, "%s", val);
    else if (!oi_stricmp(key, "APPNAME"))
        snprintf(o->appname, sizeof o->appname, "%s", val);
    else if (!oi_stricmp(key, "LOGFILE"))
        snprintf(o->logfile, sizeof o->logfile, "%s", val);
    else if (!oi_stricmp(key, "LANGUAGE"))
        snprintf(o->language, sizeof o->language, "%s", val);
    else if (!oi_stricmp(key, "MINUSER"))
        o->minuser = level_of(val, OI_NOVICE);
    else if (!oi_stricmp(key, "DEFUSER"))
        o->defuser = level_of(val, OI_AVERAGE);
    else if (!oi_stricmp(key, "NOPRETEND"))
        o->nopretend = 1;
    else if (!oi_stricmp(key, "NOLOG"))
        o->nolog = 1;
    else if (!oi_stricmp(key, "PRETEND"))
        o->pretend = 1;
    else if (!oi_stricmp(key, "ICON"))
        snprintf(o->icon, sizeof o->icon, "%s", val);
    else if (!oi_stricmp(key, "HEADLESS"))
        o->headless = 1;
    else if (!eq && !o->script[0])
        snprintf(o->script, sizeof o->script, "%s", arg);
}

#ifdef OI_AMIGA
static int amiga_args(opts *o, int argc, char **argv, char *icon, size_t iconlen)
{
    (void)argc;
    (void)argv;
    if (_WBenchMsg) {
        struct WBArg *wa = _WBenchMsg->sm_ArgList;
        int i = _WBenchMsg->sm_NumArgs > 1 ? 1 : 0;
        char name[512];
        struct DiskObject *dob;
        BPTR old;
        if (!wa[i].wa_Lock)
            return -1;
        old = CurrentDir(wa[i].wa_Lock);
        snprintf(name, sizeof name, "%s", (const char *)wa[i].wa_Name);
        IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 37);
        dob = IconBase ? GetDiskObject((STRPTR)name) : NULL;
        if (dob) {
            STRPTR *tt;
            for (tt = (STRPTR *)dob->do_ToolTypes; tt && *tt; tt++)
                take(o, (const char *)*tt);
            FreeDiskObject(dob);
        }
        /* the script's drawer is the current one, as with the Installer */
        if (!o->script[0])
            snprintf(o->script, sizeof o->script, "%s", name);
        if (NameFromLock(wa[i].wa_Lock, (STRPTR)icon, (LONG)iconlen))
            AddPart((STRPTR)icon, (STRPTR)name, (ULONG)iconlen);
        (void)old;
        return 0;
    } else {
        static const char tmpl[] =
            "SCRIPT/A,APPNAME/K,MINUSER/K,DEFUSER/K,LOGFILE/K,LANGUAGE/K,NOPRETEND/S,NOLOG/S,PRETEND/S,HEADLESS/S";
        LONG a[10];
        struct RDArgs *rd;
        memset(a, 0, sizeof a);
        rd = ReadArgs((CONST_STRPTR)tmpl, a, NULL);
        if (!rd) {
            PrintFault(IoErr(), (CONST_STRPTR)"OpenInstaller");
            return -1;
        }
        if (a[0]) snprintf(o->script, sizeof o->script, "%s", (const char *)a[0]);
        if (a[1]) snprintf(o->appname, sizeof o->appname, "%s", (const char *)a[1]);
        if (a[2]) o->minuser = level_of((const char *)a[2], OI_NOVICE);
        if (a[3]) o->defuser = level_of((const char *)a[3], OI_AVERAGE);
        if (a[4]) snprintf(o->logfile, sizeof o->logfile, "%s", (const char *)a[4]);
        if (a[5]) snprintf(o->language, sizeof o->language, "%s", (const char *)a[5]);
        o->nopretend = a[6] != 0;
        o->nolog = a[7] != 0;
        o->pretend = a[8] != 0;
        o->headless = a[9] != 0;
        FreeArgs(rd);
        IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 37);
        /* from the Shell @icon is the script, in full, so a script finds
         * the files beside it as it does from its icon */
        icon[0] = 0;
        {
            BPTR l = Lock((CONST_STRPTR)o->script, ACCESS_READ);
            if (l) {
                if (!NameFromLock(l, (STRPTR)icon, (LONG)iconlen))
                    icon[0] = 0;
                UnLock(l);
            }
        }
        return 0;
    }
}
#endif

int main(int argc, char **argv)
{
    opts o;
    oi *g;
    char *text;
    size_t len = 0;
    char err[256];
    char icon[512];
    int rc;
    memset(&o, 0, sizeof o);
    o.defuser = OI_AVERAGE;
    icon[0] = 0;
#ifdef OI_AMIGA
    if (amiga_args(&o, argc, argv, icon, sizeof icon))
        return 20;
#else
    if (argc == 2 && !strcmp(argv[1], "--has-curl")) {
#ifdef OI_NO_CURL
        puts("no");
#else
        puts("yes");
#endif
        return 0;
    }
    {
        int i;
        for (i = 1; i < argc; i++)
            take(&o, argv[i]);
        o.headless = 1;     /* the host has no window */
        snprintf(icon, sizeof icon, "%s", o.icon);
    }
#endif
    if (!o.script[0]) {
        fprintf(stderr, "OpenInstaller " OI_VERSTR ": no script given\n");
        return 20;
    }
    gs_init();
    text = gs_read_file(o.script, &len);
    if (!text) {
        fprintf(stderr, "OpenInstaller: can't read the script \"%s\"\n", o.script);
        gs_cleanup();
        return 20;
    }
    g = oi_new();
    if (!g) {
        free(text);
        gs_cleanup();
        return 20;
    }
    g->app_name = o.appname[0] ? o.appname : NULL;
    g->language = o.language[0] ? o.language : NULL;
    g->min_user = o.minuser;
    g->def_user = o.defuser < o.minuser ? o.minuser : o.defuser;
    g->no_pretend = o.nopretend;
    g->no_log = o.nolog;
    g->headless = o.headless;
    if (o.logfile[0])
        snprintf(g->log_name, sizeof g->log_name, "%s", o.logfile);
    if (oi_load(g, text, len, o.script, err, sizeof err)) {
        g->ui = o.headless ? oi_ui_text_open(g) : NULL;
#ifdef OI_AMIGA
        if (!g->ui)
            g->ui = oi_ui_olui_open(g);
        if (!g->ui)
            g->ui = oi_ui_gadtools_open(g);
#endif
        if (g->ui) {
            char *m = oi_xprintf("The script \"%s\" has a mistake in it.\n\n%s", o.script, err);
            g->ui->ops->problem(g->ui, m ? m : err, 0);
            free(m);
            g->ui->ops->close(g->ui);
        } else {
            fprintf(stderr, "OpenInstaller: %s: %s\n", o.script, err);
        }
        free(text);
        oi_delete(g);
        gs_cleanup();
        return 20;
    }
    free(text);
    g->ui = o.headless ? oi_ui_text_open(g) : NULL;
#ifdef OI_AMIGA
    if (!g->ui)
        g->ui = oi_ui_olui_open(g);
    if (!g->ui)
        g->ui = oi_ui_gadtools_open(g);
#endif
    if (!g->ui) {
        fprintf(stderr, "OpenInstaller: can't open a window\n");
        oi_delete(g);
        gs_cleanup();
        return 20;
    }
    rc = oi_run_with(g, icon, o.pretend);
    g->ui->ops->close(g->ui);
    g->ui = NULL;
    oi_delete(g);
    gn_cleanup();
    gs_cleanup();
#ifdef OI_AMIGA
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
#endif
    return rc;
}
