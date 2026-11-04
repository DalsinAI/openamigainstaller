/*
 * OpenInstaller: an Installer for AmigaOS 3.x that runs the Installer
 * script language of the OS 3.2 Installer (47.19), plus a few commands of
 * its own: downloads through libcurl and a picture panel beside the
 * window. This header is the interpreter's core: values, the parsed
 * script, the interpreter's state and the calls between its parts.
 *
 * Integers only (no floating point anywhere), so it builds soft float for
 * the 68040 with or without an FPU.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#ifndef OI_H
#define OI_H

#include <stddef.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA)
#define OI_AMIGA 1
#endif

#define OI_VERSION      1
#define OI_REVISION     0
#define OI_VERSTR       "1.0"
#define OI_DATE         "4.11.2026"
/* What (database "installer-version") and @installer-version report: the
 * Installer we follow, 47.19 (OS 3.2). */
#define OI_COMPAT_VER   47
#define OI_COMPAT_REV   19

/* ---- values ---- */

enum { GV_NUM, GV_STR };

typedef struct oi_val {
    int type;
    long num;
    char *str;          /* GV_STR: owned, NUL ended */
} oi_val;

/* ---- the parsed script ---- */

enum { GN_NUM, GN_STR, GN_SYM, GN_LIST };

typedef struct oi_node {
    int type;
    int line;
    long num;
    char *text;                 /* GN_STR, GN_SYM */
    struct oi_node **kid;       /* GN_LIST */
    int nkid;
    const struct oi_fn *fn;     /* GN_LIST with a built-in at its head */
} oi_node;

/* ---- user levels, error codes ---- */

#define OI_NOVICE   0
#define OI_AVERAGE  1
#define OI_EXPERT   2

/* The Installer's error numbers (trap's flags are 1 << (n - 1)). */
#define OI_ERR_ABORT    1
#define OI_ERR_NOMEM    2
#define OI_ERR_SCRIPT   3
#define OI_ERR_DOS      4
#define OI_ERR_BADPARAM 5

/* How evaluation is unwinding. */
enum { GU_NONE, GU_EXIT, GU_ABORT, GU_ERROR, GU_BACK };

/* ---- variables ---- */

typedef struct oi_var {
    struct oi_var *next;
    char *name;
    oi_val val;
} oi_var;

typedef struct oi_proc {
    struct oi_proc *next;
    char *name;
    oi_node *def;       /* the (procedure ...) list */
    int first_arg, nargs, first_body;
} oi_proc;

#define OI_HASH 64

struct oi_ui;

typedef struct oi {
    oi_var *vars[OI_HASH];
    oi_proc *procs;
    oi_node *script;
    const char *script_name;

    int unwind;             /* GU_* */
    int err_code;           /* OI_ERR_* of the error unwinding */
    char err_msg[512];
    int exit_quiet;
    char exit_msg[1024];
    int err_line;

    oi_node *onerror;       /* (onerror ...) */
    int trap_depth;

    int welcomed;           /* the welcome pages have been shown */
    int complete;           /* (complete n) */
    int depth;              /* recursion guard */
    int trace_on;

    /* the log */
    void *log;              /* sys file handle or NULL */
    int log_mode;           /* 0 none, 1 printer, 2 file */
    char log_name[256];

    struct oi_ui *ui;
    void *panel;            /* the side panel (oi_panel.h) */
    void *net;

    /* settings from the command line or tool types */
    int no_pretend, no_log, min_user, def_user;
    const char *app_name;
    const char *language;
    int headless;           /* tests: no window at all */
    int start_pretend;      /* PRETEND: the welcome's choice starts at pretend */
    char icon[512];         /* @icon */
} oi;

/* ---- built-in functions ---- */

/* Parameter flags, collected from (prompt ...), (help ...) and the like
 * in a call's arguments. */
