/*
 * main.c - step 3: demo of the scanner and the hash functions.
 * Scans a folder (with subfolders), shows the biggest files and fingerprints the largest one.
 */
#include "hash.h"
#include "platform.h"
#include "scanner.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int by_size_desc(const void *a, const void *b)
{
    const FileEntry *x = a, *y = b;
    return (x->size < y->size) - (x->size > y->size);
}

int main(void)
{
    char folder[OPATH], def[OPATH], sz[32];
    FileList fl;
    unsigned long long total = 0;
    size_t i;

    plat_console_init();
    ui_title("OrgaNice - step 3: scanner and hashing");
    default_downloads(def, sizeof def);
    if (!prompt_folder("Folder to scan (subfolders included)", def, folder, sizeof folder))
        return 0;

    filelist_init(&fl);
    filelist_scan(&fl, folder, 1, 0);
    for (i = 0; i < fl.count; i++)
        total += fl.items[i].size;
    human_size(total, sz, sizeof sz);
    ui_ok("Found %zu files, %s in total.", fl.count, sz);

    if (fl.count > 0) {
        const FileEntry *big;
        uint32_t crc;
        qsort(fl.items, fl.count, sizeof *fl.items, by_size_desc);

        printf("\nLargest files:\n");
        for (i = 0; i < fl.count && i < 5; i++) {
            human_size(fl.items[i].size, sz, sizeof sz);
            printf("  %10s  %s\n", sz, path_relative(fl.items[i].path, folder));
        }

        big = &fl.items[0];
        if (big->size <= (512ULL << 20) && crc32_file(big->path, &crc) == 0)
            printf("\nCRC-32 of %s: %08X\n", big->name, (unsigned)crc);
        else
            printf("\n(The largest file is bigger than 512 MB: CRC-32 skipped.)\n");
        printf("FNV-1a of its name: %016llX\n",
               (unsigned long long)fnv1a64(big->name, strlen(big->name), FNV_OFFSET));
    }
    filelist_free(&fl);
    return 0;
}
