#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "image.h"

/* ── helpers ───────────────────────────────────────────────────────────── */

static unsigned int read_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] <<  8) |  (unsigned int)p[3];
}

/* Read entire file into a malloc'd buffer. */
static unsigned char *load_raw(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* ── JPEG ──────────────────────────────────────────────────────────────── */

/*
 * Scan JPEG markers to find an SOFx marker that contains the image
 * dimensions.  Returns 1 on success.
 */
static int jpeg_dimensions(const unsigned char *d, size_t sz,
                            int *w, int *h)
{
    if (sz < 3 || d[0] != 0xFF || d[1] != 0xD8) return 0;
    size_t i = 2;
    while (i + 3 < sz) {
        if (d[i] != 0xFF) return 0;
        unsigned char m = d[i + 1];
        i += 2;
        /* SOF markers: C0-C3, C5-C7, C9-CB, CD-CF */
        if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) ||
            (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
            if (i + 7 > sz) return 0;
            /* i = length(2), precision(1), height(2), width(2) */
            *h = (int)((d[i+3] << 8) | d[i+4]);
            *w = (int)((d[i+5] << 8) | d[i+6]);
            return 1;
        }
        if (m == 0xD9) break; /* EOI */
        if (i + 2 > sz) break;
        unsigned int seg = (unsigned int)((d[i] << 8) | d[i+1]);
        if (seg < 2) break;
        i += seg;
    }
    return 0;
}

/* ── PNG ───────────────────────────────────────────────────────────────── */

static const unsigned char PNG_SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static int is_png(const unsigned char *d, size_t sz)
{
    return sz >= 8 && memcmp(d, PNG_SIG, 8) == 0;
}

