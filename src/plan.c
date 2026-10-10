/*
 * plan.c - preview, execute and undo planned file operations.
 *
 * Undo log format (one line per completed operation, tab separated):
 *   MOVE   <original path>  <new path>
 *   TRASH  <original path>  <path inside trash>
 *   LINK   <original path>  <path inside trash>   (original is now a hard link)
 * Undo replays the log backwards and puts every file back.
 */
#include "plan.h"
#include "platform.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_LIMIT 25

/* ---------------------------- locations ----------------------------- */

void plan_meta_dir(const char *root, char *out, size_t n)
{
    path_join(out, n, root, ".organice");
}

static void trash_dir(const char *root, char *out, size_t n)
{
    char meta[OPATH];
    plan_meta_dir(root, meta, sizeof meta);
    path_join(out, n, meta, "trash");
}

static void log_path(const char *root, char *out, size_t n)
{
    char meta[OPATH];
    plan_meta_dir(root, meta, sizeof meta);
    path_join(out, n, meta, "last_run.log");
}

/* A unique file name inside the trash for `src`. */
static int trash_target(const char *root, const char *src, char *out, size_t n)
{
    static unsigned counter = 0;
    char dir[OPATH], name[OPATH];
    trash_dir(root, dir, sizeof dir);
    if (plat_mkdirs(dir) != 0)
        return -1;
    do {
        snprintf(name, sizeof name, "%ld_%u_%s", (long)time(NULL), counter++, path_basename(src));
        path_join(out, n, dir, name);
    } while (plat_exists(out));
    return 0;
}

/* --------------------------- building plans --------------------------- */

void plan_init(Plan *p, const char *log_root)
{
    memset(p, 0, sizeof *p);
    snprintf(p->log_root, sizeof p->log_root, "%s", log_root);
}

void plan_free(Plan *p)
{
    size_t i;
    for (i = 0; i < p->count; i++) {
        free(p->ops[i].src);
        free(p->ops[i].dst);
        free(p->ops[i].root);
        free(p->ops[i].note);
    }
    free(p->ops);
    memset(p, 0, sizeof *p);
}

static Op *push(Plan *p, OpType type, const char *src, unsigned long long size)
{
    Op *op;
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->ops = xrealloc(p->ops, p->cap * sizeof *p->ops);
    }
    op = &p->ops[p->count++];
    memset(op, 0, sizeof *op);
    op->type = type;
    op->src = xstrdup(src);
    op->size = size;
    return op;
}

void plan_add_move(Plan *p, const char *src, const char *dst, unsigned long long size)
{
    push(p, OP_MOVE, src, size)->dst = xstrdup(dst);
}

void plan_add_trash(Plan *p, const char *path, const char *root,
                    unsigned long long size, const char *note)
{
    Op *op = push(p, OP_TRASH, path, size);
    op->root = xstrdup(root);
    op->note = note ? xstrdup(note) : NULL;
}

void plan_add_link(Plan *p, const char *copy, const char *keeper, const char *root,
                   unsigned long long size)
{
    Op *op = push(p, OP_HARDLINK, copy, size);
    op->dst = xstrdup(keeper);
    op->root = xstrdup(root);
}

int plan_claims(const Plan *p, const char *dst)
{
    size_t i;
    for (i = 0; i < p->count; i++)
        if (p->ops[i].type == OP_MOVE && path_eq(p->ops[i].dst, dst))
            return 1;
    return 0;
}

/* ------------------------------ preview ------------------------------ */

static void print_op(const Op *op, const char *root)
{
    char sz[32];
    human_size(op->size, sz, sizeof sz);
    switch (op->type) {
    case OP_MOVE:
        printf("  " C_GREEN "MOVE " C_RESET " %s\n        -> %s\n",
               path_relative(op->src, root), path_relative(op->dst, root));
        break;
    case OP_TRASH:
        printf("  " C_YELLOW "TRASH" C_RESET " %s " C_DIM "(%s%s%s)" C_RESET "\n",
               path_relative(op->src, root), sz, op->note ? ", " : "", op->note ? op->note : "");
        break;
    case OP_HARDLINK:
        printf("  " C_CYAN "LINK " C_RESET " %s " C_DIM "(%s)" C_RESET "\n        => shares data with %s\n",
               path_relative(op->src, root), sz, path_relative(op->dst, root));
        break;
    }
}

