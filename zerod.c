/* zerod v0.1-20230102 */
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
const DWORD MINBLOCKSIZE = 0x200;
const DWORD MAXCOUNTER = 0x10;
const ULONGLONG MINCALCSIZE = 0x100000000;
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

/* Convert string to unsigned long long */
DWORD read_blocksize(char *s) {
	int p = 0;
	while ( TRUE ) {
		if ( s[p] == 0 ) break;
		if ( s[p] < '0' || s[p] > '9' ) return 0;
		p++;
	}
	DWORD f = 1;
	DWORD r = 0;
	DWORD n;
	while ( --p >= 0 ) {
		n = r + ( f * (s[p]-'0') );
		if ( n < r ) return 0;
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
	DISK_GEOMETRY pdg;	// disk?
	if ( DeviceIoControl(
		fh,
		IOCTL_DISK_GET_DRIVE_GEOMETRY,
		NULL,
		0,
		&pdg,
		sizeof(pdg),
        NULL,
		NULL
	) ) size = pdg.Cylinders.QuadPart * (ULONG)pdg.TracksPerCylinder *
			(ULONG)pdg.SectorsPerTrack * (ULONG)pdg.BytesPerSector;
	else {	// file?
		LARGE_INTEGER li_filesize;	// file size as a crappy win32 file type
		if ( !GetFileSizeEx(fh, &li_filesize) ) {
			fprintf(stderr, "Error: could not determin a file or disk to wipe\n");
			error_close(fh);
		}
		size = (ULONGLONG)li_filesize.QuadPart;
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
	fprintf(stderr, "Error: found bytes that are not zero at or beyond %llu\n", position);
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

/* Write bytes to file by given block size */
ULONGLONG write_blocks(
	HANDLE fh,
	BYTE *maxblock,
	ULONGLONG towrite,
	ULONGLONG written,
	DWORD blocksize,
	char *bytesof
) {
	BOOL writectrl;
	DWORD newwritten;
	if ( towrite - written > blocksize ) {
		ULONGLONG tominusblock = towrite - blocksize;
		clock_t printclock = clock() + ONESEC;
		while ( written < tominusblock ) {	// write blocks
			if ( !WriteFile(fh, maxblock, blocksize, &newwritten, NULL)
				|| newwritten < blocksize ) error_stopped(written+newwritten, fh);
			written += newwritten;
			if ( clock() >= printclock ) {
				printf("... %llu%s", written, bytesof);
				fflush(stdout);
				printclock += ONESEC;
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

/* Verify bytes by given block size */
ULONGLONG verify_blocks(
	HANDLE fh,
	ULONGLONG written,
	ULONGLONG position,
	DWORD blocksize,
	char *bytesof
) {
	DWORD ullperblock = blocksize >> 3;	// ull in one block = blocksize / 8
	ULONGLONG ullblock[ullperblock];
	ULONGLONG tominusblock;
	DWORD newread;
	if ( written - position >= blocksize ) {
		tominusblock = written - blocksize;
		clock_t printclock = clock() + ONESEC;
		while ( position < tominusblock ) {
			if ( !ReadFile(fh,
				ullblock,
				blocksize,
				&newread,
				NULL
			) || newread != blocksize ) error_stopped(position+newread, fh);
			for (DWORD p=0; p<ullperblock; p++) if ( ullblock[p] != 0 )
				error_notzero(position + (p<<3), fh);
			position += blocksize;
			if ( clock() >= printclock ) {
				printf("... %llu%s", position, bytesof);
				fflush(stdout);
				printclock += ONESEC;
			}
		}
	}
	if ( written - position >= MINBLOCKSIZE ) {
		tominusblock = written - MINBLOCKSIZE;
		ullperblock = MINBLOCKSIZE / 8;
		while ( position < tominusblock ) {
			if ( !ReadFile(fh,
				ullblock,
				MINBLOCKSIZE,
				&newread,
				NULL
			) || newread != MINBLOCKSIZE ) error_stopped(position+newread, fh);
			for (DWORD p=0; p<ullperblock; p++) if ( ullblock[p] != 0 )
				error_notzero(position + (p<<3), fh);
			position += MINBLOCKSIZE;
		}
	}
	if ( position < written ) {
		DWORD bytesleft = (DWORD)(written - position);
		BYTE byteblock[bytesleft];
		if ( !ReadFile(fh,
			byteblock,
			bytesleft,
			&newread,
			NULL
		) || newread != bytesleft ) error_stopped(position, fh);
		for (DWORD p=0; p<bytesleft; p++) if ( byteblock[p] != 0 ) error_notzero(position, fh);
		position += bytesleft;
	}
	printf("... %llu%s", position, bytesof);
	fflush(stdout);
	return position;
}

/* Verify bytes by given block size */
ULONGLONG print_block(HANDLE fh, ULONGLONG written, ULONGLONG position) {
	set_pointer(fh, position);
	BYTE charblock[MINBLOCKSIZE];
	DWORD newread;
	if ( written - position >= MINBLOCKSIZE ) {
		if ( !ReadFile(fh,
			charblock,
			MINBLOCKSIZE,
			&newread,
			NULL
		) || newread != MINBLOCKSIZE ) error_stopped(position+newread, fh);
	} else {
		if ( !ReadFile(fh,
			charblock,
			(DWORD)(written - position),
			&newread,
			NULL
		) || newread != MINBLOCKSIZE ) error_stopped(position+newread, fh);
	}
	printf("Bytes %llu - %llu:", position, position+newread);
	int t = 1;
	BYTE check;
	for (DWORD p=0; p<newread; p++) {
		if ( --t == 0 ) {
			printf("\n");
			t = VERIFYPRINTLENGTH;
		}
		printf("%02X ", charblock[p]);

		check = check | charblock[p];
	}
	printf("\n");
	fflush(stdout);
	if ( check != 0 ) error_notzero(position, fh);
	return position;
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
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	DWORD blocksize = 0;	// block size to write
	BOOL xtrasave = FALSE;	// randomized overwrite
	BOOL full_verify = FALSE; // to verify every byte
	BOOL dummy = FALSE;	// dummy mode
	DWORD arg_blocksize = 0; // block size 0 = not set
	int argullcnt = 0;
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( ( argv[i][0] == '/' && argv[i][2] == 0 )	// x for two pass mode
			&& ( argv[i][1] == 'x' || argv[i][1] == 'X' ) 
		) {
			if ( xtrasave ) error_toomany();
			xtrasave = TRUE;
		} else if ( ( argv[i][0] == '/' && argv[i][2] == 0 )
			&& ( argv[i][1] == 'v' || argv[i][1] == 'V' )	// v for full verify
		) {
			if ( full_verify ) error_toomany();
			full_verify = TRUE;
		} else if ( ( argv[i][0] == '/' && argv[i][2] == 0 )
			&& ( argv[i][1] == 'd' || argv[i][1] == 'D' )	// d for dummy mode
		) {
			if ( dummy ) error_toomany();
			dummy = TRUE;
		} else {
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
	ULONGLONG written = 0;	// to count written bytes
	if ( dummy ) printf("Dummy mode, nothing will be written to disk\n");
	if ( xtrasave ) printf("Pass 1 of 2, writing random bytes\n");
	else printf("Pass 1 of 1, writing zeros\n");
	fflush(stdout);	// spent one day finding out that this is needed for windows stdout
	char *bytesof = (char*)malloc(32 * sizeof(char));	//  to print written bytes
	sprintf(bytesof, " of %llu bytes\n", towrite);
	if ( dummy ) {	// dummy mode
		if ( towrite >= MINCALCSIZE && blocksize == 0 ) {
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
		DWORD maxblocksize = MAXBLOCKSIZE;
		if ( blocksize > 0 ) maxblocksize = blocksize; 
		BYTE maxblock[maxblocksize];	// generate block to write
		if ( xtrasave ) for (int i=0; i<maxblocksize; i++) maxblock[i] = (char)rand();
		else memset(maxblock, 0, maxblocksize);
		if ( blocksize == 0 && towrite >= MINCALCSIZE ) {	// calculate best/fastes block size
			DWORD newwritten;
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
					if ( !WriteFile(fh, maxblock, size, &newwritten, NULL)
						|| newwritten < size ) error_stopped(written+newwritten, fh);
					written += newwritten;
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
		}  else if ( blocksize == 0 ) blocksize = MAXBLOCKSIZE;
		printf("Using block size of %lu bytes\n", blocksize);
		fflush(stdout);
		/* First pass */
		written = write_blocks(fh, maxblock, towrite, written, blocksize, bytesof);
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
			written = write_blocks(fh, maxblock, towrite, 0, blocksize, bytesof);
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
		ULONGLONG verified = verify_blocks(fh, written, 0, blocksize, bytesof);
		printf("Veriefied %llu bytes\n", verified);
	}
	ULONGLONG position = print_block(fh, written, 0);	// print first block
	ULONGLONG halfblocks = written / ( MINBLOCKSIZE << 1 );
	if ( halfblocks > 4 ) position = print_block(fh, written, halfblocks*MINBLOCKSIZE);
	if ( position + MINBLOCKSIZE <= written ) print_block(fh, written, written-MINBLOCKSIZE);
	else if ( position < written ) print_block(fh, written, position);
	close_handle(fh);
	printf("All done, %llu bytes were zeroed\n", written);
	exit(0);
}
