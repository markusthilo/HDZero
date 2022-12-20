/* zerod v0.1-20221220 */
/* written for Windows + MinGW */
/* Author: Markus Thilo' */
/* E-mail: markus.thilo@gmail.com */
/* License: GPL-3 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>

/* Convert string to unsigned long long */
ULONGLONG read_ulonglong(char *s) {
	int l = 0;
	while (1) {
		if (s[l] == 0) break;
		if ( s[l] < '0' | s[l] > '9' ) return 0;
		l++;
	}
	ULONGLONG f = 1;
	ULONGLONG r = 0;
	ULONGLONG n;
	for (int i=l-1; i>=0; i--){
		n = r + (f * (s[i]-'0'));
		if (n < r) return 0;
		r = n;
		f *= 10;
	}
	return r;
}

/* Open handle */
HANDLE open_handle(char *path) {
	HANDLE fh = CreateFile(
		path,
		GENERIC_WRITE,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open output file %s\n", path);
		exit(1);
	}
	return fh;
}

/* Close handle */
void close_handle(HANDLE fh) {
	if ( CloseHandle(fh) ) return;
	fprintf(stderr, "Error: could not close output file or device\n");
	exit(1);
}

/* Close file and exit */
void error_close(HANDLE fh) {
	close_handle(fh);
	exit(1);
}

/* Print error to stderr and exit */
void error_stopped(DWORD written, HANDLE fh) {
	fprintf(stderr, "Error: stopped after %llu bytes\n", written);
	error_close(fh);
}

/* Write bytes to file by given block size */
ULONGLONG write_blocks(
	HANDLE fh,
	char *maxblock,
	ULONGLONG towrite,
	ULONGLONG written,
	DWORD blocksize,
	DWORD pinterval
	)
{
	BOOL writectrl;
	DWORD newwritten;
	ULONGLONG blockstw = ( towrite - written ) / blocksize;
	ULONGLONG blockswrtn = 0;
	DWORD blockcnt = 1;
	while ( blockswrtn++ < blockstw ) {	// write blocks
		writectrl = WriteFile(fh, maxblock, blocksize, &newwritten, NULL);
		written += newwritten;
		if ( !writectrl | newwritten < blocksize ) error_stopped(written, fh);
		if ( blockcnt++ == pinterval ) {
			printf("... %llu Bytes\n", written);
			blockcnt = 1;
		}
	}
	towrite = towrite - written;
	if ( towrite > 0 ) {	// write what's left
		writectrl = WriteFile(fh, maxblock, towrite, &newwritten, NULL);
		written += newwritten;
		if ( !writectrl | newwritten < towrite ) error_stopped(written, fh);
		printf("... %llu Bytes\n", written);
	}
	return written;
}

/* Main function - program starts here*/
int main(int argc, char **argv) {
	// Definitions
	const clock_t MAXCLOCK = 0x7fffffff;
	const DWORD MINCALCSIZE = 0x80000000;
	const DWORD MAXBLOCKSIZE = 0x100000;
	const DWORD MINBLOCKSIZE = 0x200;
	const DWORD MAXCOUNTER = 100;
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	if ( argc > 4 ) {
		fprintf(stderr, "Error: too many arguments\n");
		exit(1);
	}
	HANDLE fh = open_handle(argv[1]);	// open file or drive
	if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open output file %s\n", argv[1]);
		exit(1);
	}
	BOOL xtrasave = FALSE;	// randomized overwrite
	ULONGLONG towrite = 0;
	ULONGLONG argull;	// file size as cli argument
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( ( argv[i][0] == '/' & argv[i][2] == 0 ) & ( argv[i][1] == 'x' | argv[i][1] == 'X'  ) )
		{	// argument is /x
			if ( xtrasave ) {
				fprintf(stderr, "Error: there is no double extra save mode\n");
				error_close(fh);
			}
			xtrasave = TRUE;
		} else {
			argull = read_ulonglong(argv[i]);
			if ( argull != 0 ) {	// argument is bytes as ullong
				if ( towrite > 0 ) {
					fprintf(stderr, "Error: only one integer for the bytes to write makes sense\n");
					error_close(fh);
				}
			towrite = argull;
			} else {
				fprintf(stderr, "Error: wrong argument\n");
				error_close(fh);
			}
		}
	}
	LARGE_INTEGER filesize;
	if ( GetFileSizeEx(fh, &filesize) ) {
		if ( towrite != 0 ) {
			fprintf(stderr, "Error: could determin file size but was given as argument\n");
			error_close(fh);
		}
		towrite = (ULONGLONG)filesize.QuadPart;
	} else if ( towrite == 0 ) {
			fprintf(stderr, "Error: could not determin file size\n");
			error_close(fh);
	}
	ULONGLONG written = 0;	// to count written bytes
	/* End of CLI */
	char maxblock[MAXBLOCKSIZE];	// block to write
	if ( xtrasave ) {
		for (int i=0; i<MAXBLOCKSIZE; i++) maxblock[i] = (char)rand();
		printf("First pass: Writing random bytes\n");
	} else {
		memset(maxblock, 0, sizeof(maxblock));
		printf("Writing zeros\n");
	}
	/* Block size */
	clock_t bestduration = MAXCLOCK;
	DWORD size = sizeof(maxblock);
	DWORD blocksize = MAXBLOCKSIZE;	// blocksize to write
	BOOL writectrl;
	DWORD newwritten;
	if ( towrite > MINCALCSIZE ) {	// calculate best/fastes Block size
		printf("Calculating best block size\n");
		clock_t start, duration;
		for (DWORD blockstw=MAXCOUNTER; size>=MINBLOCKSIZE; blockstw=blockstw<<1) {	// double blocks
			printf("Testing block size of %lu Bytes\n", size);
			clock_t start = clock();
			for (DWORD blockcnt=0; blockcnt<blockstw; blockcnt++) {
				writectrl = WriteFile(fh, maxblock, size, &newwritten, NULL);
				written += newwritten;
				if ( !writectrl | newwritten < size ) error_stopped(written, fh);
			}
			duration = clock() - start;	// duration of writeprocess
			printf("... %llu Bytes\n", written);
			if ( duration < bestduration ) {
				bestduration = duration;
				blocksize = size;
			}
			size = size >> 1;	// devide block by 2
		}
		printf("Using block size %lu\n", blocksize);
	}
	/* First pass */
	DWORD pinterval = MAXBLOCKSIZE / blocksize;	// Interval to print progress
	written = write_blocks(fh, maxblock, towrite, written, blocksize, pinterval);
	/* Second passs */
	if ( xtrasave ) {
		printf("Second pass: Writing zeros\n");
		memset(maxblock, 0, sizeof(maxblock));	// fill array with zeros
		close_handle(fh);	// close
		fh = open_handle(argv[1]);	// and open again for second pass
		written = write_blocks(fh, maxblock, towrite, 0, blocksize, pinterval);
	}
	printf("All done, %llu Bytes were zeroed\n", written);
	close_handle(fh);
	exit(0);
}