/*
 * The file layer on the Amiga: dos.library, exec, icon.library and
 * workbench.library. Requesters for a volume that isn't there are turned
 * off (the process's window pointer is -1) while the Installer runs, so
 * (exists) and the rest answer at once, as with (noreq).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT licence.
 */
#include "oi.h"
#include "oi_sys.h"
#include "oi_sys_common.h"

#ifdef OI_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <graphics/gfxbase.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/wb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ExecBase *SysBase;
extern struct GfxBase *GfxBase;
extern struct Library *IconBase;

static APTR old_window_ptr;
static int window_ptr_set;

void gs_init(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    old_window_ptr = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    window_ptr_set = 1;
}

void gs_cleanup(void)
{
    if (window_ptr_set) {
        struct Process *me = (struct Process *)FindTask(NULL);
        me->pr_WindowPtr = old_window_ptr;
        window_ptr_set = 0;
    }
}

static struct FileInfoBlock *examine(const char *path, BPTR *lock_out)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    struct FileInfoBlock *fib;
    if (!l)
        return NULL;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (!fib || !Examine(l, fib)) {
        if (fib)
            FreeDosObject(DOS_FIB, fib);
        UnLock(l);
        return NULL;
    }
    if (lock_out)
        *lock_out = l;
    else
        UnLock(l);
    return fib;
}

int gs_exists(const char *path)
{
    struct FileInfoBlock *fib = examine(path, NULL);
    int r;
    if (!fib)
        return GS_NONE;
    r = fib->fib_DirEntryType > 0 ? GS_DIR : GS_FILE;
    FreeDosObject(DOS_FIB, fib);
    return r;
}

long long gs_size(const char *path)
{
    struct FileInfoBlock *fib = examine(path, NULL);
    long long r = -1;
    if (fib) {
        if (fib->fib_DirEntryType < 0)
            r = (long long)(ULONG)fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }
    return r;
}

int gs_date_cmp(const char *a, const char *b, int *res)
{
    struct FileInfoBlock *x = examine(a, NULL), *y = examine(b, NULL);
    int r = -1;
    if (x && y) {
        LONG c = CompareDates(&x->fib_Date, &y->fib_Date);
        /* CompareDates: > 0 when the first is earlier */
        *res = c > 0 ? -1 : c < 0;
        r = 0;
    }
    if (x)
        FreeDosObject(DOS_FIB, x);
    if (y)
        FreeDosObject(DOS_FIB, y);
    return r;
}

int gs_makedir(const char *path)
{
    BPTR l;
    char *parent, *p;
    if (gs_exists(path) == GS_DIR)
        return 0;
    l = CreateDir((CONST_STRPTR)path);
    if (l) {
        UnLock(l);
        return 0;
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND)
        return -1;
    /* the drawers above it first */
    parent = oi_strdup(path);
    if (!parent)
        return -1;
    p = (char *)PathPart((STRPTR)parent);
    if (p == parent || p[-1] == ':') {
        free(parent);
        return -1;
    }
    *p = 0;
    if (gs_makedir(parent)) {
        free(parent);
        return -1;
    }
    free(parent);
    l = CreateDir((CONST_STRPTR)path);
    if (!l)
        return -1;
    UnLock(l);
    return 0;
}

int gs_delete(const char *path)
{
    return DeleteFile((CONST_STRPTR)path) ? 0 : -1;
}

int gs_rename(const char *from, const char *to)
{
    return Rename((CONST_STRPTR)from, (CONST_STRPTR)to) ? 0 : -1;
}

int gs_copy(const char *from, const char *to,
            int (*progress)(void *ud, long long now, long long total), void *ud)
{
    BPTR in, out;
    LONG n;
    long long total = gs_size(from), done = 0;
    int r = 0;
    char *buf = AllocVec(32768, MEMF_ANY);
    struct FileInfoBlock *fib = examine(from, NULL);
    if (!buf) {
        if (fib)
            FreeDosObject(DOS_FIB, fib);
        return -1;
    }
    in = Open((CONST_STRPTR)from, MODE_OLDFILE);
    out = in ? Open((CONST_STRPTR)to, MODE_NEWFILE) : 0;
    if (!in || !out) {
        r = -1;
    } else {
        while ((n = Read(in, buf, 32768)) > 0) {
            if (Write(out, buf, n) != n) {
                r = -1;
                break;
            }
            done += n;
            if (progress && progress(ud, done, total)) {
                r = -2;
                break;
            }
        }
        if (n < 0)
            r = -1;
    }
    if (in)
        Close(in);
    if (out)
        Close(out);
    if (r && out)
        DeleteFile((CONST_STRPTR)to);
    if (!r && fib) {
        /* the copy keeps the file's bits, date and comment, as C:Copy CLONE */
        SetProtection((CONST_STRPTR)to, fib->fib_Protection & ~FIBF_ARCHIVE);
        SetFileDate((CONST_STRPTR)to, &fib->fib_Date);
        if (fib->fib_Comment[0])
            SetComment((CONST_STRPTR)to, (CONST_STRPTR)fib->fib_Comment);
    }
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    FreeVec(buf);
    return r;
}

