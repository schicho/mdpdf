#ifndef PAPER_H
#define PAPER_H

/* Paper dimensions in PDF points (1 pt = 1/72 inch). */
typedef struct {
    float width;
    float height;
    const char *name;
} PaperSize;

/* Return the system-default paper size based on locale / environment. */
PaperSize paper_get_default(void);

/* Look up a paper size by name (case-insensitive).
 * Falls back to A4 for unknown names. */
PaperSize paper_from_name(const char *name);

#endif /* PAPER_H */
