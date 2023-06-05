/* zerod */
/* written for Windows + MinGW-W64 */
/* Author: Markus Thilo' */
/* E-mail: markus.thilo@gmail.com */
/* License: GPL-3 */

/* Version */
const char *VERSION = "1.1.1_20230605";

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>
#include <winioctl.h>

/* Definitions */
const int TTYPE_DISK = 1;	// types of targets
const int TTYPE_FILE = 2;
const clock_t MAXCLOCK = 0x7fffffff;
const clock_t ONESEC = 1000000 / CLOCKS_PER_SEC;
const DWORD DEFAULTBLOCKSIZE = 0x1000;	// default flash memory page size
const DWORD MINBLOCKSIZE = 0x200;	// minimal block size for drives
const int MAXBADBLOCKS = 10000;	// maximal number of bad blocks before abort
const ULONGLONG MINCALCSIZE = 0x100000000;
const int VERIFYPRINTLENGTH = 32;

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
	printf("(or zerod.exe /h for this help)\n\n");
	printf("TARGET:\n");
	printf("    File or physical drive\n\n");
	printf("BLOCK_SIZE (optional):\n");
	printf("    Size of blocks to write\n\n");
	printf("OPTIONS (optional):\n");
	printf("    /a - Write every block, regardless if it is already zeroed\n");
	printf("    /x - Two pass wipe, write blocks with random values as 1st pass, implies /a\n");
	printf("    /f - Fill with binary ones / 0xFF instad of zeros\n");
	printf("    /c - Check, do not wipe (zeros or 0xFF with /f)\n");
	printf("    /p - Print size, do not wipe\n\n");
	printf("Example:\n");
	printf("zerod.exe \\\\.\\PHYSICALDRIVE1 /x /v\n\n");
	printf("Disclaimer:\n");
	printf("The author is not responsible for any loss of data.\n");
	printf("Obviously, the tool is dangerous as its purpose is to erase data.\n\n");
	printf("Author: Markus Thilo\n");
	printf("License: GPL-3\n");
	printf("See: https://github.com/markusthilo/HDZero\n\n");
}

/* Define what you need to work with the target (file or device) */
typedef struct Z_TARGET {
	char *Path;	// string with path to device or file
    HANDLE Handle;	// handle for win api
	LONGLONG Pointer;	// working here
	LONGLONG Size;	// the full size to work
	LONGLONG FivePercent;	// number of bytes that are 5% of size
	int PercentPtr;	// percent that are processed
	LONGLONG *BadBlocks;	// array for bad blocks
	int BadBlockCnt;	// counter for bad blocks
	int Type;	// disk: 1, file: 2, error: -1
} Z_TARGET;

BOOL matches_bytes(BYTE *v, BYTE b, int len) {
	for (int i=0; i<len; i++) if ( v[i] != b ) return FALSE;
	return TRUE;
}

BOOL matches_ulls(ULONGLONG *v, ULONGLONG u, int len) {
	for (int i=0; i<len; i++) if ( v[i] != u ) return FALSE;
	return TRUE;
}

/* Close target */
void close_target(Z_TARGET *target) {
	if ( target->Type == TTYPE_DISK && !DeviceIoControl(
		target->Handle,
		IOCTL_DISK_UPDATE_PROPERTIES,
		NULL,
		0,
		NULL,
		0,
		NULL,
		NULL
	) ) printf("Warning: could not update %s\n", target->Path);
	if ( !CloseHandle(target->Handle) )
		printf("Warning: could not close %s\n", target->Path);
}

/* Set file pointer */
void set_pointer(Z_TARGET *target) {
	LARGE_INTEGER moveto;	// win still is not a real 64 bit system...
	moveto.QuadPart = target->Pointer;
	if ( SetFilePointerEx(target->Handle, moveto, NULL, FILE_BEGIN) ) return;
	fprintf(stderr, "Error: could not point to position %lld in %s\n",
		target->Pointer, target->Path);
	close_target(target);
	exit(1);
}

/* List bad blocks */
void list_bad_blocks(FILE *stream, Z_TARGET *target) {
	fprintf(stream, ": unable to wipe %lld block(s), offset(s) in bytes:\n", target->BadBlockCnt);
	for (int i; i<target->BadBlockCnt-1; i++) printf("%lld, ", target->BadBlocks[i]);
	printf("%lld\n", target->BadBlocks[target->BadBlockCnt-1]);
}

