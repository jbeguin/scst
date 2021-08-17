#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/sendfile.h>
#include "bitmap.h"
#include "debug.h"

struct corw_handler
{
    // fuse file name
	const char *file;
    // corw file name
	const char *file_name;
	int file_size;
	int block_size;
	unsigned long *bitmap;
	size_t bitmaplen;
};

struct corw_handler *corw_handler_create(char *corwfile, char *fil, int64_t file_size, int block_size);
void corw_handler_destroy(struct corw_handler *corwh);
loff_t corw_handler_read(struct corw_handler *h, int fuse_fd, void *buf, loff_t loff, size_t nbyte);
loff_t corw_handler_write(struct corw_handler *h, int fuse_fd, const void *buf, loff_t loff, size_t nbyte);