/* Paeth predictor for PNG filter type 4. */
static unsigned char paeth(unsigned char a, unsigned char b, unsigned char c)
{
    int p  = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

/*
 * Decode a PNG file to raw RGB(A) pixel data.
 * Returns 1 on success.  *pixels must be free()'d by the caller.
 */
static int decode_png(const unsigned char *d, size_t sz,
                      unsigned char **pixels, int *width, int *height,
                      int *channels)
{
    if (!is_png(d, sz) || sz < 33) return 0;

    /* IHDR starts at offset 8: [len(4)][type(4)][data(13)][crc(4)] */
    *width  = (int)read_be32(d + 16);
    *height = (int)read_be32(d + 20);
    int bit_depth  = d[24];
    int color_type = d[25];
    int interlace  = d[28];

    if (bit_depth != 8 || interlace != 0) return 0; /* only 8-bit non-interlaced */

    switch (color_type) {
        case 0: *channels = 1; break; /* greyscale */
        case 2: *channels = 3; break; /* RGB */
        case 3: *channels = 3; break; /* indexed – treat as RGB after decoding */
        case 4: *channels = 2; break; /* greyscale+alpha */
        case 6: *channels = 4; break; /* RGBA */
        default: return 0;
    }

    /* Collect PLTE (for indexed) and all IDAT chunks. */
    unsigned char plte[256 * 3];
    int have_plte = 0;
    unsigned char *idat = NULL;
    size_t idat_sz = 0;

    size_t pos = 8;
    while (pos + 12 <= sz) {
        unsigned int clen = read_be32(d + pos);
        const char  *ct   = (const char *)(d + pos + 4);
        const unsigned char *cd = d + pos + 8;

        if (pos + 12 + clen > sz) break;

        if (strncmp(ct, "PLTE", 4) == 0 && clen <= 768) {
            memcpy(plte, cd, clen);
            have_plte = 1;
        } else if (strncmp(ct, "IDAT", 4) == 0) {
            unsigned char *tmp = realloc(idat, idat_sz + clen);
            if (!tmp) { free(idat); return 0; }
            idat = tmp;
            memcpy(idat + idat_sz, cd, clen);
            idat_sz += clen;
        } else if (strncmp(ct, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + clen;
    }

    if (!idat) return 0;

    /* zlib decompress.  Each row = filter_byte(1) + channels*width bytes. */
    int bpp  = *channels; /* bytes per pixel in the raw filtered data */
    if (color_type == 3) bpp = 1; /* indexed: 1 byte per pixel in raw */
    size_t row_sz   = (size_t)(bpp * (*width) + 1);
    size_t raw_sz   = row_sz * (size_t)(*height);
    unsigned char *raw = malloc(raw_sz);
    if (!raw) { free(idat); return 0; }

    uLongf dest_len = (uLongf)raw_sz;
    if (uncompress(raw, &dest_len, idat, (uLong)idat_sz) != Z_OK) {
        free(raw); free(idat); return 0;
    }
    free(idat);

    /* Apply PNG row filters and expand indexed→RGB. */
    int out_channels = *channels; /* channels in the output pixel array */
    if (color_type == 3) { out_channels = 3; *channels = 3; }
    unsigned char *px = malloc((size_t)(*width) * (size_t)(*height) * (size_t)out_channels);
    if (!px) { free(raw); return 0; }

    for (int y = 0; y < *height; y++) {
        unsigned char  filt = raw[y * (int)row_sz];
        unsigned char *src  = raw + y * (int)row_sz + 1;
        unsigned char *dst  = px  + y * (*width) * out_channels;
        unsigned char *prev = (y > 0) ? (px + (y-1) * (*width) * out_channels) : NULL;

        /* Reconstruct raw bytes (before index expansion) */
        unsigned char *recon = malloc((size_t)bpp * (size_t)(*width));
        if (!recon) { free(px); free(raw); return 0; }
        unsigned char *prev_recon = (y > 0) ? (raw + (y-1) * (int)row_sz + 1) : NULL;

        for (int x = 0; x < bpp * (*width); x++) {
            unsigned char a  = src[x];
            unsigned char up = prev_recon ? prev_recon[x] : 0;
            unsigned char left      = (x >= bpp) ? recon[x - bpp] : 0;
            unsigned char upleft    = (prev_recon && x >= bpp) ? prev_recon[x - bpp] : 0;
            switch (filt) {
                case 0: recon[x] = a; break;
                case 1: recon[x] = a + left; break;
                case 2: recon[x] = a + up; break;
                case 3: recon[x] = a + (unsigned char)(((int)left + (int)up) / 2); break;
                case 4: recon[x] = a + paeth(left, up, upleft); break;
                default: recon[x] = a; break;
            }
        }

        /* Expand to output pixel array */
        if (color_type == 3) { /* indexed */
            for (int x = 0; x < *width; x++) {
                int idx = recon[x];
                if (have_plte && idx < 256) {
                    dst[x*3+0] = plte[idx*3+0];
                    dst[x*3+1] = plte[idx*3+1];
                    dst[x*3+2] = plte[idx*3+2];
                } else {
                    dst[x*3+0] = dst[x*3+1] = dst[x*3+2] = (unsigned char)idx;
                }
            }
        } else {
            memcpy(dst, recon, (size_t)bpp * (size_t)(*width));
        }
        free(recon);
        (void)prev; /* silence warning – we use prev_recon instead */
    }

    free(raw);
    *pixels = px;
    return 1;
}

/* ── public API ─────────────────────────────────────────────────────────── */

Image *image_load(const char *path)
{
    size_t sz = 0;
    unsigned char *raw = load_raw(path, &sz);
    if (!raw) return NULL;

    Image *img = calloc(1, sizeof(Image));
    if (!img) { free(raw); return NULL; }

    /* JPEG? */
    if (sz >= 3 && raw[0] == 0xFF && raw[1] == 0xD8) {
        if (!jpeg_dimensions(raw, sz, &img->width, &img->height)) {
            free(raw); free(img); return NULL;
        }
        img->channels  = 3;
        img->is_jpeg   = 1;
        img->data      = raw;
        img->data_size = sz;
        return img;
    }

    /* PNG? */
    if (is_png(raw, sz)) {
        unsigned char *pixels = NULL;
        int w = 0, h = 0, ch = 0;
        if (!decode_png(raw, sz, &pixels, &w, &h, &ch)) {
            free(raw); free(img); return NULL;
        }
        free(raw);
        img->width     = w;
        img->height    = h;
        img->channels  = ch;
        img->is_jpeg   = 0;
        img->data      = pixels;
        img->data_size = (size_t)w * (size_t)h * (size_t)ch;
        return img;
    }

    free(raw);
    free(img);
    return NULL; /* unsupported format */
}

void image_free(Image *img)
{
    if (!img) return;
    free(img->data);
    free(img);
}
