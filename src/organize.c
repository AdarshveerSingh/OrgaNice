/*
 * organize.c - Feature 1: file organisation.
 *
 * Every mode uses the same pipeline:
 *   scan -> filter (dates, extensions) -> pick a target folder per file
 *   -> resolve name conflicts -> Plan -> preview -> execute (undoable)
 *
 * Target folder = [date folder] / [group folder]
 *   date folder : Year | Month | Day | Week of year | Week1, Week2... from a start date
 *   group folder: Type | Extension | Name rule | Size
 */
#include "organize.h"
#include "duplicates.h"
#include "plan.h"
#include "platform.h"
#include "scanner.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------ categories ------------------------------ */

typedef struct {
    const char *folder;
    const char *exts;
} Category;

static const Category CATEGORIES[] = {
    { "Images",     "jpg jpeg png gif bmp webp svg tif tiff heic heif ico raw cr2 nef" },
    { "Videos",     "mp4 mkv avi mov wmv flv webm m4v mpg mpeg 3gp" },
    { "Audio",      "mp3 wav flac aac ogg m4a wma opus mid midi" },
    { "Documents",  "pdf doc docx txt rtf odt md tex epub xls xlsx csv ods ppt pptx odp" },
    { "Archives",   "zip rar 7z tar gz bz2 xz tgz iso" },
    { "Installers", "exe msi msix appx dmg pkg deb rpm apk" },
    { "Code",       "c h cpp hpp cs java py js ts jsx tsx html css json xml yml yaml sh bat ps1 lua go rs sql ipynb" },
    { "Fonts",      "ttf otf woff woff2" },
    { "Design_3D",  "psd ai xd fig blend fbx obj stl dae" },
};
#define NCAT ((int)(sizeof CATEGORIES / sizeof CATEGORIES[0]))
#define CAT_OTHER NCAT                       /* index of the "Other" folder */

static int category_of(const char *name)
{
    char ext[32];
    int i;
    path_ext_lower(name, ext, sizeof ext);
    if (ext[0])
        for (i = 0; i < NCAT; i++)
            if (str_in_wordlist(ext, CATEGORIES[i].exts))
                return i;
    return CAT_OTHER;
}

static const char *category_name(int i)
{
    return i < NCAT ? CATEGORIES[i].folder : "Other";
}

/* -------------------------------- config -------------------------------- */

typedef enum { GROUP_TYPE, GROUP_EXT, GROUP_NAME, GROUP_SIZE, GROUP_NONE } GroupBy;
typedef enum { DATE_OFF, DATE_YEAR, DATE_MONTH, DATE_DAY, DATE_WEEK_OF_YEAR, DATE_WEEK_NUMBERED } DateBy;

typedef struct {
    char keyword[64];
    char folder[64];
} NameRule;

#define MAX_RULES 32

static const NameRule PRESET_RULES[] = {
    { "screenshot", "Screenshots" }, { "screen shot", "Screenshots" },
    { "img_", "Camera" }, { "dsc_", "Camera" }, { "pxl_", "Camera" },
    { "whatsapp", "WhatsApp" }, { "invoice", "Invoices" }, { "receipt", "Receipts" },
    { "resume", "Resumes" }, { "assignment", "Assignments" }, { "lecture", "Lectures" },
    { "wallpaper", "Wallpapers" },
};
#define NPRESET ((int)(sizeof PRESET_RULES / sizeof PRESET_RULES[0]))

typedef struct {
    char src[OPATH];                 /* folder being organized            */
    char dst[OPATH];                 /* where the new folders are created */
    int recursive;

    int use_dates;                   /* filter on modified date           */
    time_t from, to;
    char exts[1024];                 /* "pdf jpg" - empty means all types */

    DateBy date_by;
    time_t week_start;               /* for Week1, Week2...               */
    GroupBy group_by;
    NameRule rules[MAX_RULES];
    int nrules;
    char cat_names[NCAT + 1][64];    /* user can rename type folders      */
} OrgConfig;

