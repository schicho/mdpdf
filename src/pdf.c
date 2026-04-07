/*
 * pdf.c — Raw PDF generation engine
 *
 * Produces PDF 1.4 output using only the 14 standard PDF fonts
 * (Helvetica family + Courier family).  No fonts are embedded.
 *
 * Coordinate convention inside this file:
 *   pdf->y  = distance from the TOP of the content area (increases downward)
 *   PDF_Y() = convert to PDF points from page bottom  (PDF origin = bottom-left)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <zlib.h>
#include <limits.h>

#include "pdf.h"
#include "image.h"

/* ── tunables ──────────────────────────────────────────────────────────── */
#define MARGIN_H      72.0f   /* left & right margin in points (1 inch)     */
#define MARGIN_V      72.0f   /* top & bottom margin in points              */
#define CODE_PAD_H     6.0f   /* horizontal padding inside code blocks      */
#define CODE_PAD_V     4.0f   /* vertical padding inside code blocks        */
#define CODE_FONT_SIZE 9.5f
#define BODY_FONT_SIZE 11.0f
#define CODE_BG_R      0.96f
#define CODE_BG_G      0.97f
#define CODE_BG_B      0.98f
#define QUOTE_BAR_W    3.0f
#define QUOTE_INDENT   12.0f

/* ── limits ────────────────────────────────────────────────────────────── */
#define MAX_OBJECTS  16384
#define MAX_PAGES     4096
#define MAX_IMAGES     512

/* ── font names ─────────────────────────────────────────────────────────── */
static const char *FONT_NAMES[FONT_COUNT] = {
    "Helvetica",
    "Helvetica-Bold",
    "Helvetica-Oblique",
    "Helvetica-BoldOblique",
    "Courier",
    "Courier-Bold"
};

/* ── Helvetica character widths (1/1000 em), index 0 = ASCII 32 (space) ── */
static const int HELV_W[95] = {
    278, /* ' '  32 */  278, /* !  */  355, /* "  */  556, /* #  */
    556, /* $    36 */  889, /* %  */  667, /* &  */  222, /* '  */
    333, /* (    40 */  333, /* )  */  389, /* *  */  584, /* +  */
    278, /* ,    44 */  333, /* -  */  278, /* .  */  278, /* /  */
    556, /* 0    48 */  556, /* 1  */  556, /* 2  */  556, /* 3  */
    556, /* 4    52 */  556, /* 5  */  556, /* 6  */  556, /* 7  */
    556, /* 8    56 */  556, /* 9  */  278, /* :  */  278, /* ;  */
    584, /* <    60 */  584, /* =  */  584, /* >  */  556, /* ?  */
   1015, /* @    64 */  667, /* A  */  667, /* B  */  722, /* C  */
    722, /* D    68 */  667, /* E  */  611, /* F  */  778, /* G  */
    722, /* H    72 */  278, /* I  */  500, /* J  */  667, /* K  */
    556, /* L    76 */  833, /* M  */  722, /* N  */  778, /* O  */
    667, /* P    80 */  778, /* Q  */  722, /* R  */  667, /* S  */
    611, /* T    84 */  722, /* U  */  667, /* V  */  944, /* W  */
    667, /* X    88 */  667, /* Y  */  611, /* Z  */  278, /* [  */
    278, /* \    92 */  278, /* ]  */  469, /* ^  */  556, /* _  */
    222, /* `    96 */  556, /* a  */  556, /* b  */  500, /* c  */
    556, /* d   100 */  556, /* e  */  278, /* f  */  556, /* g  */
    556, /* h   104 */  222, /* i  */  222, /* j  */  500, /* k  */
    222, /* l   108 */  833, /* m  */  556, /* n  */  556, /* o  */
    556, /* p   112 */  556, /* q  */  333, /* r  */  500, /* s  */
    278, /* t   116 */  556, /* u  */  500, /* v  */  722, /* w  */
    500, /* x   120 */  500, /* y  */  500, /* z  */  334, /* {  */
    260, /* |   124 */  334, /* }  */  584  /* ~  126 */
};

