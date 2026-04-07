#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

/*
 * Read the entire file at 'path' into a malloc'd, null-terminated buffer.
 * Returns NULL on error.  The caller must free() the returned buffer.
 */
char *input_read_file(const char *path);

#endif /* INPUT_H */
