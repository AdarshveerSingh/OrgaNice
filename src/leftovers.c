/*
 * leftovers.c - cleaning up setup and ZIP files intelligently.
 *
 * ZIPs: instead of guessing from the name, OrgaNice reads the archive's
 * central directory (name, size and CRC-32 of every file inside) and looks
 * for each of those files anywhere in the searched folders. A match only
 * counts when the CRC-32 of the file on disk equals the CRC stored in the
 * ZIP, so "extracted" means the contents really are on disk.
 *
 * Installers: the file name is reduced to a product key
 * ("VSCodeSetup-x64-1.85.exe" -> "vscode") and compared with the programs
 * listed in the Windows "Uninstall" registry keys. Older versions of an
 * installer that also exists in a newer version are flagged as superseded.
 */
#include "leftovers.h"
#include "hash.h"
#include "plan.h"
#include "platform.h"
#include "scanner.h"
#include "utils.h"
#include "zipfile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRC_VERIFY_LIMIT (512ULL << 20)   /* bigger files: name + size is enough */
#define MAX_EXTRA 8
#define NIL ((size_t)-1)

typedef enum { K_ZIP, K_INSTALLER, K_PARTIAL } Kind;
typedef enum { V_SAFE, V_PARTIAL, V_KEEP } Verdict;

typedef struct {
    size_t idx;                 /* file in the scan list */
    Kind kind;
    Verdict verdict;
    char key[128];              /* installers: product key */
    char version[32];
    char detail[OPATH + 160];
} Finding;

typedef struct {
    Finding *items;
    size_t count, cap;
} FindingList;

typedef struct {
    char *norm;                 /* "googlechrome" */
    char *name;                 /* "Google Chrome" */
    char *version;
} Program;

typedef struct {
    Program *items;
    size_t count, cap;
} ProgramList;

/* Hash index: (lower-case file name, size) -> files with that name & size. */
typedef struct {
    size_t *head, *next, nbuckets;
} NameIndex;

/* ------------------------------ name index ------------------------------ */

static size_t name_bucket(const NameIndex *ix, const char *name, unsigned long long size)
{
    uint64_t h = FNV_OFFSET;
    for (; *name; name++) {
        unsigned char ch = (unsigned char)tolower((unsigned char)*name);
        h = fnv1a64(&ch, 1, h);
    }
    h = fnv1a64(&size, sizeof size, h);
    return (size_t)(h & (ix->nbuckets - 1));
}

static void index_build(NameIndex *ix, const FileList *fl)
{
    size_t i;
    ix->nbuckets = 16;
    while (ix->nbuckets < fl->count * 2)
        ix->nbuckets <<= 1;
    ix->head = xmalloc(ix->nbuckets * sizeof *ix->head);
    ix->next = xmalloc((fl->count + 1) * sizeof *ix->next);
    for (i = 0; i < ix->nbuckets; i++)
        ix->head[i] = NIL;
    for (i = 0; i < fl->count; i++) {
        size_t b = name_bucket(ix, fl->items[i].name, fl->items[i].size);
        ix->next[i] = ix->head[b];
        ix->head[b] = i;
    }
}

/* ---------------------------------- ZIP ---------------------------------- */

/* The folder the archive was extracted into: walk up one level per
 * path component of the entry ("proj/src/a.c" -> 3 levels). */
static void extraction_root(const char *found, const char *entry, char *out, size_t n)
{
    int depth = 0, i;
    const char *p;
    for (p = entry; *p; p++)
        depth += (*p == '/' || *p == '\\');
    snprintf(out, n, "%s", found);
    for (i = 0; i <= depth; i++)
        path_dirname(out, out, n);
}

