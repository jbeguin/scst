#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "debug.h"

size_t bitmap_size(unsigned long nbits);
unsigned long *bitmap_alloc(unsigned long nbits);

/* bitmap file operations */
unsigned long *bitmap_open_file(const char *bitmapfile, unsigned long nbits, size_t *bitmaplen, int readonly, int zeroclear);
void bitmap_sync_file(unsigned long *bitmap, size_t bitmaplen);
void bitmap_close_file(unsigned long *bitmap, size_t bitmaplen);

int bitmap_test(unsigned long *bitmap, unsigned long block_index);
void bitmap_on(unsigned long *bitmap, unsigned long block_index);
unsigned long bitmap_popcount(unsigned long *bitmap, unsigned long bits);

// unsigned char* new_bitmap(unsigned long size);
// int bitmap_get(unsigned char* bitmap, unsigned long i);
// void bitmap_set(unsigned char* bitmap, unsigned long i);
// unsigned char* bitmap_to_bytes(unsigned char* bitmap);
