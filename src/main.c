/*
 * main.c - step 4: demo of the plan engine.
 * Every action builds a Plan, shows a preview, and only runs after confirmation.
 * Trashed and moved files can be restored with Undo.
 */
#include "plan.h"
#include "platform.h"
#include "utils.h"

#include <stdio.h>

static int ask_root(char *root, size_t n)
{
    return prompt_folder("Working folder (its trash and undo log live inside it)", g_last_folder, root, n);
}

static int ask_file(char *path, size_t n, unsigned long long *size)
{
    PlatStat st;
    prompt_line("Path of the file: ", path, n);
    path_normalize(path);
    if (!path[0])
        return 0;
    if (plat_stat(path, &st) != 0 || st.is_dir) {
        ui_warn("Not a file: %s", path);
        return 0;
    }
    *size = st.size;
    return 1;
}

static void do_move(void)
{
    char root[OPATH], file[OPATH], sub[128], dir[OPATH], dst[OPATH];
    unsigned long long size;
    Plan plan;

    if (!ask_root(root, sizeof root) || !ask_file(file, sizeof file, &size))
        return;
    prompt_line("Move it into this subfolder of the working folder (e.g. Sorted): ", sub, sizeof sub);
    sanitize_folder_name(sub);
    if (!sub[0])
        return;
    path_join(dir, sizeof dir, root, sub);
    path_join(dst, sizeof dst, dir, path_basename(file));

    plan_init(&plan, root);
    plan_add_move(&plan, file, dst, size);
    plan_review_and_run(&plan, root);
    plan_free(&plan);
}

static void do_trash(void)
{
    char root[OPATH], file[OPATH];
    unsigned long long size;
    Plan plan;

    if (!ask_root(root, sizeof root) || !ask_file(file, sizeof file, &size))
        return;
    plan_init(&plan, root);
    plan_add_trash(&plan, file, root, size, "demo");
    plan_review_and_run(&plan, root);
    plan_free(&plan);
}

static void do_undo(void)
{
    char root[OPATH];
    if (ask_root(root, sizeof root))
        history_undo(root);
}

static void do_empty_trash(void)
{
    char root[OPATH];
    if (ask_root(root, sizeof root))
        trash_empty(root);
}

typedef struct {
    const char *label;
    void (*run)(void);
} Item;

static const Item ITEMS[] = {
    { "Move a file into a subfolder (with preview)", do_move },
    { "Move a file to the OrgaNice trash", do_trash },
    { "Undo the last operation in a folder", do_undo },
    { "Empty the OrgaNice trash of a folder", do_empty_trash },
};
#define NITEMS ((int)(sizeof ITEMS / sizeof ITEMS[0]))

int main(void)
{
    plat_console_init();
    ui_title("OrgaNice - step 4: plan engine (preview, trash, undo)");
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
