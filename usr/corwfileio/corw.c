#include "corw.h"

void load_block(struct corw_handler *h, int corw_fd, int fuse_fd, int i) {
    TRACE_DBG("load_block %d from fuse_fd %d to corw_fd %d", i, fuse_fd, corw_fd);
    loff_t loff = ((long) i) * ((long) h->block_size);
	TRACE_DBG("load_block off %"PRId64", len %d", loff, h->block_size);
    /* SEEK corw_fd */
    lseek64(corw_fd, loff, 0/*SEEK_SET*/);
    /* COPY */
    ssize_t err = sendfile(corw_fd, fuse_fd, &loff, h->block_size);
    if (err < 0) {
        PRINT_ERROR("load_block sendfile trouble %"PRId64" < 0 %"PRId64
            " (errno %d) %s", (uint64_t)err, (uint64_t)loff,
            errno, strerror(errno));
    }
}

struct corw_handler *corw_handler_create(char *corwfile, char *file, int64_t file_size, int block_size) {
    PRINT_INFO("Opening file %s", corwfile);
    PRINT_INFO("File size %"PRId64"", file_size);

    // int fd = open(corwfile, O_WRONLY | O_APPEND | O_CREAT, 0644);
    int fd = open(corwfile, O_RDWR | O_LARGEFILE);
    if (fd < 0) {
        PRINT_ERROR("Unable to open file %s (%s)", corwfile,
            strerror(errno));
        return NULL;
    }

    int size = lseek64(fd, 0, SEEK_END);

    if (size == 0) {
        int err = ftruncate64(fd, file_size);

        if (err == -1) {
            PRINT_ERROR("ftruncate64 err %s", strerror(errno));
            close(fd);
            return NULL;
        }
    } else if (size != file_size) {
        PRINT_ERROR("sizes don't match size %d, file_size %"PRId64"", size, file_size);
        close(fd);
        return NULL;
    }


    // close(fd);

    struct corw_handler *corwh = (struct corw_handler*) malloc(sizeof(struct corw_handler));
    char *bitmapfn = malloc(strlen(corwfile) + 7);
    strcpy(bitmapfn, corwfile);
    strcat(bitmapfn, ".state");
    corwh->file = file;
    corwh->file_name = corwfile;
    corwh->file_size = file_size;
    corwh->block_size = block_size;
    corwh->corw_fd = fd;
    int nblocks = (file_size + 1) / block_size;
    // if (file_size % block_size != 0) nblocks++;
    size_t bitmaplen = 0;
    corwh->bitmap = bitmap_open_file(bitmapfn, nblocks, &bitmaplen, 0, 0);
    corwh->bitmaplen = bitmaplen;
	TRACE_DBG("corw_handler_create corwh->bitmaplen %d", corwh->bitmaplen);

    free(bitmapfn);
    
    return corwh;
}

void corw_handler_destroy(struct corw_handler *corwh) {
    close(corwh->corw_fd);
}

ssize_t corw_handler_load_range(struct corw_handler *h, int corw_fd, int fuse_fd, loff_t loff, int length) {
    int start_block = loff / h->block_size;
    int end_block = (loff + length) / h->block_size;
    TRACE_DBG("corw_handler_load_range start_block %d end_block %d", start_block, end_block);
    for (int i = start_block; i <= end_block; i++) {
        TRACE_DBG("corw_handler_load_range i %d bitmap_test(h->bitmap, i) %d", i, bitmap_test(h->bitmap, i));
        if (!bitmap_test(h->bitmap, i)) {
            load_block(h, corw_fd, fuse_fd, i);
            bitmap_on(h->bitmap, i);
        }
    }
    return 0;
}

ssize_t corw_handler_load_write_range(struct corw_handler *h, int corw_fd, int fuse_fd, loff_t loff, int length) {
    int start_block = loff / h->block_size;
    int end_block = (loff + length) / h->block_size;
    TRACE_DBG("corw_handler_load_write_range start_block %d end_block %d", start_block, end_block);
    if (!bitmap_test(h->bitmap, start_block)) {
        load_block(h, corw_fd, fuse_fd, start_block);
        bitmap_on(h->bitmap, start_block);
    }
    if (start_block != end_block) {
        TRACE_DBG("corw_handler_load_write_range end_block %d bitmap_test(h->bitmap, end_block) %d", end_block, bitmap_test(h->bitmap, end_block));
        if (!bitmap_test(h->bitmap, end_block)) {
            load_block(h, corw_fd, fuse_fd, end_block);
            bitmap_on(h->bitmap, end_block);
        }
    }
    return 0;
}

loff_t corw_handler_read(struct corw_handler *h, int fuse_fd, void *buf, loff_t loff, size_t nbyte) {
    // int corw_fd = open(h->file_name, O_RDWR | O_LARGEFILE);

    // PRINT_INFO("h->corw_fd %d", h->corw_fd);

    corw_handler_load_range(h, h->corw_fd, fuse_fd, loff, nbyte);

    /* SEEK */
    loff_t err = lseek64(h->corw_fd, loff, 0/*SEEK_SET*/);
    if (err != loff) {
        PRINT_ERROR("lseek trouble %"PRId64" != %"PRId64
            " (errno %d)", (uint64_t)err, (uint64_t)loff,
            errno);
    }
    /* READ */
    err = read(h->corw_fd, buf, nbyte);

	if ((err < 0) || (err < nbyte)) {
		PRINT_ERROR("read() returned %"PRId64" from %d (errno %d)",
			(uint64_t)err, (uint64_t)nbyte, errno);
	}

    // close(corw_fd);

    return err;
}

loff_t corw_handler_write(struct corw_handler *h, int fuse_fd, const void *buf, loff_t loff, size_t nbyte) {
    // int corw_fd = open(h->file_name, O_WRONLY | O_LARGEFILE);

    corw_handler_load_write_range(h, h->corw_fd, fuse_fd, loff, nbyte);

    /* SEEK */
    loff_t err = lseek64(h->corw_fd, loff, 0/*SEEK_SET*/);
    if (err != loff) {
        PRINT_ERROR("lseek trouble %"PRId64" != %"PRId64
            " (errno %d)", (uint64_t)err, (uint64_t)loff,
            errno);
    }
    /* WRITE */
    err = write(h->corw_fd, buf, nbyte);

	if ((err < 0) || (err < nbyte)) {
		PRINT_ERROR("write() returned %"PRId64" from %d (errno %d)",
			(uint64_t)err, (uint64_t)nbyte, errno);
	}

    // close(corw_fd);

    return err;
}