/* Read/write error */
void error_rw(Z_TARGET *target) {
	close_target(target);
	fprintf(stderr, "Error");
	list_bad_blocks(stderr, target);
	fprintf(stderr,"Too many bad blocks, aborting\n");
	exit(1);
}

void warning_unable_to_write(Z_TARGET *target, DWORD new, DWORD blocksize) {
	fprintf(stderr, "Warning: could not write block of %lu bytes at offset %lld\n",
		blocksize, target->Pointer + new);
	target->BadBlocks[target->BadBlockCnt++] = target->Pointer;
	if ( target->BadBlockCnt == MAXBADBLOCKS ) error_rw(target);
	target->Pointer += blocksize;	// continue
	set_pointer(target);
}

void warning_unable_to_read(Z_TARGET *target, DWORD new, DWORD blocksize) {
	fprintf(stderr, "Warning: could not read block of %lu bytes at offset %lld\n",
		blocksize, target->Pointer + new);
	target->BadBlocks[target->BadBlockCnt++] = target->Pointer;
	if ( target->BadBlockCnt == MAXBADBLOCKS ) error_rw(target);
	target->Pointer += blocksize;	// continue
	set_pointer(target);
}

void warning_not_wiped(Z_TARGET *target, DWORD new, DWORD blocksize) {
	fprintf(stderr, "Warning: block of %lu bytes at offset %lld is not completely wiped\n",
		blocksize, target->Pointer + new);
	target->BadBlocks[target->BadBlockCnt++] = target->Pointer;
	if ( target->BadBlockCnt == MAXBADBLOCKS ) error_rw(target);
	target->Pointer += blocksize;	// continue
}

LONGLONG print_percent(Z_TARGET *target, LONGLONG next_print) {
	target->PercentPtr += 5;
	printf("... %d\%, %lld of %lld bytes\n",
		target->PercentPtr, target->Pointer, target->Size);
	fflush(stdout);
	return next_print + target->FivePercent;
}

/* Write bytes by given block size */
void write_all_blocks(Z_TARGET *target, BYTE *byteblock, DWORD blocksize) {
	if ( target->Pointer >= target->Size ) return;
	LONGLONG blocks_tw;
	if ( blocksize < MINBLOCKSIZE ) {
		blocksize = target->Size - target->Pointer;
		blocks_tw = 1;
	} else {
		blocks_tw = (target->Size - target->Pointer) / blocksize;
		if ( blocks_tw < 1 ) return;
	}
	LONGLONG next_print = target->PercentPtr + target->FivePercent;
	DWORD new;
	for ( LONGLONG block_ptr=0; block_ptr<blocks_tw; block_ptr++ ) {
		if ( !WriteFile(target->Handle, byteblock, blocksize, &new, NULL)
			|| new < blocksize ) warning_unable_to_write(target, new, blocksize);
		else target->Pointer += new;
		if ( target->Pointer >= next_print ) next_print = print_percent(target, next_print);
	}
}

