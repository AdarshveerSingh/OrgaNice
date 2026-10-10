/*
 * utils.c - shared helpers (see utils.h).
 */
#include "utils.h"
#include "platform.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  g_input_closed = 0;
char g_last_folder[OPATH] = "";

/* ------------------------------ memory ------------------------------ */

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "OrgaNice: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *old, size_t n)
{
    void *p = realloc(old, n ? n : 1);
    if (!p) {
        fprintf(stderr, "OrgaNice: out of memory\n");
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ------------------------------ strings ----------------------------- */

void str_lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

void str_trim(char *s)
{
    size_t len = strlen(s), start = 0;
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    while (isspace((unsigned char)s[start]))
        start++;
    if (start)
        memmove(s, s + start, len - start + 1);
}

static int prefix_match(const char *s, const char *prefix, size_t n, int icase)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int a = (unsigned char)s[i], b = (unsigned char)prefix[i];
        if (icase) {
            a = tolower(a);
            b = tolower(b);
        }
        if (a != b)
            return 0;
    }
    return 1;
}

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int str_icontains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0)
        return 1;
    for (; *hay; hay++)
        if (prefix_match(hay, needle, n, 1))
            return 1;
    return 0;
}

int str_in_wordlist(const char *word, const char *list)
{
    size_t n = strlen(word);
    if (n == 0)
        return 0;
    while (*list) {
        size_t len;
        while (*list == ' ')
            list++;
        len = strcspn(list, " ");
        if (len == n && prefix_match(list, word, n, 1))
            return 1;
        list += len;
    }
    return 0;
}

void sanitize_folder_name(char *s)
{
    for (; *s; s++)
        if (strchr("\\/:*?\"<>|", *s))
            *s = '_';
}

/* ------------------------------- paths ------------------------------ */

const char *path_basename(const char *p)
{
    const char *base = p, *c;
    for (c = p; *c; c++)
        if (*c == '/' || *c == '\\')
            base = c + 1;
    return base;
}

const char *path_ext(const char *p)
{
    const char *base = path_basename(p), *dot = strrchr(base, '.');
    return (dot && dot != base) ? dot + 1 : "";
}

void path_ext_lower(const char *p, char *out, size_t n)
{
    snprintf(out, n, "%s", path_ext(p));
    str_lower(out);
}

void path_stem(const char *p, char *out, size_t n)
{
    const char *base = path_basename(p), *dot = strrchr(base, '.');
    size_t len = (dot && dot != base) ? (size_t)(dot - base) : strlen(base);
    if (len >= n)
        len = n - 1;
    memmove(out, base, len);
    out[len] = '\0';
}

void path_dirname(const char *p, char *out, size_t n)
{
    const char *base = path_basename(p);
    size_t len = (size_t)(base - p);
    if (len > 1)
        len--;                                  /* drop the trailing separator */
    if (len == 2 && p[1] == ':')
        len = 3;                                /* keep "C:\" */
    if (len == 0) {
        snprintf(out, n, ".");
        return;
    }
    if (len >= n)
        len = n - 1;
    memmove(out, p, len);
    out[len] = '\0';
}

void path_join(char *out, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len == 0)
        snprintf(out, n, "%s", name);
    else if (dir[len - 1] == '/' || dir[len - 1] == '\\')
        snprintf(out, n, "%s%s", dir, name);
    else
        snprintf(out, n, "%s%c%s", dir, PATH_SEP, name);
}

void path_normalize(char *p)
{
    size_t len;
    str_trim(p);
    len = strlen(p);
    /* "Copy as path" on Windows wraps paths in quotes */
    if (len >= 2 && (p[0] == '"' || p[0] == '\'') && p[len - 1] == p[0]) {
        memmove(p, p + 1, len - 2);
        len -= 2;
        p[len] = '\0';
    }
#ifdef _WIN32
    {
        char *c;
        for (c = p; *c; c++)
            if (*c == '/')
                *c = '\\';
    }
#endif
    while (len > 1 && (p[len - 1] == '/' || p[len - 1] == '\\') && !(len == 3 && p[1] == ':'))
        p[--len] = '\0';
}

int path_eq(const char *a, const char *b)
{
#ifdef _WIN32
    return str_ieq(a, b);
#else
    return strcmp(a, b) == 0;
#endif
}

int path_has_prefix(const char *p, const char *dir)
{
    size_t n = strlen(dir);
#ifdef _WIN32
    int icase = 1;
#else
    int icase = 0;
#endif
    if (n == 0 || strlen(p) < n || !prefix_match(p, dir, n, icase))
        return 0;
    if (p[n] == '\0' || p[n] == '/' || p[n] == '\\')
        return 1;
    return dir[n - 1] == '/' || dir[n - 1] == '\\';
}

