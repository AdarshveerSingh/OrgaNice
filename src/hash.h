/*
 * hash.h - checksums used to compare file contents.
 *   FNV-1a 64-bit : fast fingerprint for duplicate pre-filtering
 *   CRC-32        : the checksum ZIP archives store for every file
 */
#ifndef ORGANICE_HASH_H
#define ORGANICE_HASH_H

#include <stddef.h>
#include <stdint.h>

#define FNV_OFFSET 1469598103934665603ULL

uint64_t fnv1a64(const void *data, size_t len, uint64_t h);
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);  /* start with crc = 0 */
int      crc32_file(const char *path, uint32_t *out);               /* 0 on success */

#endif
