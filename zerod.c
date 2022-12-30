/* zerod v0.1-20221230 */
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
		if ( s[l] < '0' || s[l] > '9' ) return 0;
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

/* Open handle to read */
HANDLE open_handle_write(char *path) {
	HANDLE fh = CreateFile(
		path,
		GENERIC_WRITE,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	return fh;
}

/* Open handle to read */
HANDLE open_handle_read(char *path) {
	HANDLE fh = CreateFile(
		path,
		GENERIC_READ,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	return fh;
}

/* Close handle */
void close_handle(HANDLE fh) {
	if ( fh == INVALID_HANDLE_VALUE ) return;
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
	clock_t clockdelta,
	char *bytesof
	)
{
	BOOL writectrl;
	DWORD newwritten;
	if ( towrite - written > blocksize ) {
		ULONGLONG tominusblock = towrite - blocksize;
		clock_t printclock = clock() + clockdelta;
		while ( written < tominusblock ) {	// write blocks
			writectrl = WriteFile(fh, maxblock, blocksize, &newwritten, NULL);
			written += newwritten;
			if ( !writectrl || newwritten < blocksize ) error_stopped(written, fh);
			if ( clock() >= printclock ) {
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
				printclock += clockdelta;
			}
		}
	}
	DWORD wltowrite = towrite - written;
	writectrl = WriteFile(fh, maxblock, wltowrite, &newwritten, NULL);	// write what's left
	written += newwritten;
	if ( !writectrl || newwritten < wltowrite ) error_stopped(written, fh);
	printf("... %llu%s", written, bytesof);
	fflush(stdout);
	return written;
}

/* Pretend to Write */
ULONGLONG dummy_write_blocks(
	int dummycnt,
	DWORD dummysleep,
	ULONGLONG towrite,
	ULONGLONG written,
	DWORD blocksize,
	char *bytesof
	)
{
	clock_t start = clock();
	while ( written < towrite && dummycnt-- > 1 ) {
		written += blocksize;
		if ( written > towrite ) break;
		Sleep(dummysleep);
		printf("... %llu%s", written, bytesof);
		fflush(stdout);
	}
	printf("... %llu%s", towrite, bytesof);
	fflush(stdout);
	return towrite;
}

/* Main function - program starts here*/
int main(int argc, char **argv) {
	// Definitions
	const ULONGLONG MAX_LARGE_INTEGER = 0x7fffffffffffffff;
	const clock_t MAXCLOCK = 0x7fffffff;
	const clock_t ONESEC = 1000000 / CLOCKS_PER_SEC;
	const DWORD MAXBLOCKSIZE = 0x100000;
	const DWORD MINBLOCKSIZE = 0x200;
	const DWORD MAXCOUNTER = 100;
	const ULONGLONG MINCALCSIZE = 0x200000000;
	const DWORD VERIFYBYTES = 32;
	const int VERIFYLINES = 20;
	const ULONGLONG VERIFYBLOCK = ( VERIFYLINES * VERIFYBYTES );
	const DWORD DUMMYSLEEP = 500;
	const int DUMMYCNT = 20;
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	HANDLE fh = open_handle_write(argv[1]);	// open file or drive
	ULONGLONG towrite = 0;	// bytes to write
	LARGE_INTEGER li_filesize;	// file size as a crappy win32 file type
	if ( fh != INVALID_HANDLE_VALUE )
		if ( GetFileSizeEx(fh, &li_filesize) ) towrite = (ULONGLONG)li_filesize.QuadPart;
	ULONGLONG written = 0;	// to count written bytes
	DWORD blocksize = 0;	// block size to write
	BOOL xtrasave = FALSE;	// randomized overwrite
	BOOL dummy = FALSE;	// dummy mode
	ULONGLONG argull[2];	// size arguments
	int argullcnt = 0;
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( ( argv[i][0] == '/' && argv[i][2] == 0 )	// x for two pass mode
			&& ( argv[i][1] == 'x' || argv[i][1] == 'X' ) 
		) {
			if ( xtrasave ) error_toomany(fh);
			xtrasave = TRUE;
		} else if ( ( argv[i][0] == '/' && argv[i][2] == 0 )
			&& ( argv[i][1] == 'd' || argv[i][1] == 'D' )	// d for dummy mode
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
	if ( argullcnt == 1 ) {	// size arguments
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
	if ( dummy ) printf("Dummy mode, nothing will be written to disk\n");
	else if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open %s\n", argv[1]);
		error_close(fh);
	}
	if ( xtrasave ) printf("Pass 1 of 2, writing random bytes\n");
	else printf("Pass 1 of 1, writing zeros\n");
	fflush(stdout);	// spent one day finding out that this is needed for windows stdout
	char *bytesof = (char*)malloc(32 * sizeof(char));	//  to print written bytes
	sprintf(bytesof, " of %llu bytes\n", towrite);
	if ( dummy ) {	// dummy mode
		if ( towrite > MINCALCSIZE && blocksize == 0 ) {
			printf("Calculating best block size\n");
			fflush(stdout);
			blocksize = MAXBLOCKSIZE;
			for (DWORD size=blocksize; size>=MINBLOCKSIZE; size=size>>1) {
				printf("Testing block size %lu bytes\n", size);
				fflush(stdout);
				written += blocksize;
				Sleep(DUMMYSLEEP);
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
			}
			printf("Using block size %lu bytes\n", blocksize);
			fflush(stdout);
		}
		blocksize = MAXBLOCKSIZE;
		written = dummy_write_blocks(DUMMYCNT, DUMMYSLEEP, towrite, written, blocksize, bytesof);
		if ( xtrasave ) {
			printf("Pass 2 of 2, writing zeros\n");
			fflush(stdout);
			written = dummy_write_blocks(DUMMYCNT, DUMMYSLEEP, towrite, 0, blocksize, bytesof);
		}
	} else {	// the real thing starts here
		DWORD maxblocksize;	// build block at needed size to write
		if ( blocksize == 0 ) maxblocksize = MAXBLOCKSIZE;
		else maxblocksize = blocksize;
		char maxblock[maxblocksize];
		if ( xtrasave ) for (int i=0; i<maxblocksize; i++) maxblock[i] = (char)rand();
		else memset(maxblock, 0, sizeof(maxblock));
		BOOL writectrl;
		DWORD newwritten;
		/* Calculate best/fastes block size */
		if ( towrite > MINCALCSIZE && blocksize == 0 ) {
			printf("Calculating best block size\n");
			fflush(stdout);
			clock_t bestduration = MAXCLOCK;
			blocksize = maxblocksize;
			DWORD size = maxblocksize;
			clock_t duration;
			for (DWORD blockstw=MAXCOUNTER; size>=MINBLOCKSIZE; blockstw=blockstw<<1) {	// double blocks
				printf("Testing block size %lu bytes\n", size);
				clock_t start = clock();
				for (DWORD blockcnt=0; blockcnt<blockstw; blockcnt++) {
					writectrl = WriteFile(fh, maxblock, size, &newwritten, NULL);
					written += newwritten;
					if ( !writectrl || newwritten < size ) error_stopped(written, fh);
				}
				duration = clock() - start;	// duration of writeprocess
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
				if ( duration < bestduration ) {
					bestduration = duration;
					blocksize = size;
				}
				size = size >> 1;	// devide block by 2
			}
			printf("Using block size of %lu bytes\n", blocksize);
			fflush(stdout);
		} else if ( blocksize == 0 ) blocksize = maxblocksize;
		/* First pass */
		written = write_blocks(fh, maxblock, towrite, written, blocksize, ONESEC, bytesof);
		/* Second passs */
		if ( xtrasave ) {
			printf("Pass 2 of 2, writing zeros\n");
			fflush(stdout);
			memset(maxblock, 0, sizeof(maxblock));	// fill array with zeros
			close_handle(fh);	// close
			fh = open_handle_write(argv[1]);	// and open again for second pass
			if ( fh == INVALID_HANDLE_VALUE ) {
				fprintf(stderr, "Error: could not re-open %s\n", argv[1]);
				exit(1);
			}
			written = write_blocks(fh, maxblock, towrite, 0, blocksize, ONESEC, bytesof);
		}
	}
	close_handle(fh);
	/* Verify */
	printf("Verifying %s\n", argv[1]);
	fflush(stdout);
	fh = open_handle_read(argv[1]);	// open file or drive to verify
	if ( fh == INVALID_HANDLE_VALUE ) {
		if ( !dummy ) {
			fprintf(stderr, "Error: could not open %s to verify\n", argv[1]);
			error_close(fh);
		}
		printf("Bytes 0 - %llu:\n", written);
		fflush(stdout);
		for (int i=0; i<VERIFYLINES; i++) {
			printf("TH IS IS A_ DU MM Y_ OU TP UT 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n");
			fflush(stdout);
		}
	} else {
		ULONGLONG position = 0;	// position of vierified bytes
		ULONGLONG blockend, newposition;
		char readbuffer[VERIFYBYTES];
		char check;
		DWORD read_bytes;
		DWORD toread = VERIFYBYTES;
		LARGE_INTEGER moveto;
		while ( position < written ) {
			blockend = position + VERIFYBLOCK;
			if ( blockend > written ) blockend = written;
			printf("Bytes %llu - %llu:\n", position, blockend);
			fflush(stdout);
			while ( position < blockend ) {
				if ( position + toread > written ) toread = written - toread;
				if ( !ReadFile(fh,
					readbuffer,
					toread,
					&read_bytes,
					NULL
				) ) error_stopped(position+read_bytes, fh);
				position += read_bytes;
				check = 0;
				for (int i=0; i<toread; i++) {
					printf("%02X ", readbuffer[i]);
					check = check | readbuffer[i];
				}
				printf("\n");
				fflush(stdout);
				if ( check != 0 & !dummy ) {
					fprintf(stderr, "Error: found byte(s) not zero\n");
					error_close(fh);
				}
			}
			if ( position >= written ) break;
			blockend = ( written + VERIFYBLOCK ) >> 1;
			newposition = blockend - VERIFYBLOCK;
			if ( position > newposition ) position = written - VERIFYBLOCK;
			else position = newposition;
			if ( position > MAX_LARGE_INTEGER ) break;	// might be an unnecessary precaution
			moveto.QuadPart = position;	// win still is not a real 64 bit system...
			if ( !SetFilePointerEx(	// jump to position
				fh,
				moveto,
				NULL,
				FILE_BEGIN
			) ) {
				fprintf(stderr, "Error: could not go to position %lld in read %s\n",
					moveto.QuadPart, argv[1]);
				error_close(fh);
			}
		}
	}
	close_handle(fh);
	printf("All done, %llu bytes were zeroed by writing blocks of %lu bytes\n", written, blocksize);
	exit(0);
}
