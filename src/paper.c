#include "paper.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

static const PaperSize known_papers[] = {{595.276f, 841.890f, "A4"}, {612.0f, 792.0f, "Letter"},
                                         {612.0f, 1008.0f, "Legal"}, {841.890f, 1190.551f, "A3"},
                                         {419.528f, 595.276f, "A5"}, {0, 0, NULL}};

PaperSize paper_from_name(const char* name) {
    for (int i = 0; known_papers[i].name; i++) {
        if (strcasecmp(known_papers[i].name, name) == 0) return known_papers[i];
    }
    return known_papers[0]; /* default: A4 */
}

PaperSize paper_get_default(void) {
    /* 1. Explicit environment variable */
    const char* env = getenv("PAPERSIZE");
    if (env && *env) {
        PaperSize p = paper_from_name(env);
        if (p.width > 0) return p;
    }

    /* 2. /etc/papersize (Debian/Ubuntu) */
    FILE* f = fopen("/etc/papersize", "r");
    if (f) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, " \t\r\n")] = '\0';
            fclose(f);
            if (buf[0]) {
                PaperSize p = paper_from_name(buf);
                if (p.width > 0) return p;
            }
        } else {
            fclose(f);
        }
    }

    /* 3. Locale name: en_US and en_CA → Letter; everything else → A4 */
    const char* locale = setlocale(LC_ALL, NULL);
    if (!locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) {
        locale = getenv("LANG");
        if (!locale) locale = getenv("LC_ALL");
    }
    if (locale) {
        if (strncmp(locale, "en_US", 5) == 0 || strncmp(locale, "en_CA", 5) == 0)
            return known_papers[1]; /* Letter */
    }

    return known_papers[0]; /* A4 */
}
