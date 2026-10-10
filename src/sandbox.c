/*
 * sandbox.c - generates ./organice_sandbox with realistic clutter:
 *   - the same photo under three different names (content duplicates)
 *   - the same video inside two "Premiere" project folders (hard-link demo)
 *   - a ZIP that was fully extracted, one half extracted, one never opened
 *   - an unknown installer and two versions of the same installer
 *   - an unfinished browser download
 *   - files with old dates (archiving demo) and spread-out dates (Week1/Week2 demo)
 * File contents are generated from a seed, so equal seeds = identical files.
 */
#include "sandbox.h"
#include "platform.h"
#include "utils.h"
#include "zipfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SANDBOX_DIR "organice_sandbox"

typedef struct {
    const char *path;
    unsigned seed;
    size_t kb;
    int days_old;
    int text;            /* compressible text instead of random bytes */
} DemoFile;

typedef struct {
    const char *name_in_zip;
    unsigned seed;
    size_t kb;
} DemoEntry;

static const DemoFile FILES[] = {
    { "holiday.jpg",                     1, 300,  40, 0 },
    { "holiday (1).jpg",                 1, 300,   2, 0 },
    { "IMG_2041.jpg",                    1, 300,  15, 0 },
    { "Screenshot 2026-09-14.png",       2,  80,  26, 0 },
    { "report_final.pdf",                3, 120,  20, 0 },
    { "report_final_REAL.pdf",           4, 125,  18, 0 },
    { "lecture3_notes.txt",              5,   4,   9, 1 },
    { "budget.xlsx",                     6,  30,   3, 0 },
    { "song.mp3",                        7, 500,  50, 0 },
    { "old_thesis_notes.txt",            8, 200, 420, 1 },
    { "2023_sales_data.csv",             9, 300, 800, 1 },
    { "invoice_march.pdf",              10,  50, 200, 0 },
    { "SomeToolSetup.exe",              11, 150,  12, 0 },
    { "python-3.11.4-amd64.exe",        12, 200, 100, 0 },
    { "python-3.12.1-amd64.exe",        13, 210,  30, 0 },
    { "movie.mp4.crdownload",           14, 100,   5, 0 },
    { "project_assets/readme.txt",      15,   4,  30, 0 },
    { "project_assets/logo.png",        16,  60,  30, 0 },
    { "project_assets/data.csv",        17,  20,  30, 0 },
    { "half_extracted/a.txt",           18,   8,  25, 0 },
    { "Premiere_Project_A/footage.mp4", 21, 1500, 60, 0 },
    { "Premiere_Project_B/footage.mp4", 21, 1500, 58, 0 },
};

static const DemoEntry ZIP_ASSETS[] = {
    { "project_assets/readme.txt", 15, 4 },
    { "project_assets/logo.png",   16, 60 },
    { "project_assets/data.csv",   17, 20 },
};
static const DemoEntry ZIP_HALF[]  = { { "a.txt", 18, 8 }, { "b.txt", 19, 8 } };
static const DemoEntry ZIP_NEVER[] = { { "c.txt", 20, 40 } };

static unsigned char *make_bytes(unsigned seed, size_t len, int text)
{
    static const char LINE[] = "OrgaNice sandbox - an old file nobody has opened in ages. ";
    unsigned char *b = xmalloc(len ? len : 1);
    unsigned long x = seed * 2654435761u + 7u;
    size_t i = 0;

    if (text) {
        int head = snprintf((char *)b, len, "seed %u\n", seed);
        for (i = (size_t)head; i < len; i++)
            b[i] = (unsigned char)LINE[i % (sizeof LINE - 1)];
        return b;
    }
    for (; i < len; i++) {
        x = x * 1103515245u + 12345u;
        b[i] = (unsigned char)(x >> 16);
    }
    return b;
}

static void put_file(const char *root, const DemoFile *d)
{
    char full[OPATH], dir[OPATH];
    size_t len = d->kb * 1024;
    unsigned char *data = make_bytes(d->seed, len, d->text);
    FILE *f;

    path_join(full, sizeof full, root, d->path);
    path_normalize(full);
    path_dirname(full, dir, sizeof dir);
    plat_mkdirs(dir);
    if ((f = fopen(full, "wb")) != NULL) {
        fwrite(data, 1, len, f);
        fclose(f);
        plat_set_mtime(full, time(NULL) - (time_t)d->days_old * 86400 - 3600);
    }
    free(data);
}

static void put_zip(const char *root, const char *zip_name, const DemoEntry *e, size_t n, int days_old)
{
    ZipSource src[8];
    unsigned char *bufs[8];
    char path[OPATH];
    size_t i;

    for (i = 0; i < n && i < 8; i++) {
        bufs[i] = make_bytes(e[i].seed, e[i].kb * 1024, 0);
        src[i].name = e[i].name_in_zip;
        src[i].data = bufs[i];
        src[i].len = e[i].kb * 1024;
    }
    path_join(path, sizeof path, root, zip_name);
    if (zip_write_stored(path, src, i) == 0)
        plat_set_mtime(path, time(NULL) - (time_t)days_old * 86400);
    while (i-- > 0)
        free(bufs[i]);
}

int sandbox_create(char *out_path, size_t n)
{
    size_t i;
    if (plat_is_dir(SANDBOX_DIR))
        plat_remove_tree(SANDBOX_DIR);
    if (plat_mkdirs(SANDBOX_DIR) != 0)
        return -1;
    for (i = 0; i < sizeof FILES / sizeof FILES[0]; i++)
        put_file(SANDBOX_DIR, &FILES[i]);
    put_zip(SANDBOX_DIR, "project_assets.zip", ZIP_ASSETS, 3, 31);
    put_zip(SANDBOX_DIR, "half_extracted.zip", ZIP_HALF, 2, 26);
    put_zip(SANDBOX_DIR, "never_opened.zip", ZIP_NEVER, 1, 45);
    snprintf(out_path, n, "%s", SANDBOX_DIR);
    return 0;
}

void sandbox_remove(const char *path)
{
    plat_remove_tree(path);
}
