/*
 * duplicates.c - finds files with identical CONTENT, whatever their names.
 *
 * Three filters, cheapest first, so big folders stay fast:
 *   1. group by exact size            (free: already known from the scan)
 *   2. FNV-1a hash of the first 64 KB (one small read per candidate)
 *   3. byte-by-byte comparison        (100% certain, only for real suspects)
 * Paths that are already hard links to the same data are recognised by
 * their file ID and never reported as duplicates.
 *
 * Extra copies can be moved to the OrgaNice trash, or replaced by hard
 * links: every path keeps working (e.g. a video used by two Premiere Pro
 * projects) but the data is stored only once.
 */
#include "duplicates.h"
#include "hash.h"
#include "plan.h"
#include "platform.h"
#include "scanner.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FINGERPRINT_BYTES (64 * 1024)
#define MAX_ROOTS 8
#define SHOW_GROUPS 10

typedef enum { KEEP_SMART, KEEP_OLDEST, KEEP_NEWEST, KEEP_SHORTEST } KeepPolicy;

typedef struct {
    size_t *members;     /* indices into the FileList */
    size_t n;
    size_t keeper;       /* index into members        */
} DupGroup;

typedef struct {
    DupGroup *items;
    size_t count;
    size_t cap;
} DupSet;

typedef struct {
    size_t idx;
    uint64_t fp;
    unsigned long long vol, id;
} Cand;

static const FileList *s_list;   /* used by the qsort comparators */

/* --------------------------- comparing files --------------------------- */

int files_identical(const char *a, const char *b)
{
    static unsigned char ba[1 << 16], bb[1 << 16];
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int same = -1;

    if (fa && fb) {
        same = 1;
        for (;;) {
            size_t na = fread(ba, 1, sizeof ba, fa);
            size_t nb = fread(bb, 1, sizeof bb, fb);
            if (na != nb || memcmp(ba, bb, na) != 0) {
                same = 0;
                break;
            }
            if (na == 0)
                break;
        }
    }
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return same;
}

static uint64_t fingerprint(const char *path, unsigned long long size)
{
    static unsigned char buf[FINGERPRINT_BYTES];
    uint64_t h = fnv1a64(&size, sizeof size, FNV_OFFSET);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(buf, 1, sizeof buf, f);
        h = fnv1a64(buf, n, h);
        fclose(f);
    }
    return h;
}

/* ------------------------------ searching ------------------------------ */

static int cmp_size_desc(const void *a, const void *b)
{
    unsigned long long x = s_list->items[*(const size_t *)a].size;
    unsigned long long y = s_list->items[*(const size_t *)b].size;
    return (x < y) - (x > y);
}

static int cmp_cand_id(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    if (x->vol != y->vol)
        return x->vol < y->vol ? -1 : 1;
    if (x->id != y->id)
        return x->id < y->id ? -1 : 1;
    return 0;
}

static int cmp_cand_fp(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    return (x->fp > y->fp) - (x->fp < y->fp);
}

static void add_group(DupSet *set, DupGroup g)
{
    if (set->count == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 32;
        set->items = xrealloc(set->items, set->cap * sizeof *set->items);
    }
    set->items[set->count++] = g;
}

/* Candidates share size and fingerprint: split them into truly identical clusters. */
static void cluster_run(const FileList *fl, const Cand *c, size_t n, DupSet *set)
{
    size_t *rep = xmalloc(n * sizeof *rep);  /* rep[i] = first member of i's cluster */
    size_t i, j;

    for (i = 0; i < n; i++) {
        rep[i] = i;
        for (j = 0; j < i; j++)
            if (rep[j] == j && files_identical(fl->items[c[j].idx].path, fl->items[c[i].idx].path) == 1) {
                rep[i] = j;
                break;
            }
    }
    for (i = 0; i < n; i++) {
        size_t count = 0;
        DupGroup g;
        if (rep[i] != i)
            continue;
        for (j = i; j < n; j++)
            count += rep[j] == i;
        if (count < 2)
            continue;
        g.members = xmalloc(count * sizeof *g.members);
        g.n = 0;
        g.keeper = 0;
        for (j = i; j < n; j++)
            if (rep[j] == i)
                g.members[g.n++] = c[j].idx;
        add_group(set, g);
    }
    free(rep);
}

