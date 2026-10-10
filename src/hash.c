/*
 * hash.c - FNV-1a and CRC-32 implementations.
 */
#include "hash.h"

#include <stdio.h>

uint64_t fnv1a64(const void *data, size_t len, uint64_t h)
{
    const unsigned char *p = data;
    while (len--) {
        h ^= *p++;
        h *= 1099511628211ULL;               /* FNV prime */
    }
    return h;
}

static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void)
{
    uint32_t i, k;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    crc_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const unsigned char *p = data;
    if (!crc_ready)
        crc_init();
    crc = ~crc;
    while (len--)
        crc = crc_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

int crc32_file(const char *path, uint32_t *out)
{
    static unsigned char buf[1 << 16];
    FILE *f = fopen(path, "rb");
    uint32_t crc = 0;
    size_t n;
    if (!f)
        return -1;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        crc = crc32_update(crc, buf, n);
    fclose(f);
    *out = crc;
    return 0;
}
