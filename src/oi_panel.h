/*
 * The side panel: a picture a quarter of the window wide, beside every
 * page, which a script changes with (panel "file"). Pictures are read
 * with datatypes, so any picture a datatype reads will do: IFF ILBM, PNG,
 * GIF and the rest. An animated GIF plays when gfopengif.datatype (47.1
 * or newer, DalsinAI/gfopendatatypes) is there, which gives its frames;
 * with another GIF datatype its first frame shows.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#ifndef OI_PANEL_H
#define OI_PANEL_H

#include "oi.h"

typedef struct oi_panel {
    char path[512];
    char caption[256];
    int generation;     /* changes with each (panel) */
    int fit;            /* 1: scaled to the panel keeping its shape (the default) */
} oi_panel;

oi_panel *oi_panel_get(oi *g);

#ifdef OI_AMIGA
struct RastPort;
struct Screen;
typedef struct gp_pic gp_pic;

/* Read a picture for a screen: its frames, ready to draw. */
gp_pic *gp_load(const char *path, struct Screen *scr);
void gp_free(gp_pic *p);
int gp_size(gp_pic *p, int *w, int *h);
int gp_frames(gp_pic *p);
long gp_delay_ms(gp_pic *p, int frame);    /* how long a frame shows */
/* Draw a frame into a box, scaled to fit it and centred. */
void gp_draw(gp_pic *p, int frame, struct RastPort *rp, int x, int y, int w, int h);
/* The colour round it and under its see-through parts: ARGB on a
 * true-colour screen, else the pen. */
void gp_background(gp_pic *p, unsigned long argb, long pen);

/* The panel as both windows keep it: the picture the script named (or the
 * logo), the frame showing, and the box it was drawn in last. */
typedef struct oi_side {
    gp_pic *pic;
    int generation;     /* the oi_panel's it was read for (-1: none yet) */
    int frame;
    long left_ms;       /* the frame's time left */
    long last_tick;     /* DateStamp ticks (1/50 s) */
    struct RastPort *rp;
    int x, y, w, h;     /* 0 wide: not drawn yet */
    unsigned long bg_argb;
    long bg_pen;
} oi_side;

void oi_side_init(oi_side *s, unsigned long bg_argb, long bg_pen);
/* Reads the picture again when the script changed it. */
void oi_side_update(oi_side *s, oi *g, struct Screen *scr);
/* Draws it into a box. */
void oi_side_draw(oi_side *s, struct RastPort *rp, int x, int y, int w, int h);
/* An animation's next frame when its time has come (drawn where it was). */
void oi_side_tick(oi_side *s);
void oi_side_free(oi_side *s);
#endif

#endif
