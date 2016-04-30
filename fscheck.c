#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include "fs.h"

// TODO: populate with different errors
typedef enum errno{
    error1,
    error2,
    error3,
    error4
}e_errno;

int main (int argc, char *argv[]){
    e_errno errorflag;

	int fd = open(argv[1], O_RDONLY);
	assert (fd > -1);
	int rc;
	struct stat sbuf;
	int rc = fstat (fd, &sbuf);
	assert (rc == 0);

	void* img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(img_ptr != MAP_FAILED);
    struct superblock *sb;
	sb = (struct superblock *) (img_ptr + BSIZE);
	
	
	/*for inodes
	int i ;
	struct dinode *dip = (struct dinode*) (img_ptr + 2*BSIZE);
	for (i = 0; i < sb->ninodes; i++){
		printf("%s\n", dip->type);
		dip++; //will increment by size of dip
	}
	*/
	
	return 0;

    bad:
    // TODO: Handle errors.
    printf("found an error! Error number: %d\n", errorflag);

    return 1;
}