static void check_zip(const FileList *fl, const NameIndex *ix, Finding *f)
{
    const FileEntry *zf = &fl->items[f->idx];
    ZipInfo z;
    size_t i, found = 0;
    unsigned long long total = 0, got = 0;
    char where[OPATH] = "";
    double ratio;
    int rc = zip_read(zf->path, &z);

    f->verdict = V_KEEP;
    if (rc != 0) {
        snprintf(f->detail, sizeof f->detail, "%s",
                 rc == -2 ? "ZIP64 archive - not supported yet" : "could not read it (damaged or not a real ZIP)");
        return;
    }
    if (z.count == 0) {
        snprintf(f->detail, sizeof f->detail, "the archive contains no files");
        zip_free(&z);
        return;
    }
    for (i = 0; i < z.count; i++) {
        const ZipEntry *ze = &z.items[i];
        const char *base = path_basename(ze->name);
        size_t j;
        total += ze->size;
        for (j = ix->head[name_bucket(ix, base, ze->size)]; j != NIL; j = ix->next[j]) {
            const FileEntry *e = &fl->items[j];
            uint32_t crc;
            if (j == f->idx || e->size != ze->size || !str_ieq(e->name, base))
                continue;
            if (e->size <= CRC_VERIFY_LIMIT && (crc32_file(e->path, &crc) != 0 || crc != ze->crc))
                continue;                           /* same name, different content */
            found++;
            got += ze->size;
            if (!where[0])
                extraction_root(e->path, ze->name, where, sizeof where);
            break;
        }
    }
    ratio = total ? (double)got / (double)total : (double)found / (double)z.count;
    if (ratio >= 0.95)
        f->verdict = V_SAFE;
    else if (ratio >= 0.5)
        f->verdict = V_PARTIAL;

    if (found)
        snprintf(f->detail, sizeof f->detail, "%zu of %zu files found & CRC-verified (%.0f%% of data) in %s",
                 found, z.count, ratio * 100.0, where);
    else
        snprintf(f->detail, sizeof f->detail, "none of its %zu files are on disk - probably never extracted",
                 z.count);
    zip_free(&z);
}

/* ------------------------------- installers ------------------------------- */

static void normalize_alnum(const char *s, char *out, size_t n)
{
    size_t k = 0;
    for (; *s && k + 1 < n; s++)
        if (isalnum((unsigned char)*s))
            out[k++] = (char)tolower((unsigned char)*s);
    out[k] = '\0';
}

static void collect_program(const char *name, const char *version, void *ctx)
{
    ProgramList *pl = ctx;
    char norm[512];
    Program *p;
    normalize_alnum(name, norm, sizeof norm);
    if (!norm[0])
        return;
    if (pl->count == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 128;
        pl->items = xrealloc(pl->items, pl->cap * sizeof *pl->items);
    }
    p = &pl->items[pl->count++];
    p->norm = xstrdup(norm);
    p->name = xstrdup(name);
    p->version = xstrdup(version);
}

/* "python-3.12.1-amd64" -> "3.12.1" */
static void extract_version(const char *s, char *out, size_t n)
{
    out[0] = '\0';
    for (; *s; s++) {
        const char *e;
        int dots = 0;
        if (!isdigit((unsigned char)*s))
            continue;
        for (e = s; isdigit((unsigned char)*e) || (*e == '.' && isdigit((unsigned char)e[1])); e++)
            dots += *e == '.';
        if (dots) {
            size_t len = (size_t)(e - s);
            if (len >= n)
                len = n - 1;
            memcpy(out, s, len);
            out[len] = '\0';
            return;
        }
        s = e - 1;
    }
}

/* "ChromeSetup" -> "chrome", "python-3.12.1-amd64" -> "python" */
static void installer_key(const char *stem, char *key, size_t n)
{
    static const char *NOISE_WORDS = "setup installer install x64 x86 amd64 arm64 win64 win32 win windows "
                                     "64bit 32bit bit user full offline online web latest release stable "
                                     "final msi exe bootstrapper signed en us";
    static const char *NOISE_PARTS[] = { "setup", "installer", "install", "x64", "x86", "win64", "win32", NULL };
    const char *p = stem;
    char tok[128];

    key[0] = '\0';
    while (*p) {
        size_t len = 0;
        int i, digits_only = 1;
        while (*p && !isalnum((unsigned char)*p))
            p++;
        while (p[len] && isalnum((unsigned char)p[len]) && len < sizeof tok - 1) {
            tok[len] = (char)tolower((unsigned char)p[len]);
            digits_only &= isdigit((unsigned char)p[len]) != 0;
            len++;
        }
        tok[len] = '\0';
        p += len;
        if (!len || digits_only || (tok[0] == 'v' && isdigit((unsigned char)tok[1]))
            || str_in_wordlist(tok, NOISE_WORDS))
            continue;
        for (i = 0; NOISE_PARTS[i]; i++) {          /* "steamsetup" -> "steam" */
            char *q;
            while ((q = strstr(tok, NOISE_PARTS[i])) != NULL)
                memmove(q, q + strlen(NOISE_PARTS[i]), strlen(q + strlen(NOISE_PARTS[i])) + 1);
        }
        len = strlen(tok);
        while (len > 0 && isdigit((unsigned char)tok[len - 1]))
            tok[--len] = '\0';                      /* "7z2301" -> "7z" */
        if (len && strlen(key) + len + 1 < n)
            strcat(key, tok);
    }
}

