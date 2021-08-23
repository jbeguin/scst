#include "corw.h"

void load_block(struct corw_handler *h, int fuse_fd, int i) {
    PRINT_INFO("load_block %d", i);
    loff_t loff = ((long) i) * ((long) h->block_size);
    char *buf = malloc(h->block_size);

    /* READ */
    lseek64(fuse_fd, loff, 0/*SEEK_SET*/);
    int res = read(fuse_fd, buf, h->block_size);
    if (res < 0) {
		PRINT_ERROR("load_block(%d) returned %"PRId64" (errno %d)",
			i, (uint64_t)res, errno);
        return;
    }

    /* COPY to mmap */
    memcpy(&(h->buf)[loff], buf, res);

    free(buf);
}

struct corw_handler *corw_handler_create(char *corwfile, char *file, int64_t file_size, int block_size) {
    PRINT_INFO("Opening file %s", corwfile);
    PRINT_INFO("File size %"PRId64"", file_size);

    int fd = open(corwfile, O_WRONLY | O_APPEND | O_CREAT, 0644);

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
    close(fd);

    fd = open(corwfile, O_RDWR | O_LARGEFILE);

    if (fd < 0) {
        PRINT_ERROR("Unable to reopen file %s (%s)", corwfile,
            strerror(errno));
        return NULL;
    }

    struct stat st;
    fstat(fd, &st);
    char *buf = mmap(NULL, st.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED)
        PRINT_ERROR("corwfile mapping failed fd %d", fd);

    close(fd);


    struct corw_handler *corwh = (struct corw_handler*) malloc(sizeof(struct corw_handler));
    char *bitmapfn = malloc(strlen(corwfile) + 7);
    strcpy(bitmapfn, corwfile);
    strcat(bitmapfn, ".bitmap");
    corwh->fuse_fn = file;
    corwh->corw_fn = corwfile;
    corwh->file_size = file_size;
    corwh->block_size = block_size;
    corwh->buf = buf;
    corwh->nblocks = file_size / block_size;
    if (file_size % block_size == 0) corwh->nblocks++;
    size_t bitmaplen = 0;
    corwh->bitmap = bitmap_open_file(bitmapfn, corwh->nblocks, &bitmaplen, 0, 0);
    corwh->bitmap_fn = bitmapfn;
    corwh->bitmaplen = bitmaplen;
	PRINT_INFO("corw_handler_create corwh->bitmaplen %d", (int) corwh->bitmaplen);
    
    return corwh;
}

void corw_handler_destroy(struct corw_handler *corwh) {
    bitmap_close_file(corwh->bitmap, corwh->bitmaplen);
	int ret = msync(corwh->buf, corwh->file_size, MS_SYNC);
	if (ret < 0)
		PRINT_ERROR("msync corw_handler failed ret = %d", ret);
    ret = munmap(corwh->buf, corwh->file_size);
	if (ret < 0)
		PRINT_ERROR("munmap corw_handler ret = %d", ret);
    free(corwh->bitmap_fn);
    free(corwh);
}

void corw_handler_preload(struct corw_handler *h, int fuse_fd) {
	PRINT_INFO("corw_handler_preload nblocks %d", h->nblocks);
    for (int i = 0; i < h->nblocks; i++) {
        if (bitmap_test(h->bitmap, i)) {
            load_block(h, fuse_fd, i);
        }
    }
}

ssize_t corw_handler_load_range(struct corw_handler *h, int fuse_fd, loff_t loff, int length) {
    int start_block = loff / h->block_size;
    int end_block = (loff + length) / h->block_size;
    TRACE_DBG("corw_handler_load_range start_block %d end_block %d", start_block, end_block);
    for (int i = start_block; i <= end_block; i++) {
        TRACE_DBG("corw_handler_load_range i %d bitmap_test(h->bitmap, i) %d", i, bitmap_test(h->bitmap, i));
        if (!bitmap_test(h->bitmap, i)) {
            load_block(h, fuse_fd, i);
            bitmap_on(h->bitmap, i);
        }
    }
    return 0;
}

ssize_t corw_handler_load_write_range(struct corw_handler *h, int fuse_fd, loff_t loff, int length) {
    int start_block = loff / h->block_size;
    int end_block = (loff + length) / h->block_size;
    TRACE_DBG("corw_handler_load_write_range start_block %d end_block %d", start_block, end_block);
    if (!bitmap_test(h->bitmap, start_block)) {
        load_block(h, fuse_fd, start_block);
        bitmap_on(h->bitmap, start_block);
    }
    if (start_block != end_block) {
        TRACE_DBG("corw_handler_load_write_range end_block %d bitmap_test(h->bitmap, end_block) %d", end_block, bitmap_test(h->bitmap, end_block));
        if (!bitmap_test(h->bitmap, end_block)) {
            load_block(h, fuse_fd, end_block);
            bitmap_on(h->bitmap, end_block);
        }
    }
    return 0;
}

loff_t corw_handler_read(struct corw_handler *h, int fuse_fd, void *buf, loff_t loff, size_t nbyte) {
    corw_handler_load_range(h, fuse_fd, loff, nbyte);

    memcpy(buf, &(h->buf)[loff], nbyte);

    return nbyte;
}

loff_t corw_handler_write(struct corw_handler *h, int fuse_fd, const void *buf, loff_t loff, size_t nbyte) {
    corw_handler_load_write_range(h, fuse_fd, loff, nbyte);

    memcpy(&(h->buf)[loff], buf, nbyte);

    return nbyte;
}
