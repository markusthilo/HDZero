/* zerod v0.1-20221218 */
/* written for Windows + MinGW */
/* Author: Markus Thilo' */
/* E-mail: markus.thilo@gmail.com */
/* License: GPL-3 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* Convert string to unsigned long long */
unsigned long long readullong(char *s) {
	int l = 0;
	while (1) {
		if (s[l] == 0) break;
		if ( s[l] < '0' | s[l] > '9' ) return 0;
		l++;
	}
	unsigned long long f = 1;
	unsigned long long r = 0;
	unsigned long long n;
	for (int i=l-1; i>=0; i--){
		n = r + (f * (s[i]-'0'));
		if (n < r) return 0;
		r = n;
		f *= 10;
	}
	return r;
}

/* Close file and exit */
void error_close(FILE *fptr) {
	fclose(fptr);
	exit(1);
}

/* Print error to stderr and exit */
void error_stopped(unsigned long long written, FILE *fptr) {
	fprintf(stderr, "Error: stopped after %llu bytes\n", written);
	error_close(fptr);
}

/* Write bytes to file by given block size */
unsigned long long writeblocks(
	unsigned long long towrite,
	unsigned long long written,
	int blocksize,
	int pinterval,
	char *maxblock,
	FILE *fptr
	)
{
	int newwritten = blocksize;;
	int blockcnt = 1;
	if ( towrite == 0 ) {	// write till fwrite stops
		int debug = 10; // DEBUG: limit file size
		while ( newwritten == blocksize ) {
			newwritten = fwrite(maxblock, 1, blocksize, fptr);	// write one block
			written += newwritten;
			if ( blockcnt == pinterval ) {
				printf("... %llu Bytes\n", written);
				blockcnt = 1;
				if ( --debug == 0 ) newwritten = 1;	// DEBUG
			} else blockcnt++;
		}
	} else {	// write to given filesize
		unsigned long long blockstw = (towrite - written) / blocksize;
		while ( blockstw-- > 0 ) {	// write blocks
			newwritten = fwrite(maxblock, 1, blocksize, fptr);	// write one block
			written += newwritten;
			if ( blockcnt == pinterval ) {
				printf("... %llu Bytes\n", written);
				blockcnt = 1;
			} else blockcnt++;
			if ( newwritten < blocksize ) error_stopped(written, fptr);
		}
		towrite = towrite - written;
		if ( towrite > 0 ) {	// write what's left
			newwritten = fwrite(maxblock, 1, towrite, fptr);
			written += newwritten;
			printf("... %llu Bytes\n", written);
			if ( newwritten < towrite ) error_stopped(written, fptr);
		}
	}
	return written;
}

/* Write bytes til fwrite stops */
unsigned long long writetoend(
	unsigned long long written,
	int blocksize,
	int pinterval,
	char *maxblock,
	FILE *fptr
) {
	int blockcnt = 1;
	int newwritten = blocksize;

	return written;
}

/* Main function - program starts here*/
int main(int argc, char **argv) {
	// Definitions
	const clock_t MAXCLOCK = 0x7fffffff;
	const unsigned long long MINCALCSIZE = 0x80000000;
	const int MAXBLOCKSIZE = 0x100000;
	const int MINBLOCKSIZE = 0x200;
	const int MAXCOUNTER = 100;
	const int DEFAULTPINTERVAL = 1000;
	const int PINTERVALBASE = 500000;
	/* CLI arguments */
	if ( argc < 2 ) {
		fprintf(stderr, "Error: Missing argument(s)\n");
		exit(1);
	}
	if ( argc > 4 ) {
		fprintf(stderr, "Error: too many arguments\n");
		exit(1);
	}
	FILE *fptr;	// file pointer for output
	fptr = fopen(argv[1], "w");	// open to write
	if ( fptr == NULL ) {
		fprintf(stderr, "Error: could not open output file %s\n", argv[1]);
		exit(1);
	}
	int xtrasave = 1;	// 0 = do randomized overwrite
	unsigned long long written = 0;	// to count written bytes
	unsigned long long towrite = 0;	// bytes to write, 0 = no limit
	unsigned long long argull;
	for (int i=2; i<argc; i++) {	// if there are more arguments
		if ( argv[i][1] == 0 & ( argv[i][0] == 'x' | argv[i][0] == 'X'  ) )
		{	// argument is x
			if ( xtrasave == 0 ) {
				fprintf(stderr, "Error: there is no double extra save mode\n");
				error_close(fptr);
			}
			xtrasave = 0;
		} else {
			argull = readullong(argv[i]);
			if ( argull != 0 ) {	// argument is bytes as ullong
				if ( towrite > 0 ) {
					fprintf(stderr, "Error: only 1x bytes to write makes sense\n");
					error_close(fptr);
				}
			towrite = argull;
			}
		}
	}
	/* End of CLI */
	char maxblock[MAXBLOCKSIZE];	// block to write
	if ( xtrasave == 0 ) for (int i=0; i<MAXBLOCKSIZE; i++) maxblock[i] = (char)rand();
	else memset(maxblock, 0, sizeof(maxblock));	// array to write
	/* Block size */
	int size = sizeof(maxblock);
	int blocksize = MAXBLOCKSIZE;	// blocksize to write
	int pinterval = DEFAULTPINTERVAL;	// interval to write bytes to stdout
	int newwritten;
	if ( towrite > MINCALCSIZE ) {	// calculate best/fastes Block size
		printf("Calculating best block size\n");
		clock_t start, duration;
		clock_t bestduration = MAXCLOCK;
		for (int cnt=MAXCOUNTER; size>=MINBLOCKSIZE; cnt=cnt<<1) {
			clock_t start = clock();
			for (int i=0; i<cnt; i++) {
				newwritten = fwrite(maxblock, 1, size, fptr);	// write one block
				written += newwritten;
				if ( newwritten < size ) error_stopped(written, fptr);
			}
			duration = clock() - start;	// duration of writeprocess as int
			if ( duration < bestduration ) {
				bestduration = duration;
				blocksize = size;
			}
			size = size >> 1;
		}
		printf("Using block size %d\n", blocksize);
		pinterval = PINTERVALBASE / bestduration;
	}
	/* First pass */
	if ( xtrasave == 0 ) printf("First pass: Writing random bytes\n");
	else printf("Writing zeros\n");
	written = writeblocks(towrite, written, blocksize, pinterval, maxblock, fptr);
	/* Second passs */
	if ( xtrasave == 0 ) {
		printf("Second pass: Writing zeros\n");
		memset(maxblock, 0, sizeof(maxblock));	// array of zeros to write
		fclose(fptr);
		fptr = fopen(argv[1], "w");	// open to write
		written = writeblocks(written, 0, blocksize, pinterval, maxblock, fptr);
	}
	printf("All done, now %llu Bytes are zero\n", written);
	fclose(fptr);
	exit(0);
}