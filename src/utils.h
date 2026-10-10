/*
 * utils.h - small helpers shared by every module:
 * memory, strings, paths, formatting and console input/output.
 */
#ifndef ORGANICE_UTILS_H
#define ORGANICE_UTILS_H

#include <stddef.h>
#include <time.h>

/* ANSI colors (enabled on Windows 10+ by plat_console_init). */
#define C_RESET  "\x1b[0m"
#define C_BOLD   "\x1b[1m"
#define C_DIM    "\x1b[2m"
#define C_RED    "\x1b[31m"
#define C_GREEN  "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_CYAN   "\x1b[36m"

extern int  g_input_closed;    /* stdin reached EOF: menus should exit */
extern char g_last_folder[];   /* last folder the user picked (offered as a default) */

/* memory (exit with a message instead of returning NULL) */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* strings */
void str_lower(char *s);
void str_trim(char *s);
int  str_ieq(const char *a, const char *b);
int  str_icontains(const char *hay, const char *needle);
int  str_in_wordlist(const char *word, const char *list);   /* list = "jpg png gif" */
void sanitize_folder_name(char *s);

/* paths */
const char *path_basename(const char *p);
const char *path_ext(const char *p);                       /* "" when there is none */
void path_ext_lower(const char *p, char *out, size_t n);
void path_stem(const char *p, char *out, size_t n);         /* name without extension */
void path_dirname(const char *p, char *out, size_t n);      /* out may equal p */
void path_join(char *out, size_t n, const char *dir, const char *name);
void path_normalize(char *p);                               /* strip quotes / trailing slash */
int  path_eq(const char *a, const char *b);
int  path_has_prefix(const char *p, const char *dir);
const char *path_relative(const char *p, const char *root);
void home_folder(const char *sub, char *out, size_t n);
void default_downloads(char *out, size_t n);

/* formatting */
void human_size(unsigned long long bytes, char *out, size_t n);
void format_date(time_t t, char *out, size_t n);           /* YYYY-MM-DD */
int  parse_date(const char *s, time_t *out);                /* YYYY-MM-DD -> local midnight */

/* console output */
void ui_title(const char *title);
void ui_ok(const char *fmt, ...);
void ui_info(const char *fmt, ...);
void ui_warn(const char *fmt, ...);
void ui_err(const char *fmt, ...);

/* console input */
int  prompt_line(const char *prompt, char *buf, size_t n);  /* 0 on EOF */
int  prompt_int(const char *prompt, int min, int max, int def);
int  prompt_yes_no(const char *question, int def);
int  prompt_folder(const char *question, const char *def, char *out, size_t n);
void press_enter(void);

#endif