/* ── Helvetica-Bold character widths (1/1000 em), index 0 = ASCII 32 ─── */
static const int HELV_BOLD_W[95] = {
    278, /* ' '  32 */  333, /* !  */  474, /* "  */  556, /* #  */
    556, /* $    36 */  889, /* %  */  722, /* &  */  278, /* '  */
    333, /* (    40 */  333, /* )  */  389, /* *  */  584, /* +  */
    278, /* ,    44 */  333, /* -  */  278, /* .  */  278, /* /  */
    556, /* 0    48 */  556, /* 1  */  556, /* 2  */  556, /* 3  */
    556, /* 4    52 */  556, /* 5  */  556, /* 6  */  556, /* 7  */
    556, /* 8    56 */  556, /* 9  */  333, /* :  */  333, /* ;  */
    584, /* <    60 */  584, /* =  */  584, /* >  */  611, /* ?  */
    975, /* @    64 */  722, /* A  */  722, /* B  */  722, /* C  */
    722, /* D    68 */  667, /* E  */  611, /* F  */  778, /* G  */
    722, /* H    72 */  278, /* I  */  556, /* J  */  722, /* K  */
    611, /* L    76 */  833, /* M  */  722, /* N  */  778, /* O  */
    667, /* P    80 */  778, /* Q  */  722, /* R  */  667, /* S  */
    611, /* T    84 */  722, /* U  */  667, /* V  */  944, /* W  */
    667, /* X    88 */  667, /* Y  */  611, /* Z  */  333, /* [  */
    278, /* \    92 */  333, /* ]  */  584, /* ^  */  556, /* _  */
    278, /* `    96 */  556, /* a  */  611, /* b  */  556, /* c  */
    611, /* d   100 */  556, /* e  */  333, /* f  */  611, /* g  */
    611, /* h   104 */  278, /* i  */  278, /* j  */  556, /* k  */
    278, /* l   108 */  889, /* m  */  611, /* n  */  611, /* o  */
    611, /* p   112 */  611, /* q  */  389, /* r  */  556, /* s  */
    333, /* t   116 */  611, /* u  */  556, /* v  */  778, /* w  */
    556, /* x   120 */  556, /* y  */  500, /* z  */  389, /* {  */
    280, /* |   124 */  389, /* }  */  584  /* ~  126 */
};

float pdf_text_width(const char *text, int font, float size)
{
    if (!text) return 0.0f;
    float w = 0.0f;
    if (font == FONT_MONO || font == FONT_MONO_BOLD) {
        w = (float)strlen(text) * 600.0f;
    } else {
        int is_bold = (font == FONT_BOLD || font == FONT_BOLDITALIC);
        for (const char *p = text; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c >= 32 && c <= 126)
                w += (float)(is_bold ? HELV_BOLD_W[c - 32] : HELV_W[c - 32]);
            else
                w += 556.0f; /* fallback */
        }
    }
    return w * size / 1000.0f;
}

/* ── dynamic buffer ──────────────────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t size;
    size_t cap;
} Buf;

static int buf_grow(Buf *b, size_t need)
{
    if (b->size + need <= b->cap) return 1;
    size_t newcap = b->cap ? b->cap * 2 : 4096;
    while (newcap < b->size + need) newcap *= 2;
    char *p = realloc(b->data, newcap);
    if (!p) return 0;
    b->data = p;
    b->cap  = newcap;
    return 1;
}

static int buf_append(Buf *b, const char *s, size_t n)
{
    if (!buf_grow(b, n + 1)) return 0;
    memcpy(b->data + b->size, s, n);
    b->size += n;
    b->data[b->size] = '\0';
    return 1;
}

static int buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n >= sizeof(tmp)) {
        /* large format: allocate */
        char *big = malloc((size_t)n + 1);
        if (!big) return 0;
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        int r = buf_append(b, big, (size_t)n);
        free(big);
        return r;
    }
    return buf_append(b, tmp, (size_t)n);
}

/* ── PDF context ──────────────────────────────────────────────────────────── */

struct PDF {
    float pw, ph;          /* page width / height in points */
    float ml, mr, mt, mb;  /* margins */

    /* current-page cursor (from TOP of content area) */
    float y;

    /* page content streams, one per page */
    Buf  *pages;
    int   page_count;
    int   page_alloc;

    /* current page content stream being built */
    Buf   content;

    /* image resources */
    struct ImgRes {
        Image *img;
        char   resname[32];   /* e.g. "Im0" – PDF resource name */
        char   path[2048];    /* filesystem path used for cache lookup */
    } images[MAX_IMAGES];
    int image_count;

    char input_dir[2048];
};

/* ── coordinate helper ──────────────────────────────────────────────────── */
/* Convert y-from-top to PDF y (from page bottom). */
static float PDF_Y(const PDF *pdf, float y_from_top)
{
    return pdf->ph - pdf->mt - y_from_top;
}

/* ── life-cycle ─────────────────────────────────────────────────────────── */

PDF *pdf_create(float page_width, float page_height)
{
    PDF *p = calloc(1, sizeof(PDF));
    if (!p) return NULL;
    p->pw = page_width;
    p->ph = page_height;
    p->ml = MARGIN_H;
    p->mr = MARGIN_H;
    p->mt = MARGIN_V;
    p->mb = MARGIN_V;
    p->y  = 0.0f;
    /* Allocate initial pages array */
    p->page_alloc = 16;
    p->pages = calloc((size_t)p->page_alloc, sizeof(Buf));
    if (!p->pages) { free(p); return NULL; }
    /* Start first page */
    p->content.data = NULL;
    p->content.size = 0;
    p->content.cap  = 0;
    return p;
}

void pdf_free(PDF *pdf)
{
    if (!pdf) return;
    for (int i = 0; i < pdf->page_count; i++)
        free(pdf->pages[i].data);
    free(pdf->pages);
    free(pdf->content.data);
    for (int i = 0; i < pdf->image_count; i++)
        image_free(pdf->images[i].img);
    free(pdf);
}

void pdf_set_input_dir(PDF *pdf, const char *dir)
{
    if (dir)
        snprintf(pdf->input_dir, sizeof(pdf->input_dir), "%s", dir);
}

