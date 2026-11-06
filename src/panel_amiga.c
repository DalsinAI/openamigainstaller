/*
 * The side panel's pictures on the Amiga (oi_panel.h): read with
 * datatypes, so IFF ILBM, PNG, GIF and whatever else a datatype reads.
 *
 * On a true-colour screen (cybergraphics.library, depth above 8) the
 * picture is read as ARGB pixels, scaled to the panel keeping its shape,
 * laid over the panel's colour where it is see-through, and written with
 * WritePixelArray. gfopengif.datatype gives an animated GIF's frames
 * (OIA_Frames, OIM_READFRAME), each as browsers show it, with its delay.
 * On a screen of pens the datatype's remapped bitmap is scaled with
 * BitMapScale, and an animation shows its first frame.
 *
 * With no picture the panel shows the AmigaChrome logo, which is in the
 * program (logo_png.c, made from assets/ac-logo.png) and read from a copy
 * in T:.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_panel.h"

#ifdef OI_AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/scale.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <inline/macros.h>

#include <stdlib.h>
#include <string.h>

/* gfopenimage.h's, from DalsinAI/gfopendatatypes */
#define OIA_Dummy      (TAG_USER + 0x0DA15000)
#define OIA_Frames     (OIA_Dummy + 5)
#define OIM_READFRAME  (TAG_USER + 0x0DA15902)
struct oiReadFrame {
    ULONG MethodID;
    struct GadgetInfo *oirf_GInfo;
    ULONG oirf_Frame;
    UBYTE *oirf_Pixels;
    ULONG oirf_Stride;
    ULONG oirf_Flags;
    ULONG oirf_Delay;
    ULONG oirf_HasAlpha;
};

/* cybergraphics.library through its vectors: WritePixelArray (-126),
 * FillPixelArray (-150), GetCyberMapAttr (-96) */
static struct Library *CyberGfxBase;
#define CGX_WRITE(src, sx, sy, mod, rp, dx, dy, w, h, fmt) LP10(0x7e, ULONG, WritePixelArray, APTR, (src), a0, \
    UWORD, (sx), d0, UWORD, (sy), d1, UWORD, (mod), d2, struct RastPort *, (rp), a1, UWORD, (dx), d3, \
    UWORD, (dy), d4, UWORD, (w), d5, UWORD, (h), d6, UBYTE, (fmt), d7, , CyberGfxBase)
#define CGX_FILL(rp, x, y, w, h, argb) LP6(0x96, ULONG, FillPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, \
    UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, ULONG, (argb), d4, , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , CyberGfxBase)
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#define RECTFMT_ARGB 2   /* cybergraphics: RGB 0, RGBA 1, ARGB 2, LUT8 3 */

struct Library *DataTypesBase;
static int dt_users;

extern const unsigned char oi_logo_png[];
extern const unsigned long oi_logo_png_size;
#define LOGO_FILE "T:OpenInstaller.logo.png"

struct gp_pic {
    Object *o;
    int w, h;
    int frames;
    int rtg;
    /* true colour: the frame read last, and it scaled over the background */
    ULONG *src;
    int src_frame;
    ULONG delay;
    ULONG *dst;
    int dw, dh, dst_frame;
    /* pens: the remapped bitmap, and it scaled */
    struct BitMap *bm;
    struct BitMap *sbm;
    int sw, sh;
    ULONG bg_argb;
    LONG bg_pen;
};

void gp_background(gp_pic *p, unsigned long argb, long pen)
{
    if (!p)
        return;
    if (p->bg_argb != argb) {
        p->bg_argb = argb;
        p->dst_frame = -1;      /* laid over a new colour */
    }
    p->bg_pen = pen;
}

static int open_dt(void)
{
    if (!DataTypesBase)
        DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    if (!DataTypesBase)
        return 0;
    if (!CyberGfxBase)
        CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    dt_users++;
    return 1;
}

static void close_dt(void)
{
    if (--dt_users > 0)
        return;
    DeleteFile((CONST_STRPTR)LOGO_FILE);   /* nothing when it isn't there */
    if (CyberGfxBase) {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
    }
    if (DataTypesBase) {
        CloseLibrary(DataTypesBase);
        DataTypesBase = NULL;
    }
}

