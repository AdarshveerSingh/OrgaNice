/*
 * sandbox.h - builds a throw-away demo folder full of typical clutter so
 * users can try any feature without risking their real files.
 */
#ifndef ORGANICE_SANDBOX_H
#define ORGANICE_SANDBOX_H

#include <stddef.h>

int  sandbox_create(char *out_path, size_t n);   /* 0 on success */
void sandbox_remove(const char *path);

#endif
