/*
 * markdown.c -- Markdown to PDF renderer
 *
 * Supported block elements:
 *   ATX headings (# through ######), setext headings (=== / ---),
 *   fenced code blocks (``` or ~~~), indented code blocks (4 spaces / tab),
 *   unordered lists (- * +), ordered lists (N.),
 *   blockquotes (>), horizontal rules (--- *** ___),
 *   paragraphs, images ![alt](path).
 *
 * Supported inline elements:
 *   **bold** / __bold__, *italic* / _italic_,
 *   ***bold-italic*** / ___bold-italic___, `code`,
 *   [link text](url), ![alt](path).
 */

#include "markdown.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf.h"

/* ---- typographic constants -------------------------------------------- */
#define BODY_SIZE     11.0f
#define CODE_SIZE     9.5f
#define H1_SIZE       24.0f
#define H2_SIZE       18.0f
#define H3_SIZE       16.0f
#define H4_SIZE       14.0f
#define H5_SIZE       12.0f
#define H6_SIZE       11.0f

#define PARA_BEFORE   6.0f
#define PARA_AFTER    4.0f
#define HEAD_BEFORE   14.0f
#define HEAD_AFTER    6.0f
#define CODE_BEFORE   8.0f
#define CODE_AFTER    8.0f
#define BULLET_INDENT 8.0f
#define NUMBER_INDENT 8.0f
#define LIST_INDENT   14.0f
#define LIST_ITEM_SP  2.0f
#define QUOTE_INDENT  16.0f
#define HR_BEFORE     10.0f
#define HR_AFTER      10.0f
#define IMG_BEFORE    8.0f
#define IMG_AFTER     8.0f
#define QUOTE_BAR_W   3.0f

/* ---- table constants --------------------------------------------------- */
#define TABLE_CELL_PAD_H  6.0f  /* horizontal padding inside each cell */
#define TABLE_CELL_PAD_V  4.0f  /* vertical padding inside each cell (each side) */
#define TABLE_BEFORE      8.0f  /* vertical space before the table */
#define TABLE_AFTER       8.0f  /* vertical space after the table */
#define TABLE_BORDER_R    0.70f /* cell border colour */
#define TABLE_BORDER_G    0.70f
#define TABLE_BORDER_B    0.70f
#define TABLE_HDR_BG_R    0.92f /* header row background */
#define TABLE_HDR_BG_G    0.92f
#define TABLE_HDR_BG_B    0.95f
#define TABLE_MIN_COL_W   10.0f /* minimum column content width in points */
#define MAX_COLS          32    /* max columns in a table */
#define MAX_TABLE_ROWS    256   /* max rows collected per table */

/* ---- small string utilities ------------------------------------------- */

static void rtrim(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && ((unsigned char)s[n - 1] <= ' ')) s[--n] = '\0';
}

