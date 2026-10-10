/*
 * plan.h - the safety core of OrgaNice.
 *
 * No feature touches the disk directly. Each one builds a Plan (a list of
 * proposed operations), the user previews it, and only then is it executed.
 * Execution writes an undo log, and "deleted" files go to a trash folder
 * (<folder>/.organice/trash), so every run can be reverted.
 */
#ifndef ORGANICE_PLAN_H
#define ORGANICE_PLAN_H

#include <stddef.h>

typedef enum {
    OP_MOVE,        /* src -> dst                                         */
    OP_TRASH,       /* src -> OrgaNice trash                              */
    OP_HARDLINK     /* src -> trash, then src becomes a hard link to dst  */
} OpType;

typedef struct {
    OpType type;
    char *src;
    char *dst;      /* MOVE: new path, HARDLINK: file to link to          */
    char *root;     /* TRASH/HARDLINK: folder whose trash is used         */
    char *note;     /* optional reason shown in the preview               */
    unsigned long long size;
} Op;

typedef struct {
    Op *ops;
    size_t count;
    size_t cap;
    char log_root[1024];   /* where the undo log is written */
} Plan;

void plan_init(Plan *p, const char *log_root);
void plan_free(Plan *p);

void plan_add_move(Plan *p, const char *src, const char *dst, unsigned long long size);
void plan_add_trash(Plan *p, const char *path, const char *root,
                    unsigned long long size, const char *note);
void plan_add_link(Plan *p, const char *copy, const char *keeper, const char *root,
                   unsigned long long size);

int    plan_claims(const Plan *p, const char *dst);  /* is dst already a move target? */
size_t plan_review_and_run(Plan *p, const char *display_root);
size_t plan_execute(Plan *p);

void plan_meta_dir(const char *root, char *out, size_t n);
int  history_undo(const char *root);
int  trash_empty(const char *root);

#endif
