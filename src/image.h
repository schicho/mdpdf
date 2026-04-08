#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>

/*
 * Loaded image data returned by image_load().
 * The caller must call image_free() when done.
 */
typedef struct {
    int            width;
    int            height;
    int            channels; /* 1=grey, 3=RGB, 4=RGBA */
    int            is_jpeg;  /* 1 → raw JPEG bytes; 0 → raw pixel data (RGB/RGBA) */
    unsigned char* data;
    size_t         data_size;
} Image;

/*
 * Load a JPEG or PNG image from 'path'.
 * Returns NULL on failure (unsupported format, missing file, …).
 * Caller must call image_free().
 */
Image* image_load(const char* path);

void   image_free(Image* img);

#endif /* IMAGE_H */