long gs_get_protect(const char *path)
{
    struct FileInfoBlock *fib = examine(path, NULL);
    long r = -1;
    if (fib) {
        r = fib->fib_Protection;
        FreeDosObject(DOS_FIB, fib);
    }
    return r;
}

int gs_set_protect(const char *path, long bits)
{
    return SetProtection((CONST_STRPTR)path, (ULONG)bits) ? 0 : -1;
}

int gs_set_comment(const char *path, const char *comment)
{
    return SetComment((CONST_STRPTR)path, (CONST_STRPTR)comment) ? 0 : -1;
}

struct gs_dir {
    BPTR lock;
    struct FileInfoBlock *fib;
};

gs_dir *gs_opendir(const char *path)
{
    gs_dir *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->fib = examine(path, &d->lock);
    if (!d->fib || d->fib->fib_DirEntryType <= 0) {
        if (d->fib) {
            FreeDosObject(DOS_FIB, d->fib);
            UnLock(d->lock);
        }
        free(d);
        return NULL;
    }
    return d;
}

const char *gs_readdir(gs_dir *d, int *type)
{
    if (!ExNext(d->lock, d->fib))
        return NULL;
    *type = d->fib->fib_DirEntryType > 0 ? GS_DIR : GS_FILE;
    return (const char *)d->fib->fib_FileName;
}

void gs_closedir(gs_dir *d)
{
    if (!d)
        return;
    FreeDosObject(DOS_FIB, d->fib);
    UnLock(d->lock);
    free(d);
}

int gs_patmatch(const char *pattern, const char *name)
{
    LONG len = (LONG)strlen(pattern) * 2 + 2;
    char *tok = malloc((size_t)len);
    LONG r;
    int m;
    if (!tok)
        return gs_common_patmatch(pattern, name);
    r = ParsePatternNoCase((CONST_STRPTR)pattern, (STRPTR)tok, len);
    if (r < 0)
        m = gs_common_patmatch(pattern, name);
    else if (r == 0)
        m = !oi_stricmp(pattern, name);     /* no wildcards in it */
    else
        m = MatchPatternNoCase((CONST_STRPTR)tok, (STRPTR)name) != 0;
    free(tok);
    return m;
}

char *gs_read_file(const char *path, size_t *len)
{
    long long n = gs_size(path);
    BPTR f;
    char *buf;
    if (n < 0)
        return NULL;
    f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!f)
        return NULL;
    buf = malloc((size_t)n + 1);
    if (buf && Read(f, buf, (LONG)n) != (LONG)n) {
        free(buf);
        buf = NULL;
    }
    Close(f);
    if (buf) {
        buf[n] = 0;
        if (len)
            *len = (size_t)n;
    }
    return buf;
}

int gs_write_file(const char *path, const char *data, size_t len, int append)
{
    BPTR f;
    int r = 0;
    if (append) {
        f = Open((CONST_STRPTR)path, MODE_READWRITE);
        if (f)
            Seek(f, 0, OFFSET_END);
    } else {
        f = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }
    if (!f)
        return -1;
    if (len && Write(f, (APTR)data, (LONG)len) != (LONG)len)
        r = -1;
    if (!Close(f))
        r = -1;
    return r;
}

char *gs_getenv(const char *name)
{
    char buf[1024];
    LONG n = GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (n < 0)
        return NULL;
    return oi_strdup(buf);
}

int gs_setenv(const char *name, const char *value, int archive)
{
    return SetVar((CONST_STRPTR)name, (CONST_STRPTR)value, -1,
                  GVF_GLOBAL_ONLY | (archive ? GVF_SAVE_VAR : 0)) ? 0 : -1;
}

/* The name without its colon. */
static void bare(const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s", name);
    if (*out && out[strlen(out) - 1] == ':')
        out[strlen(out) - 1] = 0;
}