float pdf_get_y(PDF *pdf)         { return pdf->y; }
float pdf_content_width(PDF *pdf) { return pdf->pw - pdf->ml - pdf->mr; }
float pdf_margin_left(PDF *pdf)   { return pdf->ml; }

/* ── page management ──────────────────────────────────────────────────────── */

void pdf_new_page(PDF *pdf)
{
    /* Save current content stream */
    if (pdf->page_count >= pdf->page_alloc) {
        int na = pdf->page_alloc * 2;
        Buf *nb = realloc(pdf->pages, (size_t)na * sizeof(Buf));
        if (!nb) return;
        pdf->pages     = nb;
        pdf->page_alloc = na;
    }
    pdf->pages[pdf->page_count] = pdf->content;
    pdf->page_count++;
    /* Reset content buffer */
    pdf->content.data = NULL;
    pdf->content.size = 0;
    pdf->content.cap  = 0;
    pdf->y = 0.0f;
}

void pdf_ensure_space(PDF *pdf, float height)
{
    float available = (pdf->ph - pdf->mt - pdf->mb) - pdf->y;
    if (available < height)
        pdf_new_page(pdf);
}

void pdf_advance_y(PDF *pdf, float amount)
{
    pdf->y += amount;
}

/* ── graphics ─────────────────────────────────────────────────────────────── */

void pdf_rect_fill(PDF *pdf,
                   float x, float y_from_top,
                   float w, float h,
                   float r, float g, float b)
{
    float py = PDF_Y(pdf, y_from_top) - h;
    buf_printf(&pdf->content,
               "q %.3f %.3f %.3f rg %.3f %.3f %.3f %.3f re f Q\n",
               (double)r, (double)g, (double)b,
               (double)x, (double)py, (double)w, (double)h);
}

void pdf_hline(PDF *pdf,
               float x, float y_from_top, float width,
               float r, float g, float b, float lw)
{
    float py = PDF_Y(pdf, y_from_top);
    buf_printf(&pdf->content,
               "q %.3f %.3f %.3f RG %.2f w %.3f %.3f m %.3f %.3f l S Q\n",
               (double)r, (double)g, (double)b,
               (double)lw,
               (double)x, (double)py, (double)(x + width), (double)py);
}

void pdf_vbar(PDF *pdf,
              float x, float y_from_top, float height,
              float r, float g, float b, float lw)
{
    float py_top = PDF_Y(pdf, y_from_top);
    float py_bot = py_top - height;
    buf_printf(&pdf->content,
               "q %.3f %.3f %.3f RG %.2f w %.3f %.3f m %.3f %.3f l S Q\n",
               (double)r, (double)g, (double)b,
               (double)lw,
               (double)x, (double)py_top, (double)x, (double)py_bot);
}

/* ── PDF string escaping ──────────────────────────────────────────────────── */

/*
 * Convert a UTF-8 encoded Unicode code point to the single-byte
 * WinAnsiEncoding (Windows-1252) value, or '?' if not representable.
 *
 * WinAnsiEncoding covers:
 *   0x20-0x7E : ASCII (same)
 *   0x80-0x9F : Windows-1252 extras (mapped below)
 *   0xA0-0xFF : ISO-8859-1 (same code point)
 */
static unsigned char utf8_to_winansi(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    unsigned char c = *p;

    /* ASCII passthrough */
    if (c < 0x80) { (*pp)++; return c; }

    /* Decode UTF-8 sequence */
    unsigned int cp = 0;
    int bytes = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 3; }
    else { (*pp)++; return '?'; }  /* invalid */

    for (int i = 0; i < bytes; i++) {
        p++;
        if ((*p & 0xC0) != 0x80) { *pp = (const char *)p; return '?'; }
        cp = (cp << 6) | (*p & 0x3F);
    }
    *pp = (const char *)(p + 1);

    /* ISO-8859-1 range maps directly */
    if (cp >= 0xA0 && cp <= 0xFF) return (unsigned char)cp;

    /* Windows-1252 specific mappings */
    switch (cp) {
        case 0x20AC: return 0x80; /* € Euro sign */
        case 0x201A: return 0x82; /* ‚ single low-9 quotation */
        case 0x0192: return 0x83; /* ƒ */
        case 0x201E: return 0x84; /* „ double low-9 quotation */
        case 0x2026: return 0x85; /* … ellipsis */
        case 0x2020: return 0x86; /* † dagger */
        case 0x2021: return 0x87; /* ‡ double dagger */
        case 0x02C6: return 0x88; /* ˆ circumflex */
        case 0x2030: return 0x89; /* ‰ per mille */
        case 0x0160: return 0x8A; /* Š */
        case 0x2039: return 0x8B; /* ‹ */
        case 0x0152: return 0x8C; /* Œ */
        case 0x017D: return 0x8E; /* Ž */
        case 0x2018: return 0x91; /* ' left single quotation */
        case 0x2019: return 0x92; /* ' right single quotation */
        case 0x201C: return 0x93; /* " left double quotation */
        case 0x201D: return 0x94; /* " right double quotation */
        case 0x2022: return 0x95; /* • bullet */
        case 0x2013: return 0x96; /* – en dash */
        case 0x2014: return 0x97; /* — em dash */
        case 0x02DC: return 0x98; /* ˜ tilde */
        case 0x2122: return 0x99; /* ™ trade mark */
        case 0x0161: return 0x9A; /* š */
        case 0x203A: return 0x9B; /* › */
        case 0x0153: return 0x9C; /* œ */
        case 0x017E: return 0x9E; /* ž */
        case 0x0178: return 0x9F; /* Ÿ */
        default:     return '?';
    }
}

