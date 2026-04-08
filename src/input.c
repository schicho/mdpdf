#include "input.h"

#include <stdio.h>
#include <stdlib.h>

char* input_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char* buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}