static const Program *match_program(const ProgramList *pl, const char *key)
{
    size_t i;
    if (strlen(key) < 3)
        return NULL;
    for (i = 0; i < pl->count; i++)
        if (strstr(pl->items[i].norm, key))
            return &pl->items[i];
    return NULL;
}

static int version_cmp(const char *a, const char *b)
{
    for (;;) {
        char *ea, *eb;
        long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*ea != '.' && *eb != '.')
            return 0;
        a = (*ea == '.') ? ea + 1 : ea;
        b = (*eb == '.') ? eb + 1 : eb;
    }
}

static int is_newer(const FileList *fl, const Finding *a, const Finding *b)
{
    if (a->version[0] && b->version[0])
        return version_cmp(a->version, b->version) > 0;
    return fl->items[a->idx].mtime > fl->items[b->idx].mtime;
}

static void check_installers(const FileList *fl, FindingList *fs, const ProgramList *pl, int have_programs)
{
    size_t i, j;
    for (i = 0; i < fs->count; i++)
        if (fs->items[i].kind == K_INSTALLER) {
            char stem[256];
            path_stem(fl->items[fs->items[i].idx].name, stem, sizeof stem);
            installer_key(stem, fs->items[i].key, sizeof fs->items[i].key);
            extract_version(stem, fs->items[i].version, sizeof fs->items[i].version);
        }

    for (i = 0; i < fs->count; i++) {
        Finding *f = &fs->items[i];
        const Program *p;
        const Finding *newer = NULL;
        if (f->kind != K_INSTALLER)
            continue;
        if (have_programs && (p = match_program(pl, f->key)) != NULL) {
            f->verdict = V_SAFE;
            snprintf(f->detail, sizeof f->detail, "already installed as \"%s\"%s%s",
                     p->name, p->version[0] ? " v" : "", p->version);
            continue;
        }
        for (j = 0; j < fs->count && !newer; j++)
            if (j != i && fs->items[j].kind == K_INSTALLER && f->key[0]
                && strcmp(fs->items[j].key, f->key) == 0 && is_newer(fl, &fs->items[j], f))
                newer = &fs->items[j];
        if (newer) {
            f->verdict = V_SAFE;
            snprintf(f->detail, sizeof f->detail, "older version - newer installer %s is also here",
                     fl->items[newer->idx].name);
        } else {
            f->verdict = V_KEEP;
            snprintf(f->detail, sizeof f->detail, "%s",
                     have_programs ? "not found among installed programs - keep it if you still need it"
                                   : "installed-program check is only available on Windows");
        }
    }
}

/* ---------------------------- partial downloads ---------------------------- */

static void check_partial(const FileList *fl, Finding *f)
{
    double days = difftime(time(NULL), fl->items[f->idx].mtime) / 86400.0;
    if (days >= 1.0) {
        f->verdict = V_SAFE;
        snprintf(f->detail, sizeof f->detail, "unfinished download, untouched for %.0f days", days);
    } else {
        f->verdict = V_KEEP;
        snprintf(f->detail, sizeof f->detail, "modified today - may still be downloading");
    }
}

/* -------------------------------- report -------------------------------- */

static int kind_of(const char *name, Kind *k)
{
    char ext[32];
    path_ext_lower(name, ext, sizeof ext);
    if (strcmp(ext, "zip") == 0)
        *k = K_ZIP;
    else if (str_in_wordlist(ext, "exe msi msix appx"))
        *k = K_INSTALLER;
    else if (str_in_wordlist(ext, "crdownload part partial download opdownload"))
        *k = K_PARTIAL;
    else
        return 0;
    return 1;
}