/*
 * Write a PDF literal string "(…)" to the buffer.
 * Input is UTF-8; characters are converted to WinAnsiEncoding.
 * The special PDF characters ( ) \ are escaped with a backslash.
 */
static void buf_pdf_string(Buf *b, const char *text)
{
    buf_append(b, "(", 1);
    const char *p = text;
    while (*p) {
        unsigned char c;
        if ((unsigned char)*p < 0x80) {
            c = (unsigned char)*p++;
        } else {
            c = utf8_to_winansi(&p);
        }
        if (c == '(' || c == ')' || c == '\\') {
            char esc[3] = { '\\', (char)c, '\0' };
            buf_append(b, esc, 2);
        } else if (c < 32) {
            char oct[6];
            snprintf(oct, sizeof(oct), "\\%03o", (unsigned)c);
            buf_append(b, oct, 4);
        } else {
            buf_append(b, (char *)&c, 1);
        }
    }
    buf_append(b, ")", 1);
}

/* ── simple text line ─────────────────────────────────────────────────────── */

float pdf_text_line(PDF *pdf,
                    const char *text, int font, float size,
                    float x_offset, float leading)
{
    if (!text || !*text) return 0.0f;
    if (leading <= 0.0f) leading = size * 1.2f;
    float x  = pdf->ml + x_offset;
    float py = PDF_Y(pdf, pdf->y + size); /* baseline */

    buf_printf(&pdf->content,
               "BT /F%d %.2f Tf %.3f %.3f Td ",
               font, (double)size, (double)x, (double)py);
    buf_pdf_string(&pdf->content, text);
    buf_append(&pdf->content, " Tj ET\n", 7);

    pdf->y += leading;
    return leading;
}

/* ── inline span parser ────────────────────────────────────────────────────── */

#define MAX_SPANS 4096

typedef struct {
    int  font;    /* FONT_* constant */
    char text[512];
} Span;

/*
 * Parse a markdown inline string into an array of Span objects.
 * Handles: **bold**, *italic*, ***bold-italic***, __bold__, _italic_,
 *          `code`, and plain text.
 * Returns the number of spans written (up to max_spans).
 */
static int parse_inline(const char *src, int base_font,
                         Span *spans, int max_spans)
{
    int  n   = 0;
    int  cur = base_font;
    const char *p = src;
    char tmp[512];
    int  ti = 0;

#define FLUSH_SPAN() do { \
    if (ti > 0 && n < max_spans) { \
        tmp[ti] = '\0'; \
        spans[n].font = cur; \
        snprintf(spans[n].text, sizeof(spans[n].text), "%s", tmp); \
        n++; ti = 0; \
    } \
} while (0)

    while (*p) {
        /* Bold-italic: *** or ___ */
        if ((p[0] == '*' && p[1] == '*' && p[2] == '*') ||
            (p[0] == '_' && p[1] == '_' && p[2] == '_')) {
            FLUSH_SPAN();
            char delim = p[0];
            p += 3;
            const char *end = strstr(p, delim == '*' ? "***" : "___");
            if (end) {
                if (n < max_spans) {
                    spans[n].font = FONT_BOLDITALIC; /* always bold-italic */
                    size_t len = (size_t)(end - p);
                    if (len >= sizeof(spans[n].text)) len = sizeof(spans[n].text) - 1;
                    memcpy(spans[n].text, p, len);
                    spans[n].text[len] = '\0';
                    n++;
                }
                p = end + 3;
            } else {
                /* Not closed – treat as literal */
                tmp[ti++] = delim; tmp[ti++] = delim; tmp[ti++] = delim;
            }
            continue;
        }
        /* Bold: ** or __ */
        if ((p[0] == '*' && p[1] == '*') || (p[0] == '_' && p[1] == '_')) {
            FLUSH_SPAN();
            char delim = p[0];
            p += 2;
            const char *end = strstr(p, delim == '*' ? "**" : "__");
            if (end) {
                if (n < max_spans) {
                    spans[n].font = (base_font == FONT_ITALIC)
                                        ? FONT_BOLDITALIC : FONT_BOLD;
                    size_t len = (size_t)(end - p);
                    if (len >= sizeof(spans[n].text)) len = sizeof(spans[n].text) - 1;
                    memcpy(spans[n].text, p, len);
                    spans[n].text[len] = '\0';
                    n++;
                }
                p = end + 2;
            } else {
                tmp[ti++] = delim; tmp[ti++] = delim;
            }
            continue;
        }
        /* Italic: * or _ (not at word boundary for _) */
        if (p[0] == '*' || (p[0] == '_' && (p[1] != ' ' && p[1] != '\0'))) {
            char delim = p[0];
            const char *end = strchr(p + 1, delim);
            /* for _ only match if preceded by non-space */
            if (end && (delim == '*' || (end > p + 1 && end[-1] != ' '))) {
                FLUSH_SPAN();
                p++;
                if (n < max_spans) {
                    spans[n].font = (base_font == FONT_BOLD)
                                        ? FONT_BOLDITALIC : FONT_ITALIC;
                    size_t len = (size_t)(end - p);
                    if (len >= sizeof(spans[n].text)) len = sizeof(spans[n].text) - 1;
                    memcpy(spans[n].text, p, len);
                    spans[n].text[len] = '\0';
                    n++;
                }
                p = end + 1;
                continue;
            }
        }
        /* Inline code: ` */
        if (p[0] == '`') {
            FLUSH_SPAN();
            p++;
            const char *end = strchr(p, '`');
            if (end) {
                if (n < max_spans) {
                    spans[n].font = FONT_MONO;
                    size_t len = (size_t)(end - p);
                    if (len >= sizeof(spans[n].text)) len = sizeof(spans[n].text) - 1;
                    memcpy(spans[n].text, p, len);
                    spans[n].text[len] = '\0';
                    n++;
                }
                p = end + 1;
            } else {
                tmp[ti++] = '`';
            }
            continue;
        }
        /* Skip image markup: ![alt](path) — rendered separately */
        if (p[0] == '!' && p[1] == '[') {
            const char *pe = strchr(p, ')');
            if (pe) { p = pe + 1; continue; }
        }
        /* Skip link markup: [text](url) — keep the link text */
        if (p[0] == '[') {
            const char *end_bracket = strchr(p, ']');
            if (end_bracket && end_bracket[1] == '(') {
                const char *end_paren = strchr(end_bracket, ')');
                if (end_paren) {
                    FLUSH_SPAN();
                    size_t len = (size_t)(end_bracket - (p + 1));
                    if (len > 0 && n < max_spans) {
                        spans[n].font = base_font;
                        if (len >= sizeof(spans[n].text)) len = sizeof(spans[n].text) - 1;
                        memcpy(spans[n].text, p + 1, len);
                        spans[n].text[len] = '\0';
                        n++;
                    }
                    p = end_paren + 1;
                    continue;
                }
            }
        }
        /* Regular character */
        if (ti < (int)sizeof(tmp) - 2)
            tmp[ti++] = *p;
        p++;
    }
    FLUSH_SPAN();
