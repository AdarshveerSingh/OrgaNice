/*
 * scanner.h - collects the files of one or more folders into a FileList.
 */
#ifndef ORGANICE_SCANNER_H
#define ORGANICE_SCANNER_H

#include <stddef.h>
#include <time.h>

typedef struct {
    char *path;                    /* full path (owned) */
    const char *name;              /* points inside path */
    unsigned long long size;
    time_t mtime;
    time_t atime;
    int root_id;                   /* which scanned folder it came from */
} FileEntry;

typedef struct {
    FileEntry *items;
    size_t count;
    size_t cap;
} FileList;

void   filelist_init(FileList *fl);
void   filelist_free(FileList *fl);
size_t filelist_scan(FileList *fl, const char *root, int recursive, int root_id);

#endif
