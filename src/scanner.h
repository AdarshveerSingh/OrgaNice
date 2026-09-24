#ifndef SCANNER_H
#define SCANNER_H

#include "file_info.h"

/*
 * Scans a directory recursively and collects information
 * about files and directories found inside it.
 *
 * Returns:
 *   0  - success
 *  -1  - error
 */
int scan_directory(const char *root_path);

#endif