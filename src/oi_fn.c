/*
 * The names: built-in functions and the parameters calls take.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include "oi.h"

#include <string.h>

static const char *const param_names[GP_COUNT] = {
    "prompt", "help", "confirm", "safe", "default", "choices",
    "range", "source", "dest", "newname", "pattern", "all", "files",
    "infos", "fonts", "noposition", "optional", "delopts", "nogauge",
    "disk", "assigns", "newpath", "resident", "include", "append",
    "command", "settooltype", "setdefaulttool", "setstack",
    "swapcolors", "quiet", "back", "noreq", "gettooltype",
    "getdefaulttool", "getstack", "getposition", "setposition",
    "defaults", "force", "askuser", "fail", "nofail", "oknodelete",
    "override", "log", "checksum", "size", "ifmissing", "unpack",
    "delay", "caption", "fit",
};

int oi_param_id(const char *name)
{
    int i;
    for (i = 0; i < GP_COUNT; i++)
        if (!oi_stricmp(param_names[i], name))
            return i;
    return -1;
}

const oi_fn *oi_find_fn(const char *name)
{
    static const oi_fn *const tables[] = { oi_fn_core, oi_fn_ask, oi_fn_file, oi_fn_extra };
    size_t t;
    for (t = 0; t < sizeof tables / sizeof tables[0]; t++) {
        const oi_fn *f;
        for (f = tables[t]; f->name; f++)
            if (!oi_stricmp(f->name, name))
                return f;
    }
    return NULL;
}
