/*
 * archive.c - archives old, unused files.
 *
 * "Unused" = not modified for N days (presets: 6 months, 1 year, 2 years,
 * custom). The last-accessed date is deliberately NOT used: Windows updates
 * it unreliably, and virus scanners, indexers and OrgaNice's own scans can
 * make untouched files look recently opened. Files that are already compressed (photos,
 * videos, zips, .docx...) are skipped by default because packing them
 * saves almost nothing.
 *
 * Compression uses the system 'tar' tool (built into Windows 10+, creates a
 * .zip there; .tar.gz on Linux/macOS). Writing a DEFLATE compressor is out
 * of scope for this project. Safety steps:
 *   1. the archive is listed back with 'tar -t' and its entry count verified
 *   2. only then are the originals moved to the OrgaNice trash (undoable)
 */
#include "archive.h"
#include "plan.h"
#include "platform.h"
#include "scanner.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALREADY_COMPRESSED "zip rar 7z gz bz2 xz tgz zst jpg jpeg png gif webp heic mp4 mkv mov avi " \
                           "webm m4v mp3 aac ogg opus m4a flac docx xlsx pptx epub apk jar"

#ifdef _WIN32
#define ARCHIVE_EXT "zip"
#define TAR_CREATE "tar -a -c -f"
#else
#define ARCHIVE_EXT "tar.gz"
#define TAR_CREATE "tar -czf"
#endif

static const FileList *s_list;

static int cmp_size_desc(const void *a, const void *b)
{
    unsigned long long x = s_list->items[*(const size_t *)a].size;
    unsigned long long y = s_list->items[*(const size_t *)b].size;
    return (x < y) - (x > y);
}

static int tar_available(void)
{
    char cmd[64];
    snprintf(cmd, sizeof cmd, "tar --version > %s 2>&1", plat_null_device());
    return system(cmd) == 0;
}

/* Lists the archive back and counts the files in it. */
static long count_archive_entries(const char *archive, const char *meta)
{
    char out_path[OPATH], cmd[3 * OPATH], line[OPATH];
    FILE *f;
    long n = 0;
    path_join(out_path, sizeof out_path, meta, "archive_check.txt");
    snprintf(cmd, sizeof cmd, "tar -tf \"%s\" > \"%s\"", archive, out_path);
    if (system(cmd) != 0 || (f = fopen(out_path, "r")) == NULL)
        return -1;
    while (fgets(line, sizeof line, f)) {
        size_t len = strcspn(line, "\r\n");
        if (len && line[len - 1] != '/')
            n++;
    }
    fclose(f);
    remove(out_path);
    return n;
}