const char *path_relative(const char *p, const char *root)
{
    if (!path_has_prefix(p, root))
        return p;
    p += strlen(root);
    while (*p == '/' || *p == '\\')
        p++;
    return *p ? p : ".";
}

void home_folder(const char *sub, char *out, size_t n)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !home[0])
        home = ".";
    if (sub && sub[0])
        path_join(out, n, home, sub);
    else
        snprintf(out, n, "%s", home);
}

void default_downloads(char *out, size_t n)
{
    home_folder("Downloads", out, n);
    if (!plat_is_dir(out))
        home_folder(NULL, out, n);
}

/* ---------------------------- formatting ---------------------------- */

void human_size(unsigned long long bytes, char *out, size_t n)
{
    static const char *UNITS[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        i++;
    }
    if (i == 0)
        snprintf(out, n, "%llu B", bytes);
    else
        snprintf(out, n, "%.1f %s", v, UNITS[i]);
}

void format_date(time_t t, char *out, size_t n)
{
    struct tm *tm = localtime(&t);
    if (!tm || !strftime(out, n, "%Y-%m-%d", tm))
        snprintf(out, n, "unknown");
}

int parse_date(const char *s, time_t *out)
{
    int y, m, d;
    struct tm tmv;
    time_t t;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || y < 1970 || y > 3000
        || m < 1 || m > 12 || d < 1 || d > 31)
        return 0;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = y - 1900;
    tmv.tm_mon = m - 1;
    tmv.tm_mday = d;
    tmv.tm_isdst = -1;
    t = mktime(&tmv);
    if (t == (time_t)-1)
        return 0;
    *out = t;
    return 1;
}

/* --------------------------- console output -------------------------- */

static void say(const char *color, const char *tag, const char *fmt, va_list ap)
{
    printf("%s%s" C_RESET " ", color, tag);
    vprintf(fmt, ap);
    printf("\n");
}

void ui_title(const char *title)
{
    printf("\n" C_BOLD C_CYAN "=== %s ===" C_RESET "\n\n", title);
}

void ui_ok(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(C_GREEN, "[ok]", fmt, ap);
    va_end(ap);
}

void ui_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(C_CYAN, "[i]", fmt, ap);
    va_end(ap);
}

void ui_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(C_YELLOW, "[!]", fmt, ap);
    va_end(ap);
}

void ui_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(C_RED, "[x]", fmt, ap);
    va_end(ap);
}

/* --------------------------- console input --------------------------- */

int prompt_line(const char *prompt, char *buf, size_t n)
{
    buf[0] = '\0';
    if (g_input_closed)
        return 0;
    printf(C_BOLD "%s" C_RESET, prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) {
        g_input_closed = 1;
        buf[0] = '\0';
        printf("\n");
        return 0;
    }
    if (!strchr(buf, '\n')) {                  /* line too long: drop the rest */
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    str_trim(buf);
    return 1;
}

int prompt_int(const char *prompt, int min, int max, int def)
{
    char buf[64];
    for (;;) {
        char *end;
        long v;
        if (!prompt_line(prompt, buf, sizeof buf) || buf[0] == '\0')
            return def;
        v = strtol(buf, &end, 10);
        if (*end == '\0' && v >= min && v <= max)
            return (int)v;
        ui_warn("Please enter a number from %d to %d.", min, max);
    }
}

int prompt_yes_no(const char *question, int def)
{
    char buf[16], full[512];
    snprintf(full, sizeof full, "%s %s ", question, def ? "[Y/n]:" : "[y/N]:");
    if (!prompt_line(full, buf, sizeof buf) || buf[0] == '\0')
        return def;
    return tolower((unsigned char)buf[0]) == 'y';
}

int prompt_folder(const char *question, const char *def, char *out, size_t n)
{
    char buf[OPATH], full[OPATH + 128];
    for (;;) {
        if (def && def[0])
            snprintf(full, sizeof full, "%s\n  [Enter = %s]: ", question, def);
        else
            snprintf(full, sizeof full, "%s (blank = cancel): ", question);
        if (!prompt_line(full, buf, sizeof buf))
            return 0;
        if (buf[0] == '\0') {
            if (!def || !def[0])
                return 0;
            snprintf(buf, sizeof buf, "%s", def);
        }
        path_normalize(buf);
        if (plat_is_dir(buf)) {
            snprintf(out, n, "%s", buf);
            snprintf(g_last_folder, OPATH, "%s", buf);
            return 1;
        }
        ui_warn("'%s' is not a folder.", buf);
        def = NULL;                             /* blank now means cancel */
    }
}

void press_enter(void)
{
    char buf[8];
    prompt_line("\nPress Enter to continue...", buf, sizeof buf);
}
