/*
 * main.c - step 2: demo of the platform layer and the utility helpers.
 * Lists the files of one folder with size and modified date.
 */
#include "platform.h"
#include "utils.h"

#include <stdio.h>

typedef struct {
    size_t files;
    unsigned long long bytes;
} Totals;

static int show_file(const char *path, const PlatStat *st, void *ctx)
{
    Totals *t = ctx;
    char size[32], date[16];

    human_size(st->size, size, sizeof size);
    format_date(st->mtime, date, sizeof date);
    printf("  %-44s %10s  %s\n", path_basename(path), size, date);
    t->files++;
    t->bytes += st->size;
    return 0;
}

int main(void)
{
    char folder[OPATH], def[OPATH], total[32];
    Totals t = { 0, 0 };

    plat_console_init();
    ui_title("OrgaNice - step 2: platform layer and utilities");
    default_downloads(def, sizeof def);
    if (!prompt_folder("Folder to list", def, folder, sizeof folder))
        return 0;

    printf("\nFiles in %s:\n", folder);
    plat_walk(folder, 0, show_file, &t);
    human_size(t.bytes, total, sizeof total);
    ui_ok("%zu files, %s in total.", t.files, total);
    return 0;
}
