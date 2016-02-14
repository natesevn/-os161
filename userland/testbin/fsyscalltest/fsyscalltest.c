/*
 * fsyscalltest.c
 *
 * Tests file-related system calls open, close, read and write.
 *
 * Should run on emufs. This test allows testing the file-related system calls
 * early on, before much of the functionality implemented. This test does not
 * rely on full process functionality (e.g., fork/exec).
 *
 * Much of the code is borrowed from filetest.c
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>

/* 
 * This is essentially the same code as in filetest.c, except we don't
 * expect any arguments, so the test can be executed before processes are
 * fully implemented. Furthermore, we do not call remove, because emufs does not
 * support it, and we would like to be able to run on emufs.
 */
static void
simple_test()
{
  	static char writebuf[41] = 
		"Twiddle dee dee, Twiddle dum dum.......\n";
	static char readbuf[41];

	const char *file;
	int fd, rv;

	file = "testfile";

	fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0664);
	if (fd<0) {
		err(1, "%s: open for write", file);
	}

	rv = write(fd, writebuf, 40);
	if (rv<0) {
		err(1, "%s: write", file);
	}

	rv = close(fd);
	if (rv<0) {
		err(1, "%s: close (1st time)", file);
	}

	fd = open(file, O_RDONLY);
	if (fd<0) {
		err(1, "%s: open for read", file);
	}

	rv = read(fd, readbuf, 40);
	if (rv<0) {
		err(1, "%s: read", file);
	}
	rv = close(fd);
	if (rv<0) {
		err(1, "%s: close (2nd time)", file);
	}
	/* ensure null termination */
	readbuf[40] = 0;

	if (strcmp(readbuf, writebuf)) {
		errx(1, "Buffer data mismatch!");
	}
}

			

/* This test takes no arguments, so we can run it before argument passing
 * is fully implemented. 
 */
int
main()
{
	//test_openfile_limits();
	//printf("Passed Part 1 of fsyscalltest\n");

	simple_test();
	printf("Passed Part 2 of fsyscalltest\n");
	
	/*simultaneous_write_test();
	printf("Passed Part 3 of fsyscalltest\n");
	
	test_dup2();
	printf("Passed Part 4 of fsyscalltest\n");

	dir_test();
	printf("Passed Part 5 of fsyscalltest\n");*/
	
	printf("All done!\n");
	
	return 0;
}