static void find_duplicates(const FileList *fl, unsigned long long min_size,
                            DupSet *set, size_t *already_linked)
{
    size_t *order = xmalloc((fl->count + 1) * sizeof *order);
    Cand *c = xmalloc((fl->count + 1) * sizeof *c);
    size_t n = 0, i, j, k, done = 0;

    for (i = 0; i < fl->count; i++)
        if (fl->items[i].size > 0 && fl->items[i].size >= min_size)
            order[n++] = i;
    s_list = fl;
    qsort(order, n, sizeof *order, cmp_size_desc);

    for (i = 0; i < n; i = j) {
        unsigned long long size = fl->items[order[i]].size;
        size_t m = 0, start;

        for (j = i; j < n && fl->items[order[j]].size == size; j++)
            ;
        done += j - i;
        if (j - i < 2)
            continue;                                   /* unique size: can't be a duplicate */

        /* step 1: same size. Drop paths that are already the same physical file. */
        for (k = i; k < j; k++, m++) {
            c[m].idx = order[k];
            if (plat_file_id(fl->items[order[k]].path, &c[m].vol, &c[m].id) != 0) {
                c[m].vol = (unsigned long long)-1;
                c[m].id = order[k];
            }
        }
        qsort(c, m, sizeof *c, cmp_cand_id);
        for (k = 1, start = 1; k < m; k++) {
            if (c[k].vol == c[start - 1].vol && c[k].id == c[start - 1].id)
                (*already_linked)++;
            else
                c[start++] = c[k];
        }
        m = start;
        if (m < 2)
            continue;

        /* step 2: fingerprint of the first 64 KB */
        for (k = 0; k < m; k++)
            c[k].fp = fingerprint(fl->items[c[k].idx].path, size);
        qsort(c, m, sizeof *c, cmp_cand_fp);

        /* step 3: byte-by-byte inside each fingerprint run */
        for (start = 0; start < m; start = k) {
            for (k = start; k < m && c[k].fp == c[start].fp; k++)
                ;
            if (k - start >= 2)
                cluster_run(fl, c + start, k - start, set);
        }
        printf("\r  Comparing candidates... %zu/%zu", done, n);
        fflush(stdout);
    }
    printf("\r%60s\r", "");
    free(order);
    free(c);
}

/* --------------------------- choosing a keeper --------------------------- */

static int copy_penalty(const char *name)
{
    const char *p;
    if (str_icontains(name, "copy"))
        return 10;
    for (p = strchr(name, '('); p; p = strchr(p + 1, '(')) {   /* "photo (1).jpg" */
        const char *q = p + 1;
        while (isdigit((unsigned char)*q))
            q++;
        if (q > p + 1 && *q == ')')
            return 10;
    }
    return 0;
}

static int location_penalty(const char *path)
{
    int s = 0;
    if (str_icontains(path, "downloads"))
        s += 3;
    if (str_icontains(path, "/temp/") || str_icontains(path, "\\temp\\"))
        s += 3;
    return s;
}

/* Is a a better file to keep than b? */
static int better(const FileEntry *a, const FileEntry *b, KeepPolicy policy)
{
    int pa, pb;
    switch (policy) {
    case KEEP_OLDEST:   return a->mtime < b->mtime;
    case KEEP_NEWEST:   return a->mtime > b->mtime;
    case KEEP_SHORTEST: return strlen(a->path) < strlen(b->path);
    default:
        pa = copy_penalty(a->name) + location_penalty(a->path);
        pb = copy_penalty(b->name) + location_penalty(b->path);
        if (pa != pb)
            return pa < pb;
        if (a->mtime != b->mtime)
            return a->mtime < b->mtime;
        return strlen(a->path) < strlen(b->path);
    }
}

static void choose_keeper(const FileList *fl, DupGroup *g, KeepPolicy policy)
{
    size_t i;
    g->keeper = 0;
    for (i = 1; i < g->n; i++)
        if (better(&fl->items[g->members[i]], &fl->items[g->members[g->keeper]], policy))
            g->keeper = i;
}

static unsigned long long wasted(const FileList *fl, const DupGroup *g)
{
    return fl->items[g->members[0]].size * (unsigned long long)(g->n - 1);
}

static int cmp_group_waste(const void *a, const void *b)
{
    unsigned long long x = wasted(s_list, a), y = wasted(s_list, b);
    return (x < y) - (x > y);
}

/* ------------------------------- output ------------------------------- */