static void print_summary(const Plan *p)
{
    size_t i, moves = 0, trashes = 0, links = 0;
    unsigned long long freed = 0;
    char sz[32];
    for (i = 0; i < p->count; i++) {
        switch (p->ops[i].type) {
        case OP_MOVE: moves++; break;
        case OP_TRASH: trashes++; freed += p->ops[i].size; break;
        case OP_HARDLINK: links++; freed += p->ops[i].size; break;
        }
    }
    human_size(freed, sz, sizeof sz);
    printf("\n" C_BOLD "Summary:" C_RESET " %zu moves, %zu to trash, %zu hard links", moves, trashes, links);
    if (freed)
        printf(" - frees %s once the trash is emptied", sz);
    printf("\n");
}

size_t plan_review_and_run(Plan *p, const char *display_root)
{
    char buf[16];
    size_t i;

    if (p->count == 0) {
        ui_info("Nothing to do - no changes are needed.");
        return 0;
    }
    ui_title("Proposed changes (nothing has been changed yet)");
    for (i = 0; i < p->count && i < PREVIEW_LIMIT; i++)
        print_op(&p->ops[i], display_root);
    if (p->count > PREVIEW_LIMIT)
        printf("  ... and %zu more\n", p->count - PREVIEW_LIMIT);
    print_summary(p);

    for (;;) {
        if (!prompt_line("\n[A]pply  [V]iew all  [C]ancel: ", buf, sizeof buf))
            return 0;
        switch (tolower((unsigned char)buf[0])) {
        case 'a':
            return plan_execute(p);
        case 'v':
            for (i = 0; i < p->count; i++)
                print_op(&p->ops[i], display_root);
            break;
        case 'c':
            ui_info("Cancelled. Nothing was changed.");
            return 0;
        default:
            break;
        }
    }
}

/* ------------------------------ execute ------------------------------ */

static int run_op(const Op *op, FILE *log)
{
    char dir[OPATH], t[OPATH];
    switch (op->type) {
    case OP_MOVE:
        path_dirname(op->dst, dir, sizeof dir);
        plat_mkdirs(dir);
        if (plat_move(op->src, op->dst) != 0)
            return 0;
        fprintf(log, "MOVE\t%s\t%s\n", op->src, op->dst);
        return 1;
    case OP_TRASH:
        if (trash_target(op->root, op->src, t, sizeof t) != 0 || plat_move(op->src, t) != 0)
            return 0;
        fprintf(log, "TRASH\t%s\t%s\n", op->src, t);
        return 1;
    case OP_HARDLINK:
        if (!plat_exists(op->dst))
            return 0;                                   /* kept file vanished: do nothing */
        if (trash_target(op->root, op->src, t, sizeof t) != 0 || plat_move(op->src, t) != 0)
            return 0;
        if (plat_hardlink(op->dst, op->src) != 0) {
            plat_move(t, op->src);                      /* roll back this file */
            return 0;
        }
        fprintf(log, "LINK\t%s\t%s\n", op->src, t);
        return 1;
    }
    return 0;
}

