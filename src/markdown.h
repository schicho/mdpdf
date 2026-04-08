#ifndef MARKDOWN_H
#define MARKDOWN_H

#include "pdf.h"

/*
 * Parse the Markdown source in 'content' and render it into 'pdf'.
 * 'input_path' is used only to resolve relative image paths.
 * Returns 0 on success.
 */
int markdown_to_pdf(const char* content, PDF* pdf, const char* input_path);

#endif /* MARKDOWN_H */
