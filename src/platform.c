/*
 * platform.c - the only file that talks to the operating system directly.
 * Windows uses the Win32 API, Linux/macOS use POSIX.
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE
#endif

#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <sys/utime.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#endif

/* Called for each entry of a directory (files and sub-folders). */
typedef int (*ChildFn)(const char *full, const char *name, const PlatStat *st, void *ctx);

static void join(char *out, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
        snprintf(out, n, "%s%s", dir, name);
    else
        snprintf(out, n, "%s%c%s", dir, PATH_SEP, name);
}

/* Folders OrgaNice never walks into. */
static int skip_dir(const char *name)
{
    return strcmp(name, ".organice") == 0
        || strcmp(name, "$RECYCLE.BIN") == 0
        || strcmp(name, "System Volume Information") == 0;
}

#ifdef _WIN32
/* =============================== Windows =============================== */

static time_t filetime_to_unix(FILETIME ft)
{
    const unsigned long long EPOCH_DIFF = 116444736000000000ULL; /* 1601 -> 1970, 100 ns units */
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t < EPOCH_DIFF ? 0 : (time_t)((t - EPOCH_DIFF) / 10000000ULL);
}

void plat_console_init(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode))
        SetConsoleMode(out, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING: ANSI colors */);
}

int plat_stat(const char *path, PlatStat *st)
{
    struct _stat64 s;
    if (_stat64(path, &s) != 0)
        return -1;
    st->size = (unsigned long long)s.st_size;
    st->mtime = (time_t)s.st_mtime;
    st->atime = (time_t)s.st_atime;
    st->is_dir = (s.st_mode & _S_IFDIR) != 0;
    return 0;
}

static int mkdir_one(const char *p) { return _mkdir(p); }

int plat_move(const char *src, const char *dst)
{
    if (plat_exists(dst))
        return -1;                                  /* never overwrite silently */
    return MoveFileExA(src, dst, MOVEFILE_COPY_ALLOWED) ? 0 : -1;
}

int plat_rmdir(const char *path) { return RemoveDirectoryA(path) ? 0 : -1; }

int plat_hardlink(const char *existing, const char *link_path)
{
    return CreateHardLinkA(link_path, existing, NULL) ? 0 : -1;
}

int plat_file_id(const char *path, unsigned long long *volume, unsigned long long *index)
{
    BY_HANDLE_FILE_INFORMATION info;
    BOOL ok;
    HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    if (!ok)
        return -1;
    *volume = info.dwVolumeSerialNumber;
    *index = ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    return 0;
}

int plat_set_mtime(const char *path, time_t t)
{
    struct _utimbuf ub;
    ub.actime = t;
    ub.modtime = t;
    return _utime(path, &ub);
}

static int list_children(const char *dir, ChildFn fn, void *ctx)
{
    char pattern[OPATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int stop = 0;

    join(pattern, sizeof pattern, dir, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char full[OPATH];
        PlatStat st;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;                               /* symlinks / junctions */
        join(full, sizeof full, dir, fd.cFileName);
        st.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        st.mtime = filetime_to_unix(fd.ftLastWriteTime);
        st.atime = filetime_to_unix(fd.ftLastAccessTime);
        st.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        stop = fn(full, fd.cFileName, &st, ctx);
    } while (!stop && FindNextFileA(h, &fd));
    FindClose(h);
    return stop;
}

int plat_fseek64(FILE *f, long long offset, int whence) { return _fseeki64(f, offset, whence); }
long long plat_ftell64(FILE *f) { return _ftelli64(f); }

static void enum_uninstall(HKEY root, const char *sub, REGSAM view, ProgramFn fn, void *ctx)
{
    HKEY h;
    DWORD i;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ | view, &h) != ERROR_SUCCESS)
        return;
    for (i = 0;; i++) {
        char keyname[256];
        DWORD klen = sizeof keyname;
        HKEY sk;
        LONG r = RegEnumKeyExA(h, i, keyname, &klen, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        if (r != ERROR_SUCCESS)
            continue;
        if (RegOpenKeyExA(h, keyname, 0, KEY_READ | view, &sk) == ERROR_SUCCESS) {
            char name[512] = "", version[128] = "";
            DWORD len = sizeof name - 1, type = 0;
            if (RegQueryValueExA(sk, "DisplayName", NULL, &type, (LPBYTE)name, &len) == ERROR_SUCCESS
                && type == REG_SZ && name[0]) {
                len = sizeof version - 1;
                if (RegQueryValueExA(sk, "DisplayVersion", NULL, &type, (LPBYTE)version, &len) != ERROR_SUCCESS
                    || type != REG_SZ)
                    version[0] = '\0';
                fn(name, version, ctx);
            }
            RegCloseKey(sk);
        }
    }
    RegCloseKey(h);
}

int plat_list_installed(ProgramFn fn, void *ctx)
{
    static const char *KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    enum_uninstall(HKEY_LOCAL_MACHINE, KEY, KEY_WOW64_64KEY, fn, ctx);
    enum_uninstall(HKEY_LOCAL_MACHINE, KEY, KEY_WOW64_32KEY, fn, ctx);
    enum_uninstall(HKEY_CURRENT_USER, KEY, 0, fn, ctx);
    return 0;
}