#undef FLUSH_SPAN
    return n;
}

/* ── paragraph renderer (word-wrap + inline markup) ───────────────────────── */

/* ── word token list ─────────────────────────────────────────────────────── */

/*
 * A "word token" is a run of non-space characters from one span.
 * We include the trailing space (or not) so that line widths can be computed.
 */
typedef struct {
    int   font;
    char  text[512];  /* includes trailing space if present in source */
    float w;          /* pre-computed width at a given size */
} Token;

#define MAX_TOKENS 8192

/*
 * Tokenise all spans into individual word tokens.
 * Returns token count.
 */
static int build_tokens(const Span *spans, int ns,
                         Token *toks, int max_toks, float font_size)
{
    int n = 0;
    for (int s = 0; s < ns && n < max_toks; s++) {
        const char *p = spans[s].text;
        while (*p && n < max_toks) {
            /* skip leading spaces (keep one space to attach to previous token) */
            if (*p == ' ') {
                if (n > 0) {
                    /* Append space width to previous token */
                    size_t tl = strlen(toks[n-1].text);
                    if (tl + 1 < sizeof(toks[n-1].text)) {
                        toks[n-1].text[tl]   = ' ';
                        toks[n-1].text[tl+1] = '\0';
                        toks[n-1].w = pdf_text_width(toks[n-1].text,
                                                      toks[n-1].font, font_size);
                    }
                }
                p++;
                continue;
            }
            /* collect word */
            const char *start = p;
            while (*p && *p != ' ') p++;
            size_t wlen = (size_t)(p - start);
            if (wlen == 0) { p++; continue; }
            if (wlen >= sizeof(toks[n].text))
                wlen = sizeof(toks[n].text) - 1;
            memcpy(toks[n].text, start, wlen);
            toks[n].text[wlen] = '\0';
            toks[n].font = spans[s].font;
            toks[n].w    = pdf_text_width(toks[n].text, toks[n].font, font_size);
            n++;
        }
    }
    return n;
}

