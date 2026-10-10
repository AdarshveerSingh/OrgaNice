/*
 * ui.c - menus: Basic mode, Advanced mode, the "What do you want to do?"
 * feature search, and the Sandbox.
 *
 * Every feature is described once in the FEATURES table (name, keywords,
 * description, where to find it, function to run). The menus, the search
 * and the sandbox are all generated from that table.
 */
#include "ui.h"
#include "archive.h"
#include "duplicates.h"
#include "leftovers.h"
#include "organize.h"
#include "plan.h"
#include "platform.h"
#include "sandbox.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*FeatureFn)(const char *root);   /* root = NULL means "ask the user" */

typedef struct {
    const char *name;
    const char *tagline;
    const char *keywords;
    const char *description;
    const char *where;
    FeatureFn run;                              /* NULL = planned, not built yet */
} Feature;

static void run_quick(const char *r)         { organize_run(r, ORGANIZE_QUICK); }
static void run_by_date(const char *r)       { organize_run(r, ORGANIZE_BY_DATE); }
static void run_org_advanced(const char *r)  { organize_run(r, ORGANIZE_ADVANCED); }
static void run_dups(const char *r)          { duplicates_run(r, 0); }
static void run_dups_advanced(const char *r) { duplicates_run(r, 1); }
static void run_leftovers(const char *r)     { leftovers_run(r, 0); }
static void run_leftovers_adv(const char *r) { leftovers_run(r, 1); }
static void run_archive(const char *r)       { archive_run(r, 1); }

static int folder_for(const char *title, const char *root, char *out, size_t n)
{
    char def[OPATH];
    if (root) {
        snprintf(out, n, "%s", root);
        return 1;
    }
    ui_title(title);
    if (g_last_folder[0])
        snprintf(def, sizeof def, "%s", g_last_folder);
    else
        default_downloads(def, sizeof def);
    return prompt_folder("Which folder?", def, out, n);
}

static void run_undo(const char *r)
{
    char f[OPATH];
    if (folder_for("Undo last operation", r, f, sizeof f))
        history_undo(f);
}

static void run_empty_trash(const char *r)
{
    char f[OPATH];
    if (folder_for("Empty OrgaNice trash", r, f, sizeof f))
        trash_empty(f);
}

static const Feature FEATURES[] = {
    { "Quick tidy", "sort a folder by file type",
      "sort organize organise tidy clean messy clutter type category downloads images photos documents videos music",
      "Sorts the files of one folder into Images, Documents, Videos, Archives, Installers... folders.",
      "Basic mode > 1", run_quick },
    { "Organize by date", "Week1/Week2, months or years",
      "date week weekly week1 month monthly year day modified time timeline semester course lecture",
      "Creates folders such as Week1, Week2... (from a start date), 2026-10 October or 2026 from each\n"
      "     file's modified date, optionally sorted by type inside each one.",
      "Basic mode > 2", run_by_date },
    { "Find duplicates", "same content, any name",
      "duplicate copy copies same identical twin double repeated redundant space",
      "Finds files with identical content even when their names differ and moves the extra copies\n"
      "     to the OrgaNice trash.",
      "Basic mode > 3", run_dups },
    { "Leftover installers & ZIPs", "already extracted / already installed",
      "zip unzip unzipped extracted extract installer setup exe msi installed install leftover cleanup "
      "crdownload unfinished download",
      "Checks whether each ZIP's contents already exist on disk (CRC-verified) and whether each\n"
      "     installer's program is installed or superseded by a newer installer.",
      "Basic mode > 4", run_leftovers },
    { "Advanced organizer", "filters, name rules, size, custom folders",
      "filter extension pdf jpg only between range dates name keyword rule size custom rename folder "
      "names destination subfolders conflict",
      "Full control: date-range and extension filters, grouping by type/extension/name rules/size/date,\n"
      "     custom folder names, another destination, subfolders.",
      "Advanced mode > 1", run_org_advanced },
    { "Duplicates across drives + hard links", "many folders, keep policy, hard links",
      "hard link hardlink project premiere video footage drive whole system multiple folders keep policy "
      "oldest newest",
      "Compares several folders or drives at once, lets you choose which copy to keep, and can replace\n"
      "     copies with hard links so every project path keeps working while the data is stored once.",
      "Advanced mode > 2", run_dups_advanced },
    { "Leftovers with custom search folders", "choose where extractions may be",
      "zip extracted elsewhere other location search folder installer subfolders",
      "Like the leftovers check, but you choose extra folders/drives where ZIPs may have been\n"
      "     extracted, and can include subfolders.",
      "Advanced mode > 3", run_leftovers_adv },
    { "Archive old files", "compress files unused for months",
      "old unused stale compress compression zip archive backup months year storage space",
      "Packs files you haven't used for 6 months / 1 year / 2 years (or custom) into one verified\n"
      "     compressed archive; the originals go to the OrgaNice trash.",
      "Advanced mode > 4", run_archive },
    { "Empty OrgaNice trash", "permanently free the space",
      "trash empty delete permanently free space bin recycle",
      "Permanently deletes what OrgaNice moved to its trash. Afterwards those changes can't be undone.",
      "Advanced mode > 5", run_empty_trash },
    { "Undo last operation", "put everything back",
      "undo revert restore mistake back recover rollback",
      "Reverses the last organize / cleanup run in a folder using its undo log.",
      "Main menu > 5", run_undo },
    { "Storage analyzer", "planned",
      "storage space disk full drive usage using big large biggest analyzer analyse analyze",
      "Planned: shows what is using your disk space, with a drill-down per folder.",
      "not available yet", NULL },
    { "Fast whole-drive scan (NTFS MFT)", "planned",
      "mft ntfs fast scan whole drive system index everything",
      "Planned: reads the NTFS Master File Table directly to index a whole drive in seconds.",
      "not available yet", NULL },
    { "File name analyzer", "planned",
      "rename renaming name names naming final_final unclear bad",
      "Planned: spots unclear names like final_final_REAL.pdf or IMG_4839.jpg and suggests better ones.",
      "not available yet", NULL },
};
#define NFEATURES ((int)(sizeof FEATURES / sizeof FEATURES[0]))
#define UNDO_FEATURE 9

