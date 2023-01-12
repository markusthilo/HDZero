/* zerod v1.0.1_20230112 */
/* written for Windows + MinGW */
/* Author: Markus Thilo' */
/* E-mail: markus.thilo@gmail.com */
/* License: GPL-3 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>
#include <winioctl.h>

// Definitions
const ULONGLONG MAX_LARGE_INTEGER = 0x7fffffffffffffff;
const clock_t MAXCLOCK = 0x7fffffff;
const clock_t ONESEC = 1000000 / CLOCKS_PER_SEC;
const DWORD MAXBLOCKSIZE = 0x100000;
const DWORD MINBLOCKSIZE = 0x200;	// 512 bytes = 1 sector
const int TESTBLOCKS = 0x10;
const ULONGLONG MINCALCSIZE = 0x100000000;
const int RETRIES = 20;
const DWORD RETRY_SLEEP = 1000;
const int VERIFYPRINTLENGTH = 32;
const DWORD DUMMYSLEEP = 250;
const int DUMMYCNT = 10;

/* Print error to stderr and exit */
void error_toomany() {
	fprintf(stderr, "Error: too many arguments\n");
	exit(1);
}

/* Print error to stderr and exit */
void error_wrong(char *arg) {
	fprintf(stderr, "Error: wrong argument %s\n", arg);
	exit(1);
}

