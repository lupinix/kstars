/*
 * Star data to read the binary format used by KStars and output it as plain text records, for testing purposes
 *
 * Date: 28th May 2008, Author: Akarsh Simha <akarshsimha@gmail.com>
 * License: GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

/*
 * struct to store star data, to be written in this format, into the binary file.
 */

typedef struct starData {
    int32_t RA;
    int32_t Dec;
    int32_t dRA;
    int32_t dDec;
    int32_t parallax;
    int32_t HD;
    int16_t mag;
    int16_t bv_index;
    char spec_type[2];
    char flags;
    char unused;
} starData;


/*
 * enum listing out various possible data types
 */

enum dataType {
    DT_CHAR,              /* Character */
    DT_INT8,              /* 8-bit Integer */
    DT_UINT8,             /* 8-bit Unsigned Integer */
    DT_INT16,             /* 16-bit Integer */
    DT_UINT16,            /* 16-bit Unsigned Integer */
    DT_INT32,             /* 32-bit Integer */
    DT_UINT32,            /* 32-bit Unsigned Integer */
    DT_CHARV,             /* Fixed-length array of characters */
    DT_STR,               /* Variable length array of characters, either terminated by NULL or by the limit on field size */
    DT_SPCL = 128         /* Flag indicating that the field requires special treatment (eg: Different bits may mean different things) */
};

/*
 * struct to store the description of a field / data element in the binary files
 */

typedef struct dataElement {
    char name[10];
    int8_t size;
    u_int8_t type;
    int32_t scale;
} dataElement;

dataElement de[100];
u_int16_t nfields;
long index_offset, data_offset;
char byteswap;
u_int16_t ntrixels;

void charv2str(char *str, char *charv, int n) {
    int i;
    for(i = 0; i < n; ++i) {
	*str = *charv;
	str++;
	charv++;
    }
    *str = '\0';
}

int displayDataElementDescription(dataElement *e) {
    char str[11];
    charv2str(str, e -> name, 10);
    printf("\nData Field:\n");
    printf("  Name: %s\n", str);
    printf("  Size: %d\n", e -> size);
    printf("  Type: %d\n", e -> type);
    printf("  Scale: %ld\n", e -> scale);
}

// NOTE: Ineffecient. Not to be used for high-productivity
// applications
void swapbytes(void *ptr, int nbytes) {

    char *destptr;
    char *i;

    if(!byteswap)
	return;

    destptr = (char *)malloc(nbytes);
    i = ((char *)ptr + (nbytes - 1));
    while( i >= (char *)ptr ) {
	*destptr = *i;
	++destptr;
	--i;
    }

    destptr -= nbytes;

    memcpy(ptr, (void *)destptr, nbytes);
    free(destptr);

}

// WARNING: The following two functions work only for a Level 3 HTM
int trixel2number(char *trixel) {
    // NOTE: We do not test the validity of the trixel
    return (trixel[4] - '0') + (trixel[3] - '0') * 4 + (trixel[2] - '0') * 16 + (trixel[1] - '0') * 64 + ((trixel[0] == 'N')?0:256);
}

char *number2trixel(char *trixel, int number) {

    trixel[0] = ((number >= 256)?('S'+((number -= 256) - number)):'N'); // Obfuscated Code Contest Winner
    trixel[1] = number / 64 + '0';
    number = number % 64;
    trixel[2] = number / 16 + '0';
    number = number % 16;
    trixel[3] = number / 4 + '0';
    trixel[4] = number % 4 + '0';
    return trixel;
}

int verifyIndexValidity(FILE *f) {

    int i;
    char str[6];
    u_int16_t trixel;
    u_int32_t offset;
    u_int32_t prev_offset;
    u_int16_t nrecs;
    u_int16_t prev_nrecs;
    unsigned int nerr;

    fprintf(stdout, "Performing Index Table Validity Check...\n");
    fprintf(stdout, "Assuming that index starts at %X\n", ftell(f));
    index_offset = ftell(f);

    prev_offset = 0;
    prev_nrecs = 0;
    str[5]='\0';
    nerr = 0;

    for(i = 0; i < ntrixels; ++i) {
	if(!fread(&trixel, 2, 1, f)) {
	    fprintf(stderr, "Table truncated before expected! Read i = %d records so far\n", i);
	    +nerr;
	    break;
	}
	if(trixel >= ntrixels) {
	    fprintf(stderr, "Trixel number %u is greater than the expected number of trixels %u\n", trixel, ntrixels);
	    ++nerr;
	}
	if(trixel != i) {

	    fprintf(stderr, "Found trixel = %s with number = %d, while I expected number = %d\n", 
		    number2trixel(str, trixel), trixel, i);
	    ++nerr;
	}
	fread(&offset, 4, 1, f);
	fread(&nrecs, 2, 1, f);
	if(prev_offset != 0 && prev_nrecs != (-prev_offset + offset)/0x20) { // CAUTION: Change 0x20 to sizeof(starData) if starData changes
	    fprintf(stderr, "Expected %u  = (%X - %x) / 32 records, but found %u, in trixel %s\n", (offset - prev_offset) / 32, offset, prev_offset, nrecs, str);
	    ++nerr;
	}
	prev_offset = offset;
	prev_nrecs = nrecs;
    }

    data_offset = ftell(f);

    if(nerr) { fprintf(stderr, "ERROR ;-): The index seems to have %u errors\n", nerr); }
    else { fprintf(stdout, "Index verified. PASSED.\n"); }

}

