#ifndef FILE_INFO_H
#define FILE_INFO_H

#include <time.h>

#define MAX_PATH_LENGTH 1024
#define MAX_NAME_LENGTH 256
#define MAX_EXTENSION_LENGTH 32

typedef struct {
    char path[MAX_PATH_LENGTH];
    char name[MAX_NAME_LENGTH];
    char extension[MAX_EXTENSION_LENGTH];

    unsigned long long size;

    time_t created;
    time_t modified;
    time_t accessed;

    int is_directory;
} FileInfo;

#endif