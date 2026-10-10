/*
 * zipfile.h - minimal ZIP support written from the format specification.
 *   zip_read          : lists files + CRC32 + sizes from the central directory
 *   zip_write_stored  : writes an uncompressed ZIP (used to build the sandbox)
 */
#ifndef ORGANICE_ZIPFILE_H
#define ORGANICE_ZIPFILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;                    /* path inside the archive, '/' separated */
    uint32_t crc;
    unsigned long long size;       /* uncompressed size */
} ZipEntry;

typedef struct {
    ZipEntry *items;
    size_t count;
} ZipInfo;

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t len;
} ZipSource;

/* 0 = ok, -1 = not a readable zip, -2 = ZIP64 (not supported yet) */
int  zip_read(const char *path, ZipInfo *out);
void zip_free(ZipInfo *z);
int  zip_write_stored(const char *path, const ZipSource *src, size_t n);

#endif
