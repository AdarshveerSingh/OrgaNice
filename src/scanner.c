/*
 * scanner.c - walks folders and records every file with its size and dates.
 */
#include "scanner.h"
#include "platform.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FileList *fl;
    int root_id;
    size_t added;
} ScanCtx;

/* Files that belong to the OS (or to OrgaNice itself) are never touched. */
static int is_protected(const char *name)
{
    return str_ieq(name, "desktop.ini") || str_ieq(name, "thumbs.db")
        || str_ieq(name, ".DS_Store") || str_ieq(name, "organice.exe")
        || strcmp(name, "organice") == 0;
}

static int on_file(const char *path, const PlatStat *st, void *ctx)
{
    ScanCtx *sc = ctx;
    FileList *fl = sc->fl;
    FileEntry *e;

    if (is_protected(path_basename(path)))
        return 0;
    if (fl->count == fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 256;
        fl->items = xrealloc(fl->items, fl->cap * sizeof *fl->items);
    }
    e = &fl->items[fl->count++];
    e->path = xstrdup(path);
    e->name = path_basename(e->path);
    e->size = st->size;
    e->mtime = st->mtime;
    e->atime = st->atime;
    e->root_id = sc->root_id;

    if (++sc->added % 2000 == 0) {
        printf("\r  Scanning... %zu files", sc->added);
        fflush(stdout);
    }
    return 0;
}

void filelist_init(FileList *fl)
{
    memset(fl, 0, sizeof *fl);
}

void filelist_free(FileList *fl)
{
    size_t i;
    for (i = 0; i < fl->count; i++)
        free(fl->items[i].path);
    free(fl->items);
    memset(fl, 0, sizeof *fl);
}

size_t filelist_scan(FileList *fl, const char *root, int recursive, int root_id)
{
    ScanCtx sc;
    sc.fl = fl;
    sc.root_id = root_id;
    sc.added = 0;
    plat_walk(root, recursive, on_file, &sc);
    if (sc.added >= 2000)
        printf("\r%40s\r", "");
    return sc.added;
}