/* Verify bytes by given block size, wipe block if it is not already */
void selective_write_blocks(Z_TARGET *target, BYTE *byteblock, DWORD blocksize) {
	if ( target->Pointer >= target->Size ) return;
	DWORD new;
	if ( blocksize < MINBLOCKSIZE ) {
		blocksize = target->Size - target->Pointer;
		BYTE *readblock = malloc(blocksize);
		if ( ReadFile(target->Handle, readblock, blocksize, &new, NULL)	// zeroed?
				&& new == blocksize && matches_bytes(readblock, byteblock[0], blocksize)
			) target->Pointer += new;
		else {
			set_pointer(target);	// write block
			if ( !WriteFile(target->Handle, byteblock, blocksize, &new, NULL)
				|| new < blocksize ) warning_unable_to_write(target, new, blocksize);
			else target->Pointer += new;
		}
	} else {
		ULONGLONG zeroff_ull;
		if ( byteblock[0] == 0 ) zeroff_ull = 0;
		else zeroff_ull = 0xffffffffffffffff;
		LONGLONG next_print = target->PercentPtr + target->FivePercent;
		LONGLONG blocks_trw = (target->Size - target->Pointer) / blocksize;
		if ( blocks_trw < 1 ) return;
		ULONGLONG *readblock = malloc(blocksize);
		int ullperblock = blocksize >> 3;	// ull in one block = blocksize / 8
		for ( LONGLONG block_ptr=0; block_ptr<blocks_trw; block_ptr++ ) {
			if ( ReadFile(target->Handle, readblock, blocksize, &new, NULL)	// zeroed?
				&& new == blocksize && matches_ulls(readblock, zeroff_ull, ullperblock)
			) target->Pointer += new;
			else {
				set_pointer(target);	// write block
				if ( !WriteFile(target->Handle, byteblock, blocksize, &new, NULL)
					|| new < blocksize ) warning_unable_to_write(target, new, blocksize);
				else target->Pointer += new;
			}
			if ( target->Pointer >= next_print ) next_print = print_percent(target, next_print);
		}
	}
}

/* Verify bytes by given block size */
void verify_blocks(Z_TARGET *target, DWORD blocksize, BYTE zeroff) {
	if ( target->Pointer >= target->Size ) return;
	DWORD new;
	if ( blocksize < MINBLOCKSIZE ) {
		blocksize = target->Size - target->Pointer;
		BYTE *readblock = malloc(blocksize);
		if ( ReadFile(target->Handle, readblock, blocksize, &new, NULL)	// zeroed?
			&& new == blocksize && !matches_bytes(readblock, zeroff, blocksize) )
			target->Pointer += new;
		else {
			warning_not_wiped(target, new, blocksize);
			target->Pointer += blocksize;
			set_pointer(target);
		}
	} else {
		ULONGLONG zeroff_ull;
		if ( zeroff == 0 ) zeroff_ull = 0;
		else zeroff_ull = 0xffffffffffffffff;
		LONGLONG next_print = target->PercentPtr + target->FivePercent;
		LONGLONG blocks_tr = (target->Size - target->Pointer) / blocksize;
		if ( blocks_tr < 1 ) return;
		ULONGLONG *readblock = malloc(blocksize);
		int ullperblock = blocksize >> 3;	// ull in one block = blocksize / 8
		for ( LONGLONG block_ptr=0; block_ptr<blocks_tr; block_ptr++ ) {
			if ( ReadFile(target->Handle, readblock, blocksize, &new, NULL)	// zeroed?
				&& new == blocksize && matches_ulls(readblock, zeroff_ull, ullperblock)
			) target->Pointer += new;
			else {
				warning_not_wiped(target, new, blocksize);
				target->Pointer += blocksize;
				set_pointer(target);	// write block
			}
			if ( target->Pointer >= next_print ) next_print = print_percent(target, next_print);
		}
	}
}

