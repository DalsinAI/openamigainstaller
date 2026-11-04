/*
 * The Installer's pages, as both window front ends show them. ui_pages.c
 * turns each oi_ui_ops call into a page (its text, its choices, its
 * buttons) and asks a back end to show it: ui_olui.c (gfopenlayout.library
 * 11) or ui_gadtools.c (GadTools, when that library is missing). Every
 * page has the picture panel down its left quarter.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#ifndef UI_PAGES_H
#define UI_PAGES_H

#include "oi_ui.h"

enum {
    PG_TEXT,        /* text and buttons */
    PG_RADIO,       /* one of items (value) */
    PG_CHECK,       /* any of items (value, a mask) */
    PG_STRING,      /* a field (str) */
    PG_NUMBER,      /* a number from min to max (value) */
    PG_PROGRESS     /* a bar (value 0-100, or -1 while the size is unknown) and a line under it (sub) */
};

#define PG_MAXBTN   5
#define PG_MAXITEMS 32

typedef struct oi_page {
    const char *title;      /* a heading over the text, or NULL */
    const char *text;
    int kind;
    const char *items[PG_MAXITEMS];
    int nitems;
    unsigned long disabled; /* items that can't be chosen (a mask) */
    long value, min, max;
    char str[512];
    int file_button;        /* PG_STRING: 1 a drawer requester, 2 a file requester */
    /* a second set of choices under the first (the welcome's log) */
    const char *items2[4];
    int nitems2;
    long value2;
    const char *label2;
    /* PG_PROGRESS */
    char sub[256];
    /* the buttons, left to right; what run() returns is the pressed one's index */
    const char *btn[PG_MAXBTN];
    int nbtn;
    int enter_btn;          /* Return presses it (-1: none) */
} oi_page;

typedef struct oi_backend {
    /* show the page and wait for a button: its index, or -1 if the window
     * was closed. The page's value, value2 and str are updated. */
    int (*run)(void *b, oi_page *p);
    /* show a PG_PROGRESS page, or update the one showing, and look at the
     * window without waiting: a button's index if one was pressed, else -1 */
    int (*progress)(void *b, oi_page *p);
    /* the panel's picture changed: read it again and show it */
    void (*panel)(void *b);
    /* events and the panel's animation, without waiting (between pages) */
    void (*idle)(void *b);
    void (*close)(void *b);
} oi_backend;

/* An oi_ui on a back end. */
oi_ui *oi_pages_open(oi *g, const oi_backend *be, void *b);

#endif