static const int BASIC_ITEMS[] = { 0, 1, 2, 3 };
static const int ADVANCED_ITEMS[] = { 4, 5, 6, 7, 8 };

static void run_feature(const Feature *f, const char *root)
{
    if (f->run)
        f->run(root);
    else
        ui_info("'%s' is planned but not built yet.", f->name);
    press_enter();
}

static void feature_menu(const char *title, const char *intro, const int *items, int n)
{
    for (;;) {
        int i, choice;
        ui_title(title);
        printf("%s\n\n", intro);
        for (i = 0; i < n; i++)
            printf("  %d) %-40s " C_DIM "%s" C_RESET "\n", i + 1, FEATURES[items[i]].name, FEATURES[items[i]].tagline);
        printf("  %d) %-40s " C_DIM "%s" C_RESET "\n  0) Back\n", n + 1,
               FEATURES[UNDO_FEATURE].name, FEATURES[UNDO_FEATURE].tagline);
        choice = prompt_int("\nChoose: ", 0, n + 1, -1);
        if (g_input_closed || choice == 0)
            return;
        if (choice == n + 1)
            run_feature(&FEATURES[UNDO_FEATURE], NULL);
        else if (choice > 0)
            run_feature(&FEATURES[items[choice - 1]], NULL);
    }
}

/* ------------------------------- sandbox ------------------------------- */

static int sandbox_start(char *root, size_t n)
{
    if (sandbox_create(root, n) != 0) {
        ui_err("Could not create the sandbox folder.");
        return 0;
    }
    ui_ok("Sandbox ready in ./%s", root);
    printf("    It holds duplicates with different names, old files, an extracted ZIP, a half-\n"
           "    extracted ZIP, a never-opened ZIP, installers (one superseded), an unfinished\n"
           "    download and the same video in two 'Premiere' project folders.\n"
           "    Nothing outside this folder is touched.\n");
    return 1;
}

static void sandbox_finish(const char *root)
{
    if (prompt_yes_no("\nDelete the sandbox folder now?", 1)) {
        sandbox_remove(root);
        ui_ok("Sandbox removed.");
    } else {
        ui_info("The sandbox stays in ./%s - open it to inspect the results.", root);
    }
}

static void sandbox_menu(void)
{
    char root[OPATH];
    int map[NFEATURES], i, n, choice;

    ui_title("Sandbox - try features on demo files");
    if (!sandbox_start(root, sizeof root))
        return;
    for (;;) {
        printf("\nRun a feature inside the sandbox:\n");
        for (i = 0, n = 0; i < NFEATURES; i++)
            if (FEATURES[i].run) {
                map[n++] = i;
                printf("  %d) %s\n", n, FEATURES[i].name);
            }
        printf("  %d) Reset the sandbox to its original state\n  0) Leave the sandbox\n", n + 1);
        choice = prompt_int("\nChoose: ", 0, n + 1, -1);
        if (g_input_closed || choice == 0)
            break;
        if (choice == n + 1)
            sandbox_start(root, sizeof root);
        else if (choice > 0)
            run_feature(&FEATURES[map[choice - 1]], root);
    }
    sandbox_finish(root);
}

/* ------------------------ "What do you want to do?" ------------------------ */

