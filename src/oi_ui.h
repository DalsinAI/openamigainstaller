/*
 * What the interpreter asks of a front end. There are three:
 *   ui_olui.c      the window, on gfopenlayout.library 11 (HTML and CSS)
 *   ui_gadtools.c  the same pages on GadTools, when that library is missing
 *   ui_text.c      no window: the tests' front end, its answers read from
 *                  a file, and what it would show printed
 * main.c picks one. Each page shows the side panel (oi_panel.h) at a
 * quarter of the window's width: the script's picture, or the logo.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#ifndef OI_UI_H
#define OI_UI_H

#include "oi.h"

/* What a page returns besides its answer. */
#define OI_UI_OK      1
#define OI_UI_NO      0
#define OI_UI_ABORT  -1
#define OI_UI_BACK   -2
#define OI_UI_SKIP   -3

typedef struct oi_ask {
    const char *prompt;
    const char *help;
    int can_back;               /* the script gave (back ...) */
    /* askbool */
    const char *yes, *no;
    /* askchoice, askoptions */
    const char **choices;
    int nchoices;
    /* asknumber */
    long min, max;
    /* defaults and answers */
    long num;                   /* askbool/askchoice/asknumber, askoptions' mask */
    const char *dflt;           /* askstring/askdir/askfile */
    char *answer;               /* askstring/askdir/askfile: malloc'd by the front end */
    int newpath, disk, assigns;
} oi_ask;

typedef struct oi_ui_ops {
    const char *name;
    /* The first pages: the user level, install or pretend, and the log.
     * level, pretend and log hold the defaults and get the answers;
     * min_level is the lowest the user may choose. */
    int (*welcome)(struct oi_ui *u, const char *text, int *level, int min_level,
                   int *pretend, int no_pretend, int *log, int no_log);
    int (*message)(struct oi_ui *u, const char *text, int can_back);
    int (*askbool)(struct oi_ui *u, oi_ask *q);
    int (*askchoice)(struct oi_ui *u, oi_ask *q);
    int (*askoptions)(struct oi_ui *u, oi_ask *q);
    int (*asknumber)(struct oi_ui *u, oi_ask *q);
    int (*askstring)(struct oi_ui *u, oi_ask *q);
    int (*askdir)(struct oi_ui *u, oi_ask *q);
    int (*askfile)(struct oi_ui *u, oi_ask *q);
    /* askdisk: OI_UI_OK when the disk is in, the front end checks */
    int (*askdisk)(struct oi_ui *u, oi_ask *q, const char *volume);
    /* (confirm): "Proceed", "Skip this part", "Abort" */
    int (*confirm)(struct oi_ui *u, const char *prompt, const char *help);
    /* "Working on installation..." and the text under it */
    void (*working)(struct oi_ui *u, const char *text);
    /* the bar of (complete n) */
    void (*complete)(struct oi_ui *u, int percent);
    /* a file being copied or downloaded: OI_UI_ABORT when the user stops it */
    int (*gauge)(struct oi_ui *u, const char *what, long long now, long long total);
    /* an error or problem: shown, then the answer (OK, or Retry/Abort when
     * retry is set: OI_UI_OK retries) */
    int (*problem)(struct oi_ui *u, const char *text, int retry);
    /* the last page: "Installation complete", or the abort's text */
    void (*final)(struct oi_ui *u, const char *text, int aborted);
    /* the side panel's picture changed (oi_panel.h has it) */
    void (*panel_changed)(struct oi_ui *u);
    /* events now and then (animations, the Abort button) */
    int (*poll)(struct oi_ui *u);
    void (*close)(struct oi_ui *u);
} oi_ui_ops;

typedef struct oi_ui {
    const oi_ui_ops *ops;
    oi *g;
    void *priv;
} oi_ui;

oi_ui *oi_ui_text_open(oi *g);
oi_ui *oi_ui_olui_open(oi *g);       /* NULL without gfopenlayout.library 11 */
oi_ui *oi_ui_gadtools_open(oi *g);

#endif