const char *plat_null_device(void) { return "NUL"; }

#else
/* ============================ Linux / macOS ============================ */

void plat_console_init(void) {}

int plat_stat(const char *path, PlatStat *st)
{
    struct stat s;
    if (stat(path, &s) != 0)
        return -1;
    st->size = (unsigned long long)s.st_size;
    st->mtime = s.st_mtime;
    st->atime = s.st_atime;
    st->is_dir = S_ISDIR(s.st_mode);
    return 0;
}

static int mkdir_one(const char *p) { return mkdir(p, 0755); }

int plat_move(const char *src, const char *dst)
{
    if (plat_exists(dst))
        return -1;                                  /* never overwrite silently */
    if (rename(src, dst) == 0)
        return 0;
    if (errno == EXDEV && plat_copy(src, dst) == 0) { /* different drive: copy + delete */
        if (remove(src) == 0)
            return 0;
        remove(dst);
    }
    return -1;
}

int plat_rmdir(const char *path) { return rmdir(path); }

int plat_hardlink(const char *existing, const char *link_path) { return link(existing, link_path); }

int plat_file_id(const char *path, unsigned long long *volume, unsigned long long *index)
{
    struct stat s;
    if (stat(path, &s) != 0)
        return -1;
    *volume = (unsigned long long)s.st_dev;
    *index = (unsigned long long)s.st_ino;
    return 0;
}

int plat_set_mtime(const char *path, time_t t)
{
    struct utimbuf ub;
    ub.actime = t;
    ub.modtime = t;
    return utime(path, &ub);
}

static int list_children(const char *dir, ChildFn fn, void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int stop = 0;
    if (!d)
        return 0;
    while (!stop && (e = readdir(d)) != NULL) {
        char full[OPATH];
        struct stat s;
        PlatStat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        join(full, sizeof full, dir, e->d_name);
        if (lstat(full, &s) != 0 || S_ISLNK(s.st_mode))
            continue;
        if (!S_ISDIR(s.st_mode) && !S_ISREG(s.st_mode))
            continue;
        st.size = (unsigned long long)s.st_size;
        st.mtime = s.st_mtime;
        st.atime = s.st_atime;
        st.is_dir = S_ISDIR(s.st_mode);
        stop = fn(full, e->d_name, &st, ctx);
    }
    closedir(d);
    return stop;
}

int plat_fseek64(FILE *f, long long offset, int whence) { return fseeko(f, (off_t)offset, whence); }
long long plat_ftell64(FILE *f) { return (long long)ftello(f); }

int plat_list_installed(ProgramFn fn, void *ctx)
{
    (void)fn;
    (void)ctx;
    return -1;
}

const char *plat_null_device(void) { return "/dev/null"; }

#endif

/* ============================ shared code ============================ */

int plat_exists(const char *path)
{
    PlatStat st;
    return plat_stat(path, &st) == 0;
}

int plat_is_dir(const char *path)
{
    PlatStat st;
    return plat_stat(path, &st) == 0 && st.is_dir;
}

int plat_remove(const char *path) { return remove(path); }

int plat_mkdirs(const char *path)
{
    char tmp[OPATH];
    size_t i, len;
    snprintf(tmp, sizeof tmp, "%s", path);
    len = strlen(tmp);
    for (i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (!(i == 2 && tmp[1] == ':') && !plat_is_dir(tmp)) {   /* skip "C:" */
                if (mkdir_one(tmp) != 0 && !plat_is_dir(tmp))
                    return -1;
            }
            tmp[i] = saved;
        }
    }
    return 0;
}

int plat_copy(const char *src, const char *dst)
{
    static unsigned char buf[1 << 16];
    PlatStat st;
    FILE *in, *out;
    size_t n;
    int ok = 1;

    if (plat_stat(src, &st) != 0 || (in = fopen(src, "rb")) == NULL)
        return -1;
    if ((out = fopen(dst, "wb")) == NULL) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            break;
        }
    fclose(in);
    if (fclose(out) != 0)
        ok = 0;
    if (!ok) {
        remove(dst);
        return -1;
    }
    plat_set_mtime(dst, st.mtime);
    return 0;
}

typedef struct {
    int recursive;
    WalkFn fn;
    void *ctx;
} WalkState;

static int walk_child(const char *full, const char *name, const PlatStat *st, void *ctx)
{
    WalkState *ws = ctx;
    if (st->is_dir) {
        if (!ws->recursive || skip_dir(name))
            return 0;
        return list_children(full, walk_child, ws);
    }
    return ws->fn(full, st, ws->ctx);
}

int plat_walk(const char *root, int recursive, WalkFn fn, void *ctx)
{
    WalkState ws;
    ws.recursive = recursive;
    ws.fn = fn;
    ws.ctx = ctx;
    return list_children(root, walk_child, &ws);
}

static int remove_child(const char *full, const char *name, const PlatStat *st, void *ctx)
{
    (void)name;
    (void)ctx;
    if (st->is_dir) {
        list_children(full, remove_child, NULL);
        plat_rmdir(full);
    } else {
        plat_remove(full);
    }
    return 0;
}

int plat_remove_tree(const char *path)
{
    list_children(path, remove_child, NULL);
    return plat_rmdir(path);
}
