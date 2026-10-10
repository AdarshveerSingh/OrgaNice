/*
 * main.c - step 6: organizer and duplicates
 * A small menu that exposes only the modules written so far.
 */
#include "duplicates.h"
#include "organize.h"
#include "plan.h"
#include "platform.h"
#include "utils.h"
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
    { "Undo the last operation in a folder", do_undo },
    { "Empty the OrgaNice trash of a folder", do_empty_trash },
};
#define NITEMS ((int)(sizeof ITEMS / sizeof ITEMS[0]))

int main(void)
{
    plat_console_init();
    ui_title("OrgaNice - step 6: organizer and duplicates");
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