char *gs_getassign(const char *name, const char *opts)
{
    ULONG flags = 0;
    struct DosList *dl;
    char nm[128], path[512];
    char *r = NULL;
    const char *o;
    for (o = opts; *o; o++) {
        if (*o == 'a' || *o == 'A') flags |= LDF_ASSIGNS;
        if (*o == 'v' || *o == 'V') flags |= LDF_VOLUMES;
        if (*o == 'd' || *o == 'D') flags |= LDF_DEVICES;
    }
    if (!flags)
        flags = LDF_ASSIGNS;
    bare(name, nm, sizeof nm);
    dl = LockDosList(flags | LDF_READ);
    dl = FindDosEntry(dl, (CONST_STRPTR)nm, flags);
    if (dl) {
        if (dl->dol_Type == DLT_DIRECTORY && dl->dol_Lock &&
            NameFromLock(dl->dol_Lock, (STRPTR)path, sizeof path))
            r = oi_strdup(path);
        else if (dl->dol_Type == DLT_LATE || dl->dol_Type == DLT_NONBINDING)
            r = oi_strdup(dl->dol_misc.dol_assign.dol_AssignName ?
                          (const char *)dl->dol_misc.dol_assign.dol_AssignName : "");
        else
            r = oi_xprintf("%s:", nm);
    }
    UnLockDosList(flags | LDF_READ);
    return r;
}

int gs_makeassign(const char *name, const char *path, int add)
{
    char nm[128];
    BPTR l = 0;
    bare(name, nm, sizeof nm);
    if (!path)
        return AssignLock((CONST_STRPTR)nm, 0) ? 0 : -1;
    l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    if (!l)
        return -1;
    if (add) {
        if (!AssignAdd((CONST_STRPTR)nm, l)) {
            UnLock(l);
            return -1;
        }
        return 0;
    }
    if (!AssignLock((CONST_STRPTR)nm, l)) {
        UnLock(l);
        return -1;
    }
    return 0;    /* the assign owns the lock now */
}

char *gs_getdevice(const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    struct FileLock *fl;
    struct DosList *dl;
    char *r = NULL;
    if (!l)
        return NULL;
    fl = (struct FileLock *)BADDR(l);
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_DEVICES)) != NULL) {
        if (dl->dol_Task == fl->fl_Task) {
            const UBYTE *b = (const UBYTE *)BADDR(dl->dol_Name);
            r = malloc((size_t)b[0] + 1);
            if (r) {
                memcpy(r, b + 1, b[0]);
                r[b[0]] = 0;
            }
            break;
        }
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);
    UnLock(l);
    return r;
}

long long gs_diskspace(const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    struct InfoData *id;
    long long r = -1;
    if (!l)
        return -1;
    id = AllocVec(sizeof *id, MEMF_ANY | MEMF_CLEAR);
    if (id && Info(l, id))
        r = (long long)(ULONG)(id->id_NumBlocks - id->id_NumBlocksUsed) * (ULONG)id->id_BytesPerBlock;
    FreeVec(id);
    UnLock(l);
    return r;
}

char *gs_expandpath(const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    char buf[512];
    char *r = NULL;
    if (!l)
        return NULL;
    if (NameFromLock(l, (STRPTR)buf, sizeof buf))
        r = oi_strdup(buf);
    UnLock(l);
    return r;
}

long gs_getversion(const char *path, int resident)
{
    char *buf;
    size_t len = 0;
    long v;
    if (!path || !*path)
        return ((long)SysBase->LibNode.lib_Version << 16) | SysBase->SoftVer;
    if (resident) {
        /* a library or device in memory */
        struct Library *lib;
        const char *base = (const char *)FilePart((CONST_STRPTR)path);
        Forbid();
        lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)base);
        if (!lib)
            lib = (struct Library *)FindName(&SysBase->DeviceList, (CONST_STRPTR)base);
        v = lib ? (((long)lib->lib_Version << 16) | lib->lib_Revision) : -1;
        Permit();
        if (v >= 0)
            return v;
    }
    buf = gs_read_file(path, &len);
    if (!buf && !strchr(path, ':') && !strchr(path, '/')) {
        char *p = oi_xprintf("LIBS:%s", path);
        buf = p ? gs_read_file(p, &len) : NULL;
        free(p);
    }
    if (!buf)
        return -1;
    v = gs_common_scan_version(buf, len);
    free(buf);
    return v;
}