/* Convert string to DWORD for block size as cli argument */
DWORD read_blocksize(char *s) {
	int p = 0;
	while ( TRUE ) {
		if ( s[p] == 0 ) break;
		if ( s[p] < '0' || s[p] > '9' ) return 0;	// return 0 if not a number
		p++;
	}
	DWORD f = 1;
	DWORD r = 0;
	DWORD n;
	while ( --p >= 0 ) {
		n = r + ( f * (s[p]-'0') );
		if ( n < r ) return 0;	// in case given size is too large
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
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
	if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open output file or device %s to write\n", path);
		exit(1);
	}
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

/* Get size */
ULONGLONG get_size(HANDLE fh) {
	ULONGLONG size;
	DISK_GEOMETRY_EX dge;		// disk?
	if ( DeviceIoControl(fh, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &dge, sizeof(dge), NULL, NULL) )
		size = dge.DiskSize.QuadPart;
	else {	// file?
		LARGE_INTEGER li_filesize;
		if ( !GetFileSizeEx(fh, &li_filesize) ) {
			fprintf(stderr, "Error: could not determin a file or disk to wipe\n");
			error_close(fh);
		}
		size = li_filesize.QuadPart;
	}
	return size;
}

/* Open handle to read */
HANDLE open_handle_read(char *path) {
	HANDLE fh = CreateFile(
		path,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
	return fh;
}

void error_blocksize(HANDLE fh) {
	fprintf(stderr, "Error: given blocksize is over limit\n");
	error_close(fh);
}

void error_stopped(ULONGLONG position, HANDLE fh) {
	fprintf(stderr, "Error: stopped after %llu bytes\n", position);
	error_close(fh);
}

void error_notzero(ULONGLONG position, HANDLE fh) {
	fprintf(stderr, "Error: found bytes that are not wiped at %llu\n", position);
	error_close(fh);
}

/* Set file pointer */
void set_pointer(HANDLE fh, ULONGLONG position) {
	if ( position > MAX_LARGE_INTEGER ) {	// might be an unnecessary precaution
		fprintf(stderr, "Error: position %lld is out of range\n", position);
		error_close(fh);
	}
	LARGE_INTEGER moveto;	// win still is not a real 64 bit system...
	moveto.QuadPart = position;
	if ( !SetFilePointerEx(	// jump to position
		fh,
		moveto,
		NULL,
		FILE_BEGIN
	) ) {
		fprintf(stderr, "Error: could not point to position %lld\n", moveto.QuadPart);
		error_close(fh);
	}
}

/* Retry to write on write errors */
ULONGLONG retry_write_block(
	HANDLE fh,
	BYTE *maxblock,
	ULONGLONG position,
	DWORD blocksize
) {
	DWORD newwritten;
	printf("Warning: could not write, retrying to write block at %llu of %lu bytes\n", position, blocksize);
	for (int cnt=RETRIES; cnt>=1; cnt--) {
		printf("Retrying %d... \n", cnt);
		fflush(stdout);
		set_pointer(fh, position);
		if ( WriteFile(fh, maxblock, blocksize, &newwritten, NULL) && newwritten == blocksize )
			return position + newwritten;
		Sleep(RETRY_SLEEP);
	}
	error_stopped(position+newwritten, fh);
}

/* Write bytes to file by given block size */
ULONGLONG write_blocks(
	HANDLE fh,
	BYTE *maxblock,
	ULONGLONG towrite,
	ULONGLONG written,
	DWORD blocksize,
	char *bytesof
) {
	if ( towrite - written >= blocksize ) {	// write blocks
		ULONGLONG tominusblock = towrite - blocksize;
		DWORD newwritten;
		clock_t printclock = clock() + ONESEC;
		while ( written < tominusblock ) {	// write blocks
			if ( !WriteFile(fh, maxblock, blocksize, &newwritten, NULL) || newwritten < blocksize )
				written = retry_write_block(fh, maxblock, written, blocksize);
			else written += newwritten;
			if ( clock() >= printclock ) {
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
				printclock = clock() + ONESEC;
			}
		}
	}
	return written;
}

/* Write bytes to file by given block size */
ULONGLONG write_to(
	HANDLE fh,
	BYTE *maxblock,
	ULONGLONG towrite,
	ULONGLONG written,
	DWORD blocksize,
	char *bytesof
) {
	written = write_blocks(fh, maxblock, towrite, written, blocksize, bytesof);	// write full block size
	written = write_blocks(fh, maxblock, towrite, written, MINBLOCKSIZE, bytesof);	// write minimal full blocks
	DWORD leftsize = towrite - written;
	if ( leftsize > 0 ) {	// < 512 bytes
		DWORD newwritten;
		if ( !WriteFile(fh, maxblock, leftsize, &newwritten, NULL) || newwritten < leftsize )
			written = retry_write_block(fh, maxblock, written, leftsize);
		else written += newwritten;
	}
	printf("... %llu%s", written, bytesof);
	fflush(stdout);
	return written;
}

/* Write by given block size and easure the time needed */
clock_t write_blocks_timer(
	HANDLE fh,
	BYTE *maxblock,
	int blockstw,
	DWORD blocksize,
	ULONGLONG written
) {
	DWORD newwritten;
	clock_t start = clock();	// get start time
	for (int blockcnt=0; blockcnt<blockstw; blockcnt++) {
		if ( !WriteFile(fh, maxblock, blocksize, &newwritten, NULL) || newwritten < blocksize ) {
			set_pointer(fh, written);	// ...go back to last correct block
			return ~blockcnt;	// return negative blocks on write error
		}
		written += newwritten;
	}
	return clock() - start;	// return duration of writeprocess
}

/* Retry to read block of ULONGLONGs */
void retry_read_ullblock(
	HANDLE fh,
	ULONGLONG *ullblock,
	ULONGLONG position,
	DWORD blocksize
) {
	DWORD newread;
	printf("Warning: could not read, retrying to read block at %llu of %lu bytes\n", position, blocksize);
	for (int cnt=RETRIES; cnt>=1; cnt--) {
		printf("Retrying %d... \n", cnt);
		fflush(stdout);
		set_pointer(fh, position);
		if ( ReadFile(fh, ullblock, blocksize, &newread, NULL) && newread == blocksize ) return;
		Sleep(RETRY_SLEEP);
	}
	error_stopped(position+newread, fh);
}

/* Retry to read block of BYTEs */
void retry_read_byteblock(
	HANDLE fh,
	BYTE *byteblock,
	ULONGLONG position,
	DWORD blocksize
) {
	DWORD newread;
	printf("Warning: could not read, retrying to read block at %llu of %lu bytes (end of target)\n", position, blocksize);
	for (int cnt=RETRIES; cnt>=1; cnt--) {
		printf("Retrying %d... \n", cnt);
		fflush(stdout);
		set_pointer(fh, position);
		if ( ReadFile(fh, byteblock, blocksize, &newread, NULL) && newread == blocksize ) return;
		Sleep(RETRY_SLEEP);
	}
	error_stopped(position+newread, fh);
}

/* Verify bytes by given block size */
ULONGLONG verify_blocks(
	HANDLE fh,
	ULONGLONG written,
	ULONGLONG position,
	DWORD blocksize,
	BYTE zeroff,
	char *bytesof
) {
	if ( written - position >= blocksize ) {
		DWORD ullperblock = blocksize >> 3;	// ull in one block = blocksize / 8
		ULONGLONG ullblock[ullperblock];
		ULONGLONG tominusblock;
		ULONGLONG check;
		memset(&check, zeroff, sizeof(ULONGLONG));
		DWORD newread;
		tominusblock = written - blocksize;
		clock_t printclock = clock() + ONESEC;
		while ( position < tominusblock ) {
			if ( !ReadFile(fh, ullblock, blocksize, &newread, NULL ) || newread != blocksize )
				retry_read_ullblock(fh, ullblock, position, blocksize);
			for (DWORD p=0; p<ullperblock; p++)
				if ( ullblock[p] != check ) error_notzero(position + ( p * sizeof(ULONGLONG) ), fh);
			position += blocksize;
			if ( clock() >= printclock ) {
				printf("... %llu%s", position, bytesof);
				fflush(stdout);
				printclock = clock() + ONESEC;
			}
		}
	}
	return position;
}

/* Verify bytes by given block size */
ULONGLONG verify_all(
	HANDLE fh,
	ULONGLONG written,
	DWORD blocksize,
	BYTE zeroff,
	char *bytesof
) {
	ULONGLONG position = verify_blocks(fh, written, 0, blocksize, zeroff, bytesof);
	position = verify_blocks(fh, written, position, blocksize, zeroff, bytesof);
	if ( position < written ) {	// verify less than minimal block - only for files, not disks
		DWORD bytesleft = (DWORD)(written - position);
		BYTE byteblock[bytesleft];
		DWORD newread;
		if ( !ReadFile(fh, byteblock, bytesleft, &newread, NULL) || newread != bytesleft )
			retry_read_byteblock(fh, byteblock, position, bytesleft);
		for (DWORD p=0; p<bytesleft; p++) if ( byteblock[p] != zeroff ) {
			fprintf(stderr, "Error: found byte that is not zero at position %lld\n", position+p);
			error_close(fh);
		}
		position += bytesleft;
	}
	printf("... %llu%s", position, bytesof);
	fflush(stdout);
	return position;
}

/* Print block to stdout */
ULONGLONG print_block(HANDLE fh, ULONGLONG written, ULONGLONG position, BYTE zeroff) {
	set_pointer(fh, position);
	if ( position >= written ) return written;	// just in case...
	DWORD blocksize = written - position;
	if ( blocksize > MINBLOCKSIZE ) blocksize = MINBLOCKSIZE;	// do not show more than 512 bytes
	BYTE byteblock[blocksize];
	DWORD newread;
	if ( !ReadFile(fh, byteblock, blocksize, &newread, NULL ) || newread != blocksize )
		retry_read_byteblock(fh, byteblock, position, blocksize);
	printf("Bytes %llu - %llu", position, position+newread);
	int t = 1;
	BOOL badbyte = FALSE;
	for (DWORD p=0; p<newread; p++) {
		if ( --t == 0 ) {
			printf("\n");
			t = VERIFYPRINTLENGTH;
		}
		printf("%02X ", byteblock[p]);
		if ( byteblock[p] != zeroff ) badbyte = TRUE;
	}
	printf("\n");
	fflush(stdout);
	if ( badbyte ) error_notzero(position, fh);
	return position + newread;
}

/* Main function - program starts here*/
int main(int argc, char **argv) {
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	BYTE zeroff = 0;	// char / value to write
	DWORD blocksize = 0;	// block size to write
	BOOL xtrasave = FALSE;	// randomized overwrite
	BOOL full_verify = FALSE; // to verify every byte
	BOOL dummy = FALSE;	// dummy mode
	DWORD arg_blocksize = 0; // block size 0 = not set
	int argullcnt = 0;
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( argv[i][0] == '/' && argv[i][2] == 0 ) {	// swith?
			if ( argv[i][1] == 'x' || argv[i][1] == 'X' ) {	// x for two pass mode
				if ( xtrasave ) error_toomany();
				xtrasave = TRUE;
			} else if ( argv[i][1] == 'f' || argv[i][1] == 'F' ) {	// f to fill with 0xff
				if ( zeroff != 0 ) error_toomany();
				zeroff = 0xff;
			} else if ( argv[i][1] == 'v' || argv[i][1] == 'V' ) {	// v for full verify
				if ( full_verify ) error_toomany();
				full_verify = TRUE;
			} else if ( argv[i][1] == 'd' || argv[i][1] == 'D' ) {	// d for dummy mode
				if ( dummy ) error_toomany();
				dummy = TRUE;
			} else error_wrong(argv[i]);
		} else {	// not a swtich, may be blocksize?
			arg_blocksize = read_blocksize(argv[i]);
			if ( arg_blocksize > 0 ) {
				if ( blocksize > 0 ) error_toomany();
				blocksize = arg_blocksize;
			} else error_wrong(argv[i]);
		}
	}
	/* End of CLI */
	HANDLE fh = open_handle_write(argv[1]);	// open file or drive
	ULONGLONG towrite = get_size(fh);	// get size of disk or file
	if ( towrite == 0 ) {
		printf("All done, 0 bytes to process");
		exit(0);
	}
	ULONGLONG written = 0;	// to count written bytes
	if ( dummy ) printf("Dummy mode, nothing will be written to disk\n");
	if ( xtrasave ) printf("Pass 1 of 2, writing random bytes\n");
	else printf("Pass 1 of 1, writing 0x%02X\n", zeroff);
	fflush(stdout);	// spent one day finding out that this is needed for windows stdout
	char *bytesof = (char*)malloc(32 * sizeof(char));	//  to print "of ... bytes"
	sprintf(bytesof, " of %llu bytes\n", towrite);
	if ( dummy ) {	// dummy mode
		if ( blocksize == 0 ) blocksize = MAXBLOCKSIZE;
		printf("Using block size %lu bytes\n", blocksize);
		clock_t start = clock();
		int cnt = DUMMYCNT;
		while ( written < towrite && cnt-- > 1 ) {
			written += blocksize;
			if ( written > towrite ) break;
			Sleep(DUMMYSLEEP);
			printf("... %llu%s", written, bytesof);
			fflush(stdout);
		}
		written = towrite;
	} else {	// the real thing starts here
		DWORD maxblocksize = MAXBLOCKSIZE;
		if ( blocksize > 0 ) maxblocksize = blocksize;
		BYTE maxblock[maxblocksize];	// generate block to write
		if ( xtrasave ) for (int i=0; i<maxblocksize; i++) maxblock[i] = (char)rand();
		else memset(maxblock, zeroff, maxblocksize);
		if ( blocksize == 0 && towrite >= MINCALCSIZE ) {	// calculate best/fastes block size
			written = write_to(fh, maxblock, maxblocksize*TESTBLOCKS, written, maxblocksize, bytesof);
			printf("Calculating best block size\n");
			fflush(stdout);
			clock_t duration;
			clock_t bestduration = MAXCLOCK;
			int blockstw = TESTBLOCKS;
			blocksize = MAXBLOCKSIZE;
			DWORD testsize = blocksize;
			while ( testsize>=MINBLOCKSIZE ) {
				printf("Testing block size %lu bytes\n", testsize);
				duration = write_blocks_timer(fh, maxblock, blockstw, testsize, written);
				if ( duration < 0 ) written += ( (int)~duration ) * testsize;	// write error
				else written += blockstw * testsize;
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
				if ( duration >= 0 ) {
					if ( duration < bestduration ) {
						bestduration = duration;
						blocksize = testsize;
					} else if ( duration > bestduration ) break;
				}
				testsize = testsize >> 1;	// testsize / 2
				blockstw = blockstw << 1;	// double blocks to test
			}
			if ( bestduration == MAXCLOCK) blocksize = MINBLOCKSIZE; 	// try minimal block size as backup
		}  else if ( blocksize == 0 ) blocksize = MAXBLOCKSIZE;
		printf("Using block size of %lu bytes\n", blocksize);
		fflush(stdout);
		/* First pass */
		written = write_to(fh, maxblock, towrite, written, blocksize, bytesof);
		/* Second passs */
		if ( xtrasave ) {
			printf("Pass 2 of 2, writing 0x%02X\n", zeroff);
			fflush(stdout);
			memset(maxblock, zeroff, maxblocksize);
			close_handle(fh);	// close
			fh = open_handle_write(argv[1]);	// and open again for second pass
			if ( fh == INVALID_HANDLE_VALUE ) {
				fprintf(stderr, "Error: could not re-open %s\n", argv[1]);
				exit(1);
			}
			written = write_to(fh, maxblock, towrite, 0, blocksize, bytesof);
		}
	}
	close_handle(fh);
	/* Verify */
	printf("Verifying %s\n", argv[1]);
	fflush(stdout);
	fh = open_handle_read(argv[1]);	// open file or drive to verify
	if ( fh == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open %s to verify\n", argv[1]);
		error_close(fh);
	}
	if ( full_verify ) {	// full verify checks every byte
		ULONGLONG verified = verify_all(fh, written, blocksize, zeroff, bytesof);
		printf("Verified %llu bytes\n", verified);
	}
	ULONGLONG position = print_block(fh, written, 0, zeroff);	// print first block
	ULONGLONG halfblocks = written / ( MINBLOCKSIZE << 1 );
	if ( halfblocks >= 4 ) position = print_block(fh, written, halfblocks*MINBLOCKSIZE, zeroff);
	if ( position + MINBLOCKSIZE <= written ) print_block(fh, written, written-MINBLOCKSIZE, zeroff);
	else if ( position < written ) print_block(fh, written, position, zeroff);
	close_handle(fh);
	printf("All done, %llu bytes were wiped\n", written);
	exit(0);
}
