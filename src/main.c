/*
 * main.c - step 8: leftover installers and ZIPs
 * A small menu that exposes only the modules written so far.
 */
#include "duplicates.h"
#include "leftovers.h"
#include "organize.h"
#include "plan.h"
#include "platform.h"
#include "utils.h"
#include "zipfile.h"
#include <stdio.h>

typedef struct {
    const char *label;
    void (*run)(void);
} Item;

static void do_quick(void)    { organize_run(NULL, ORGANIZE_QUICK); }
static void do_by_date(void)  { organize_run(NULL, ORGANIZE_BY_DATE); }
static void do_org_adv(void)  { organize_run(NULL, ORGANIZE_ADVANCED); }

static void do_dups(void)     { duplicates_run(NULL, 0); }
static void do_dups_adv(void) { duplicates_run(NULL, 1); }

static void do_zip_list(void)
{
    char path[OPATH], sz[32];
    ZipInfo z;
    size_t i;
    int rc;

    prompt_line("Path of a .zip file: ", path, sizeof path);
    path_normalize(path);
    if (!path[0])
        return;
    rc = zip_read(path, &z);
    if (rc != 0) {
        ui_err("%s", rc == -2 ? "ZIP64 archives are not supported yet." : "Could not read it as a ZIP file.");
        return;
    }
    printf("\n%zu files (name, size, CRC-32 stored in the archive):\n", z.count);
    for (i = 0; i < z.count; i++) {
        human_size(z.items[i].size, sz, sizeof sz);
        printf("  %-40s %10s  %08X\n", z.items[i].name, sz, (unsigned)z.items[i].crc);
    }
    zip_free(&z);
}

static void do_zip_make(void)
{
    static const unsigned char HELLO[] = "Hello from OrgaNice\n";
    static const unsigned char NOTES[] = "A second file inside the demo archive.\n";
    char folder[OPATH], path[OPATH];
    ZipSource src[2];

    if (!prompt_folder("Folder for demo.zip", g_last_folder, folder, sizeof folder))
        return;
    src[0].name = "hello.txt";
    src[0].data = HELLO;
    src[0].len = sizeof HELLO - 1;
    src[1].name = "docs/notes.txt";
    src[1].data = NOTES;
    src[1].len = sizeof NOTES - 1;
    path_join(path, sizeof path, folder, "demo.zip");
    if (zip_write_stored(path, src, 2) == 0)
        ui_ok("Created %s", path);
    else
        ui_err("Could not write %s", path);
}

static void do_leftovers(void)     { leftovers_run(NULL, 0); }
static void do_leftovers_adv(void) { leftovers_run(NULL, 1); }

static void do_undo(void)
{
    char root[OPATH];
    if (prompt_folder("Undo the last operation in which folder?", g_last_folder, root, sizeof root))
        history_undo(root);
}

static void do_empty_trash(void)
{
    char root[OPATH];
    if (prompt_folder("Empty the OrgaNice trash of which folder?", g_last_folder, root, sizeof root))
        trash_empty(root);
}

static const Item ITEMS[] = {
    { "Quick tidy (sort by file type)", do_quick },
    { "Organize by date (Week1, Week2, months...)", do_by_date },
    { "Advanced organizer", do_org_adv },
    { "Find duplicate files", do_dups },
    { "Duplicates across folders and drives (+ hard links)", do_dups_adv },
    { "Show what is inside a ZIP file", do_zip_list },
    { "Create a small demo ZIP", do_zip_make },
    { "Leftover installers and ZIPs", do_leftovers },
    { "Leftovers with custom search folders", do_leftovers_adv },
    { "Undo the last operation in a folder", do_undo },
    { "Empty the OrgaNice trash of a folder", do_empty_trash },
};
#define NITEMS ((int)(sizeof ITEMS / sizeof ITEMS[0]))

int main(void)
{
    plat_console_init();
    ui_title("OrgaNice - step 8: leftover installers and ZIPs");
    for (;;) {
        int i, choice;
        for (i = 0; i < NITEMS; i++)
            printf("  %d) %s\n", i + 1, ITEMS[i].label);
        printf("  0) Exit\n");
        choice = prompt_int("\nChoose: ", 0, NITEMS, -1);
        if (g_input_closed || choice == 0)
            break;
        if (choice > 0) {
            ITEMS[choice - 1].run();
            press_enter();
        }
    }
    return 0;
}
