#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include "fs.h"

#define STDERR_FD 2

// TODO: populate with different errors
typedef enum errno{
	noimg,
    badinode,
    badinodeadd,
    norootdir,
    baddirformat,
	parentdirmismatch,
	addmarkedfree,
	blknotused,
	addused,
	inodenotindir,
	inodefree,
	badrefcnt,
	dirused
}e_errno;

int main (int argc, char *argv[]){
    e_errno errorflag;

	int fd = open(argv[1], O_RDONLY);
	if (fd < 0){
		errorflag = noimg;
		goto bad;
	}
	int rc;
	struct stat sbuf;
	int rc = fstat (fd, &sbuf);
	assert (rc == 0);

	void* img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(img_ptr != MAP_FAILED);
    struct superblock *sb;
	sb = (struct superblock *) (img_ptr + BSIZE);
	
	
	int i ;
	struct dinode *dip = (struct dinode*) (img_ptr + 2*BSIZE);
	for (i = 0; i < sb->ninodes; i++){
		if (dip->type < 0 || dip->type > 3){
			errorflag = badinode;
			goto bad;
		}
		
		//direct pointers
		int j;
		for (j = 0; j < NDIRECT; j++){
			if (dip->addrs[j] > (sb->size * BPB){
				errorflag = badinodeadd;
				goto bad;
			}
		}
		
		//indirect pointers
		
		
		dip++; //will increment by size of dip
	}
	
	return 0;

    bad:
	char* error_message;
	
    switch(errorflag){
		case noimg :
			error_message = "image not found"; break;
		case badinode : 
			error_message = "bad inode"; break;
		case badinodeadd : 
			error_message = "bad address in inode"; break;
		case norootdir : 
			error_message = "root directory does not exist"; break;
		case baddirformat : 
			error_message = "directory not properly formatted"; break;
		case parentdirmismatch : 
			error_message = "parent directory mismatch"; break;
		case addmarkedfree : 
			error_message = "address used by inode but marked free in bitmap"; break;
		case blknotused : 
			error_message = "bitmap marks block in use but it is not in use"; break;
		case addused : 
			error_message = "address used more than once"; break;
		case inodenotindir : 
			error_message = "inode marked use but not found in a directory"; break;
		case inodefree : 
			error_message = "inode referred to in directory but marked free"; break;
		case badrefcnt : 
			error_message = "bad reference count for file"; break;
		case dirused : 
			error_message = "directory appears more than once in file system"; break;
	}
	
	write(STDERR_FILENO, error_message, strlen(error_message));
    return 1;
}