static int cmp_kind(const void *a, const void *b)
{
    const Finding *x = a, *y = b;
    if (x->kind != y->kind)
        return (int)x->kind - (int)y->kind;
    return (int)x->verdict - (int)y->verdict;
}

static const char *verdict_tag(Verdict v)
{
    switch (v) {
    case V_SAFE:    return C_GREEN "SAFE   " C_RESET;
    case V_PARTIAL: return C_YELLOW "PARTIAL" C_RESET;
    default:        return C_DIM "KEEP   " C_RESET;
    }
}

static void print_report(const FileList *fl, const FindingList *fs,
                         unsigned long long *safe_bytes, size_t *safe_count)
{
    static const char *HEADINGS[] = { "ZIP archives", "Installers", "Unfinished downloads" };
    int last = -1;
    size_t i;
    char sz[32];
    for (i = 0; i < fs->count; i++) {
        const Finding *f = &fs->items[i];
        const FileEntry *e = &fl->items[f->idx];
        if ((int)f->kind != last) {
            last = (int)f->kind;
            printf("\n" C_BOLD "%s" C_RESET "\n", HEADINGS[last]);
        }
        human_size(e->size, sz, sizeof sz);
        printf("  [%2zu] %s %-38s %10s\n         " C_DIM "%s" C_RESET "\n",
               i + 1, verdict_tag(f->verdict), e->name, sz, f->detail);
        if (f->verdict == V_SAFE) {
            *safe_bytes += e->size;
            (*safe_count)++;
        }
    }
}

/* --------------------------------- entry --------------------------------- */

static void add_finding(FindingList *fs, size_t idx, Kind k)
{
    if (fs->count == fs->cap) {
        fs->cap = fs->cap ? fs->cap * 2 : 32;
        fs->items = xrealloc(fs->items, fs->cap * sizeof *fs->items);
    }
    memset(&fs->items[fs->count], 0, sizeof *fs->items);
    fs->items[fs->count].idx = idx;
    fs->items[fs->count].kind = k;
    fs->count++;
}

static int overlaps(const char *dir, const char *target, char extra[][OPATH], int nextra)
{
    int i;
    if (path_has_prefix(dir, target) || path_has_prefix(target, dir))
        return 1;
    for (i = 0; i < nextra; i++)
        if (path_has_prefix(dir, extra[i]) || path_has_prefix(extra[i], dir))
            return 1;
    return 0;
}

static const char *trash_reason(Kind k)
{
    switch (k) {
    case K_ZIP:       return "already extracted";
    case K_INSTALLER: return "installer not needed";
    default:          return "unfinished download";
    }
}

