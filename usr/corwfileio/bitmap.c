#include "bitmap.h"

/* some of the below definitions are from Linux kernel */
#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#define BITS_PER_BYTE           8
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define BITS_PER_LONG		(sizeof(unsigned long) * BITS_PER_BYTE)

size_t bitmap_size(unsigned long nbits)
{
	unsigned long narrays = BITS_TO_LONGS(nbits);
	return sizeof(unsigned long) * narrays;
}

// unsigned long *bitmap_alloc(unsigned long nbits)
// {
// 	unsigned long *bitmap_array;
// 	unsigned long narrays = BITS_TO_LONGS(nbits);

//     bitmap_array = (unsigned long*) malloc(narrays * sizeof(unsigned long));

// 	return bitmap_array;
// }

void bitmap_sync_file(unsigned long *bitmap, size_t bitmaplen)
{
	TRACE_DBG("msync bitmap %p", bitmap);
	int ret = msync(bitmap, bitmaplen, MS_SYNC);
	if (ret < 0)
		PRINT_ERROR("msync bitmap failed ret = %d", ret);
}

void bitmap_close_file(unsigned long *bitmap, size_t bitmaplen)
{
	/* do nothing if the size was zero when opened */
	if (!bitmap) {
		return;
	}

	bitmap_sync_file(bitmap, bitmaplen);

	int ret = munmap(bitmap, bitmaplen);
	if (ret < 0)
		PRINT_ERROR("munmap  ret = %d", ret);
}

off_t get_disksize(int fd) {
	struct stat st;
	off_t disksize = 0;


	int ret = fstat(fd, &st);
	if (ret < 0) {
		if (errno == EOVERFLOW)
			PRINT_ERROR("enable 64bit offset support ret = %d", ret);
	}

	/* device file may return st_size == 0 */
	if (S_ISREG(st.st_mode)) {
		disksize = st.st_size;

		return disksize;

	} else if (S_ISBLK(st.st_mode)) {
		disksize = lseek(fd, 0, SEEK_END);
		if (disksize < 0)
			PRINT_ERROR("lseek failed: %d", errno);

		return disksize;

	} else
		PRINT_ERROR("file type %d not supported", st.st_mode);


	PRINT_ERROR("failed to detect disk size %s", "");

	/* NOT REACHED */
	return 0;
}

