#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "markdown.h"
#include "paper.h"
#include "pdf.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.md> [output.pdf]\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];

    /* Determine output filename */
    char output_file[4096];
    if (argc >= 3) {
        snprintf(output_file, sizeof(output_file), "%s", argv[2]);
    } else {
        snprintf(output_file, sizeof(output_file), "%s", input_file);
        char* dot = strrchr(output_file, '.');
        if (dot && strcmp(dot, ".md") == 0) {
            strcpy(dot, ".pdf");
        } else {
            size_t rem = sizeof(output_file) - strlen(output_file) - 1;
            strncat(output_file, ".pdf", rem);
        }
    }

    /* Read the Markdown source */
    char* content = input_read_file(input_file);
    if (!content) {
        fprintf(stderr, "mdpdf: cannot read '%s'\n", input_file);
        return 1;
    }

    /* Detect paper size */
    PaperSize paper = paper_get_default();

    /* Create PDF context */
    PDF* pdf = pdf_create(paper.width, paper.height);
    if (!pdf) {
        fprintf(stderr, "mdpdf: out of memory\n");
        free(content);
        return 1;
    }

    /* Parse and render Markdown */
    if (markdown_to_pdf(content, pdf, input_file) != 0) {
        fprintf(stderr, "mdpdf: rendering failed\n");
        pdf_free(pdf);
        free(content);
        return 1;
    }

    /* Write PDF */
    if (pdf_write(pdf, output_file) != 0) {
        fprintf(stderr, "mdpdf: cannot write '%s'\n", output_file);
        pdf_free(pdf);
        free(content);
        return 1;
    }

    pdf_free(pdf);
    free(content);
    return 0;
}