static const char* ltrim_ptr(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* ---- table utilities --------------------------------------------------- */

/* A row is a table row if it contains at least one pipe character. */
static int is_table_row(const char* line) { return strchr(line, '|') != NULL; }

/*
 * A separator row contains only '|', '-', ':', and whitespace and must have
 * at least one '|' and one '-'.
 */
static int is_table_sep_row(const char* line) {
    if (!strchr(line, '|')) return 0;
    if (!strchr(line, '-')) return 0;
    for (const char* p = line; *p; p++) {
        char c = *p;
        if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') return 0;
    }
    return 1;
}

/* TableRow: a parsed table row with up to MAX_COLS cell strings. */
typedef struct {
    char* cells[MAX_COLS];
    int   col_count;
    int   is_header; /* 1 for the header row */
} TableRow;

/*
 * Parse cell strings from a table row line, splitting on '|'.
 * Cells are trimmed of leading/trailing whitespace.
 * Returns number of cells parsed (stored in row->cells[]; ownership transferred).
 */
static int parse_table_row(const char* line, TableRow* row) {
    const char* p = ltrim_ptr(line);
    if (*p == '|') p++; /* skip optional leading pipe */

    int n = 0;
    while (*p && n < MAX_COLS) {
        const char* end = strchr(p, '|');
        size_t      len = end ? (size_t)(end - p) : strlen(p);

        /* ltrim */
        const char* cs = p;
        while (len > 0 && (*cs == ' ' || *cs == '\t')) {
            cs++;
            len--;
        }
        /* rtrim */
        while (len > 0 && (unsigned char)cs[len - 1] <= ' ') len--;

        /* copy trimmed content */
        char* cell = malloc(len + 1);
        if (!cell) break;
        memcpy(cell, cs, len);
        cell[len] = '\0';
        row->cells[n++] = cell;

        if (end)
            p = end + 1;
        else
            break;
    }
    row->col_count = n;
    return n;
}

static void table_row_free(TableRow* row) {
    for (int c = 0; c < row->col_count; c++) {
        free(row->cells[c]);
        row->cells[c] = NULL;
    }
    row->col_count = 0;
}

/* ---- table renderer ---------------------------------------------------- */

static void render_table(PDF* pdf, TableRow* rows, int row_count, int col_count) {
    if (row_count == 0 || col_count == 0) return;

    float cw = pdf_content_width(pdf);
    float ml = pdf_margin_left(pdf);

    /* --- compute natural column widths ------------------------------------ */
    float nat_w[MAX_COLS];
    for (int c = 0; c < col_count; c++) nat_w[c] = 0.0f;

    for (int r = 0; r < row_count; r++) {
        int font = rows[r].is_header ? FONT_BOLD : FONT_NORMAL;
        for (int c = 0; c < rows[r].col_count; c++) {
            if (!rows[r].cells[c]) continue;
            float w = pdf_inline_width(rows[r].cells[c], BODY_SIZE, font);
            if (w > nat_w[c]) nat_w[c] = w;
        }
    }

    /* --- scale columns to fit content width ------------------------------- */
    /* total available width for content (inside padding) */
    float total_nat = 0.0f;
    for (int c = 0; c < col_count; c++) total_nat += nat_w[c];

    float total_content_avail = cw - (float)col_count * 2.0f * TABLE_CELL_PAD_H;
    if (total_content_avail < (float)col_count * TABLE_MIN_COL_W)
        total_content_avail = (float)col_count * TABLE_MIN_COL_W; /* minimum sane width */

    if (total_nat > total_content_avail && total_nat > 0.0f) {
        float scale = total_content_avail / total_nat;
        for (int c = 0; c < col_count; c++) nat_w[c] *= scale;
    }

    /* --- column x positions (content-area-relative) ----------------------- */
    /* col_x[c] = left edge of column c (including its left cell padding) */
    float col_x[MAX_COLS + 1];
    col_x[0] = 0.0f;
    for (int c = 0; c < col_count; c++)
        col_x[c + 1] = col_x[c] + nat_w[c] + 2.0f * TABLE_CELL_PAD_H;

    float table_w = col_x[col_count];

    /* --- render each row -------------------------------------------------- */
    for (int r = 0; r < row_count; r++) {
        int font = rows[r].is_header ? FONT_BOLD : FONT_NORMAL;

        /* measure row height: max over all cells */
        float leading = BODY_SIZE * 1.4f;
        float row_h   = leading; /* at least one line */
        for (int c = 0; c < rows[r].col_count; c++) {
            if (!rows[r].cells[c] || !rows[r].cells[c][0]) continue;
            float li = col_x[c] + TABLE_CELL_PAD_H;
            float ri = cw - col_x[c + 1] + TABLE_CELL_PAD_H;
            float h  = pdf_measure_paragraph(pdf, rows[r].cells[c], li, ri, BODY_SIZE, font, 0.0f);
            if (h > row_h) row_h = h;
        }
        row_h += 2.0f * TABLE_CELL_PAD_V;

        /* ensure the whole row fits on the page */
        pdf_ensure_space(pdf, row_h + 1.0f);
        float y0 = pdf_get_y(pdf);

        /* header background */
        if (rows[r].is_header)
            pdf_rect_fill(pdf, ml, y0, table_w, row_h, TABLE_HDR_BG_R, TABLE_HDR_BG_G,
                          TABLE_HDR_BG_B);

        /* top border of this row */
        pdf_hline(pdf, ml, y0, table_w, TABLE_BORDER_R, TABLE_BORDER_G, TABLE_BORDER_B, 0.5f);

        /* left and right outer borders */
        pdf_vbar(pdf, ml, y0, row_h, TABLE_BORDER_R, TABLE_BORDER_G, TABLE_BORDER_B, 0.5f);
        pdf_vbar(pdf, ml + table_w, y0, row_h, TABLE_BORDER_R, TABLE_BORDER_G, TABLE_BORDER_B,
                 0.5f);

        /* inner column dividers */
        for (int c = 1; c < col_count; c++)
            pdf_vbar(pdf, ml + col_x[c], y0, row_h, TABLE_BORDER_R, TABLE_BORDER_G, TABLE_BORDER_B,
                     0.5f);

        /* render cell text */
        for (int c = 0; c < rows[r].col_count; c++) {
            if (!rows[r].cells[c] || !rows[r].cells[c][0]) continue;
            float li = col_x[c] + TABLE_CELL_PAD_H;
            float ri = cw - col_x[c + 1] + TABLE_CELL_PAD_H;

            /* position Y at cell top + padding */
            float cell_y = y0 + TABLE_CELL_PAD_V;
            pdf_advance_y(pdf, cell_y - pdf_get_y(pdf));

            pdf_paragraph(pdf, rows[r].cells[c], li, ri, BODY_SIZE, font, 0.0f);
        }

        /* advance past the full row height */
        pdf_advance_y(pdf, y0 + row_h - pdf_get_y(pdf));
    }

    /* bottom border of the last row */
    pdf_hline(pdf, ml, pdf_get_y(pdf), table_w, TABLE_BORDER_R, TABLE_BORDER_G, TABLE_BORDER_B,
              0.5f);
}

/* Count leading '#' for ATX headings; 0 if not a heading. */
static int atx_level(const char* line) {
    int n = 0;
    while (line[n] == '#') n++;
    if (n == 0 || n > 6) return 0;
    if (line[n] != ' ' && line[n] != '\t') return 0;
    return n;
}

/* Is the line a setext underline?  Returns 1 (=) or 2 (-) or 0. */
static int setext_underline(const char* line) {
    const char* p = ltrim_ptr(line);
    if (*p == '=') {
        while (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        return (*p == '\0') ? 1 : 0;
    }
    if (*p == '-') {
        while (*p == '-') p++;
        while (*p == ' ' || *p == '\t') p++;
        return (*p == '\0') ? 2 : 0;
    }
    return 0;
}

/* Is the line a thematic break (HR)? */
static int is_hr(const char* line) {
    const char* p = ltrim_ptr(line);
    char        c = *p;
    if (c != '-' && c != '*' && c != '_') return 0;
    int count = 0;
    while (*p) {
        if (*p == c)
            count++;
        else if (*p != ' ' && *p != '\t')
            return 0;
        p++;
    }
    return count >= 3;
}

/* Is the line an unordered list item? Returns indent+1 or 0. */
static int ul_item(const char* line, int* indent_out) {
    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t') indent++;
    char c = line[indent];
    if ((c == '-' || c == '*' || c == '+') &&
        (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        if (indent_out) *indent_out = indent;
        return 1;
    }
    return 0;
}

/* Is the line an ordered list item?  Returns the number or 0. */
static int ol_item(const char* line, int* num_out, int* indent_out) {
    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t') indent++;
    int i = indent;
    if (!isdigit((unsigned char)line[i])) return 0;
    int num = 0;
    while (isdigit((unsigned char)line[i])) {
        num = num * 10 + (line[i] - '0');
        i++;
    }
    if ((line[i] == '.' || line[i] == ')') && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
        if (num_out) *num_out = num;
        if (indent_out) *indent_out = indent;
        return 1;
    }
    return 0;
}

/* Is the line a fenced code block delimiter (``` or ~~~)? */
static int fence_delim(const char* line, char* fence_char_out) {
    const char* p = ltrim_ptr(line);
    char        c = *p;
    if (c != '`' && c != '~') return 0;
    int n = 0;
    while (*p == c) {
        n++;
        p++;
    }
    if (n >= 3) {
        if (fence_char_out) *fence_char_out = c;
        return n;
    }
    return 0;
}

/* Is the line an indented code block line (4 spaces or 1 tab)? */
static int indented_code(const char* line) {
    if (strncmp(line, "    ", 4) == 0) return 1;
    if (line[0] == '\t') return 1;
    return 0;
}

/* Strip the list-item prefix and return pointer to the text. */
static const char* ul_text(const char* line) {
    const char* p = ltrim_ptr(line);
    p++; /* skip - * + */
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char* ol_text(const char* line) {
    const char* p = ltrim_ptr(line);
    while (isdigit((unsigned char)*p)) p++;
    p++; /* skip . or ) */
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* ---- heading font helper ---------------------------------------------- */

static float heading_size(int level) {
    switch (level) {
        case 1:
            return H1_SIZE;
        case 2:
            return H2_SIZE;
        case 3:
            return H3_SIZE;
        case 4:
            return H4_SIZE;
        case 5:
            return H5_SIZE;
        default:
            return H6_SIZE;
    }
}

/* ---- image extraction from inline text --------------------------------- */
/*
 * If the line (after trimming) is a standalone image ![alt](path), copy
 * the path into path_out (up to path_max bytes) and return 1.
 */
static int extract_image(const char* text, char* path_out, size_t path_max) {
    const char* p = ltrim_ptr(text);
    if (p[0] != '!' || p[1] != '[') return 0;
    const char* alt_end = strchr(p + 2, ']');
    if (!alt_end) return 0;
    if (alt_end[1] != '(') return 0;
    const char* path_start = alt_end + 2;
    const char* path_end = strchr(path_start, ')');
    if (!path_end) return 0;
    size_t len = (size_t)(path_end - path_start);
    if (len >= path_max) len = path_max - 1;
    memcpy(path_out, path_start, len);
    path_out[len] = '\0';
    /* Check nothing follows on the line */
    const char* after = ltrim_ptr(path_end + 1);
    return (*after == '\0');
}

/* ---- line-array helpers ------------------------------------------------ */

/* Split text into lines; returns malloc'd array of malloc'd strings. */
static char** split_lines(const char* text, int* count_out) {
    int         count = 0;
    const char* p = text;
    while (*p) {
        if (*p == '\n') count++;
        p++;
    }
    count++; /* last line with no trailing newline */

    char** lines = malloc((size_t)(count + 1) * sizeof(char*));
    if (!lines) {
        *count_out = 0;
        return NULL;
    }

    int         i = 0;
    const char* start = text;
    p = text;
    while (1) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            lines[i] = malloc(len + 1);
            if (!lines[i]) { /* OOM: clean up */
                break;
            }
            memcpy(lines[i], start, len);
            lines[i][len] = '\0';
            rtrim(lines[i]);
            i++;
            if (*p == '\0') break;
            start = p + 1;
        }
        p++;
    }
    lines[i] = NULL;
    *count_out = i;
    return lines;
}

static void free_lines(char** lines, int count) {
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

/* ---- render a heading -------------------------------------------------- */

static void render_heading(PDF* pdf, const char* text, int level) {
    float size = heading_size(level);
    float leading = size * 1.2f;
    float cw = pdf_content_width(pdf);
    float ml = pdf_margin_left(pdf);
    float h_needed =
        HEAD_BEFORE + leading + HEAD_AFTER + (level <= 2 ? 4.0f : 0.0f); /* extra for underline */

    pdf_ensure_space(pdf, h_needed);
    pdf_advance_y(pdf, HEAD_BEFORE);

    int font = FONT_BOLD;
    pdf_text_line(pdf, text, font, size, 0.0f, leading);

    /* H1 / H2 get a decorative underline */
    if (level == 1) {
        pdf_hline(pdf, ml, pdf_get_y(pdf) + 5.0f, cw, 0.89f, 0.90f, 0.93f, 0.75f);
        pdf_advance_y(pdf, 6.0f);
    } else if (level == 2) {
        pdf_hline(pdf, ml, pdf_get_y(pdf) + 5.0f, cw, 0.89f, 0.90f, 0.93f, 0.75f);
        pdf_advance_y(pdf, 6.0f);
    }
    pdf_advance_y(pdf, HEAD_AFTER);
}

/* ---- render a paragraph ------------------------------------------------ */

static void render_paragraph(PDF* pdf, const char* text, float left_indent, float right_indent,
                             float font_size, int base_font) {
    /* Check for standalone image */
    char img_path[1024];
    if (extract_image(text, img_path, sizeof(img_path))) {
        pdf_advance_y(pdf, IMG_BEFORE);
        pdf_image(pdf, img_path, 0.0f, 0.0f);
        pdf_advance_y(pdf, IMG_AFTER);
        return;
    }

    pdf_advance_y(pdf, PARA_BEFORE);
    pdf_paragraph(pdf, text, left_indent, right_indent, font_size, base_font, 0.0f);
    pdf_advance_y(pdf, PARA_AFTER);
}

/* ---- collect continuation lines for a paragraph ----------------------- */

/*
 * Append ' ' + line to dest (growing it as needed).
 * Returns 1 on success.
 */
static int para_append(char** dest, size_t* cap, const char* line) {
    size_t dlen = strlen(*dest);
    size_t llen = strlen(line);
    size_t need = dlen + llen + 2; /* space + null */
    if (need > *cap) {
        size_t newcap = *cap ? *cap * 2 : 256;
        while (newcap < need) newcap *= 2;
        char* p = realloc(*dest, newcap);
        if (!p) return 0;
        *dest = p;
        *cap = newcap;
    }
    if (dlen > 0) (*dest)[dlen++] = ' ';
    memcpy(*dest + dlen, line, llen + 1);
    return 1;
}

/* ---- list rendering ---------------------------------------------------- */

typedef struct {
    char* text; /* may contain inline markup */
} ListItem;

static void render_ulist(PDF* pdf, ListItem* items, int n, float extra_indent) {
    float ml = pdf_margin_left(pdf);
    float bullet_x = ml + extra_indent + BULLET_INDENT;
    float text_x = bullet_x + LIST_INDENT;
    float leading = BODY_SIZE * 1.4f;

    for (int i = 0; i < n; i++) {
        pdf_ensure_space(pdf, leading + LIST_ITEM_SP);

        /* Bullet — pass explicit leading so we know how much it advances */
        pdf_text_line(pdf, "\xe2\x80\xa2", FONT_NORMAL, BODY_SIZE, bullet_x - ml, leading);
        /* Go back up by the same amount */
        pdf_advance_y(pdf, -leading);

        /* Item text — same leading, same starting y */
        float used =
            pdf_paragraph(pdf, items[i].text, text_x - ml, 0.0f, BODY_SIZE, FONT_NORMAL, leading);
        /* If text was shorter than one line height, pad */
        if (used < leading) pdf_advance_y(pdf, leading - used);
        pdf_advance_y(pdf, LIST_ITEM_SP);
    }
}

static void render_olist(PDF* pdf, ListItem* items, int n, int start, float extra_indent) {
    float ml = pdf_margin_left(pdf);
    float num_x = ml + extra_indent + NUMBER_INDENT;
    float text_x = num_x + LIST_INDENT;
    float leading = BODY_SIZE * 1.4f;

    for (int i = 0; i < n; i++) {
        char num_str[16];
        snprintf(num_str, sizeof(num_str), "%d.", start + i);

        pdf_ensure_space(pdf, leading + LIST_ITEM_SP);

        /* Number */
        pdf_text_line(pdf, num_str, FONT_NORMAL, BODY_SIZE, num_x - ml, leading);
        pdf_advance_y(pdf, -leading);

        /* Text */
        float used =
            pdf_paragraph(pdf, items[i].text, text_x - ml, 0.0f, BODY_SIZE, FONT_NORMAL, leading);
        if (used < leading) pdf_advance_y(pdf, leading - used);
        pdf_advance_y(pdf, LIST_ITEM_SP);
    }
}

/* ---- blockquote -------------------------------------------------------- */

static void render_blockquote(PDF* pdf, const char* text) {
    float y_start = pdf_get_y(pdf);
    float bar_x = pdf_margin_left(pdf);
    float left_ind = QUOTE_INDENT;

    pdf_advance_y(pdf, PARA_BEFORE);
    pdf_paragraph(pdf, text, left_ind, 0.0f, BODY_SIZE, FONT_ITALIC, 0.0f);
    pdf_advance_y(pdf, PARA_AFTER);

    float y_end = pdf_get_y(pdf);
    pdf_vbar(pdf, bar_x, y_start, y_end - y_start, 0.5f, 0.5f, 0.5f, QUOTE_BAR_W);
}

/* ---- code block accumulator ------------------------------------------- */

/* Append a line to a growing code buffer. */
static int code_append(char** dest, size_t* cap, const char* line) {
    size_t dlen = *dest ? strlen(*dest) : 0;
    size_t llen = strlen(line);
    size_t need = dlen + llen + 2; /* newline + null */
    if (need > *cap) {
        size_t nc = *cap ? *cap * 2 : 512;
        while (nc < need) nc *= 2;
        char* p = realloc(*dest, nc);
        if (!p) return 0;
        *dest = p;
        *cap = nc;
    }
    if (dlen > 0) (*dest)[dlen++] = '\n';
    memcpy(*dest + dlen, line, llen + 1);
    return 1;
}

/* ---- list item accumulator -------------------------------------------- */

#define MAX_ITEMS 512

static void items_free(ListItem* items, int n) {
    for (int i = 0; i < n; i++) free(items[i].text);
}

/* ---- main entry point -------------------------------------------------- */

int markdown_to_pdf(const char* content, PDF* pdf, const char* input_path) {
    /* Set input directory so images can be found. */
    if (input_path) {
        char dir[4096];
        snprintf(dir, sizeof(dir), "%s", input_path);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            pdf_set_input_dir(pdf, dir);
        } else {
            pdf_set_input_dir(pdf, ".");
        }
    }

    int    line_count = 0;
    char** lines = split_lines(content, &line_count);
    if (!lines) return -1;

    /* State */
    enum {
        ST_NORMAL,
        ST_PARA,
        ST_CODE_FENCE,
        ST_CODE_INDENT,
        ST_ULIST,
        ST_OLIST,
        ST_BLOCKQUOTE,
        ST_TABLE,
    } state = ST_NORMAL;

    char*  para_buf = NULL;
    size_t para_cap = 0;
    char*  code_buf = NULL;
    size_t code_cap = 0;
    char*  quote_buf = NULL;
    size_t quote_cap = 0;
    char   fence_char = '`';
    float  list_indent = 0.0f;
    int    ol_start = 1;

    /* Table accumulator */
    TableRow tbl_rows[MAX_TABLE_ROWS];
    int      tbl_row_count = 0;
    int      tbl_col_count = 0;

    ListItem items[MAX_ITEMS];
    int      item_count = 0;

    /* Helper: flush accumulated paragraph */
#define FLUSH_PARA()                                                             \
    do {                                                                         \
        if (para_buf && para_buf[0]) {                                           \
            render_paragraph(pdf, para_buf, 0.0f, 0.0f, BODY_SIZE, FONT_NORMAL); \
            para_buf[0] = '\0';                                                  \
        }                                                                        \
    } while (0)

    /* Helper: flush code block */
#define FLUSH_CODE()                             \
    do {                                         \
        if (code_buf && code_buf[0]) {           \
            pdf_advance_y(pdf, CODE_BEFORE);     \
            pdf_code_block(pdf, code_buf, 0.0f); \
            pdf_advance_y(pdf, CODE_AFTER);      \
            code_buf[0] = '\0';                  \
        }                                        \
    } while (0)

    /* Helper: flush unordered list */
#define FLUSH_ULIST()                                          \
    do {                                                       \
        if (item_count > 0) {                                  \
            pdf_advance_y(pdf, PARA_BEFORE);                   \
            render_ulist(pdf, items, item_count, list_indent); \
            pdf_advance_y(pdf, PARA_AFTER);                    \
            items_free(items, item_count);                     \
            item_count = 0;                                    \
        }                                                      \
    } while (0)

    /* Helper: flush ordered list */
#define FLUSH_OLIST()                                                    \
    do {                                                                 \
        if (item_count > 0) {                                            \
            pdf_advance_y(pdf, PARA_BEFORE);                             \
            render_olist(pdf, items, item_count, ol_start, list_indent); \
            pdf_advance_y(pdf, PARA_AFTER);                              \
            items_free(items, item_count);                               \
            item_count = 0;                                              \
        }                                                                \
    } while (0)

    /* Helper: flush blockquote */
#define FLUSH_QUOTE()                          \
    do {                                       \
        if (quote_buf && quote_buf[0]) {       \
            render_blockquote(pdf, quote_buf); \
            quote_buf[0] = '\0';               \
        }                                      \
    } while (0)

    /* Helper: flush table */
#define FLUSH_TABLE()                                                               \
    do {                                                                            \
        if (tbl_row_count > 0) {                                                    \
            pdf_advance_y(pdf, TABLE_BEFORE);                                       \
            render_table(pdf, tbl_rows, tbl_row_count, tbl_col_count);              \
            pdf_advance_y(pdf, TABLE_AFTER);                                        \
            for (int _r = 0; _r < tbl_row_count; _r++) table_row_free(&tbl_rows[_r]); \
            tbl_row_count = 0;                                                      \
            tbl_col_count = 0;                                                      \
        }                                                                           \
    } while (0)

    for (int li = 0; li < line_count; li++) {
        const char* line = lines[li];

        /* ---- inside a fenced code block ------------------------------- */
        if (state == ST_CODE_FENCE) {
            char fc2 = ' ';
            if (fence_delim(line, &fc2) >= 3 && fc2 == fence_char) {
                /* closing fence */
                FLUSH_CODE();
                state = ST_NORMAL;
            } else {
                code_append(&code_buf, &code_cap, line);
            }
            continue;
        }

        /* ---- blank line ----------------------------------------------- */
        if (line[0] == '\0') {
            switch (state) {
                case ST_PARA:
                    FLUSH_PARA();
                    state = ST_NORMAL;
                    break;
                case ST_CODE_INDENT:
                    /* A blank line inside an indented code block is kept */
                    code_append(&code_buf, &code_cap, "");
                    break;
                case ST_ULIST:
                    FLUSH_ULIST();
                    state = ST_NORMAL;
                    break;
                case ST_OLIST:
                    FLUSH_OLIST();
                    state = ST_NORMAL;
                    break;
                case ST_BLOCKQUOTE:
                    FLUSH_QUOTE();
                    state = ST_NORMAL;
                    break;
                case ST_TABLE:
                    FLUSH_TABLE();
                    state = ST_NORMAL;
                    break;
                default:
                    break;
            }
            continue;
        }

        /* ---- look-ahead: setext heading? ------------------------------ */
        if (li + 1 < line_count) {
            int sul = setext_underline(lines[li + 1]);
            if (sul && line[0] != '\0' && !atx_level(line)) {
                /* Flush whatever was in progress */
                switch (state) {
                    case ST_PARA:
                        FLUSH_PARA();
                        break;
                    case ST_ULIST:
                        FLUSH_ULIST();
                        break;
                    case ST_OLIST:
                        FLUSH_OLIST();
                        break;
                    case ST_BLOCKQUOTE:
                        FLUSH_QUOTE();
                        break;
                    case ST_CODE_INDENT:
                        FLUSH_CODE();
                        break;
                    case ST_TABLE:
                        FLUSH_TABLE();
                        break;
                    default:
                        break;
                }
                render_heading(pdf, line, sul);
                li++; /* skip underline */
                state = ST_NORMAL;
                continue;
            }
        }

        /* ---- look-ahead: table header? -------------------------------- */
        {
            int line_has_pipe = is_table_row(line);
            if (li + 1 < line_count && line_has_pipe && is_table_sep_row(lines[li + 1])) {
                /* Flush whatever was in progress */
                switch (state) {
                    case ST_PARA:
                        FLUSH_PARA();
                        break;
                    case ST_ULIST:
                        FLUSH_ULIST();
                        break;
                    case ST_OLIST:
                        FLUSH_OLIST();
                        break;
                    case ST_BLOCKQUOTE:
                        FLUSH_QUOTE();
                        break;
                    case ST_CODE_INDENT:
                        FLUSH_CODE();
                        break;
                    case ST_TABLE:
                        FLUSH_TABLE(); /* shouldn't happen, but be safe */
                        break;
                    default:
                        break;
                }
                /* Parse header row */
                tbl_rows[0].is_header = 1;
                int nc = parse_table_row(line, &tbl_rows[0]);
                tbl_row_count = 1;
                tbl_col_count = nc;
                li++; /* skip the separator row */
                state = ST_TABLE;
                continue;
            }
        }

        /* ---- ATX heading --------------------------------------------- */
        {
            int hlv = atx_level(line);
            if (hlv > 0) {
                switch (state) {
                    case ST_PARA:
                        FLUSH_PARA();
                        break;
                    case ST_ULIST:
                        FLUSH_ULIST();
                        break;
                    case ST_OLIST:
                        FLUSH_OLIST();
                        break;
                    case ST_BLOCKQUOTE:
                        FLUSH_QUOTE();
                        break;
                    case ST_CODE_INDENT:
                        FLUSH_CODE();
                        break;
                    case ST_TABLE:
                        FLUSH_TABLE();
                        break;
                    default:
                        break;
                }
                const char* htxt = ltrim_ptr(line + hlv);
                /* Strip optional trailing ### */
                char hbuf[1024];
                snprintf(hbuf, sizeof(hbuf), "%s", htxt);
                rtrim(hbuf);
                /* Remove trailing # */
                int hlen = (int)strlen(hbuf);
                while (hlen > 0 && hbuf[hlen - 1] == '#') hlen--;
                while (hlen > 0 && hbuf[hlen - 1] == ' ') hlen--;
                hbuf[hlen] = '\0';
                render_heading(pdf, hbuf, hlv);
                state = ST_NORMAL;
                continue;
            }
        }

        /* ---- horizontal rule ------------------------------------------ */
        if (is_hr(line)) {
            switch (state) {
                case ST_PARA:
                    FLUSH_PARA();
                    break;
                case ST_ULIST:
                    FLUSH_ULIST();
                    break;
                case ST_OLIST:
                    FLUSH_OLIST();
                    break;
                case ST_BLOCKQUOTE:
                    FLUSH_QUOTE();
                    break;
                case ST_CODE_INDENT:
                    FLUSH_CODE();
                    break;
                case ST_TABLE:
                    FLUSH_TABLE();
                    break;
                default:
                    break;
            }
            pdf_advance_y(pdf, HR_BEFORE);
            pdf_ensure_space(pdf, 4.0f);
            pdf_hline(pdf, pdf_margin_left(pdf), pdf_get_y(pdf), pdf_content_width(pdf), 0.89f,
                      0.90f, 0.93f, 0.75f);
            pdf_advance_y(pdf, 4.0f + HR_AFTER);
            state = ST_NORMAL;
            continue;
        }

        /* ---- fenced code block start ---------------------------------- */
        {
            char fc = '`';
            if (fence_delim(line, &fc) >= 3) {
                switch (state) {
                    case ST_PARA:
                        FLUSH_PARA();
                        break;
                    case ST_ULIST:
                        FLUSH_ULIST();
                        break;
                    case ST_OLIST:
                        FLUSH_OLIST();
                        break;
                    case ST_BLOCKQUOTE:
                        FLUSH_QUOTE();
                        break;
                    case ST_CODE_INDENT:
                        FLUSH_CODE();
                        break;
                    case ST_TABLE:
                        FLUSH_TABLE();
                        break;
                    default:
                        break;
                }
                fence_char = fc;
                if (code_buf) code_buf[0] = '\0';
                state = ST_CODE_FENCE;
                continue;
            }
        }

        /* ---- indented code block -------------------------------------- */
        if (indented_code(line) && state != ST_PARA && state != ST_ULIST && state != ST_OLIST &&
            state != ST_TABLE) {
            if (state != ST_CODE_INDENT) {
                switch (state) {
                    case ST_BLOCKQUOTE:
                        FLUSH_QUOTE();
                        break;
                    default:
                        break;
                }
                if (code_buf) code_buf[0] = '\0';
                state = ST_CODE_INDENT;
            }
            const char* code_line = (line[0] == '\t') ? line + 1 : line + 4;
            code_append(&code_buf, &code_cap, code_line);
            continue;
        } else if (state == ST_CODE_INDENT) {
            FLUSH_CODE();
            state = ST_NORMAL;
        }

        /* ---- table data row ------------------------------------------- */
        if (state == ST_TABLE) {
            if (is_table_row(line) && tbl_row_count < MAX_TABLE_ROWS) {
                TableRow* row = &tbl_rows[tbl_row_count];
                row->is_header = 0;
                int nc = parse_table_row(line, row);
                if (nc > tbl_col_count) tbl_col_count = nc;
                tbl_row_count++;
                continue;
            } else {
                FLUSH_TABLE();
                state = ST_NORMAL;
                /* fall through to process this line normally */
            }
        }

        /* ---- blockquote ----------------------------------------------- */
        if (line[0] == '>') {
            if (state != ST_BLOCKQUOTE) {
                switch (state) {
                    case ST_PARA:
                        FLUSH_PARA();
                        break;
                    case ST_ULIST:
                        FLUSH_ULIST();
                        break;
                    case ST_OLIST:
                        FLUSH_OLIST();
                        break;
                    default:
                        break;
                }
                if (quote_buf) quote_buf[0] = '\0';
                state = ST_BLOCKQUOTE;
            }
            const char* qt = line + 1;
            if (*qt == ' ') qt++;
            if (quote_buf && quote_buf[0])
                para_append(&quote_buf, &quote_cap, qt);
            else {
                /* first line */
                if (!quote_buf) {
                    quote_buf = malloc(512);
                    quote_cap = 512;
                    if (!quote_buf) continue;
                }
                snprintf(quote_buf, quote_cap, "%s", qt);
            }
            continue;
        } else if (state == ST_BLOCKQUOTE) {
            FLUSH_QUOTE();
            state = ST_NORMAL;
        }

        /* ---- unordered list ------------------------------------------- */
        {
            int ul_indent = 0;
            if (ul_item(line, &ul_indent)) {
                if (state != ST_ULIST) {
                    switch (state) {
                        case ST_PARA:
                            FLUSH_PARA();
                            break;
                        case ST_OLIST:
                            FLUSH_OLIST();
                            break;
                        default:
                            break;
                    }
                    item_count = 0;
                    list_indent = (float)ul_indent;
                    state = ST_ULIST;
                }
                if (item_count < MAX_ITEMS) {
                    items[item_count].text = strdup(ul_text(line));
                    item_count++;
                }
                continue;
            }
        }

        /* ---- ordered list --------------------------------------------- */
        {
            int ol_num = 0, ol_indent = 0;
            if (ol_item(line, &ol_num, &ol_indent)) {
                if (state != ST_OLIST) {
                    switch (state) {
                        case ST_PARA:
                            FLUSH_PARA();
                            break;
                        case ST_ULIST:
                            FLUSH_ULIST();
                            break;
                        default:
                            break;
                    }
                    item_count = 0;
                    ol_start = ol_num;
                    list_indent = (float)ol_indent;
                    state = ST_OLIST;
                }
                if (item_count < MAX_ITEMS) {
                    items[item_count].text = strdup(ol_text(line));
                    item_count++;
                }
                continue;
            }
        }

        /* ---- continuation lines for lists / blockquote ---------------- */
        if (state == ST_ULIST) {
            /* Non-blank, non-list lines are continuation of last item */
            if (item_count > 0 && !ul_item(line, NULL) && !ol_item(line, NULL, NULL)) {
                char*  prev = items[item_count - 1].text;
                size_t plen = strlen(prev);
                size_t llen = strlen(line);
                char*  np = realloc(prev, plen + llen + 2);
                if (np) {
                    np[plen] = ' ';
                    memcpy(np + plen + 1, line, llen + 1);
                    items[item_count - 1].text = np;
                }
                continue;
            }
        }
        if (state == ST_OLIST) {
            if (item_count > 0 && !ul_item(line, NULL) && !ol_item(line, NULL, NULL)) {
                char*  prev = items[item_count - 1].text;
                size_t plen = strlen(prev);
                size_t llen = strlen(line);
                char*  np = realloc(prev, plen + llen + 2);
                if (np) {
                    np[plen] = ' ';
                    memcpy(np + plen + 1, line, llen + 1);
                    items[item_count - 1].text = np;
                }
                continue;
            }
        }

        /* ---- paragraph / continuation --------------------------------- */
        if (state == ST_ULIST || state == ST_OLIST) {
            /* starting a new block that isn't a list item */
            if (state == ST_ULIST)
                FLUSH_ULIST();
            else
                FLUSH_OLIST();
            state = ST_NORMAL;
        }

        if (state == ST_PARA) {
            para_append(&para_buf, &para_cap, line);
        } else {
            if (!para_buf) {
                para_buf = malloc(1024);
                para_cap = 1024;
                if (!para_buf) continue;
            }
            snprintf(para_buf, para_cap, "%s", line);
            state = ST_PARA;
        }
    } /* end for */

    /* ---- flush remaining state ---------------------------------------- */
    switch (state) {
        case ST_PARA:
            FLUSH_PARA();
            break;
        case ST_CODE_FENCE:
        case ST_CODE_INDENT:
            FLUSH_CODE();
            break;
        case ST_ULIST:
            FLUSH_ULIST();
            break;
        case ST_OLIST:
            FLUSH_OLIST();
            break;
        case ST_BLOCKQUOTE:
            FLUSH_QUOTE();
            break;
        case ST_TABLE:
            FLUSH_TABLE();
            break;
        default:
            break;
    }

#undef FLUSH_PARA
#undef FLUSH_CODE
#undef FLUSH_ULIST
#undef FLUSH_OLIST
#undef FLUSH_QUOTE
#undef FLUSH_TABLE

    free(para_buf);
    free(code_buf);
    free(quote_buf);
    free_lines(lines, line_count);
    return 0;
}
