#include <stdio.h>
#include <windows.h>

#include "scanner.h"

static void scan_directory_recursive(const char *path) {
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;

    char search_path[MAX_PATH_LENGTH];

    snprintf(search_path, sizeof(search_path), "%s\\*", path);

    find_handle = FindFirstFileA(search_path, &find_data);

    if (find_handle == INVALID_HANDLE_VALUE) {
        printf("Unable to access: %s\n", path);
        return;
    }

    do {
        const char *name = find_data.cFileName;

        /* Ignore . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_LENGTH];

        snprintf(
            full_path,
            sizeof(full_path),
            "%s\\%s",
            path,
            name
        );

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            printf("[DIR]  %s\n", full_path);

            scan_directory_recursive(full_path);
        } else {
            printf("[FILE] %s\n", full_path);
        }

    } while (FindNextFileA(find_handle, &find_data));

    FindClose(find_handle);
}

int scan_directory(const char *root_path) {
    if (root_path == NULL) {
        return -1;
    }

    scan_directory_recursive(root_path);

    return 0;
    
}