unsigned long gs_getsum(const char *path, int *ok)
{
    size_t len = 0;
    char *buf = gs_read_file(path, &len);
    unsigned long s;
    *ok = buf != NULL;
    if (!buf)
        return 0;
    s = gs_common_crc32(0, (const unsigned char *)buf, len);
    free(buf);
    return s;
}

char *gs_database(const char *what)
{
    UWORD af = SysBase->AttnFlags;
    if (!oi_stricmp(what, "cpu")) {
        if (af & 0x80)          /* AFF_68060 */
            return oi_strdup("68060");
        if (af & AFF_68040)
            return oi_strdup("68040");
        if (af & AFF_68030)
            return oi_strdup("68030");
        if (af & AFF_68020)
            return oi_strdup("68020");
        if (af & AFF_68010)
            return oi_strdup("68010");
        return oi_strdup("68000");
    }
    if (!oi_stricmp(what, "fpu")) {
        if (af & AFF_FPU40)
            return oi_strdup((af & 0x80) ? "68060" : "68040");
        if (af & AFF_68882)
            return oi_strdup("68882");
        if (af & AFF_68881)
            return oi_strdup("68881");
        return oi_strdup("NOFPU");
    }
    if (!oi_stricmp(what, "vblank"))
        return oi_xprintf("%u", (unsigned)SysBase->VBlankFrequency);
    if (!oi_stricmp(what, "chiprev")) {
        UBYTE c = GfxBase ? GfxBase->ChipRevBits0 : 0;
        if (c & GFXF_AA_ALICE)
            return oi_strdup("AGA");
        if (c & GFXF_HR_DENISE)
            return oi_strdup("ECS");
        return oi_strdup("OCS");
    }
    if (!oi_stricmp(what, "graphics-mem"))
        return oi_xprintf("%lu", (unsigned long)AvailMem(MEMF_CHIP));
    if (!oi_stricmp(what, "total-mem"))
        return oi_xprintf("%lu", (unsigned long)AvailMem(MEMF_ANY));
    if (!oi_stricmp(what, "os"))
        return oi_strdup("AmigaOS");
    return oi_strdup("unknown");
}

long gs_run(const char *command, const char *dir, int wait)
{
    BPTR out, old = 0, dl = 0;
    LONG rc;
    if (wait < 0) {
        ColdReboot();
        return 0;
    }
    if (!command)
        return -1;
    /* the program's output, in a window that opens only when it writes */
    out = Open((CONST_STRPTR)"CON:20/20/600/180/OpenInstaller/AUTO/CLOSE/WAIT", MODE_NEWFILE);
    if (dir && *dir) {
        dl = Lock((CONST_STRPTR)dir, SHARED_LOCK);
        if (dl)
            old = CurrentDir(dl);
    }
    rc = SystemTags((CONST_STRPTR)command, SYS_Input, out ? out : 0, SYS_Output, 0,
                    NP_StackSize, 32768, TAG_DONE);
    if (dl) {
        CurrentDir(old);
        UnLock(dl);
    }
    if (out)
        Close(out);
    return rc;
}

int gs_is_volume(const char *path)
{
    size_t n = strlen(path);
    return n > 1 && path[n - 1] == ':' && strchr(path, ':') == path + n - 1;
}

int gs_volume_mounted(const char *volume)
{
    char nm[128];
    struct DosList *dl;
    int r;
    bare(volume, nm, sizeof nm);
    dl = LockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES | LDF_READ);
    r = FindDosEntry(dl, (CONST_STRPTR)nm, LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES) != NULL;
    UnLockDosList(LDF_VOLUMES | LDF_ASSIGNS | LDF_DEVICES | LDF_READ);
    return r;
}

/* ---------------------------------------------------------------- icons */

static struct DiskObject *icon_get(const char *path)
{
    if (!IconBase)
        return NULL;
    return GetDiskObject((CONST_STRPTR)path);
}

static int icon_put(const char *path, struct DiskObject *dob)
{
    int r = PutDiskObject((CONST_STRPTR)path, dob) ? 0 : -1;
    return r;
}

