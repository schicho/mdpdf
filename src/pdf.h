#ifndef PDF_H
#define PDF_H

#include "paper.h"

/* Font indices used throughout the renderer. */
#define FONT_NORMAL     0 /* Helvetica            */
#define FONT_BOLD       1 /* Helvetica-Bold       */
#define FONT_ITALIC     2 /* Helvetica-Oblique    */
#define FONT_BOLDITALIC 3 /* Helvetica-BoldOblique*/
#define FONT_MONO       4 /* Courier              */
#define FONT_MONO_BOLD  5 /* Courier-Bold         */
#define FONT_COUNT      6

/* Opaque PDF context. */
typedef struct PDF PDF;

/* ── life-cycle ─────────────────────────────────────────────────────────── */

PDF* pdf_create(float page_width, float page_height);
void pdf_free(PDF* pdf);

/* Write the assembled PDF to a file.  Returns 0 on success. */
int pdf_write(PDF* pdf, const char* path);

/* Tell the engine which directory to resolve relative image paths against. */
void pdf_set_input_dir(PDF* pdf, const char* dir);

/* ── page geometry ──────────────────────────────────────────────────────── */

/* Current Y position measured from the TOP of the page content area
 * (i.e. 0 = just below the top margin). */
float pdf_get_y(PDF* pdf);

/* Usable content width (page width minus both horizontal margins). */
float pdf_content_width(PDF* pdf);

/* Left-margin x coordinate in PDF points. */
float pdf_margin_left(PDF* pdf);

/* Ensure at least 'height' points remain on the page; break otherwise. */
void pdf_ensure_space(PDF* pdf, float height);

/* Force a new page. */
void pdf_new_page(PDF* pdf);

/* Move the Y cursor down by 'amount' points. */
void pdf_advance_y(PDF* pdf, float amount);

/* ── graphics primitives ─────────────────────────────────────────────────── */

/* Filled axis-aligned rectangle (r, g, b in 0–1). */
void pdf_rect_fill(PDF* pdf, float x, float y_from_top, float w, float h, float r, float g,
                   float b);

/* Stroked horizontal line. */
void pdf_hline(PDF* pdf, float x, float y_from_top, float width, float r, float g, float b,
               float lw);

/* Vertical bar (for blockquotes). */
void pdf_vbar(PDF* pdf, float x, float y_from_top, float height, float r, float g, float b,
              float lw);

/* ── text ──────────────────────────────────────────────────────────────── */

/*
 * Return the width (in PDF points) of 'text' rendered at 'size' in 'font'.
 * This is purely a metrics calculation; nothing is drawn.
 */
float pdf_text_width(const char* text, int font, float size);

/*
 * Render a single unwrapped line of plain text at the current Y position
 * using the given font, size and x offset from the left margin.
 * Advances Y by 'leading' (pass 0 to use 1.2×size).
 * Returns the actual line height used.
 */
float pdf_text_line(PDF* pdf, const char* text, int font, float size, float x_offset,
                    float leading);

/*
 * Render a paragraph of inline-formatted text with automatic word-wrap.
 *
 * 'text'              : raw markdown inline text (may contain **bold**,
 *                       *italic*, `code`).
 * 'left_indent'       : extra left indentation from the content left margin.
 * 'right_indent'      : extra right indentation from the content right margin.
 * 'font_size'         : base font size.
 * 'base_font'         : FONT_NORMAL or FONT_ITALIC (for blockquotes).
 * 'leading'           : line height (0 = 1.2×size).
 *
 * Automatically calls pdf_new_page() when the text reaches the bottom margin.
 * Returns total height used.
 */
float pdf_paragraph(PDF* pdf, const char* text, float left_indent, float right_indent,
                    float font_size, int base_font, float leading);

/*
 * Render a pre-formatted code block.
 * Wraps long lines; handles page breaks between code lines.
 * Returns total height used.
 */
float pdf_code_block(PDF* pdf, const char* code, float left_indent);

/*
 * Measure the height (in PDF points) that pdf_paragraph() would use to render
 * 'text' with the given parameters, without drawing anything.
 * Useful for pre-computing row heights in table cells.
 */
float pdf_measure_paragraph(PDF* pdf, const char* text, float left_indent, float right_indent,
                            float font_size, int base_font, float leading);

/*
 * Return the natural (single-line, unwrapped) width in PDF points of 'text'
 * when rendered with inline markup at 'font_size' and 'base_font'.
 * Does not require a PDF context.
 */
float pdf_inline_width(const char* text, float font_size, int base_font);

/*
 * Embed an image from 'path' (JPEG or PNG), scaled to fit within
 * max_width × max_height (pass 0 for "no limit").
 * Returns the height used in points, or 0 on failure.
 */
float pdf_image(PDF* pdf, const char* path, float max_width, float max_height);

/* ── bookmarks (PDF outline) ───────────────────────────────────────────── */

/*
 * Record a navigation bookmark at the current page/position.
 * 'title' is the plain-text heading label shown in the PDF outline panel.
 * Must be called during rendering (before pdf_write).
 */
void pdf_add_bookmark(PDF* pdf, const char* title);

#endif /* PDF_H */