static int feature_score(const Feature *f, const char *query)
{
    static const char *STOP = "the and for you want how can file files folder folders my all with that "
                              "this from into some are have get make please";
    char q[256], *word;
    int score = 0;

    snprintf(q, sizeof q, "%s", query);
    str_lower(q);
    for (word = strtok(q, " ,.?!\"'"); word; word = strtok(NULL, " ,.?!\"'")) {
        size_t len = strlen(word);
        if (len < 3 || str_in_wordlist(word, STOP))
            continue;
        if (len > 4 && word[len - 1] == 's')    /* "duplicates" -> "duplicate" */
            word[len - 1] = '\0';
        if (str_icontains(f->keywords, word))
            score += 3;
        if (str_icontains(f->name, word))
            score += 2;
        if (str_icontains(f->description, word))
            score += 1;
    }
    return score;
}

static void help_search(void)
{
    char q[256], buf[32];
    int scores[NFEATURES], best[3], nbest = 0, i, k;

    ui_title("What do you want to do?");
    printf("Describe your goal in a few words, for example:\n"
           "  \"delete duplicate photos\", \"sort lecture files by week\", \"are my zips extracted\"\n\n");
    if (!prompt_line("> ", q, sizeof q) || !q[0])
        return;

    for (i = 0; i < NFEATURES; i++)
        scores[i] = feature_score(&FEATURES[i], q);
    for (k = 0; k < 3; k++) {
        int top = -1;
        for (i = 0; i < NFEATURES; i++)
            if (scores[i] > 0 && (top < 0 || scores[i] > scores[top]))
                top = i;
        if (top < 0)
            break;
        best[nbest++] = top;
        scores[top] = 0;
    }
    if (nbest == 0) {
        ui_info("OrgaNice can't do that (yet). Try words like: duplicate, week, zip, installer, old, undo.");
        press_enter();
        return;
    }

    printf("\nBest matches:\n");
    for (k = 0; k < nbest; k++) {
        const Feature *f = &FEATURES[best[k]];
        printf("\n  " C_BOLD "%d) %s" C_RESET "  " C_DIM "[%s]" C_RESET "\n     %s\n",
               k + 1, f->name, f->where, f->description);
    }
    prompt_line("\nType a number to run it, s + number to try it in the sandbox (e.g. s1),\n"
                "or press Enter to go back: ", buf, sizeof buf);
    if (buf[0]) {
        int in_sandbox = tolower((unsigned char)buf[0]) == 's';
        int num = atoi(buf + in_sandbox);
        const Feature *f;
        char root[OPATH];
        if (num < 1 || num > nbest)
            return;
        f = &FEATURES[best[num - 1]];
        if (!f->run) {
            ui_info("'%s' is planned but not built yet.", f->name);
            press_enter();
        } else if (!in_sandbox) {
            run_feature(f, NULL);
        } else if (sandbox_start(root, sizeof root)) {
            run_feature(f, root);
            sandbox_finish(root);
        }
    }
}

/* -------------------------------- main menu -------------------------------- */

static void banner(void)
{
    printf(C_CYAN C_BOLD
           "   ___                   _  _ _\n"
           "  / _ \\ _ _ __ _ __ _   | \\| (_)__ ___\n"
           " | (_) | '_/ _` / _` |  | .` | / _/ -_)\n"
           "  \\___/|_| \\__, \\__,_|  |_|\\_|_\\__\\___|\n"
           "           |___/\n" C_RESET
           C_DIM "  Organize nicely. Clean safely. Undo anything.\n" C_RESET);
}

void ui_run(void)
{
    banner();
    for (;;) {
        int choice;
        ui_title("Main menu");
        printf("  1) Basic mode                " C_DIM "simple, safe one-step cleanups" C_RESET "\n"
               "  2) Advanced mode             " C_DIM "filters, parameters, several folders" C_RESET "\n"
               "  3) What do you want to do?   " C_DIM "describe a goal, find the right feature" C_RESET "\n"
               "  4) Sandbox                   " C_DIM "try any feature on demo files first" C_RESET "\n"
               "  5) Undo last operation       " C_DIM "put everything back" C_RESET "\n"
               "  0) Exit\n");
        choice = prompt_int("\nChoose: ", 0, 5, -1);
        if (g_input_closed || choice == 0)
            break;
        switch (choice) {
        case 1:
            feature_menu("Basic mode", "Simple tasks with safe defaults. You always see a preview first.",
                         BASIC_ITEMS, 4);
            break;
        case 2:
            feature_menu("Advanced mode", "Every parameter is yours to set.", ADVANCED_ITEMS, 5);
            break;
        case 3:
            help_search();
            break;
        case 4:
            sandbox_menu();
            break;
        case 5:
            run_feature(&FEATURES[UNDO_FEATURE], NULL);
            break;
        default:
            break;
        }
    }
    printf("\nBye - enjoy the tidy folders!\n");
}
