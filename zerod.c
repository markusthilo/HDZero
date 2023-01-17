/* zerod */
/* written for Windows + MinGW-W64 */
/* Author: Markus Thilo' */
/* E-mail: markus.thilo@gmail.com */
/* License: GPL-3 */

/* Version */
const char *VERSION = "1.0.1_2023011/";

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>
#include <winioctl.h>

/* Definitions */
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

/* Print help text */
void print_help() {
	printf("\n                                       000\n");
	printf("                                       000\n");
	printf("                                       000\n");
	printf("00000000  000000  0000000 000000   0000000\n");
	printf("   0000  000  000 00000  00000000 0000 000\n");
	printf("  0000   00000000 000    000  000 000  000\n");
	printf(" 0000    0000     000    00000000 0000 000\n");
	printf("00000000  000000  000     000000   0000000\n\n");
	printf("v%s\n\n", VERSION);
	printf("Overwrite file or device with zeros\n\n");
	printf("Usage:\n");
	printf("zerod.exe TARGET [BLOCK_SIZE] [OPTIONS]\n");
	printf("or zerod.exe /h for this help\n\n");
	printf("TARGET:\n");
	printf("    file or physical drive\n\n");
	printf("BLOCK_SIZE (optional):\n");
	printf("    size of blocks to write\n\n");
	printf("OPTIONS (optional):\n");
	printf("    /x - 2 pass wipe, blocks with random values as 1st pass\n");
	printf("    /f - fill with binary ones / 0xff instad of zeros\n");
	printf("    /v - verify every byte after wipe\n");
	printf("    /p - only print size\n\n");
	printf("Example:\n");
	printf("zerod.exe \\\\.\\PHYSICALDRIVE1 /x /v\n\n");
	printf("Disclaimer:\n");
	printf("The author is not responsible for any loss of data.\n");
	printf("Obviously, the tool is dangerous as its purpose is to erase data.\n\n");
	printf("Author: Markus Thilo\n");
	printf("License: GPL-3\n");
	printf("See: https://github.com/markusthilo/HDZero\n\n");
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

/* Open file or device to write */
HANDLE open_handle_write(char *path) {
	return CreateFile(
		path,
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
}

/* Open file or device to read */
HANDLE open_handle_read(char *path) {
	return CreateFile(
		path,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
}

/* Close handle */
BOOL close_handle(HANDLE fh) {
	if ( fh == INVALID_HANDLE_VALUE || CloseHandle(fh) ) return TRUE;
	return FALSE;
}

/* Get size */
LONGLONG get_size(HANDLE fh) {
	DISK_GEOMETRY_EX dge;		// disk?
	LARGE_INTEGER li_filesize;	// file?
	if ( DeviceIoControl(
		fh,
		IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
		NULL,
		0,
		&dge,
		sizeof(dge),
		NULL,
		NULL
	) ) return dge.DiskSize.QuadPart;
	if ( GetFileSizeEx(fh, &li_filesize) ) return li_filesize.QuadPart;
	return -1;
}

/* Retrie warning and delay */
void warning_retry(char *warning, int cnt, int attempts) {
	if ( cnt == 0 ) return;
	if ( cnt == 1 ) printf("Warning: %s\n", warning);
	if ( cnt >= 1 ) {
		Sleep(RETRY_SLEEP);
		printf("Attempt %d of %d\n", cnt, attempts);
		fflush(stdout);
	}
}

/* Define what you need to work with the target (file or device) */
typedef struct Z_TARGET {
    HANDLE Handle;
	LONGLONG Pointer;
	LONGLONG Size;
} Z_TARGET;

/* Open target, get size and write first block */
Z_TARGET open_write_target(char *path, BYTE *byteblock, DWORD blocksize) {
	Z_TARGET target;
	char warning[sizeof(path)+32];
	sprintf(warning, "could not open %s to write", path);
	BOOL error_open = FALSE;	// give exact error
	BOOL error_size = FALSE;
	BOOL error_write = FALSE;
	LARGE_INTEGER li_zero;	// win still is not a real 64 bit system...
	li_zero.QuadPart = 0;
	DWORD newwritten;
	for (int cnt=0; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		target.Handle = open_handle_write(path);
		if ( target.Handle == INVALID_HANDLE_VALUE ) {
			error_open = TRUE;
			continue;
		} else error_open = FALSE;
		target.Size = get_size(target.Handle);
		if ( target.Size < 0 ) {
			error_size = TRUE;
			continue;
		} else error_size = FALSE;
		if ( !WriteFile(
			target.Handle,
			byteblock,
			blocksize,
			&newwritten,
			NULL
		) || newwritten != blocksize ) {
			SetFilePointerEx(target.Handle, li_zero, NULL, FILE_BEGIN);
			error_write = TRUE;
		} else {
			target.Pointer = newwritten;
			return target;
		}
	}
	fprintf(stderr, "Error: ");
	if ( error_open ) fprintf(stderr, "could not open %s\n", path);
	if ( error_size ) fprintf(stderr, "could not get size of %s\n", path);
	if ( error_write ) fprintf(stderr, "could not write to %s\n", path);
	close_handle(target.Handle);
	exit(1);
}

/* Open handle to read */
Z_TARGET open_read_target(char *path) {
	Z_TARGET target;
	char warning[sizeof(path)+32];
	sprintf(warning, "could not open %s to read", path);
	for (int cnt=0; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		target.Handle = open_handle_read(path);
		if ( target.Handle == INVALID_HANDLE_VALUE ) continue;
		target.Size = get_size(target.Handle);
		if ( target.Size >= 0 ) {
			target.Pointer = 0;
			return target;
		}
	}
	fprintf(stderr, "Error: could not open %s to read\n", path);
	close_handle(target.Handle);
	exit(1);
}

/* Close target */
void close_target(Z_TARGET target) {
	if ( !close_handle(target.Handle) ) {
		fprintf(stderr, "Error: could not close output file or device\n");
		exit(1);
	}
}

/* Set file pointer */
void set_pointer(Z_TARGET target) {
	LARGE_INTEGER moveto;	// win still is not a real 64 bit system...
	moveto.QuadPart = target.Pointer;
	char warning[64];
	sprintf(warning, "could not point to position %lld", target.Pointer);
	for (int cnt=0; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		if ( SetFilePointerEx(	// jump to position
			target.Handle,
			moveto,
			NULL,
			FILE_BEGIN
		) ) return;
	}
	fprintf(stderr, "Error: could not point to position %lld\n", target.Pointer);
	close_target(target);
	exit(1);
}

/* Retry to write on errors */
Z_TARGET retry_write_block(Z_TARGET target, BYTE *byteblock, DWORD blocksize) {
	char warning[96];
	sprintf(
		warning,
		"could not write, retrying to write block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	DWORD newwritten;
	for (int cnt=1; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		set_pointer(target);
		if ( WriteFile(
			target.Handle,
			byteblock,
			blocksize,
			&newwritten,
			NULL
		) && newwritten == blocksize ) {
			target.Pointer += newwritten;	
			return target;
		}
	}
	fprintf(stderr, "Error: could not write block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	close_target(target);
	exit(1);
}

/* Write bytes to file by given block size */
Z_TARGET write_blocks(Z_TARGET target, BYTE *byteblock, DWORD blocksize, int blockstw) {
	if ( blocksize > 0  && blockstw != 0 && target.Size - target.Pointer >= blocksize ) {
		char bytesof[32];	//  to print "of ... bytes"
		sprintf(bytesof, " of %lld bytes\n", target.Size);
		LONGLONG tominusblock;
		if ( blockstw < 0 ) tominusblock = target.Size - blocksize;	// -1 => to possible end
		else tominusblock = target.Pointer + ( blocksize * blockstw );
		DWORD newwritten;
		clock_t printclock = clock() + ONESEC;
		while ( target.Pointer <= tominusblock ) {	// write blocks
			if ( !WriteFile(
				target.Handle,
				byteblock,
				blocksize,
				&newwritten,
				NULL
			) || newwritten < blocksize )
				target = retry_write_block(target, byteblock, blocksize);
			else target.Pointer += newwritten;
			if ( clock() >= printclock ) {
				printf("... %lld%s", target.Pointer, bytesof);
				fflush(stdout);
				printclock = clock() + ONESEC;
			}
		}
	}
	return target;
}

/* Write from position to end of target */
Z_TARGET write_to_end(Z_TARGET target, BYTE *byteblock, DWORD blocksize) {
	target = write_blocks(target, byteblock, blocksize, -1);	// write full block size
	target = write_blocks(target, byteblock, MINBLOCKSIZE, -1);	// write minimal full blocks
	target = write_blocks(target, byteblock, (DWORD)(target.Size-target.Pointer), -1);
	printf("... %lld of %lld bytes\n", target.Pointer, target.Size);
	fflush(stdout);
	return target;
}

/* Print error to stderr and exit */
void error_read(Z_TARGET target, DWORD blocksize) {
	fprintf(stderr, "Error: could not read block at %lld of %lu bytes\n", target.Pointer, blocksize);
	close_target(target);
	exit(1);
}

/* Print error to stderr and exit */
void error_notzero(Z_TARGET target, DWORD offset) {
	fprintf(stderr, "Error: found bytes that are not wiped at %lld\n", target.Pointer + offset);
	close_target(target);
	exit(1);
}

/* Retry to read block of ULONGLONGs */
Z_TARGET retry_read_ullblock(Z_TARGET target, ULONGLONG *ullblock, DWORD blocksize) {
	char warning[96];
	sprintf(
		warning,
		"could not read, retrying to read block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	DWORD newread;
	for (int cnt=1; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		set_pointer(target);
		if ( ReadFile(
			target.Handle,
			ullblock,
			blocksize,
			&newread,
			NULL
		) && newread == blocksize ) {
			target.Pointer += newread;	
			return target;
		}
	}
	fprintf(stderr, "Error: could not read block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	close_target(target);
	exit(1);
}

/* Retry to read block of BYTEs */
Z_TARGET retry_read_byteblock(Z_TARGET target, BYTE *byteblock, DWORD blocksize) {
	char warning[96];
	sprintf(
		warning,
		"could not read, retrying to read block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	DWORD newread;
	for (int cnt=1; cnt<=RETRIES; cnt++) {
		warning_retry(warning, cnt, RETRIES);
		set_pointer(target);
		if ( ReadFile(
			target.Handle,
			byteblock,
			blocksize,
			&newread,
			NULL
		) && newread == blocksize ) {
			target.Pointer += newread;	
			return target;
		}
	}
	fprintf(stderr, "Error: could not read block at %lld of %lu bytes",
		target.Pointer, 
		blocksize
	);
	close_target(target);
	exit(1);
}

/* Verify bytes by given block size */
Z_TARGET verify_blocks(Z_TARGET target, DWORD blocksize, BYTE zeroff) {
	if ( blocksize > 0 && target.Size - target.Pointer >= blocksize ) {
		char bytesof[32];	//  to print "of ... bytes"
		sprintf(bytesof, " of %lld bytes\n", target.Size);
		if ( blocksize >= MINBLOCKSIZE ) {	// full block size
			ULONGLONG *ullblock = malloc(blocksize);
			DWORD ullperblock = blocksize >> 3;	// ull in one block = blocksize / 8
			ULONGLONG check;
			memset(&check, zeroff, sizeof(ULONGLONG));
			DWORD newread;
			LONGLONG tominusblock = target.Size - blocksize;
			clock_t printclock = clock() + ONESEC;
			while ( target.Pointer <= tominusblock ) {
				if ( !ReadFile(
					target.Handle,
					ullblock,
					blocksize,
					&newread,
					NULL
				) || newread != blocksize ) retry_read_ullblock(target, ullblock, blocksize);
				for (DWORD p=0; p<ullperblock; p++)
					if ( ullblock[p] != check ) error_notzero(target, p * sizeof(ULONGLONG) );
				target.Pointer += newread;
				if ( clock() >= printclock ) {
					printf("... %llu%s", target.Pointer, bytesof);
					fflush(stdout);
					printclock = clock() + ONESEC;
				}
			}
		} else {	// check the bytes left
			blocksize = target.Size - target.Pointer;
			BYTE *byteblock = malloc(blocksize);
			DWORD newread;
			if ( !ReadFile(
				target.Handle,
				byteblock,
				blocksize,
				&newread,
				NULL
			) || newread != blocksize ) retry_read_byteblock(target, byteblock, blocksize);
			for (DWORD p=0; p<blocksize; p++)
				if ( byteblock[p] != zeroff ) error_notzero(target, p);
			target.Pointer += newread;
		}
	}
	return target;
}

/* Print block to stdout */
Z_TARGET print_block(Z_TARGET target, BYTE zeroff) {
	set_pointer(target);
	DWORD blocksize = target.Size - target.Pointer;
	if ( blocksize > MINBLOCKSIZE ) blocksize = MINBLOCKSIZE;	// do not show more than 512 bytes
	BYTE byteblock[blocksize];
	DWORD newread;
	if ( !ReadFile(
		target.Handle,
		byteblock,
		blocksize,
		&newread,
		NULL
	) || newread != blocksize ) {
		printf("Warning: could not read block at %lld of %lu bytes", target.Pointer, blocksize);
		target.Pointer += blocksize;
		return target;
	}
	printf("Bytes %llu - %llu", target.Pointer, target.Pointer+newread);
	int t = 1;
	int badbyte = -1;
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
	if ( badbyte > -1 ) error_notzero(target, badbyte);
	target.Pointer += newread;
	return target;
}

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

/* Main function - program starts here*/
int main(int argc, char **argv) {
	/* CLI arguments */
	if  ( argc == 2 && argv[1][2] == 0
		&& ( argv[1][0] == '/' || argv[1][0] == '-' )
		&& ( argv[1][1] == 'h' || argv[1][1] == 'H' ) ) {
		print_help();
		exit(0);
	}
	if ( argc < 2 ) {
		print_help();
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	BYTE zeroff = 0;	// char / value to write
	DWORD blocksize = 0;	// block size to write
	BOOL xtrasave = FALSE;	// randomized overwrite
	BOOL full_verify = FALSE; // to verify every byte
	BOOL print_size = FALSE; // only print size
	DWORD arg_blocksize = 0; // block size 0 = not set
	for (int i=2; i<argc; i++) {
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
			} else if ( argv[i][1] == 'p' || argv[i][1] == 'P' ) {	// p for probe access
				if ( argc > 3 ) error_toomany();
				print_size = TRUE;
			} else error_wrong(argv[i]);
		} else {	// not a swtich, may be blocksize?
			arg_blocksize = read_blocksize(argv[i]);
			if ( arg_blocksize > 0 ) {
				if ( blocksize > 0 ) error_toomany();
				blocksize = arg_blocksize;
			} else error_wrong(argv[i]);
		}
	}
	/* Print size */
	if ( print_size ) {	// print size, do nothing more
		HANDLE fh  = open_handle_read(argv[1]);
		if ( fh == INVALID_HANDLE_VALUE ) {
			fprintf(stderr, "Error: could not open %s to get size\n", argv[1]);
			exit(1);
		}
		LONGLONG size = get_size(fh);
		if ( size < 0 ) {
			fprintf(stderr, "Error: could not determin size of %s\n", argv[1]);
			exit(1);
		}
		printf("All done, %s has %llu bytes\n", argv[1], size);
		exit(0);
	}
	/* Main task */
	DWORD maxblocksize = MAXBLOCKSIZE;
	if ( blocksize > 0 ) maxblocksize = blocksize;
	BYTE *byteblock = malloc(maxblocksize);	// generate block to write
	if ( xtrasave ) {
		 for (int i=0; i<maxblocksize; i++) byteblock[i] = (char)rand();
		 printf("Pass 1 of 2, writing random bytes\n");
	} else {
		memset(byteblock, zeroff, maxblocksize);
		printf("Pass 1 of 1, writing 0x%02X\n", zeroff);
	}
	fflush(stdout);	// spent one day finding out that this is needed for windows stdout
	Z_TARGET target = open_write_target(argv[1], byteblock, maxblocksize);
	if ( blocksize == 0 && target.Size >= MINCALCSIZE ) {	// calculate best/fastes block size
		blocksize = MAXBLOCKSIZE;
		int blockstw = TESTBLOCKS;
		printf("Calculating best block size\n");
		fflush(stdout);
		clock_t start, duration;
		clock_t bestduration = MAXCLOCK;
		DWORD testsize = blocksize;
		DWORD newwritten;
		while ( testsize>=MINBLOCKSIZE ) {
			printf("Testing block size %lu bytes\n", testsize);
			start = clock();	// get start time
			for (int blockcnt=0; blockcnt<blockstw; blockcnt++) {
				if ( !WriteFile(
					target.Handle,
					byteblock,
					testsize,
					&newwritten,
					NULL
				) || newwritten < testsize ) {
					set_pointer(target);	// on write error go back to last correct block
					duration = MAXCLOCK;
					break;
				} else target.Pointer += newwritten;
			}
			duration = clock() - start;
			printf("... %lld of %lld bytes\n", target.Pointer, target.Size);
			fflush(stdout);
			if ( duration < bestduration ) {
				bestduration = duration;
				blocksize = testsize;
			} else if ( duration > bestduration ) break;
			testsize = testsize >> 1;	// testsize / 2
			blockstw = blockstw << 1;	// double blocks to test
		}
		if ( bestduration == MAXCLOCK) blocksize = MINBLOCKSIZE; 	// try minimal block size as backup
	}  else if ( blocksize == 0 ) blocksize = MAXBLOCKSIZE;
	printf("Using block size of %lu bytes\n", blocksize);
	fflush(stdout);
	/* First pass */
	target = write_to_end(target, byteblock, blocksize);
	/* Second passs */
	if ( xtrasave ) {
		printf("Pass 2 of 2, writing 0x%02X\n", zeroff);
		fflush(stdout);
		memset(byteblock, zeroff, maxblocksize);
		target.Pointer = 0;
		set_pointer(target);
		target = write_to_end(target, byteblock, blocksize);
	}
	free(byteblock);
	close_target(target);
	/* Verify */
	printf("Verifying %s\n", argv[1]);
	fflush(stdout);
	target = open_read_target(argv[1]);	// open file or drive to verify
	if ( full_verify ) {	// full verify checks every byte
		target = verify_blocks(target, blocksize, zeroff);
		target = verify_blocks(target, MINBLOCKSIZE, zeroff);
		target = verify_blocks(target, (DWORD)(target.Size-target.Pointer), zeroff);
		printf("... %lld of %lld bytes\n", target.Pointer, target.Size);
		printf("Verified all %lld bytes\n", target.Pointer);
		fflush(stdout);
	}
	target.Pointer = 0;	// print first block
	target = print_block(target, zeroff);
	LONGLONG halfblocks = target.Size / ( MINBLOCKSIZE << 1 );	// block in the middle?
	if ( halfblocks >= 4 ) {
		target.Pointer = halfblocks*MINBLOCKSIZE;
		target = print_block(target, zeroff);
	}
	if ( target.Pointer < target.Size) {	// last block?
		target.Pointer = target.Size - MINBLOCKSIZE;
		target = print_block(target, zeroff);
	}
	close_target(target);
	printf("All done, %lld bytes were wiped\n", target.Size);
	exit(0);
}
