/*
 * duplicates.h - Features 2 & 3: content-based duplicate detection,
 * within one folder or across several folders/drives.
 */
#ifndef ORGANICE_DUPLICATES_H
#define ORGANICE_DUPLICATES_H

/* 1 = identical bytes, 0 = different, -1 = could not read */
int  files_identical(const char *a, const char *b);

/* advanced = multiple folders, size filter, keeper policy */
void duplicates_run(const char *preset_root, int advanced);

#endif