static void print_group(const FileList *fl, const DupGroup *g, size_t num)
{
    char sz[32], waste[32], d[16];
    size_t i;
    human_size(fl->items[g->members[0]].size, sz, sizeof sz);
    human_size(wasted(fl, g), waste, sizeof waste);
    printf(C_BOLD "[%zu]" C_RESET " %zu identical files, %s each " C_YELLOW "(%s reclaimable)" C_RESET "\n",
           num, g->n, sz, waste);
    for (i = 0; i < g->n; i++) {
        const FileEntry *e = &fl->items[g->members[i]];
        format_date(e->mtime, d, sizeof d);
        printf("   %zu. %s%s" C_RESET " %s " C_DIM "(%s)" C_RESET "\n", i + 1,
               i == g->keeper ? C_GREEN : C_DIM, i == g->keeper ? "KEEP" : "copy", e->path, d);
    }
}

static void warn_projects(void)
{
    printf("\n");
    ui_warn("Before removing copies, make sure no project depends on them.");
    printf("    Apps like Premiere Pro, Blender or VS Code remember files by PATH.\n"
           "    If a copy disappears, the project shows 'missing media'.\n"
           "    Safer: replace copies with HARD LINKS - every path keeps working,\n"
           "    the data is stored once. (Linked paths share data: editing one edits all.)\n");
}

/* ------------------------------- actions ------------------------------- */

static void plan_trash_copies(Plan *p, const FileList *fl, const DupGroup *g, char roots[][OPATH])
{
    size_t i;
    for (i = 0; i < g->n; i++)
        if (i != g->keeper) {
            const FileEntry *e = &fl->items[g->members[i]];
            plan_add_trash(p, e->path, roots[e->root_id], e->size, "duplicate");
        }
}

/* Returns how many copies could not be linked (different drive). */
static size_t plan_link_copies(Plan *p, const FileList *fl, const DupGroup *g, char roots[][OPATH])
{
    const FileEntry *k = &fl->items[g->members[g->keeper]];
    unsigned long long kv, ki, v, id;
    size_t i, cross = 0;
    if (plat_file_id(k->path, &kv, &ki) != 0)
        return g->n - 1;
    for (i = 0; i < g->n; i++) {
        const FileEntry *e = &fl->items[g->members[i]];
        if (i == g->keeper)
            continue;
        if (plat_file_id(e->path, &v, &id) != 0 || v != kv) {
            cross++;                                    /* hard links can't cross drives */
            continue;
        }
        plan_add_link(p, e->path, k->path, roots[e->root_id], e->size);
    }
    return cross;
}

static size_t review_groups(Plan *p, const FileList *fl, DupSet *set, char roots[][OPATH])
{
    char buf[32];
    size_t i, cross = 0;
    int c = 0;

    printf("\nFor each group:  Enter = trash the copies    l = replace copies with hard links\n"
           "                 k2 = keep file #2 instead    s = skip    q = stop reviewing\n\n");
    for (i = 0; i < set->count; i++) {
        DupGroup *g = &set->items[i];
        for (;;) {
            print_group(fl, g, i + 1);
            if (!prompt_line("  > ", buf, sizeof buf))
                return cross;
            c = tolower((unsigned char)buf[0]);
            if (c != 'k')
                break;
            {
                int num = atoi(buf + 1);
                if (num >= 1 && (size_t)num <= g->n)
                    g->keeper = (size_t)num - 1;
                else
                    ui_warn("There is no file #%s in this group.", buf + 1);
            }
        }
        if (c == 'q')
            break;
        if (c == 's')
            continue;
        if (c == 'l')
            cross += plan_link_copies(p, fl, g, roots);
        else
            plan_trash_copies(p, fl, g, roots);
    }
    return cross;
}

/* -------------------------------- entry -------------------------------- */

static int ask_roots(char roots[][OPATH], int advanced)
{
    char def[OPATH], buf[OPATH];
    int n = 0, r;

    default_downloads(def, sizeof def);
    if (!prompt_folder("Folder to search (subfolders included)", def, roots[0], OPATH))
        return 0;
    n = 1;
    if (!advanced)
        return n;
    printf("Add more folders or whole drives to compare against (e.g. D:\\Projects or D:\\).\n");
    while (n < MAX_ROOTS && prompt_line("  another folder (blank = done): ", buf, sizeof buf) && buf[0]) {
        path_normalize(buf);
        if (!plat_is_dir(buf)) {
            ui_warn("Not a folder: %s", buf);
            continue;
        }
        for (r = 0; r < n; r++)
            if (path_has_prefix(buf, roots[r]) || path_has_prefix(roots[r], buf))
                break;
        if (r < n) {
            ui_warn("That overlaps with %s - pick folders that don't contain each other.", roots[r]);
            continue;
        }
        snprintf(roots[n++], OPATH, "%s", buf);
    }
    return n;
}