void leftovers_run(const char *preset_root, int advanced)
{
    char target[OPATH], def[OPATH], buf[OPATH], dir[OPATH], sz[32];
    char (*extra)[OPATH] = xmalloc(MAX_EXTRA * sizeof *extra);
    int nextra = 0, recursive = 0, have_programs, r, choice;
    FileList fl;
    NameIndex ix = { NULL, NULL, 0 };
    ProgramList pl = { NULL, 0, 0 };
    FindingList fs = { NULL, 0, 0 };
    unsigned long long safe_bytes = 0;
    size_t i, safe_count = 0;
    Plan plan;

    ui_title("Leftover installers & ZIP files");
    printf("For each ZIP, OrgaNice checks whether its contents already exist on disk\n"
           "(every file matched by name, size and CRC-32). For each installer it checks\n"
           "whether the program is installed or a newer installer is present.\n\n");

    if (preset_root) {
        snprintf(target, sizeof target, "%s", preset_root);
    } else {
        default_downloads(def, sizeof def);
        if (!prompt_folder("Folder with the ZIPs/installers", def, target, sizeof target)) {
            free(extra);
            return;
        }
    }
    if (advanced)
        recursive = prompt_yes_no("Also check ZIPs and installers inside subfolders?", 0);

    if (!preset_root && advanced) {
        printf("Where else might things have been extracted? (e.g. D:\\Projects)\n");
        while (nextra < MAX_EXTRA && prompt_line("  extra folder (blank = done): ", buf, sizeof buf) && buf[0]) {
            path_normalize(buf);
            if (!plat_is_dir(buf))
                ui_warn("Not a folder: %s", buf);
            else if (overlaps(buf, target, extra, nextra))
                ui_warn("That overlaps with a folder already being searched.");
            else
                snprintf(extra[nextra++], OPATH, "%s", buf);
        }
    } else if (!preset_root && prompt_yes_no("Also look for extracted files in Desktop and Documents?", 1)) {
        static const char *SUBS[] = { "Desktop", "Documents" };
        for (r = 0; r < 2; r++) {
            home_folder(SUBS[r], buf, sizeof buf);
            if (plat_is_dir(buf) && !overlaps(buf, target, extra, nextra))
                snprintf(extra[nextra++], OPATH, "%s", buf);
        }
    }

    filelist_init(&fl);
    filelist_scan(&fl, target, 1, 0);
    for (r = 0; r < nextra; r++)
        filelist_scan(&fl, extra[r], 1, r + 1);
    index_build(&ix, &fl);
    have_programs = plat_list_installed(collect_program, &pl) == 0;

    for (i = 0; i < fl.count; i++) {
        Kind k;
        if (fl.items[i].root_id != 0 || !kind_of(fl.items[i].name, &k))
            continue;
        if (!recursive) {
            path_dirname(fl.items[i].path, dir, sizeof dir);
            if (!path_eq(dir, target))
                continue;
        }
        add_finding(&fs, i, k);
    }
    if (fs.count == 0) {
        ui_ok("No ZIPs, installers or unfinished downloads found in %s.", target);
        goto out;
    }
    ui_info("Checking %zu items against %zu files on disk...", fs.count, fl.count);
    for (i = 0; i < fs.count; i++) {
        if (fs.items[i].kind == K_ZIP)
            check_zip(&fl, &ix, &fs.items[i]);
        else if (fs.items[i].kind == K_PARTIAL)
            check_partial(&fl, &fs.items[i]);
    }
    check_installers(&fl, &fs, &pl, have_programs);
    qsort(fs.items, fs.count, sizeof *fs.items, cmp_kind);
    print_report(&fl, &fs, &safe_bytes, &safe_count);

    if (safe_count == 0) {
        printf("\n");
        ui_info("Nothing is clearly safe to remove. KEEP / PARTIAL items were left alone.");
        goto out;
    }
    human_size(safe_bytes, sz, sizeof sz);
    printf("\n" C_BOLD "%zu SAFE items, %s reclaimable." C_RESET "\n"
           "  1) Move all SAFE items to the OrgaNice trash (undoable)\n"
           "  2) Choose items by number\n"
           "  0) Nothing for now\n", safe_count, sz);
    choice = prompt_int("Choice [1]: ", 0, 2, 1);
    if (choice == 0)
        goto out;

    plan_init(&plan, target);
    if (choice == 1) {
        for (i = 0; i < fs.count; i++)
            if (fs.items[i].verdict == V_SAFE) {
                const FileEntry *e = &fl.items[fs.items[i].idx];
                plan_add_trash(&plan, e->path, target, e->size, trash_reason(fs.items[i].kind));
            }
    } else {
        char *tok;
        prompt_line("Numbers to remove (e.g. 1 4 5): ", buf, sizeof buf);
        for (tok = strtok(buf, " ,;"); tok; tok = strtok(NULL, " ,;")) {
            long n = strtol(tok, NULL, 10);
            if (n >= 1 && (size_t)n <= fs.count) {
                const FileEntry *e = &fl.items[fs.items[n - 1].idx];
                plan_add_trash(&plan, e->path, target, e->size, trash_reason(fs.items[n - 1].kind));
            }
        }
    }
    plan_review_and_run(&plan, target);
    plan_free(&plan);

out:
    for (i = 0; i < pl.count; i++) {
        free(pl.items[i].norm);
        free(pl.items[i].name);
        free(pl.items[i].version);
    }
    free(pl.items);
    free(fs.items);
    free(ix.head);
    free(ix.next);
    filelist_free(&fl);
    free(extra);
}