void archive_run(const char *preset_root, int advanced)
{
    char src[OPATH], dest[OPATH], def[OPATH], meta[OPATH], list_path[OPATH];
    char archive[OPATH], name[96], stamp[32], cmd[4 * OPATH], sz[32], sz2[32], d[16];
    const char *src_arg_suffix;
    FileList fl;
    size_t *pick = NULL, npick = 0, skipped_compressed = 0, too_recent = 0, i;
    unsigned long long total = 0;
    int days, include_compressed = 0;
    time_t cutoff, now = time(NULL);
    FILE *lf;
    long entries;
    PlatStat st;
    Plan plan;

    ui_title("Archive old, unused files");
    printf("Files you haven't touched for a long time are packed into one compressed archive.\n"
           "The archive is verified first; then the originals go to the OrgaNice trash,\n"
           "so everything can still be undone.\n\n");

    if (preset_root) {
        snprintf(src, sizeof src, "%s", preset_root);
    } else {
        default_downloads(def, sizeof def);
        if (!prompt_folder("Folder to look in (subfolders included)", def, src, sizeof src))
            return;
    }
    printf("\nArchive files not modified for:\n  1) 6 months\n  2) 1 year\n  3) 2 years\n  4) custom number of days\n");
    switch (prompt_int("Choice [2]: ", 1, 4, 2)) {
    case 1: days = 182; break;
    case 3: days = 730; break;
    case 4: days = prompt_int("Days: ", 1, 36500, 365); break;
    default: days = 365; break;
    }
    path_join(dest, sizeof dest, src, "OrgaNice_Archives");
    if (advanced) {
        include_compressed = prompt_yes_no("Include already-compressed files (photos, videos, zips)? They barely shrink.", 0);
        if (prompt_yes_no("Save the archive somewhere other than OrgaNice_Archives in this folder?", 0)
            && !prompt_folder("Archive destination", src, dest, sizeof dest))
            return;
    }
    cutoff = now - (time_t)days * 86400;

    filelist_init(&fl);
    filelist_scan(&fl, src, 1, 0);
    pick = xmalloc((fl.count + 1) * sizeof *pick);
    for (i = 0; i < fl.count; i++) {
        const FileEntry *e = &fl.items[i];
        char ext[32];
        if (path_has_prefix(e->path, dest))
            continue;
        if (e->mtime >= cutoff) {                  /* modified recently: still in use */
            too_recent++;
            continue;
        }
        path_ext_lower(e->name, ext, sizeof ext);
        if (!include_compressed && str_in_wordlist(ext, ALREADY_COMPRESSED)) {
            skipped_compressed++;
            continue;
        }
        pick[npick++] = i;
        total += e->size;
    }
    if (skipped_compressed)
        ui_info("%zu old files skipped: already compressed (photos, videos, zips...).", skipped_compressed);
    if (npick == 0) {
        ui_ok("No compressible files unmodified for %d+ days (%zu files are newer, %zu are already compressed).",
              days, too_recent, skipped_compressed);
        goto out;
    }

    s_list = &fl;
    qsort(pick, npick, sizeof *pick, cmp_size_desc);
    human_size(total, sz, sizeof sz);
    printf("\n" C_BOLD "%zu files (%s) haven't been modified in %d+ days." C_RESET " Largest:\n", npick, sz, days);
    for (i = 0; i < npick && i < 10; i++) {
        const FileEntry *e = &fl.items[pick[i]];
        human_size(e->size, sz2, sizeof sz2);
        format_date(e->mtime, d, sizeof d);
        printf("  %10s  %s " C_DIM "(last modified %s)" C_RESET "\n", sz2, path_relative(e->path, src), d);
    }
    if (!prompt_yes_no("\nPack them into a compressed archive?", 1))
        goto out;
    if (!tar_available()) {
        ui_err("The 'tar' tool was not found (it ships with Windows 10 and later).");
        goto out;
    }

    /* list of files for tar, relative to src */
    plan_meta_dir(src, meta, sizeof meta);
    plat_mkdirs(meta);
    plat_mkdirs(dest);
    path_join(list_path, sizeof list_path, meta, "archive_list.txt");
    if ((lf = fopen(list_path, "w")) == NULL) {
        ui_err("Cannot write %s", list_path);
        goto out;
    }
    for (i = 0; i < npick; i++)
        fprintf(lf, "%s\n", path_relative(fl.items[pick[i]].path, src));
    fclose(lf);

    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M", localtime(&now));
    snprintf(name, sizeof name, "OrgaNice_Archive_%s.%s", stamp, ARCHIVE_EXT);
    path_join(archive, sizeof archive, dest, name);
    /* a trailing backslash before a quote would escape it ("C:\" -> "C:\.") */
    src_arg_suffix = (src[strlen(src) - 1] == '\\') ? "." : "";
    snprintf(cmd, sizeof cmd, TAR_CREATE " \"%s\" -C \"%s%s\" -T \"%s\"", archive, src, src_arg_suffix, list_path);

    ui_info("Compressing %zu files...", npick);
    if (system(cmd) != 0 || plat_stat(archive, &st) != 0 || st.size == 0) {
        ui_err("Creating the archive failed. Nothing was changed.");
        plat_remove(archive);
        goto out;
    }
    entries = count_archive_entries(archive, meta);
    if (entries != (long)npick) {
        ui_err("Verification failed (%ld of %zu files in the archive). Originals were NOT touched.",
               entries, npick);
        goto out;
    }
    human_size(total, sz, sizeof sz);
    human_size(st.size, sz2, sizeof sz2);
    ui_ok("Archive verified: %s (%s -> %s)", archive, sz, sz2);
    remove(list_path);

    printf("\nThe originals can now be moved to the OrgaNice trash.\n");
    plan_init(&plan, src);
    for (i = 0; i < npick; i++) {
        const FileEntry *e = &fl.items[pick[i]];
        plan_add_trash(&plan, e->path, src, e->size, "archived");
    }
    plan_review_and_run(&plan, src);
    plan_free(&plan);

out:
    free(pick);
    filelist_free(&fl);
}