int gs_icon_tooltype(const char *path, const char *name, const char *value, int remove)
{
    struct DiskObject *dob;
    STRPTR *old, *nt;
    int n = 0, i, j, r;
    size_t nl;
    char *entry = NULL;
    if (!IconBase)
        return -1;
    if (remove < 0) {
        /* a drawer's new icon, the default one */
        struct DiskObject *d = GetDefDiskObject(WBDRAWER);
        if (!d)
            return -1;
        r = icon_put(path, d);
        FreeDiskObject(d);
        return r;
    }
    dob = icon_get(path);
    if (!dob)
        return -1;
    old = (STRPTR *)dob->do_ToolTypes;
    while (old && old[n])
        n++;
    nt = calloc((size_t)n + 2, sizeof *nt);
    if (!nt) {
        FreeDiskObject(dob);
        return -1;
    }
    nl = strlen(name);
    for (i = 0, j = 0; i < n; i++) {
        const char *t = (const char *)old[i];
        const char *s = t;
        /* "(NAME=...)" is the same tool type turned off */
        if (*s == '(')
            s++;
        if (!strncasecmp(s, name, nl) && (s[nl] == '=' || s[nl] == 0 || s[nl] == ')'))
            continue;
        nt[j++] = (STRPTR)t;
    }
    if (!remove) {
        entry = value && *value ? oi_xprintf("%s=%s", name, value) : oi_strdup(name);
        if (entry)
            nt[j++] = (STRPTR)entry;
    }
    nt[j] = NULL;
    dob->do_ToolTypes = (STRPTR *)nt;
    r = icon_put(path, dob);
    dob->do_ToolTypes = (STRPTR *)old;
    FreeDiskObject(dob);
    free(entry);
    free(nt);
    return r;
}

char *gs_icon_get_tooltype(const char *path, const char *name)
{
    struct DiskObject *dob = icon_get(path);
    char *r = NULL;
    STRPTR v;
    if (!dob)
        return NULL;
    v = FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (CONST_STRPTR)name);
    if (v)
        r = oi_strdup((const char *)v);
    FreeDiskObject(dob);
    return r;
}

int gs_icon_default_tool(const char *path, const char *tool)
{
    struct DiskObject *dob = icon_get(path);
    STRPTR old;
    int r;
    if (!dob)
        return -1;
    old = dob->do_DefaultTool;
    dob->do_DefaultTool = (STRPTR)tool;
    r = icon_put(path, dob);
    dob->do_DefaultTool = old;
    FreeDiskObject(dob);
    return r;
}

char *gs_icon_get_default_tool(const char *path)
{
    struct DiskObject *dob = icon_get(path);
    char *r = NULL;
    if (!dob)
        return NULL;
    if (dob->do_DefaultTool)
        r = oi_strdup((const char *)dob->do_DefaultTool);
    FreeDiskObject(dob);
    return r;
}

int gs_icon_stack(const char *path, long stack)
{
    struct DiskObject *dob = icon_get(path);
    int r;
    if (!dob)
        return -1;
    dob->do_StackSize = stack;
    r = icon_put(path, dob);
    FreeDiskObject(dob);
    return r;
}

long gs_icon_get_stack(const char *path)
{
    struct DiskObject *dob = icon_get(path);
    long r;
    if (!dob)
        return -1;
    r = dob->do_StackSize;
    FreeDiskObject(dob);
    return r;
}

int gs_icon_noposition(const char *path)
{
    char *p = oi_strdup(path);
    struct DiskObject *dob;
    int r;
    if (!p)
        return -1;
    /* the icon's own name, without .info */
    if (strlen(p) > 5 && !oi_stricmp(p + strlen(p) - 5, ".info"))
        p[strlen(p) - 5] = 0;
    dob = icon_get(p);
    if (!dob) {
        free(p);
        return -1;
    }
    dob->do_CurrentX = NO_ICON_POSITION;
    dob->do_CurrentY = NO_ICON_POSITION;
    r = icon_put(p, dob);
    FreeDiskObject(dob);
    free(p);
    return r;
}

int gs_icon_swapcolors(const char *path)
{
    (void)path;
    return 0;   /* OS 3.2's icons need no swapping */
}

int gs_wb_object(const char *name, int what)
{
    struct Library *WorkbenchBase = OpenLibrary((CONST_STRPTR)"workbench.library", 44);
    BOOL ok = FALSE;
    if (!WorkbenchBase)
        return -1;
    if (what == 0)
        ok = OpenWorkbenchObjectA((STRPTR)name, NULL);
    else if (what == 1)
        ok = MakeWorkbenchObjectVisibleA((STRPTR)name, NULL);
    else
        ok = CloseWorkbenchObjectA((STRPTR)name, NULL);
    CloseLibrary(WorkbenchBase);
    return ok ? 0 : -1;
}

void gs_delay_ms(long ms)
{
    LONG t = (LONG)(ms / 20);
    if (t < 1)
        t = 1;
    Delay(t);
}

#endif