float pdf_paragraph(PDF *pdf,
                    const char *text,
                    float left_indent, float right_indent,
                    float font_size,   int base_font,
                    float leading)
{
    if (!text || !*text) return 0.0f;
    if (leading <= 0.0f) leading = font_size * 1.4f;

    float x_left  = pdf->ml + left_indent;
    float x_right = pdf->pw - pdf->mr - right_indent;
    float max_w   = x_right - x_left;
    if (max_w <= 0.0f) return 0.0f;

    /* Parse inline markup into spans */
    Span spans[MAX_SPANS];
    int  ns = parse_inline(text, base_font, spans, MAX_SPANS);
    if (ns == 0) return 0.0f;

    /* Build word token list */
    Token *toks = malloc(MAX_TOKENS * sizeof(Token));
    if (!toks) return 0.0f;
    int nt = build_tokens(spans, ns, toks, MAX_TOKENS, font_size);

    float total_h = 0.0f;
    int   ti      = 0; /* current token index */

    while (ti < nt) {
        pdf_ensure_space(pdf, leading);
        float py = PDF_Y(pdf, pdf->y + font_size);

        /* Pack tokens onto this line */
        float line_w = 0.0f;
        int   line_start = ti;
        int   line_end   = ti; /* one past last token on this line */

        while (ti < nt) {
            float tw = toks[ti].w;
            if (line_end > line_start && line_w + tw > max_w)
                break; /* wrap */
            line_w += tw;
            line_end = ti + 1;
            ti++;
        }

        /* Render the tokens on this line */
        float x = x_left;
        for (int k = line_start; k < line_end; k++) {
            /* Strip trailing space from the last token on the line */
            const char *tok_text = toks[k].text;
            char stripped[512];
            if (k == line_end - 1) {
                size_t tl = strlen(tok_text);
                while (tl > 0 && tok_text[tl-1] == ' ') tl--;
                if (tl < sizeof(stripped)) {
                    memcpy(stripped, tok_text, tl);
                    stripped[tl] = '\0';
                    tok_text = stripped;
                }
            }
            if (*tok_text) {
                buf_printf(&pdf->content,
                           "BT /F%d %.2f Tf %.3f %.3f Td ",
                           toks[k].font, (double)font_size,
                           (double)x, (double)py);
                buf_pdf_string(&pdf->content, tok_text);
                buf_append(&pdf->content, " Tj ET\n", 7);
                x += pdf_text_width(tok_text, toks[k].font, font_size);
            }
        }

        pdf->y  += leading;
        total_h += leading;
    }

    free(toks);
    return total_h;
}

/* ── code block renderer ──────────────────────────────────────────────────── */

float pdf_code_block(PDF *pdf, const char *code, float left_indent)
{
    float fs      = CODE_FONT_SIZE;
    float leading = fs * 1.3f;
    float x_left  = pdf->ml + left_indent + CODE_PAD_H;
    float block_w = pdf->pw - pdf->ml - pdf->mr - left_indent;

    /* Split into lines */
    size_t code_len = strlen(code);
    char *copy = malloc(code_len + 1);
    if (!copy) return 0.0f;
    memcpy(copy, code, code_len + 1);

    /* Count lines for background box */
    /* We'll render line-by-line and handle page breaks */
    char *line = copy;
    char *next;
    float total_h = 0.0f;

    /* We draw the bg rect per line to handle page breaks */
    while (line) {
        next = strchr(line, '\n');
        if (next) *next++ = '\0';

        pdf_ensure_space(pdf, leading + CODE_PAD_V * 2);

        /* background rectangle for this line */
        pdf_rect_fill(pdf,
                      pdf->ml + left_indent,
                      pdf->y,
                      block_w, leading + CODE_PAD_V,
                      CODE_BG_R, CODE_BG_G, CODE_BG_B);

        /* text */
        float py = PDF_Y(pdf, pdf->y + fs + CODE_PAD_V / 2.0f);
        buf_printf(&pdf->content,
                   "BT /F%d %.2f Tf %.3f %.3f Td ",
                   FONT_MONO, (double)fs, (double)x_left, (double)py);
        buf_pdf_string(&pdf->content, line);
        buf_append(&pdf->content, " Tj ET\n", 7);

        pdf->y  += leading + CODE_PAD_V;
        total_h += leading + CODE_PAD_V;

        line = next;
    }
    free(copy);
    return total_h;
}

/* ── image renderer ───────────────────────────────────────────────────────── */

float pdf_image(PDF *pdf, const char *path, float max_width, float max_height)
{
    /* Resolve relative paths */
    char full_path[2048];
    if (path[0] == '/' || !pdf->input_dir[0]) {
        snprintf(full_path, sizeof(full_path), "%s", path);
    } else {
        /* Use snprintf with clamped sizes to avoid truncation warning */
        int n = snprintf(full_path, sizeof(full_path), "%s", pdf->input_dir);
        if (n > 0 && n < (int)sizeof(full_path) - 2) {
            full_path[n] = '/';
            snprintf(full_path + n + 1, sizeof(full_path) - (size_t)n - 1,
                     "%s", path);
        }
    }

    /* Check if already loaded */
    int idx = -1;
    for (int i = 0; i < pdf->image_count; i++) {
        if (strcmp(pdf->images[i].path, full_path) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (pdf->image_count >= MAX_IMAGES) return 0.0f;
        Image *img = image_load(full_path);
        if (!img) {
            fprintf(stderr, "Warning: cannot load image '%s'\n", full_path);
            return 0.0f;
        }
        idx = pdf->image_count++;
        pdf->images[idx].img = img;
        snprintf(pdf->images[idx].resname, sizeof(pdf->images[idx].resname),
                 "Im%d", idx);
        snprintf(pdf->images[idx].path, sizeof(pdf->images[idx].path),
                 "%s", full_path);
    }

    Image *img = pdf->images[idx].img;

    /* Scale to fit */
    float w_pts = (float)img->width  * (72.0f / 96.0f); /* assume 96 DPI */
    float h_pts = (float)img->height * (72.0f / 96.0f);
    float cw    = pdf_content_width(pdf);
    if (max_width  <= 0.0f) max_width  = cw;
    if (max_height <= 0.0f) max_height = (pdf->ph - pdf->mt - pdf->mb) * 0.7f;
    if (max_width  > cw) max_width = cw;

    if (w_pts > max_width) {
        h_pts = h_pts * max_width / w_pts;
        w_pts = max_width;
    }
    if (h_pts > max_height) {
        w_pts = w_pts * max_height / h_pts;
        h_pts = max_height;
    }

    pdf_ensure_space(pdf, h_pts + 12.0f);

    /* Centre the image horizontally */
    float x = pdf->ml + (cw - w_pts) / 2.0f;
    float py = PDF_Y(pdf, pdf->y + h_pts);

    buf_printf(&pdf->content,
               "q %.3f 0 0 %.3f %.3f %.3f cm /Img%d Do Q\n",
               (double)w_pts, (double)h_pts,
               (double)x, (double)py,
               idx);

    pdf->y += h_pts + 6.0f;
    return h_pts + 6.0f;
}

/* ── PDF file assembly ────────────────────────────────────────────────────── */

/* Append raw bytes to a file-building Buf */
static void fbuf_write(Buf *b, const char *s, size_t n) { buf_append(b, s, n); }
static void fbuf_printf(Buf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n < sizeof(tmp)) {
            buf_append(b, tmp, (size_t)n);
        } else {
            char *big = malloc((size_t)n + 1);
            if (big) {
                va_start(ap, fmt);
                vsnprintf(big, (size_t)n + 1, fmt, ap);
                va_end(ap);
                buf_append(b, big, (size_t)n);
                free(big);
            }
        }
    }
}