void readStarList(FILE *f, char *trixel, FILE *names) {
    int id;
    long offset;
    long n;
    u_int16_t nrecs;
    u_int16_t trix;
    char bayerName[8];
    char longName[32];
    char str[6];
    starData data;

    str[5] = '\0';

    id = trixel2number(trixel);
    rewind(f);
    offset = index_offset + id * 8; // CAUTION: Change if the size of each entry in the index table changes
    fseek(f, offset, SEEK_SET);
    fread(&trix, 2, 1, f);
    if(trix != id) {
	fprintf(stderr, "ERROR: Something fishy in the index. I guessed that %s would be here, but instead I find %s. Aborting.\n", trixel, number2trixel(str,trix));
	return;
    }
    fread(&offset, 4, 1, f);
    fread(&nrecs, 2, 1, f);
    if(fseek(f, offset, SEEK_SET)) {
	fprintf(stderr, "ERROR: Could not seek to position %X in the file. The file is either truncated or the indexes are bogus.\n", offset);
	return;
    }

    for(id = 0; id < nrecs; ++id) {
	offset = ftell(f);
	n = (offset - data_offset)/0x20;
	fread(&data, sizeof(starData), 1, f);
	printf("Entry #%d\n", id);
	printf("\tRA = %f\n", data.RA / 1000000.0);
	printf("\tDec = %f\n", data.Dec / 100000.0);
	printf("\tdRA/dt = %f\n", data.dRA / 10.0);
	printf("\tdDec/dt = %f\n", data.dDec / 10.0);
	printf("\tParallax = %f\n", data.parallax / 10.0);
	printf("\tHD Catalog # = %lu\n", data.HD);
	printf("\tMagnitude = %f\n", data.mag / 100.0);
	printf("\tB-V Index = %f\n", data.bv_index / 100.0);
	printf("\tSpectral Type = %c%c\n", data.spec_type[0], data.spec_type[1]);
	printf("\tHas a name? %s\n", ((data.flags & 0x01)?"Yes":"No"));
	/*
	  if(data.flags & 0x01 && names) {
	  fseek(names, n * (32 + 8) + 0xA0, SEEK_SET);
	  fread(bayerName, 8, 1, names);
	  fread(longName, 32, 1, names);
	  printf("\t\tBayer Designation = %s\n", bayerName);
	  printf("\t\tLong Name = %s\n", longName);
	  }
	*/
	printf("\tMultiple Star? %s\n", ((data.flags & 0x02)?"Yes":"No"));
	printf("\tVariable Star? %s\n", ((data.flags & 0x04)?"Yes":"No"));
    }
}

/**
 *@short  Read the KStars binary file header and display its contents
 *@param f  Binary file to read from
 *@returns  non-zero if succesful, zero if not
 */

int readFileHeader(FILE *f) {
    int i;
    int16_t endian_id;
    char ASCII_text[125];

    if(f == NULL)
	return 0;

    fread(ASCII_text, 124, 1, f);
    ASCII_text[125] = '\0';
    printf("%s", ASCII_text);

    fread(&endian_id, 2, 1, f);
    if(endian_id != 0x4B53)
	byteswap = 1;
    else
	byteswap = 0;

    fread(&nfields, 2, 1, f);
  
    for(i = 0; i < nfields; ++i) {
	fread(&(de[i]), sizeof(struct dataElement), 1, f);
	displayDataElementDescription(&(de[i]));
    }

    fread(&ntrixels, 2, 1, f);

    return 1;
}


int main(int argc, char *argv[]) {

    FILE *f, *names;
    names = NULL;
    if(argc <= 1) {
	fprintf(stderr, "USAGE: %s filename [trixel]\n", argv[0]);
	fprintf(stderr, "Designed for use only with KStars star data files\n")
	return 1;
    }

    f = fopen(argv[1], "rb");

    if(f == NULL) {
	fprintf(stderr, "ERROR: Could not open file %s for binary read.\n", argv[1]);
	return 1;
    }

    readFileHeader(f);

    verifyIndexValidity(f);

    if(argc > 2) {
	/*
	  if(argc > 3)
	  names = fopen(argv[3], "rb");
	  else
	  names = NULL;
	
	  fprintf(stderr, "Names = %s\n", ((names)?"Yes":"No"));
	*/
	readStarList(f, argv[2], names);
    }

    fclose(f);
    if(names) fclose(names);

    return 0;
}
