/*
 * zipfile.c - reads the ZIP "central directory" without extracting anything.
 *
 * Layout of a ZIP file:
 *   [local header + data] ... [central directory entries] [End Of Central Directory]
 * The EOCD record (signature PK\5\6) sits in the last 64 KB of the file and
 * tells us where the central directory starts. Each central directory entry
 * (signature PK\1\2) holds the file name, CRC-32 and uncompressed size -
 * exactly what we need to recognise already-extracted files on disk.
 */
#include "zipfile.h"
#include "hash.h"
#include "platform.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_EOCD    0x06054b50u
#define EOCD_SIZE   22
#define EOCD_SEARCH (EOCD_SIZE + 65535)    /* EOCD + longest possible comment */
#define CENTRAL_HDR 46
#define MAX_CD_SIZE (256u * 1024u * 1024u)

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void add_entry(ZipInfo *z, size_t *cap, char *name, uint32_t crc, unsigned long long size)
{
    if (z->count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        z->items = xrealloc(z->items, *cap * sizeof *z->items);
    }
    z->items[z->count].name = name;
    z->items[z->count].crc = crc;
    z->items[z->count].size = size;
    z->count++;
}

int zip_read(const char *path, ZipInfo *out)
{
    FILE *f = fopen(path, "rb");
    unsigned char *tail = NULL, *cd = NULL;
    long long fsize, tail_len, i;
    uint32_t cd_size, cd_off;
    size_t pos, cap = 0;
    int rc = -1;

    out->items = NULL;
    out->count = 0;
    if (!f)
        return -1;

    /* 1. find the End Of Central Directory record, scanning backwards */
    if (plat_fseek64(f, 0, SEEK_END) != 0 || (fsize = plat_ftell64(f)) < EOCD_SIZE)
        goto done;
    tail_len = fsize < EOCD_SEARCH ? fsize : EOCD_SEARCH;
    tail = xmalloc((size_t)tail_len);
    if (plat_fseek64(f, fsize - tail_len, SEEK_SET) != 0
        || fread(tail, 1, (size_t)tail_len, f) != (size_t)tail_len)
        goto done;
    for (i = tail_len - EOCD_SIZE; i >= 0; i--)
        if (rd32(tail + i) == SIG_EOCD)
            break;
    if (i < 0)
        goto done;                                   /* not a ZIP */

    cd_size = rd32(tail + i + 12);
    cd_off = rd32(tail + i + 16);
    if (cd_off == 0xFFFFFFFFu || cd_size == 0xFFFFFFFFu) {
        rc = -2;                                     /* ZIP64 */
        goto done;
    }
    if ((long long)cd_off + cd_size > fsize || cd_size > MAX_CD_SIZE)
        goto done;

    /* 2. read the whole central directory in one go */
    cd = xmalloc(cd_size);
    if (plat_fseek64(f, cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size)
        goto done;

    /* 3. walk its entries */
    for (pos = 0; pos + CENTRAL_HDR <= cd_size && rd32(cd + pos) == SIG_CENTRAL;) {
        uint32_t crc = rd32(cd + pos + 16);
        uint32_t usize = rd32(cd + pos + 24);
        uint16_t nlen = rd16(cd + pos + 28);
        uint16_t xlen = rd16(cd + pos + 30);
        uint16_t clen = rd16(cd + pos + 32);
        const unsigned char *name = cd + pos + CENTRAL_HDR;

        if (pos + CENTRAL_HDR + nlen > cd_size)
            break;
        /* skip folder entries ("dir/") and macOS metadata */
        if (nlen > 0 && name[nlen - 1] != '/' && !(nlen >= 9 && memcmp(name, "__MACOSX/", 9) == 0)) {
            char *copy = xmalloc((size_t)nlen + 1);
            memcpy(copy, name, nlen);
            copy[nlen] = '\0';
            add_entry(out, &cap, copy, crc, usize);
        }
        pos += CENTRAL_HDR + (size_t)nlen + xlen + clen;
    }
    rc = 0;

done:
    free(tail);
    free(cd);
    fclose(f);
    if (rc != 0)
        zip_free(out);
    return rc;
}

void zip_free(ZipInfo *z)
{
    size_t i;
    for (i = 0; i < z->count; i++)
        free(z->items[i].name);
    free(z->items);
    z->items = NULL;
    z->count = 0;
}

/* ------------------------------- writer ------------------------------- */

static void wr16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void wr32(FILE *f, uint32_t v)
{
    wr16(f, v & 0xFFFF);
    wr16(f, v >> 16);
}

int zip_write_stored(const char *path, const ZipSource *src, size_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t *offset, *crc;
    long cd_start, cd_end;
    size_t i;

    if (!f)
        return -1;
    offset = xmalloc((n + 1) * sizeof *offset);
    crc = xmalloc((n + 1) * sizeof *crc);

    for (i = 0; i < n; i++) {                       /* local headers + data */
        unsigned nlen = (unsigned)strlen(src[i].name);
        offset[i] = (uint32_t)ftell(f);
        crc[i] = crc32_update(0, src[i].data, src[i].len);
        wr32(f, SIG_LOCAL);
        wr16(f, 20); wr16(f, 0); wr16(f, 0);        /* version, flags, method 0 = stored */
        wr16(f, 0); wr16(f, 0x21);                  /* time, date (1980-01-01) */
        wr32(f, crc[i]); wr32(f, (uint32_t)src[i].len); wr32(f, (uint32_t)src[i].len);
        wr16(f, nlen); wr16(f, 0);
        fwrite(src[i].name, 1, nlen, f);
        fwrite(src[i].data, 1, src[i].len, f);
    }
    cd_start = ftell(f);
    for (i = 0; i < n; i++) {                       /* central directory */
        unsigned nlen = (unsigned)strlen(src[i].name);
        wr32(f, SIG_CENTRAL);
        wr16(f, 20); wr16(f, 20); wr16(f, 0); wr16(f, 0);
        wr16(f, 0); wr16(f, 0x21);
        wr32(f, crc[i]); wr32(f, (uint32_t)src[i].len); wr32(f, (uint32_t)src[i].len);
        wr16(f, nlen); wr16(f, 0); wr16(f, 0);      /* name, extra, comment lengths */
        wr16(f, 0); wr16(f, 0); wr32(f, 0);         /* disk, internal & external attrs */
        wr32(f, offset[i]);
        fwrite(src[i].name, 1, nlen, f);
    }
    cd_end = ftell(f);
    wr32(f, SIG_EOCD);                              /* end of central directory */
    wr16(f, 0); wr16(f, 0);
    wr16(f, (unsigned)n); wr16(f, (unsigned)n);
    wr32(f, (uint32_t)(cd_end - cd_start));
    wr32(f, (uint32_t)cd_start);
    wr16(f, 0);

    free(offset);
    free(crc);
    return fclose(f) == 0 ? 0 : -1;
}