/* frame n into src: TRUE when it's there */
static int read_frame(gp_pic *p, int n)
{
    if (p->src_frame == n)
        return 1;
    if (p->frames > 1) {
        struct oiReadFrame m;
        m.MethodID = OIM_READFRAME;
        m.oirf_GInfo = NULL;
        m.oirf_Frame = (ULONG)n;
        m.oirf_Pixels = (UBYTE *)p->src;
        m.oirf_Stride = (ULONG)p->w * 4;
        m.oirf_Flags = 0;
        m.oirf_Delay = 0;
        m.oirf_HasAlpha = 0;
        if (!DoMethodA(p->o, (Msg)&m))
            return 0;
        p->delay = m.oirf_Delay;
    } else {
        struct pdtBlitPixelArray m;
        m.MethodID = PDTM_READPIXELARRAY;
        m.pbpa_PixelData = p->src;
        m.pbpa_PixelFormat = PBPAFMT_ARGB;
        m.pbpa_PixelArrayMod = (ULONG)p->w * 4;
        m.pbpa_Left = 0;
        m.pbpa_Top = 0;
        m.pbpa_Width = (ULONG)p->w;
        m.pbpa_Height = (ULONG)p->h;
        if (!DoMethodA(p->o, (Msg)&m))
            return 0;
        p->delay = 0;
    }
    p->src_frame = n;
    p->dst_frame = -1;
    return 1;
}

gp_pic *gp_load(const char *path, struct Screen *scr)
{
    gp_pic *p;
    struct BitMapHeader *bmh = NULL;
    ULONG frames = 0;
    if (!scr || !open_dt())
        return NULL;
    p = calloc(1, sizeof *p);
    if (!p) {
        close_dt();
        return NULL;
    }
    p->src_frame = p->dst_frame = -1;
    p->bg_argb = 0xff000000UL;
    /* The logo is written to T: and read from there as a file: from
     * memory (DTST_MEMORY) AmigaOS 3.2.3's PNG datatype gave nothing, and
     * the panel was an empty black box. */
    if (!(path && *path)) {
        BPTR fh = Open((CONST_STRPTR)LOGO_FILE, MODE_NEWFILE);
        if (fh) {
            LONG n = Write(fh, (APTR)oi_logo_png, (LONG)oi_logo_png_size);
            Close(fh);
            if (n == (LONG)oi_logo_png_size)
                path = LOGO_FILE;
        }
    }
    if (path && *path)
        p->o = NewDTObject((APTR)path, DTA_SourceType, DTST_FILE, DTA_GroupID, GID_PICTURE,
                           PDTA_Remap, TRUE, PDTA_Screen, (ULONG)scr, PDTA_DestMode, PMODE_V43,
                           PDTA_UseFriendBitMap, TRUE, TAG_DONE);
    if (!p->o)
        goto fail;
    if (GetDTAttrs(p->o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) != 1 || !bmh)
        goto fail;
    p->w = bmh->bmh_Width;
    p->h = bmh->bmh_Height;
    if (p->w <= 0 || p->h <= 0)
        goto fail;
    if (GetDTAttrs(p->o, OIA_Frames, (ULONG)&frames, TAG_DONE) != 1 || frames < 1)
        frames = 1;
    p->frames = (int)frames;
    p->rtg = CyberGfxBase && CGX_MAPATTR(scr->RastPort.BitMap, CYBRMATTR_ISCYBERGFX) &&
             CGX_MAPATTR(scr->RastPort.BitMap, CYBRMATTR_DEPTH) > 8;
    if (p->rtg) {
        p->src = malloc((size_t)p->w * (size_t)p->h * 4);
        if (p->src && read_frame(p, 0))
            return p;
        /* a datatype without READPIXELARRAY: the bitmap's way */
        free(p->src);
        p->src = NULL;
        p->rtg = 0;
    }
    p->frames = 1;
    {
        struct gpLayout gl;
        gl.MethodID = DTM_PROCLAYOUT;
        gl.gpl_GInfo = NULL;
        gl.gpl_Initial = TRUE;
        DoDTMethodA(p->o, NULL, NULL, (Msg)&gl);
    }
    if (GetDTAttrs(p->o, PDTA_DestBitMap, (ULONG)&p->bm, TAG_DONE) != 1 || !p->bm)
        GetDTAttrs(p->o, PDTA_BitMap, (ULONG)&p->bm, TAG_DONE);
    if (!p->bm)
        goto fail;
    return p;
fail:
    gp_free(p);
    return NULL;
}

