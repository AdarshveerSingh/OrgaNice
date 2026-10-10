/*
 * platform.h - thin operating-system abstraction layer.
 *
 * Everything that differs between Windows and Linux/macOS lives behind
 * these functions, so the rest of OrgaNice is plain, portable C.
 */
#ifndef ORGANICE_PLATFORM_H
#define ORGANICE_PLATFORM_H

#include <stdio.h>
#include <time.h>

#define OPATH 1024                 /* longest path OrgaNice handles */

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

typedef struct {
    unsigned long long size;
    time_t mtime;                  /* last modified */
    time_t atime;                  /* last accessed */
    int is_dir;
} PlatStat;

/* Called once for every regular file found by plat_walk. Return non-zero to stop. */
typedef int (*WalkFn)(const char *path, const PlatStat *st, void *ctx);

/* Called once per installed program by plat_list_installed. */
typedef void (*ProgramFn)(const char *name, const char *version, void *ctx);

void plat_console_init(void);

int  plat_stat(const char *path, PlatStat *st);
int  plat_exists(const char *path);
int  plat_is_dir(const char *path);

int  plat_mkdirs(const char *path);                  /* like mkdir -p */
int  plat_move(const char *src, const char *dst);    /* never overwrites */
int  plat_copy(const char *src, const char *dst);
int  plat_remove(const char *path);
int  plat_rmdir(const char *path);                   /* only removes empty folders */
int  plat_remove_tree(const char *path);             /* rm -r, used for the sandbox */
int  plat_hardlink(const char *existing, const char *link_path);
int  plat_set_mtime(const char *path, time_t t);

/* Identifies the physical file: two paths with the same (volume, index)
 * are already hard links to the same data. */
int  plat_file_id(const char *path, unsigned long long *volume, unsigned long long *index);

/* Visits every file below root. Skips symlinks/junctions and .organice folders. */
int  plat_walk(const char *root, int recursive, WalkFn fn, void *ctx);

int       plat_fseek64(FILE *f, long long offset, int whence);
long long plat_ftell64(FILE *f);

/* Lists installed programs (Windows registry). Returns -1 where unsupported. */
int  plat_list_installed(ProgramFn fn, void *ctx);

const char *plat_null_device(void);

#endif