static void config_init(OrgConfig *c)
{
    int i;
    memset(c, 0, sizeof *c);
    for (i = 0; i <= NCAT; i++)
        snprintf(c->cat_names[i], sizeof c->cat_names[i], "%s", category_name(i));
    c->group_by = GROUP_TYPE;
    c->date_by = DATE_OFF;
}

/* ------------------------- deciding the target ------------------------- */

static int passes_filter(const OrgConfig *c, const FileEntry *e)
{
    if (c->use_dates && (e->mtime < c->from || e->mtime > c->to))
        return 0;
    if (c->exts[0]) {
        char ext[32];
        path_ext_lower(e->name, ext, sizeof ext);
        if (!str_in_wordlist(ext[0] ? ext : "(none)", c->exts))
            return 0;
    }
    return 1;
}

static void date_folder(const OrgConfig *c, time_t t, char *out, size_t n)
{
    static const char *MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December" };
    struct tm *tm = localtime(&t);
    if (!tm) {
        snprintf(out, n, "Unknown date");
        return;
    }
    switch (c->date_by) {
    case DATE_YEAR:
        snprintf(out, n, "%d", tm->tm_year + 1900);
        break;
    case DATE_MONTH:
        snprintf(out, n, "%d-%02d %s", tm->tm_year + 1900, tm->tm_mon + 1, MONTHS[tm->tm_mon]);
        break;
    case DATE_DAY:
        snprintf(out, n, "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        break;
    case DATE_WEEK_OF_YEAR:
        snprintf(out, n, "%d Week %02d", tm->tm_year + 1900, tm->tm_yday / 7 + 1);
        break;
    case DATE_WEEK_NUMBERED: {
        double days = difftime(t, c->week_start) / 86400.0;
        if (days < 0)
            snprintf(out, n, "Before Week1");
        else
            snprintf(out, n, "Week%d", (int)(days / 7) + 1);
        break;
    }
    default:
        out[0] = '\0';
    }
}

/* Returns 0 when the file should stay where it is (e.g. no name rule matched). */
static int group_folder(const OrgConfig *c, const FileEntry *e, char *out, size_t n)
{
    int i;
    char *p;
    switch (c->group_by) {
    case GROUP_TYPE:
        snprintf(out, n, "%s", c->cat_names[category_of(e->name)]);
        return 1;
    case GROUP_EXT:
        path_ext_lower(e->name, out, n);
        if (!out[0])
            snprintf(out, n, "No extension");
        else
            for (p = out; *p; p++)
                *p = (char)toupper((unsigned char)*p);
        return 1;
    case GROUP_NAME:
        for (i = 0; i < c->nrules; i++)
            if (str_icontains(e->name, c->rules[i].keyword)) {
                snprintf(out, n, "%s", c->rules[i].folder);
                return 1;
            }
        return 0;
    case GROUP_SIZE:
        if (e->size < (1ULL << 20))
            snprintf(out, n, "Small (under 1 MB)");
        else if (e->size < (100ULL << 20))
            snprintf(out, n, "Medium (1-100 MB)");
        else if (e->size < (1ULL << 30))
            snprintf(out, n, "Large (100 MB-1 GB)");
        else
            snprintf(out, n, "Huge (over 1 GB)");
        return 1;
    case GROUP_NONE:
        out[0] = '\0';
        return 1;
    }
    return 0;
}

static int target_folder(const OrgConfig *c, const FileEntry *e, char *out, size_t n)
{
    char d[64] = "", g[128] = "";
    if (c->date_by != DATE_OFF)
        date_folder(c, e->mtime, d, sizeof d);
    if (!group_folder(c, e, g, sizeof g))
        return 0;
    if (d[0] && g[0])
        snprintf(out, n, "%s%c%s", d, PATH_SEP, g);
    else
        snprintf(out, n, "%s", d[0] ? d : g);
    return out[0] != '\0';
}

/* --------------------------- name conflicts --------------------------- */

typedef enum { CONFLICT_ASK, CONFLICT_RENAME, CONFLICT_SKIP, CONFLICT_REPLACE } Conflict;

/* "report.pdf" -> "report (2).pdf", the first name that is free. */
static void unique_name(const Plan *p, char *path, size_t n)
{
    char dir[OPATH], stem[256], ext[32], name[OPATH];
    int i;
    path_dirname(path, dir, sizeof dir);
    path_stem(path, stem, sizeof stem);
    snprintf(ext, sizeof ext, "%s", path_ext(path));
    for (i = 2;; i++) {
        if (ext[0])
            snprintf(name, sizeof name, "%s (%d).%s", stem, i, ext);
        else
            snprintf(name, sizeof name, "%s (%d)", stem, i);
        path_join(path, n, dir, name);
        if (!plat_exists(path) && !plan_claims(p, path))
            return;
    }
}

static Conflict ask_conflict(const FileEntry *e, const char *dst, int *apply_all)
{
    PlatStat st;
    char a[32], b[32], da[16], db[16], buf[16];

    printf("\n" C_YELLOW "Conflict:" C_RESET " '%s' already exists in the destination.\n", e->name);
    if (plat_stat(dst, &st) == 0) {
        human_size(st.size, a, sizeof a);
        format_date(st.mtime, da, sizeof da);
        human_size(e->size, b, sizeof b);
        format_date(e->mtime, db, sizeof db);
        printf("   existing: %s, modified %s\n   incoming: %s, modified %s\n", a, da, b, db);
        if (st.size == e->size && files_identical(dst, e->path) == 1)
            printf("   " C_GREEN "The two files are identical." C_RESET "\n");
    } else {
        printf("   (another file in this run wants the same name)\n");
    }
    prompt_line("   [R]ename & keep both  [S]kip  [O]verwrite (old one -> trash)\n"
                "   add ! to apply to all conflicts, e.g. R! : ", buf, sizeof buf);
    *apply_all = strchr(buf, '!') != NULL;
    switch (tolower((unsigned char)buf[0])) {
    case 's': return CONFLICT_SKIP;
    case 'o': return CONFLICT_REPLACE;
    default:  return CONFLICT_RENAME;           /* safest default */
    }
}

/* ----------------------------- the plan ----------------------------- */

static size_t build_plan(const OrgConfig *c, const FileList *fl, Plan *p)
{
    Conflict remembered = CONFLICT_ASK;
    size_t i, skipped = 0;

    for (i = 0; i < fl->count; i++) {
        const FileEntry *e = &fl->items[i];
        char folder[OPATH], dir[OPATH], dst[OPATH];

        if (!passes_filter(c, e) || !target_folder(c, e, folder, sizeof folder))
            continue;
        path_join(dir, sizeof dir, c->dst, folder);
        path_join(dst, sizeof dst, dir, e->name);
        if (path_eq(dst, e->path))
            continue;                               /* already in the right place */

        if (plat_exists(dst) || plan_claims(p, dst)) {
            Conflict choice = remembered;
            if (choice == CONFLICT_ASK) {
                int all = 0;
                choice = ask_conflict(e, dst, &all);
                if (all)
                    remembered = choice;
            }
            if (choice == CONFLICT_SKIP) {
                skipped++;
                continue;
            }
            if (choice == CONFLICT_REPLACE && plat_exists(dst) && !plan_claims(p, dst)) {
                PlatStat st;
                plat_stat(dst, &st);
                plan_add_trash(p, dst, c->dst, st.size, "replaced");
            } else {
                unique_name(p, dst, sizeof dst);
            }
        }
        plan_add_move(p, e->path, dst, e->size);
    }
    return skipped;
}

/* ------------------------------ questions ------------------------------ */

static void print_breakdown(const FileList *fl)
{
    size_t counts[NCAT + 1] = { 0 }, i;
    unsigned long long bytes[NCAT + 1] = { 0 };
    char sz[32];
    int k;
    for (i = 0; i < fl->count; i++) {
        k = category_of(fl->items[i].name);
        counts[k]++;
        bytes[k] += fl->items[i].size;
    }
    printf("  %-12s %7s %11s\n", "Type", "Files", "Size");
    for (k = 0; k <= NCAT; k++)
        if (counts[k]) {
            human_size(bytes[k], sz, sizeof sz);
            printf("  %-12s %7zu %11s\n", category_name(k), counts[k], sz);
        }
}

static int ask_date(const char *prompt, time_t *out)
{
    char buf[64];
    for (;;) {
        if (!prompt_line(prompt, buf, sizeof buf) || !buf[0])
            return 0;
        if (parse_date(buf, out))
            return 1;
        ui_warn("Please use the format YYYY-MM-DD, e.g. 2026-08-03.");
    }
}

static void ask_date_filter(OrgConfig *c)
{
    time_t now = time(NULL);
    printf("\nOnly include files modified...\n"
           "  1) any time\n  2) in the last 7 days\n  3) in the last 30 days\n  4) between two dates\n");
    switch (prompt_int("Choice [1]: ", 1, 4, 1)) {
    case 2:
        c->use_dates = 1;
        c->from = now - 7 * 86400;
        c->to = now + 86400;
        break;
    case 3:
        c->use_dates = 1;
        c->from = now - 30 * 86400;
        c->to = now + 86400;
        break;
    case 4:
        c->use_dates = 1;
        c->from = 0;
        c->to = now + 86400;
        ask_date("  From (YYYY-MM-DD, blank = no limit): ", &c->from);
        if (ask_date("  To   (YYYY-MM-DD, blank = today): ", &c->to))
            c->to += 86399;                         /* include the whole last day */
        break;
    default:
        break;
    }
}

typedef struct {
    char ext[32];
    size_t count;
} ExtCount;

static int cmp_extcount(const void *a, const void *b)
{
    const ExtCount *x = a, *y = b;
    return (x->count < y->count) - (x->count > y->count);
}

/* "Checkbox" style: pick by number, or type the extensions. */
static void pick_extensions(const FileList *fl, char *out, size_t n)
{
    ExtCount *ec = NULL;
    size_t nec = 0, cap = 0, i, j;
    char buf[512], *tok;

    for (i = 0; i < fl->count; i++) {
        char ext[32];
        path_ext_lower(fl->items[i].name, ext, sizeof ext);
        if (!ext[0])
            snprintf(ext, sizeof ext, "(none)");
        for (j = 0; j < nec && strcmp(ec[j].ext, ext) != 0; j++)
            ;
        if (j == nec) {
            if (nec == cap) {
                cap = cap ? cap * 2 : 32;
                ec = xrealloc(ec, cap * sizeof *ec);
            }
            snprintf(ec[nec].ext, sizeof ec[nec].ext, "%s", ext);
            ec[nec++].count = 0;
        }
        ec[j].count++;
    }
    qsort(ec, nec, sizeof *ec, cmp_extcount);

    printf("\nExtensions in this folder:\n");
    for (i = 0; i < nec; i++)
        printf("  %2zu) %-10s %5zu%s", i + 1, ec[i].ext, ec[i].count, (i % 3 == 2) ? "\n" : "   ");
    printf("\n");
    prompt_line("Tick by number (e.g. 1 3 4) or type extensions (e.g. pdf docx): ", buf, sizeof buf);

    out[0] = '\0';
    for (tok = strtok(buf, " ,;"); tok; tok = strtok(NULL, " ,;")) {
        char word[40];
        char *end;
        long idx = strtol(tok, &end, 10);
        if (*end == '\0' && idx >= 1 && (size_t)idx <= nec)
            snprintf(word, sizeof word, "%s", ec[idx - 1].ext);
        else
            snprintf(word, sizeof word, "%s", tok[0] == '.' ? tok + 1 : tok);
        str_lower(word);
        if (strlen(out) + strlen(word) + 2 < n) {
            strcat(out, word);
            strcat(out, " ");
        }
    }
    if (out[0])
        printf("Selected: %s\n", out);
    else
        printf("Nothing selected - all types will be organized.\n");
    free(ec);
}

static void ask_name_rules(OrgConfig *c)
{
    char buf[256];
    int i;
    c->nrules = 0;
    printf("\nName rules: a file whose name CONTAINS the keyword goes into that folder.\n"
           "Files that match no rule stay where they are.\n"
           "Add your own as keyword=Folder (e.g. invoice=Bills). Blank line when done.\n");
    while (c->nrules < MAX_RULES && prompt_line("  rule: ", buf, sizeof buf) && buf[0]) {
        char *eq = strchr(buf, '=');
        NameRule *r;
        if (!eq) {
            ui_warn("Use the form keyword=Folder");
            continue;
        }
        *eq++ = '\0';
        str_trim(buf);
        str_trim(eq);
        if (!buf[0] || !eq[0])
            continue;
        r = &c->rules[c->nrules++];
        snprintf(r->keyword, sizeof r->keyword, "%.63s", buf);
        snprintf(r->folder, sizeof r->folder, "%.63s", eq);
        sanitize_folder_name(r->folder);
    }
    printf("Preset rules:");
    for (i = 0; i < NPRESET; i++)
        printf("%s %s->%s", i ? "," : "", PRESET_RULES[i].keyword, PRESET_RULES[i].folder);
    printf("\n");
    if (prompt_yes_no("Use the preset rules too?", 1))
        for (i = 0; i < NPRESET && c->nrules < MAX_RULES; i++)
            c->rules[c->nrules++] = PRESET_RULES[i];
}

static void ask_custom_names(OrgConfig *c, const FileList *fl)
{
    int used[NCAT + 1] = { 0 }, k;
    size_t i;
    char buf[64], prompt[96];
    if (!prompt_yes_no("Rename any type folders (e.g. Images -> Photos)?", 0))
        return;
    for (i = 0; i < fl->count; i++)
        if (passes_filter(c, &fl->items[i]))
            used[category_of(fl->items[i].name)] = 1;
    printf("Press Enter to keep a name.\n");
    for (k = 0; k <= NCAT; k++) {
        if (!used[k])
            continue;
        snprintf(prompt, sizeof prompt, "  %s -> ", c->cat_names[k]);
        if (prompt_line(prompt, buf, sizeof buf) && buf[0]) {
            sanitize_folder_name(buf);
            snprintf(c->cat_names[k], sizeof c->cat_names[k], "%s", buf);
        }
    }
}

static void ask_week_start(OrgConfig *c, const FileList *fl)
{
    time_t earliest = 0;
    char def[16], prompt[96];
    size_t i;
    for (i = 0; i < fl->count; i++)
        if (passes_filter(c, &fl->items[i]) && (!earliest || fl->items[i].mtime < earliest))
            earliest = fl->items[i].mtime;
    if (!earliest)
        earliest = time(NULL);
    format_date(earliest, def, sizeof def);
    parse_date(def, &c->week_start);                /* midnight of the earliest day */
    snprintf(prompt, sizeof prompt, "Week1 starts on (YYYY-MM-DD) [%s]: ", def);
    ask_date(prompt, &c->week_start);
}

static void ask_sub_grouping(OrgConfig *c)
{
    printf("\nInside each date folder, also sort files by:\n"
           "  0) nothing\n  1) type (Images, Documents...)\n  2) extension\n  3) name rules\n  4) size\n");
    switch (prompt_int("Choice [0]: ", 0, 4, 0)) {
    case 1: c->group_by = GROUP_TYPE; break;
    case 2: c->group_by = GROUP_EXT; break;
    case 3: c->group_by = GROUP_NAME; ask_name_rules(c); break;
    case 4: c->group_by = GROUP_SIZE; break;
    default: c->group_by = GROUP_NONE; break;
    }
}

static void ask_date_grouping(OrgConfig *c, const FileList *fl)
{
    printf("\nCreate date folders by (based on each file's last-modified date):\n"
           "  1) Month          e.g. 2026-10 October\n"
           "  2) Year           e.g. 2026\n"
           "  3) Day            e.g. 2026-10-10\n"
           "  4) Week of year   e.g. 2026 Week 41\n"
           "  5) Week1, Week2, Week3... counted from a start date (great for courses)\n");
    switch (prompt_int("Choice [1]: ", 1, 5, 1)) {
    case 2: c->date_by = DATE_YEAR; break;
    case 3: c->date_by = DATE_DAY; break;
    case 4: c->date_by = DATE_WEEK_OF_YEAR; break;
    case 5: c->date_by = DATE_WEEK_NUMBERED; ask_week_start(c, fl); break;
    default: c->date_by = DATE_MONTH; break;
    }
    ask_sub_grouping(c);
}

static void ask_filters(OrgConfig *c, const FileList *fl)
{
    size_t i, matching = 0;
    ask_date_filter(c);
    if (prompt_yes_no("Only organize certain file types?", 0))
        pick_extensions(fl, c->exts, sizeof c->exts);
    for (i = 0; i < fl->count; i++)
        matching += passes_filter(c, &fl->items[i]);
    printf("%zu of %zu files match your filters.\n", matching, fl->count);
}

static void ask_grouping(OrgConfig *c, const FileList *fl)
{
    printf("\nGroup the files by:\n"
           "  1) Type        Images/, Documents/, Videos/...\n"
           "  2) Extension   PDF/, JPG/, ZIP/...\n"
           "  3) Name rules  'invoice' -> Invoices/, 'screenshot' -> Screenshots/...\n"
           "  4) Size        Small/, Medium/, Large/, Huge/\n"
           "  5) Date        Week1/, 2026-10 October/... (optionally sorted inside)\n");
    switch (prompt_int("Choice [1]: ", 1, 5, 1)) {
    case 2: c->group_by = GROUP_EXT; break;
    case 3: c->group_by = GROUP_NAME; ask_name_rules(c); break;
    case 4: c->group_by = GROUP_SIZE; break;
    case 5: ask_date_grouping(c, fl); break;
    default: c->group_by = GROUP_TYPE; ask_custom_names(c, fl); break;
    }
}

/* -------------------------------- entry -------------------------------- */

void organize_run(const char *preset_root, OrganizeStyle style)
{
    static const char *TITLES[] = { "Quick tidy - sort by file type", "Organize by date",
                                    "Advanced organizer" };
    OrgConfig *c = xmalloc(sizeof *c);           /* large struct: keep it off the stack */
    FileList fl;
    Plan plan;
    char def[OPATH];
    size_t skipped;

    config_init(c);
    ui_title(TITLES[style]);
    if (preset_root) {
        snprintf(c->src, sizeof c->src, "%s", preset_root);
    } else {
        default_downloads(def, sizeof def);
        if (!prompt_folder("Which folder should be organized?", def, c->src, sizeof c->src)) {
            free(c);
            return;
        }
    }
    snprintf(c->dst, sizeof c->dst, "%s", c->src);

    if (style == ORGANIZE_ADVANCED) {
        if (prompt_yes_no("Create the new folders somewhere else?", 0)
            && !prompt_folder("Destination folder", c->src, c->dst, sizeof c->dst)) {
            free(c);
            return;
        }
        c->recursive = prompt_yes_no("Include files inside subfolders? (they get pulled into the new folders)", 0);
    }

    filelist_init(&fl);
    filelist_scan(&fl, c->src, c->recursive, 0);
    if (fl.count == 0) {
        ui_info("No files found in %s.", c->src);
        filelist_free(&fl);
        free(c);
        return;
    }
    printf("Found %zu files:\n", fl.count);
    print_breakdown(&fl);

    switch (style) {
    case ORGANIZE_QUICK:    c->group_by = GROUP_TYPE; break;
    case ORGANIZE_BY_DATE:  ask_date_grouping(c, &fl); break;
    case ORGANIZE_ADVANCED: ask_filters(c, &fl); ask_grouping(c, &fl); break;
    }

    plan_init(&plan, c->src);
    skipped = build_plan(c, &fl, &plan);
    if (skipped)
        ui_info("%zu files skipped because of name conflicts.", skipped);
    plan_review_and_run(&plan, c->src);

    plan_free(&plan);
    filelist_free(&fl);
    free(c);
}