size_t plan_execute(Plan *p)
{
    char meta[OPATH], lp[OPATH], sz[32];
    FILE *log;
    size_t i, done = 0, failed = 0;
    unsigned long long reclaim = 0;

    plan_meta_dir(p->log_root, meta, sizeof meta);
    plat_mkdirs(meta);
    log_path(p->log_root, lp, sizeof lp);
    if ((log = fopen(lp, "w")) == NULL) {
        ui_err("Cannot write the undo log (%s) - aborting for safety.", lp);
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        if (run_op(&p->ops[i], log)) {
            done++;
            if (p->ops[i].type != OP_MOVE)
                reclaim += p->ops[i].size;
        } else {
            failed++;
            ui_err("Could not process %s", p->ops[i].src);
        }
        fflush(log);                                    /* crash-safe undo */
    }
    fclose(log);

    ui_ok("Done: %zu of %zu changes applied.", done, p->count);
    if (failed)
        ui_warn("%zu changes failed (file in use, missing, or no permission).", failed);
    if (reclaim) {
        human_size(reclaim, sz, sizeof sz);
        ui_info("%s can be freed with 'Empty OrgaNice trash'. Until then, 'Undo' restores everything.", sz);
    }
    return done;
}

/* -------------------------------- undo -------------------------------- */

int history_undo(const char *root)
{
    char lp[OPATH], done_path[OPATH + 16], line[3 * OPATH], dir[OPATH];
    char **lines = NULL;
    size_t n = 0, cap = 0, i;
    int restored = 0, failed = 0;
    FILE *f;

    log_path(root, lp, sizeof lp);
    if ((f = fopen(lp, "r")) == NULL) {
        ui_info("Nothing to undo in %s.", root);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            lines = xrealloc(lines, cap * sizeof *lines);
        }
        lines[n++] = xstrdup(line);
    }
    fclose(f);

    printf("The last operation in this folder changed %zu files.\n", n);
    if (n && prompt_yes_no("Undo all of them?", 1)) {
        for (i = n; i-- > 0;) {
            char *type = lines[i], *orig, *now;
            if ((orig = strchr(type, '\t')) == NULL)
                continue;
            *orig++ = '\0';
            if ((now = strchr(orig, '\t')) == NULL)
                continue;
            *now++ = '\0';

            path_dirname(orig, dir, sizeof dir);
            plat_mkdirs(dir);
            if (strcmp(type, "LINK") == 0)
                plat_remove(orig);                      /* drop the hard link first */
            if (plat_move(now, orig) == 0) {
                restored++;
                if (strcmp(type, "MOVE") == 0) {        /* tidy folders we created */
                    path_dirname(now, dir, sizeof dir);
                    if (plat_rmdir(dir) == 0) {
                        path_dirname(dir, dir, sizeof dir);
                        plat_rmdir(dir);
                    }
                }
            } else {
                failed++;
            }
        }
        snprintf(done_path, sizeof done_path, "%s.undone", lp);
        plat_remove(done_path);
        rename(lp, done_path);
        ui_ok("Restored %d files.", restored);
        if (failed)
            ui_warn("%d files could not be restored (moved or deleted since).", failed);
    }
    for (i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
    return restored;
}

/* ----------------------------- empty trash ----------------------------- */

typedef struct {
    size_t files;
    unsigned long long bytes;
} TrashStats;

static int count_file(const char *path, const PlatStat *st, void *ctx)
{
    TrashStats *ts = ctx;
    (void)path;
    ts->files++;
    ts->bytes += st->size;
    return 0;
}

int trash_empty(const char *root)
{
    char t[OPATH], lp[OPATH], sz[32];
    TrashStats ts = { 0, 0 };

    trash_dir(root, t, sizeof t);
    if (plat_is_dir(t))
        plat_walk(t, 1, count_file, &ts);
    if (ts.files == 0) {
        ui_info("The OrgaNice trash in %s is already empty.", root);
        return 0;
    }
    human_size(ts.bytes, sz, sizeof sz);
    ui_warn("This PERMANENTLY deletes %zu files (%s). They can't be restored afterwards.", ts.files, sz);
    if (!prompt_yes_no("Empty the trash now?", 0))
        return 0;
    plat_remove_tree(t);
    log_path(root, lp, sizeof lp);
    plat_remove(lp);                                    /* undo is no longer possible */
    ui_ok("Freed %s.", sz);
    return 1;
}