int pdf_write(PDF *pdf, const char *path)
{
    /* Finish last page */
    if (pdf->content.size > 0) {
        if (pdf->page_count >= pdf->page_alloc) {
            int na = pdf->page_alloc * 2;
            Buf *nb = realloc(pdf->pages, (size_t)na * sizeof(Buf));
            if (!nb) return -1;
            pdf->pages     = nb;
            pdf->page_alloc = na;
        }
        pdf->pages[pdf->page_count] = pdf->content;
        pdf->page_count++;
        pdf->content.data = NULL;
        pdf->content.size = 0;
        pdf->content.cap  = 0;
    }
    if (pdf->page_count == 0) return -1;

    /*
     * Object layout:
     *  1       Catalog
     *  2       Pages
     *  3..8    Font objects (F0-F5)
     *  9..9+2N-1  Content streams + Page objects (pairs per page)
     *  after:  Image XObjects
     */
    int n_pages  = pdf->page_count;
    int n_images = pdf->image_count;
    int obj_font_base    = 3;          /* objects 3..8 = fonts 0..5 */
    int obj_content_base = 9;          /* objects 9, 11, 13, … = content streams */
    int obj_page_base    = 10;         /* objects 10, 12, 14, … = page dicts */
    int obj_image_base   = obj_content_base + n_pages * 2;
    int total_objs       = obj_image_base + n_images;

    long *offsets = calloc((size_t)(total_objs + 2), sizeof(long));
    if (!offsets) return -1;

    Buf out = {0};

    /* PDF header */
    fbuf_printf(&out, "%%PDF-1.4\n%%\xe2\xe3\xcf\xd3\n");

    /* ── Font objects 3..8 ── */
    for (int fi = 0; fi < FONT_COUNT; fi++) {
        int oid = obj_font_base + fi;
        offsets[oid] = (long)out.size;
        fbuf_printf(&out,
                    "%d 0 obj\n"
                    "<< /Type /Font /Subtype /Type1 /BaseFont /%s"
                    " /Encoding /WinAnsiEncoding >>\n"
                    "endobj\n",
                    oid, FONT_NAMES[fi]);
    }

    /* ── Image XObjects (compress RGB with zlib, embed JPEG raw) ── */
    for (int ii = 0; ii < n_images; ii++) {
        int oid = obj_image_base + ii;
        offsets[oid] = (long)out.size;
        Image *img = pdf->images[ii].img;

        if (img->is_jpeg) {
            fbuf_printf(&out,
                        "%d 0 obj\n"
                        "<< /Type /XObject /Subtype /Image\n"
                        "   /Width %d /Height %d\n"
                        "   /ColorSpace /DeviceRGB /BitsPerComponent 8\n"
                        "   /Filter /DCTDecode /Length %zu >>\n"
                        "stream\n",
                        oid, img->width, img->height, img->data_size);
            fbuf_write(&out, (char *)img->data, img->data_size);
            fbuf_printf(&out, "\nendstream\nendobj\n");
        } else {
            /* Compress raw pixels with zlib */
            uLongf clen = compressBound((uLong)img->data_size);
            unsigned char *cbuf = malloc(clen);
            if (!cbuf) { free(offsets); buf_append(&out, "", 0); return -1; }
            if (compress(cbuf, &clen, img->data, (uLong)img->data_size) != Z_OK) {
                free(cbuf);
                /* Fall back to uncompressed */
                /* 4-channel images: write only the RGB channels (drop alpha) */
                const char *cs = (img->channels == 1) ? "/DeviceGray" : "/DeviceRGB";
                int out_ch = (img->channels >= 3) ? 3 : 1;
                size_t raw_sz = (size_t)img->width * (size_t)img->height * (size_t)out_ch;
                fbuf_printf(&out,
                            "%d 0 obj\n"
                            "<< /Type /XObject /Subtype /Image\n"
                            "   /Width %d /Height %d /ColorSpace %s\n"
                            "   /BitsPerComponent 8 /Length %zu >>\n"
                            "stream\n",
                            oid, img->width, img->height, cs, raw_sz);
                fbuf_write(&out, (char *)img->data, raw_sz);
                fbuf_printf(&out, "\nendstream\nendobj\n");
            } else {
                const char *cs = (img->channels == 1) ? "/DeviceGray" : "/DeviceRGB";
                fbuf_printf(&out,
                            "%d 0 obj\n"
                            "<< /Type /XObject /Subtype /Image\n"
                            "   /Width %d /Height %d /ColorSpace %s\n"
                            "   /BitsPerComponent 8\n"
                            "   /Filter /FlateDecode /Length %lu >>\n"
                            "stream\n",
                            oid, img->width, img->height, cs, (unsigned long)clen);
                fbuf_write(&out, (char *)cbuf, (size_t)clen);
                fbuf_printf(&out, "\nendstream\nendobj\n");
                free(cbuf);
            }
        }
    }

    /* ── Content streams and Page objects ── */
    /* Build font resource string once */
    char font_res[512];
    {
        int k = 0;
        int rem = (int)sizeof(font_res);
        int n2 = snprintf(font_res + k, (size_t)rem, "<< ");
        if (n2 > 0 && n2 < rem) { k += n2; rem -= n2; }
        for (int fi = 0; fi < FONT_COUNT && rem > 0; fi++) {
            n2 = snprintf(font_res + k, (size_t)rem,
                          "/F%d %d 0 R ", fi, obj_font_base + fi);
            if (n2 > 0 && n2 < rem) { k += n2; rem -= n2; }
        }
        if (rem > 2) snprintf(font_res + k, (size_t)rem, ">>");
    }
    /* Build XObject resource string */
    char xobj_res[2048] = "<< ";
    for (int ii = 0; ii < n_images; ii++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "/Img%d %d 0 R ", ii, obj_image_base + ii);
        strncat(xobj_res, tmp, sizeof(xobj_res) - strlen(xobj_res) - 1);
    }
    strncat(xobj_res, ">>", sizeof(xobj_res) - strlen(xobj_res) - 1);

    for (int pi = 0; pi < n_pages; pi++) {
        int cs_oid   = obj_content_base + pi * 2;
        int page_oid = obj_page_base    + pi * 2;

        /* Content stream */
        offsets[cs_oid] = (long)out.size;
        size_t cs_len = pdf->pages[pi].size;
        fbuf_printf(&out,
                    "%d 0 obj\n<< /Length %zu >>\nstream\n",
                    cs_oid, cs_len);
        if (cs_len > 0)
            fbuf_write(&out, pdf->pages[pi].data, cs_len);
        fbuf_printf(&out, "endstream\nendobj\n");

        /* Page object */
        offsets[page_oid] = (long)out.size;
        fbuf_printf(&out,
                    "%d 0 obj\n"
                    "<< /Type /Page /Parent 2 0 R\n"
                    "   /MediaBox [0 0 %.3f %.3f]\n"
                    "   /Resources << /Font %s",
                    page_oid,
                    (double)pdf->pw, (double)pdf->ph,
                    font_res);
        if (n_images > 0)
            fbuf_printf(&out, " /XObject %s", xobj_res);
        fbuf_printf(&out,
                    " >>\n"
                    "   /Contents %d 0 R >>\n"
                    "endobj\n",
                    cs_oid);
    }

    /* ── Pages object (2) ── */
    offsets[2] = (long)out.size;
    fbuf_printf(&out, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", n_pages);
    for (int pi = 0; pi < n_pages; pi++)
        fbuf_printf(&out, "%d 0 R ", obj_page_base + pi * 2);
    fbuf_printf(&out, "] >>\nendobj\n");

    /* ── Catalog (1) ── */
    offsets[1] = (long)out.size;
    fbuf_printf(&out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    /* ── xref table ── */
    long xref_offset = (long)out.size;
    fbuf_printf(&out, "xref\n0 %d\n", total_objs + 1);
    fbuf_printf(&out, "0000000000 65535 f \n");
    for (int i = 1; i <= total_objs; i++) {
        if (offsets[i])
            fbuf_printf(&out, "%010ld 00000 n \n", offsets[i]);
        else
            fbuf_printf(&out, "0000000000 65535 f \n");
    }

    /* ── trailer ── */
    fbuf_printf(&out,
                "trailer\n<< /Size %d /Root 1 0 R >>\n"
                "startxref\n%ld\n%%%%EOF\n",
                total_objs + 1, xref_offset);

    free(offsets);

    /* Write to file */
    FILE *f = fopen(path, "wb");
    if (!f) { free(out.data); return -1; }
    fwrite(out.data, 1, out.size, f);
    fclose(f);
    free(out.data);
    return 0;
}