void duplicates_run(const char *preset_root, int advanced)
{
    char (*roots)[OPATH] = xmalloc(MAX_ROOTS * sizeof *roots);
    char sz[32];
    int nroots, r, choice;
    size_t i, linked = 0, cross = 0, copies = 0;
    unsigned long long min_size = 1, total_waste = 0;
    KeepPolicy policy = KEEP_SMART;
    FileList fl;
    DupSet set = { NULL, 0, 0 };
    Plan plan;

    ui_title(advanced ? "Duplicates across folders and drives" : "Find duplicate files");
    printf("Files are compared by CONTENT, not by name:\n"
           "  same size -> same 64 KB fingerprint -> byte-by-byte check\n"
           "so 'photo.jpg', 'photo (1).jpg' and 'IMG_2938.jpg' are caught if they're identical.\n\n");

    if (preset_root) {
        snprintf(roots[0], OPATH, "%s", preset_root);
        nroots = 1;
    } else if ((nroots = ask_roots(roots, advanced)) == 0) {
        free(roots);
        return;
    }
    if (advanced) {
        printf("\nOnly look at files of at least:\n  1) any size\n  2) 1 MB\n  3) 100 MB (videos, disk images...)\n");
        switch (prompt_int("Choice [1]: ", 1, 3, 1)) {
        case 2: min_size = 1ULL << 20; break;
        case 3: min_size = 100ULL << 20; break;
        default: break;
        }
        printf("\nWhich copy should be kept?\n"
               "  1) Smart: cleanest name, not in Downloads/Temp, oldest\n"
               "  2) Oldest\n  3) Newest\n  4) Shortest path\n");
        policy = (KeepPolicy)(prompt_int("Choice [1]: ", 1, 4, 1) - 1);
    }

    filelist_init(&fl);
    for (r = 0; r < nroots; r++)
        filelist_scan(&fl, roots[r], 1, r);
    ui_info("Scanned %zu files.", fl.count);

    find_duplicates(&fl, min_size, &set, &linked);
    if (linked)
        ui_info("%zu paths are already hard links of each other (no space wasted) - ignored.", linked);
    if (set.count == 0) {
        ui_ok("No duplicates found.");
        goto out;
    }

    for (i = 0; i < set.count; i++) {
        choose_keeper(&fl, &set.items[i], policy);
        total_waste += wasted(&fl, &set.items[i]);
        copies += set.items[i].n - 1;
    }
    s_list = &fl;
    qsort(set.items, set.count, sizeof *set.items, cmp_group_waste);

    human_size(total_waste, sz, sizeof sz);
    printf("\n" C_BOLD "%zu groups of duplicates, %zu extra copies, %s reclaimable" C_RESET "\n\n",
           set.count, copies, sz);
    for (i = 0; i < set.count && i < SHOW_GROUPS; i++)
        print_group(&fl, &set.items[i], i + 1);
    if (set.count > SHOW_GROUPS)
        printf("  ... and %zu more groups (choose 'Review group by group' to see all)\n",
               set.count - SHOW_GROUPS);

    warn_projects();
    printf("\nWhat should happen to the extra copies?\n"
           "  1) Move them to the OrgaNice trash (undoable)\n"
           "  2) Replace them with hard links (all paths keep working, data stored once)\n"
           "  3) Review group by group\n"
           "  0) Nothing for now\n");
    choice = prompt_int("Choice [0]: ", 0, 3, 0);

    plan_init(&plan, roots[0]);
    for (i = 0; i < set.count && choice != 3; i++) {
        if (choice == 1)
            plan_trash_copies(&plan, &fl, &set.items[i], roots);
        else if (choice == 2)
            cross += plan_link_copies(&plan, &fl, &set.items[i], roots);
    }
    if (choice == 3)
        cross = review_groups(&plan, &fl, &set, roots);
    if (cross)
        ui_warn("%zu copies are on a different drive than the kept file and can't be hard-linked; "
                "they were left alone.", cross);
    if (choice)
        plan_review_and_run(&plan, roots[0]);
    plan_free(&plan);

out:
    for (i = 0; i < set.count; i++)
        free(set.items[i].members);
    free(set.items);
    filelist_free(&fl);
    free(roots);
}