unsigned long *bitmap_open_file(const char *bitmapfile, unsigned long nbits, size_t *bitmaplen, int readonly, int zeroclear)
{
	void *buf = NULL;
	unsigned long narrays = BITS_TO_LONGS(nbits);
	size_t buflen = sizeof(unsigned long) * narrays;

	int mmap_flag = readonly ? PROT_READ : PROT_WRITE;
	int open_flag = readonly ? O_RDONLY : (O_RDWR | O_CREAT);

	/* mmap() of zero length results in EINVAL */
	if (nbits == 0) {
		PRINT_ERROR("open a zero-length bitmap, %s", bitmapfile);
		return NULL;
	}

	// {
	// 	/* O_NOATIME will not give us visible performance improvement. Drop? */
	// 	struct stat st;
	// 	int ret = stat(bitmapfile, &st);
	// 	if (ret < 0) {
	// 		if (errno == ENOENT)
	// 			open_flag |= O_NOATIME;
	// 		else
	// 			err("stat %s, %m", bitmapfile);
	// 	} else {
	// 		if (st.st_uid == geteuid())
	// 			open_flag |= O_NOATIME;
	// 	}
	// }

	/* !zeroclear is considered "reuse_data" */

	/* if (readonly && !zeroclear)
	 *   open the existing file as readonly, and use data in it
	 *
	 * if (readonly && zeroclear)
	 *   error
	 *
	 * if (!readonly && !zeroclear)
	 *   open the existing file as read/write, and use data in it
	 *
	 * if (!readonly && zeroclear)
	 *   open the existing file as read/write, and zero-clear data
	 *
	 *
	 * if the obtained file size is different from the requested size,
	 *    (readonly && *) is not possible
	 *    (!readonly && !zeroclear) is not possible
	 *    (!readonly && zeroclear) is possible
	 *
	 * if the file is to be created,
	 *    (!readonly && *) is possible,
	 *       i.e., the given value of zeroclear is not referred
	 *
	 *
	 */

	{
		int fd = open(bitmapfile, open_flag, S_IRUSR | S_IWUSR);
		if (fd < 0)
			PRINT_ERROR("bitmap open %s, %m", bitmapfile);

		/* get the file size of a bitmap file */
		off_t size = get_disksize(fd);
		if (size != (off_t) buflen) {
			if (readonly)
				PRINT_ERROR("cannot resize readonly bitmap file (%s)", bitmapfile);

			/* if the bitmap file did not exist, the obtained size is zero */
			if (size == 0)
				zeroclear = 1;

			if (!zeroclear)
				PRINT_ERROR("deny using bitmap file (%s) without clearing it. The bitmap size is different (%ju != %zu)",
						bitmapfile, size, buflen);

			int ret = ftruncate(fd, buflen);
			if (ret < 0)
				PRINT_ERROR("ftruncate %d", ret);
		}

		/* now we get the bitmap file of the required file size */

		buf = mmap(NULL, buflen, mmap_flag, MAP_SHARED | MAP_POPULATE | MAP_LOCKED, fd, 0);
		if (buf == MAP_FAILED)
			PRINT_ERROR("bitmap mapping failed fd %d", fd);

		close(fd);
	}


	PRINT_INFO("bitmap file %s (%zu bytes = %lu arrays of %zu bytes), %lu nbits",
			bitmapfile, buflen, narrays, sizeof(unsigned long), nbits);


	if (zeroclear) {
		PRINT_INFO("make bitmap file (%s) zero-cleared", bitmapfile);
		memset(buf, 0, buflen);

		/* get disk space for bitmap */
		int ret = msync(buf, buflen, MS_SYNC);
		if (ret < 0)
			PRINT_ERROR("bitmap msync failed, %s", strerror(errno));
	} else
		PRINT_INFO("reuse previous state from bitmap file %s", bitmapfile);


	*bitmaplen = buflen;

	return (unsigned long *) buf;
}

int bitmap_test(unsigned long *bitmap_array, unsigned long block_index)
{
	//printf("%p, %u\n",  bitmap_array, block_index);

	unsigned long bitmap_index = block_index / BITS_PER_LONG;
	unsigned long *bitmap = &(bitmap_array[bitmap_index]);

	unsigned long val = *bitmap & (1UL << (block_index % BITS_PER_LONG));

	//dbg("val %08x, bitmap %p block_index mod 32 %u, bitmap %08x",
	//		val, bitmap, block_index % 32, *bitmap);

	if (val > 0)
		return 1;
	else
		return 0;
}


void bitmap_on(unsigned long *bitmap_array, unsigned long block_index)
{
	unsigned long bitmap_index = block_index / BITS_PER_LONG;
	unsigned long *bitmap = &(bitmap_array[bitmap_index]);

	//dbg("set_bitmap %08x", *bitmap);
	//printf("bitmap %p block_index mod 32 %d\n", bitmap, block_index % 32);

	*bitmap |= (1UL << (block_index % BITS_PER_LONG));

	//dbg("set_bitmap %08x", *bitmap);
}

/* we can make it faster. use __builtin_popcountl()? */
int bitmap_popcount(unsigned long *bm, unsigned long nbits)
{
	int cached = 0;
	for (unsigned long index = 0; index < nbits; index++) {
		if (bitmap_test(bm, index))
			cached += 1;
	}

	// unsigned long nl = (nbits + sizeof(unsigned long) -1) / sizeof(unsigned long);
	// PRINT_INFO("nl %"PRId64, nl);
	// for (unsigned long li = 0; li < nl; li++) {
	// 	PRINT_INFO("cached %d", cached);
	// 	cached += __builtin_popcountl(bm[li]);
	// }

	return cached;

}