enum {
    GP_PROMPT, GP_HELP, GP_CONFIRM, GP_SAFE, GP_DEFAULT, GP_CHOICES,
    GP_RANGE, GP_SOURCE, GP_DEST, GP_NEWNAME, GP_PATTERN, GP_ALL, GP_FILES,
    GP_INFOS, GP_FONTS, GP_NOPOSITION, GP_OPTIONAL, GP_DELOPTS, GP_NOGAUGE,
    GP_DISK, GP_ASSIGNS, GP_NEWPATH, GP_RESIDENT, GP_INCLUDE, GP_APPEND,
    GP_COMMAND, GP_SETTOOLTYPE, GP_SETDEFAULTTOOL, GP_SETSTACK,
    GP_SWAPCOLORS, GP_QUIET, GP_BACK, GP_NOREQ, GP_GETTOOLTYPE,
    GP_GETDEFAULTTOOL, GP_GETSTACK, GP_GETPOSITION, GP_SETPOSITION,
    GP_DEFAULTS, GP_FORCE, GP_ASKUSER, GP_FAIL, GP_NOFAIL, GP_OKNODELETE,
    GP_OVERRIDE, GP_LOG, GP_CHECKSUM, GP_SIZE, GP_IFMISSING, GP_UNPACK,
    GP_DELAY, GP_CAPTION, GP_FIT,
    GP_COUNT
};

#define OI_MAXPARAM 16

typedef struct oi_param {
    int set;
    int n;
    oi_val v[OI_MAXPARAM];
    oi_node *node;      /* the (param ...) list itself, for (back ...) */
} oi_param;

typedef struct oi_args {
    int n;
    oi_node **node;     /* positional arguments, not yet evaluated */
    oi_param p[GP_COUNT];
} oi_args;

typedef oi_val (*oi_fnptr)(oi *g, oi_node *call, oi_args *a);

#define GF_SPECIAL  1   /* gets its arguments unevaluated (if, while ...) */
#define GF_NOPARAM  2   /* its arguments are never parameters */

typedef struct oi_fn {
    const char *name;
    oi_fnptr fn;
    int min_args;       /* positional, after parameters are taken out */
    int flags;
} oi_fn;

/* ---- core calls (oi_eval.c) ---- */

oi_val oi_num(long n);
oi_val oi_str(const char *s);
oi_val oi_strn(const char *s, size_t n);
oi_val oi_str_own(char *s);
void oi_free(oi_val *v);
oi_val oi_copy(const oi_val *v);
long oi_tonum(const oi_val *v);
char *oi_tostr(const oi_val *v);      /* malloc'd */
int oi_true(const oi_val *v);

oi_val oi_eval(oi *g, oi_node *n);
oi_val oi_eval_seq(oi *g, oi_node **n, int count);
oi_val oi_arg(oi *g, oi_args *a, int i);
long oi_argnum(oi *g, oi_args *a, int i);
char *oi_argstr(oi *g, oi_args *a, int i);  /* malloc'd */
char *oi_cat_args(oi *g, oi_args *a, int from);    /* all of them, malloc'd */
char *oi_param_str(oi_args *a, int p);  /* the parameter's values joined, malloc'd, NULL if absent */
long oi_param_num(oi_args *a, int p, int i, long dflt);

oi_var *oi_lookup(oi *g, const char *name);
void oi_set(oi *g, const char *name, oi_val v);        /* takes v */
void oi_set_num(oi *g, const char *name, long n);
void oi_set_str(oi *g, const char *name, const char *s);
long oi_get_num(oi *g, const char *name);
const char *oi_get_str(oi *g, const char *name);   /* "" if unset or a number */

void oi_error(oi *g, int code, const char *fmt, ...);
void oi_abort(oi *g, const char *msg);

oi *oi_new(void);
void oi_delete(oi *g);
int oi_load(oi *g, const char *text, size_t len, const char *name, char *err, size_t errlen);
int oi_run(oi *g);              /* 0 done, 5 aborted, 10 error */
int oi_run_with(oi *g, const char *icon, int pretend);

/* ---- the parser (oi_parse.c) ---- */

oi_node *oi_parse(const char *text, size_t len, char *err, size_t errlen);
void oi_node_free(oi_node *n);

/* ---- built-ins (oi_fn_*.c) ---- */

const oi_fn *oi_find_fn(const char *name);
int oi_param_id(const char *name);
extern const oi_fn oi_fn_core[], oi_fn_file[], oi_fn_ask[], oi_fn_extra[];

/* ---- helpers ---- */

char *oi_strdup(const char *s);
char *oi_xprintf(const char *fmt, ...);
int oi_stricmp(const char *a, const char *b);
void oi_log(oi *g, const char *fmt, ...);
int oi_confirm_needed(oi *g, oi_args *a);
int oi_pretend(oi *g);
void oi_ensure_welcome(oi *g);
void oi_welcome_page(oi *g, const char *extra);
char *oi_tackon(const char *path, const char *file);
char *oi_format(oi *g, const char *fmt, oi_args *a, int from);

#endif