void gp_free(gp_pic *p)
{
    if (!p)
        return;
    if (p->sbm)
        FreeBitMap(p->sbm);
    free(p->src);
    free(p->dst);
    if (p->o)
        DisposeDTObject(p->o);
    free(p);
    close_dt();
}

int gp_size(gp_pic *p, int *w, int *h)
{
    if (!p)
        return 0;
    *w = p->w;
    *h = p->h;
    return 1;
}

int gp_frames(gp_pic *p)
{
    return p ? p->frames : 0;
}

long gp_delay_ms(gp_pic *p, int frame)
{
    (void)frame;
    return p ? (long)p->delay : 0;
}

/* The size it is drawn at in a w by h box: its shape kept, as big as fits. */
static void fit(gp_pic *p, int w, int h, int *sw, int *sh)
{
    if ((long)p->w * h > (long)p->h * w) {
        *sw = w;
        *sh = (int)((long)p->h * w / p->w);
    } else {
        *sh = h;
        *sw = (int)((long)p->w * h / p->h);
    }
    if (*sw < 1)
        *sw = 1;
    if (*sh < 1)
        *sh = 1;
}

/* src scaled into dst (nearest, sw by sh), laid over the background */
static int scale_argb(gp_pic *p, int sw, int sh)
{
    int x, y;
    ULONG br = (p->bg_argb >> 16) & 255, bgc = (p->bg_argb >> 8) & 255, bb = p->bg_argb & 255;
    ULONG *col;
    if (p->dw != sw || p->dh != sh || !p->dst) {
        free(p->dst);
        p->dst = malloc((size_t)sw * (size_t)sh * 4);
        if (!p->dst)
            return 0;
        p->dw = sw;
        p->dh = sh;
        p->dst_frame = -1;
    }
    if (p->dst_frame == p->src_frame)
        return 1;
    col = malloc((size_t)sw * sizeof *col);
    if (!col)
        return 0;
    for (x = 0; x < sw; x++)
        col[x] = (ULONG)((long)x * p->w / sw);
    for (y = 0; y < sh; y++) {
        const ULONG *s = p->src + (size_t)((long)y * p->h / sh) * (size_t)p->w;
        ULONG *d = p->dst + (size_t)y * (size_t)sw;
        for (x = 0; x < sw; x++) {
            ULONG c = s[col[x]];
            ULONG a = c >> 24;
            if (a == 255) {
                d[x] = c;
            } else if (a == 0) {
                d[x] = p->bg_argb;
            } else {
                ULONG r = ((c >> 16) & 255) * a + br * (255 - a);
                ULONG g = ((c >> 8) & 255) * a + bgc * (255 - a);
                ULONG b = (c & 255) * a + bb * (255 - a);
                d[x] = 0xff000000UL | ((r / 255) << 16) | ((g / 255) << 8) | (b / 255);
            }
        }
    }
    free(col);
    p->dst_frame = p->src_frame;
    return 1;
}

static void fill_around(gp_pic *p, struct RastPort *rp, int x, int y, int w, int h, int ox, int oy, int sw, int sh)
{
    /* the box's margins round the picture */
    struct { int x, y, w, h; } r[4];
    int i;
    r[0].x = x; r[0].y = y; r[0].w = w; r[0].h = oy;
    r[1].x = x; r[1].y = y + oy + sh; r[1].w = w; r[1].h = h - oy - sh;
    r[2].x = x; r[2].y = y + oy; r[2].w = ox; r[2].h = sh;
    r[3].x = x + ox + sw; r[3].y = y + oy; r[3].w = w - ox - sw; r[3].h = sh;
    if (!p->rtg)
        SetAPen(rp, (ULONG)p->bg_pen);
    for (i = 0; i < 4; i++) {
        if (r[i].w <= 0 || r[i].h <= 0)
            continue;
        if (p->rtg)
            CGX_FILL(rp, r[i].x, r[i].y, r[i].w, r[i].h, p->bg_argb);
        else
            RectFill(rp, r[i].x, r[i].y, r[i].x + r[i].w - 1, r[i].y + r[i].h - 1);
    }
}

