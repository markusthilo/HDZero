/* zerod v0.1-20221222 */
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
void error_toomany(HANDLE fh) {
	fprintf(stderr, "Error: too many arguments\n");
	error_close(fh);
}

void error_blocksize(HANDLE fh) {
	fprintf(stderr, "Error: given blocksize is over limit\n");
	error_close(fh);
}

void error_stopped(ULONGLONG written, HANDLE fh) {
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
	const DWORD MINCALCSIZE = 0xf00000;	// 0x80000000;
	const DWORD MAXBLOCKSIZE = 0x100000;
	const DWORD MINBLOCKSIZE = 0x200;
	const DWORD MAXCOUNTER = 100;
	const DWORD DUMMYSLEEP = 1000;
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	HANDLE fh = open_handle(argv[1]);	// open file or drive
	if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open output file %s\n", argv[1]);
		exit(1);
	}
	LARGE_INTEGER li_filesize;	// file size as a crappy win32 file type
	ULONGLONG towrite = 0;	// bytes to write
	if ( GetFileSizeEx(fh, &li_filesize) ) towrite = (ULONGLONG)li_filesize.QuadPart;
	DWORD blocksize = 0;	// block size to write
	BOOL xtrasave = FALSE;	// randomized overwrite
	BOOL dummy = FALSE;	// dummy mode
	ULONGLONG argull[2];	// size arguments
	int argullcnt = 0;
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( ( argv[i][0] == '/' & argv[i][2] == 0 )	// x for two pass mode
			& ( argv[i][1] == 'x' | argv[i][1] == 'X' ) 
		) {
			if ( xtrasave ) error_toomany(fh);
			xtrasave = TRUE;
		} else if ( ( argv[i][0] == '/' & argv[i][2] == 0 )
			& ( argv[i][1] == 'd' | argv[i][1] == 'D' )	// d for dummy mode
		) {
			if ( dummy ) error_toomany(fh);
			dummy = TRUE;
		} else {
			argull[argullcnt] = read_ulonglong(argv[i]);
			if ( argull[argullcnt] > 0 ) {	// argument is size in bytes
				if ( argullcnt++ > 1 ) error_toomany(fh);
			} else {
				fprintf(stderr, "Error: wrong argument\n");
				error_close(fh);
			}
		}
	}
	if ( argullcnt == 1 ) {
		if ( towrite > 0 ) {
			if ( argull[0] > MAXBLOCKSIZE ) error_blocksize(fh);
			blocksize = argull[0];
		} else towrite = argull[0];
	} else if ( argullcnt == 2 ) {
		if ( towrite > 0 ) error_toomany(fh);
		if ( argull[0] >= argull[1] ) {
			if ( argull[1] > MAXBLOCKSIZE ) error_blocksize(fh);
			towrite = argull[0];
			blocksize = (DWORD)argull[1];
		} else {
			if ( argull[0] > MAXBLOCKSIZE ) error_blocksize(fh);
			blocksize = (DWORD)argull[0];
			towrite = argull[1];
		}
	}
	if ( towrite == 0 ) {
		fprintf(stderr, "Error: could not determin number of bytes to write\n");
		error_close(fh);
	}
	/* End of CLI */
	if ( blocksize == 0 ) blocksize = MAXBLOCKSIZE;	// start with max block size if not given
	ULONGLONG written = 0;	// to count written bytes
	if ( dummy ) {	// dummy mode
		printf("Dummy mode, nothing will be written to disk\n");
		if ( towrite > MINCALCSIZE ) {
			printf("Calculating best block size\n");
			for (DWORD size=MAXBLOCKSIZE; size>=MINBLOCKSIZE; size=size>>1) {
				printf("Testing block size of %lu Bytes\n", size);
				written += MAXBLOCKSIZE;
				Sleep(DUMMYSLEEP);
				printf("... %llu Bytes\n", written);
			}
		}
		blocksize = MAXBLOCKSIZE;
		printf("Using block size of %lu Bytes\n", blocksize);
		while ( written < towrite ) {
			written += blocksize;
			if ( written > towrite ) written = towrite;
			Sleep(DUMMYSLEEP);
			printf("... %llu Bytes\n", written);
		}
		if ( xtrasave ) {
			printf("Second pass: Writing zeros\n");
			written = 0;
			while ( written < towrite ) {
				written += blocksize;
				if ( written > towrite ) written = towrite;
				Sleep(DUMMYSLEEP);
				printf("... %llu Bytes\n", written);
			}
		}
	} else {	// the real thing starts here
		char maxblock[blocksize];	// block at max needed size to write
		if ( xtrasave ) {
			for (int i=0; i<blocksize; i++) maxblock[i] = (char)rand();
			printf("First pass: Writing random bytes\n");
		} else {
			memset(maxblock, 0, sizeof(maxblock));
			printf("Writing zeros\n");
		}
		/* Block size */
		clock_t bestduration = MAXCLOCK;
		BOOL writectrl;
		DWORD newwritten;
		if ( towrite > MINCALCSIZE & blocksize == 0 ) {	// calculate best/fastes block size
			printf("Calculating best block size\n");
			DWORD size = sizeof(maxblock);
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
			printf("Using block size of %lu Bytes\n", blocksize);
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
	}
	printf("All done, %llu Bytes were zeroed\n", written);
	close_handle(fh);
	exit(0);
}