/* Print block to stdout */
void print_block(Z_TARGET *target, BYTE zeroff) {
	set_pointer(target);
	DWORD blocksize = target->Size - target->Pointer;
	if ( blocksize > MINBLOCKSIZE ) blocksize = MINBLOCKSIZE;	// do not show more than 512 bytes
	BYTE byteblock[blocksize];
	DWORD new;
	if ( !ReadFile(target->Handle, byteblock, blocksize, &new, NULL)
		|| new != blocksize ) {
		printf("Warning: could not read block of %lu bytes at offset %lld",
			blocksize, target->Pointer);
		return;
	}
	printf("Bytes %llu - %llu", target->Pointer, target->Pointer + new);
	target->Pointer += new;
	int t = 1;
	int badbyte = -1;
	for (DWORD p=0; p<new; p++) {
		if ( --t == 0 ) {
			printf("\n");
			t = VERIFYPRINTLENGTH;
		}
		printf("%02X ", byteblock[p]);
		if ( byteblock[p] != zeroff ) badbyte = TRUE;
	}
	printf("\n");
	fflush(stdout);
	if ( badbyte > -1 ) fprintf(stderr, "Warning: block is not completely wiped\n");
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
	if  ( argc == 2 && argv[1][2] == 0	// process cli arguments
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
	BOOL write_all = FALSE; // overwrite every block
	BOOL pure_check = FALSE;	// only check if blocks are zeroed
	BOOL print_size = FALSE; // only print size
	for (int i=2; i<argc; i++) {
		if ( argv[i][0] == '/' && argv[i][2] == 0 ) {	// swith?
			if ( argv[i][1] == 'x' || argv[i][1] == 'X' ) {	// x for two pass mode
				if ( xtrasave || pure_check ) error_toomany();
				xtrasave = TRUE;
			} else if ( argv[i][1] == 'f' || argv[i][1] == 'F' ) {	// f to fill with 0xff
				if ( zeroff != 0 ) error_toomany();
				zeroff = 0xff;
			} else if ( argv[i][1] == 'a' || argv[i][1] == 'A' ) {	// a to write every block
				if ( write_all || pure_check ) error_toomany();
				write_all = TRUE;
			} else if ( argv[i][1] == 'c' || argv[i][1] == 'C' ) {	// c for pure check
				if ( pure_check || write_all || xtrasave ) error_toomany();
				pure_check = TRUE;
			} else if ( argv[i][1] == 'p' || argv[i][1] == 'P' ) {	// p to get size
				if ( argc > 3 ) error_toomany();
				print_size = TRUE;
			} else error_wrong(argv[i]);
		} else {	// not a swtich, may be blocksize?
			int ptr = 0;
			while ( TRUE ) {
				if ( argv[i][ptr] == 0 ) break;
				if ( argv[i][ptr] < '0' || argv[i][ptr] > '9' ) error_wrong(argv[i]);
				ptr++;
			}
			if ( blocksize > 0 ) error_toomany();
			DWORD fac = 1;
			DWORD newbs;
			while ( --ptr >= 0 ) {
				newbs = blocksize + ( fac * (argv[i][ptr]-'0') );
				if ( newbs < blocksize ) {
					fprintf(stderr, "Error: block size out of range\n");
					exit(1);
				}
				blocksize = newbs;
				fac *= 10;
			}
		}
	}
	if ( print_size ) {	// print size, do nothing more
		HANDLE fHandle = CreateFile(	// open file or device to read
			argv[1],
			FILE_READ_DATA,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			0,
			NULL
		);
		if ( fHandle == INVALID_HANDLE_VALUE ) {
			fprintf(stderr, "Error: could not open %s to get size\n", argv[1]);
			exit(1);
			}
		LONGLONG size;
		DISK_GEOMETRY_EX dge;	// disk?
		if ( DeviceIoControl(
			fHandle,
			IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL,
			0,
			&dge,
			sizeof(dge),
			NULL,
			NULL
		) ) size = dge.DiskSize.QuadPart; 
		else {
			LARGE_INTEGER filesize;	// file?
			if ( GetFileSizeEx(fHandle, &filesize) ) size = filesize.QuadPart;
			else {
				fprintf(stderr, "Error: could not determin size of %s\n", argv[1]);
				exit(1);
			}
		}
		printf("Size of %s is %llu bytes\n", argv[1], size);
		exit(0);
	}
	Z_TARGET target;	// wipe, not /p
	target.Path = argv[1];
	target.Pointer = 0;
	if ( pure_check ) target.Handle = CreateFile(
		target.Path,
		FILE_READ_DATA,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
	else target.Handle = CreateFile(
		target.Path,
		FILE_READ_DATA | FILE_WRITE_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);
	if ( target.Handle == INVALID_HANDLE_VALUE ) {
		fprintf(stderr, "Error: could not open %s\n", target.Path);
		exit(1);
	}
	LARGE_INTEGER filesize;	// file?
	if ( GetFileSizeEx(target.Handle, &filesize) ) {
			target.Size = filesize.QuadPart;
			target.Type = TTYPE_FILE;
	} else {
		DISK_GEOMETRY_EX dge;	// disk?
		if ( DeviceIoControl(
			target.Handle,
			IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL,
			0,
			&dge,
			sizeof(dge),
			NULL,
			NULL
		) ) {
			target.Size = dge.DiskSize.QuadPart;
			target.Type = TTYPE_DISK;
		} else {
			fprintf(stderr, "Error: could not determin size or type of %s\n", target.Path);
			exit(1);
		}
		if ( !pure_check && !DeviceIoControl(
			target.Handle,
			IOCTL_DISK_DELETE_DRIVE_LAYOUT,
			NULL,
			0,
			NULL,
			0,
			NULL,
			NULL
		) ) {
			fprintf(stderr, "Error: could not delete drive layout of %s\n", target.Path);
			exit(1);
		}
	}
	if ( xtrasave ) write_all = TRUE;
	if ( blocksize == 0 ) blocksize = DEFAULTBLOCKSIZE;
	else if ( target.Type == TTYPE_DISK && blocksize % MINBLOCKSIZE != 0 ) {
		fprintf(stderr, "Error: drives require block sizes n*512\n");
		exit(1);
	}
	if ( target.Size > 100 ) target.FivePercent = target.Size / 20;
	else target.FivePercent = target.Size+1;	// no percentage on mini sizes
	target.PercentPtr = 0;
	BYTE *byteblock = malloc(blocksize);	// build block to write
	target.BadBlocks = malloc(sizeof(LONGLONG)*MAXBADBLOCKS);
	if ( xtrasave ) {
		 for (int i=0; i<blocksize; i++) byteblock[i] = (char)rand();
		 printf("Pass 1 of 2, writing random bytes\n");
	} else {
		memset(byteblock, zeroff, blocksize);
		printf("Pass 1 of 1, writing 0x%02X\n", zeroff);
	}
	fflush(stdout);
	clock_t start, duration;
	if ( write_all ) {	// write every block
		start = clock();	// 1st pass
		write_all_blocks(&target, byteblock, blocksize);
		write_all_blocks(&target, byteblock, MINBLOCKSIZE);
		write_all_blocks(&target, byteblock, (DWORD)(target.Size-target.Pointer));
		duration = clock() - start;
		printf("1st pass took %f second(s) / %ld clock units\n", duration/CLOCKS_PER_SEC, duration);
		if ( xtrasave ) {	// 2nd pass on /x
			printf("Pass 2 of 2, writing 0x%02X\n", zeroff);
			fflush(stdout);
			memset(byteblock, zeroff, blocksize);
			target.Pointer = 0;
			set_pointer(&target);
			target.PercentPtr = 0;
			start = clock();
			write_all_blocks(&target, byteblock, blocksize);
			write_all_blocks(&target, byteblock, MINBLOCKSIZE);
			write_all_blocks(&target, byteblock, 1);
			duration = clock() - start;
			printf("2nd pass took %f second(s) / %ld clock units\n", duration/CLOCKS_PER_SEC, duration);
		}
	} else {	// overwrite only blocks that are not zeroed
		start = clock();
		selective_write_blocks(&target, byteblock, blocksize);
		selective_write_blocks(&target, byteblock, MINBLOCKSIZE);
		selective_write_blocks(&target, byteblock, 1);
		duration = clock() - start;
		printf("Verifying and wiping took %f second(s) / %ld clock units\n",
			duration/CLOCKS_PER_SEC, duration);
	}
	free(byteblock);
	/* Verify */
	if ( FALSE ) {	// full verify checks every byte
		printf("Verifying %s\n", target.Path);
		target.Pointer = 0;
		set_pointer(&target);	// start verification at first byte
		verify_blocks(&target, blocksize, zeroff);
		verify_blocks(&target, MINBLOCKSIZE, zeroff);
		verify_blocks(&target, (DWORD)(target.Size-target.Pointer), zeroff);
		printf("Verified %lld bytes\n", target.Pointer);
	}
	printf("Sample:\n");
	target.Pointer = 0;	// print first block
	print_block(&target, zeroff);
	LONGLONG halfblocks = target.Size / ( MINBLOCKSIZE << 1 );	// block in the middle?
	if ( halfblocks >= 4 ) {
		target.Pointer = halfblocks*MINBLOCKSIZE;
		print_block(&target, zeroff);
	}
	if ( target.Pointer < target.Size) {	// last block?
		target.Pointer = target.Size - MINBLOCKSIZE;
		print_block(&target, zeroff);
	}
	close_target(&target);
	printf("All done, %lld bytes ", target.Size);
	printf("were written\n");
	exit(0);
}
