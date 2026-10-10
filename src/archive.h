/*
 * archive.h - Feature 2 (part): pack files that haven't been used for a
 * long time into one compressed archive instead of deleting them.
 */
#ifndef ORGANICE_ARCHIVE_H
#define ORGANICE_ARCHIVE_H

void archive_run(const char *preset_root, int advanced);

#endif
