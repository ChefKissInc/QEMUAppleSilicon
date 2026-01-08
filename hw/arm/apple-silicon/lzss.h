#ifndef HW_ARM_APPLE_SILICON_LZSS_H
#define HW_ARM_APPLE_SILICON_LZSS_H
#include "qemu/compiler.h"
#include <stdint.h>

typedef struct {
    uint32_t signature;
    uint32_t compress_type;
    uint32_t adler32;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    uint32_t reserved[11];
    uint8_t platform_name[64];
    uint8_t root_path[256];
    uint8_t data[];
} QEMU_PACKED LzssCompHeader;

int decompress_lzss(uint8_t *dst, uint8_t *src, uint32_t srclen);

#define N 4096
#define F 18
#define THRESHOLD 2
#define NIL N

int decompress_lzss(uint8_t *dst, uint8_t *src, uint32_t srclen)
{
    uint8_t text_buf[N + F - 1];
    uint8_t *dststart = dst;
    uint8_t *srcend = src + srclen;
    int i, j, k, r, c;
    unsigned int flags;

    dst = dststart;
    srcend = src + srclen;
    for (i = 0; i < N - F; i++)
        text_buf[i] = ' ';
    r = N - F;
    flags = 0;
    for (;;) {
        if (((flags >>= 1) & 0x100) == 0) {
            if (src < srcend)
                c = *src++;
            else
                break;
            flags = c | 0xFF00;
        }
        if (flags & 1) {
            if (src < srcend)
                c = *src++;
            else
                break;
            *dst++ = c;
            text_buf[r++] = c;
            r &= (N - 1);
        } else {
            if (src < srcend)
                i = *src++;
            else
                break;
            if (src < srcend)
                j = *src++;
            else
                break;
            i |= ((j & 0xF0) << 4);
            j = (j & 0x0F) + THRESHOLD;
            for (k = 0; k <= j; k++) {
                c = text_buf[(i + k) & (N - 1)];
                *dst++ = c;
                text_buf[r++] = c;
                r &= (N - 1);
            }
        }
    }

    return dst - dststart;
}

#endif /* HW_ARM_APPLE_SILICON_LZSS_H */