void gp_draw(gp_pic *p, int frame, struct RastPort *rp, int x, int y, int w, int h)
{
    int sw, sh, ox, oy;
    if (!p || w <= 0 || h <= 0)
        return;
    fit(p, w, h, &sw, &sh);
    ox = (w - sw) / 2;
    oy = (h - sh) / 2;
    if (p->rtg) {
        if (frame < 0 || frame >= p->frames)
            frame = 0;
        if (!read_frame(p, frame) && p->src_frame < 0)
            return;
        if (!scale_argb(p, sw, sh))
            return;
        CGX_WRITE(p->dst, 0, 0, sw * 4, rp, x + ox, y + oy, sw, sh, RECTFMT_ARGB);
    } else {
        if (!p->sbm || p->sw != sw || p->sh != sh) {
            struct BitScaleArgs bsa;
            if (p->sbm)
                FreeBitMap(p->sbm);
            p->sbm = AllocBitMap((ULONG)sw, (ULONG)sh, GetBitMapAttr(p->bm, BMA_DEPTH), BMF_CLEAR, p->bm);
            if (!p->sbm)
                return;
            p->sw = sw;
            p->sh = sh;
            memset(&bsa, 0, sizeof bsa);
            bsa.bsa_SrcWidth = (UWORD)p->w;
            bsa.bsa_SrcHeight = (UWORD)p->h;
            bsa.bsa_XSrcFactor = (UWORD)p->w;
            bsa.bsa_YSrcFactor = (UWORD)p->h;
            bsa.bsa_XDestFactor = (UWORD)sw;
            bsa.bsa_YDestFactor = (UWORD)sh;
            bsa.bsa_SrcBitMap = p->bm;
            bsa.bsa_DestBitMap = p->sbm;
            BitMapScale(&bsa);
        }
        BltBitMapRastPort(p->sbm, 0, 0, rp, x + ox, y + oy, sw, sh, 0xc0);
    }
    fill_around(p, rp, x, y, w, h, ox, oy, sw, sh);
}
/* ---- The panel as the windows keep it ------------------------------------------ */

#include <dos/dos.h>
#include <proto/dos.h>

static long ticks_now(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Minute % 1440) * 3000L + d.ds_Tick;
}

void oi_side_init(oi_side *s, unsigned long bg_argb, long bg_pen)
{
    memset(s, 0, sizeof *s);
    s->generation = -1;
    s->bg_argb = bg_argb;
    s->bg_pen = bg_pen;
}

static void start_frame(oi_side *s)
{
    long d = gp_delay_ms(s->pic, s->frame);
    /* as browsers do: a delay under 20 ms is 100 ms */
    s->left_ms = d < 20 ? 100 : d;
    s->last_tick = ticks_now();
}

void oi_side_update(oi_side *s, oi *g, struct Screen *scr)
{
    oi_panel *p = oi_panel_get(g);
    if (s->generation == p->generation && s->pic)
        return;
    s->generation = p->generation;
    if (s->pic)
        gp_free(s->pic);
    s->pic = p->path[0] ? gp_load(p->path, scr) : NULL;
    if (!s->pic)
        s->pic = gp_load(NULL, scr);    /* the logo, for none and for one that can't be read */
    if (s->pic)
        gp_background(s->pic, s->bg_argb, s->bg_pen);
    s->frame = 0;
    if (s->pic)
        start_frame(s);
    if (s->w > 0)
        oi_side_draw(s, s->rp, s->x, s->y, s->w, s->h);
}

void oi_side_draw(oi_side *s, struct RastPort *rp, int x, int y, int w, int h)
{
    s->rp = rp;
    s->x = x;
    s->y = y;
    s->w = w;
    s->h = h;
    if (!rp || w <= 0 || h <= 0)
        return;
    if (s->pic) {
        gp_draw(s->pic, s->frame, rp, x, y, w, h);
    } else {
        SetAPen(rp, (ULONG)s->bg_pen);
        RectFill(rp, x, y, x + w - 1, y + h - 1);
    }
}

void oi_side_tick(oi_side *s)
{
    long now, gone;
    if (!s->pic || gp_frames(s->pic) < 2 || s->w <= 0)
        return;
    now = ticks_now();
    gone = (now - s->last_tick) * 20;
    if (gone < 0)
        gone = 20;      /* past midnight */
    if (gone < s->left_ms)
        return;
    s->frame = (s->frame + 1) % gp_frames(s->pic);
    gp_draw(s->pic, s->frame, s->rp, s->x, s->y, s->w, s->h);
    start_frame(s);
}

void oi_side_free(oi_side *s)
{
    if (s->pic)
        gp_free(s->pic);
    s->pic = NULL;
    s->w = 0;
}
#endif
