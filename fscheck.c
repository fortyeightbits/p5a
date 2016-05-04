#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include "fs.h"
#include "types.h"

#define STDERR_FD 2
#define INTS_PER_BLOCK 128

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

typedef union buffer{
    char charbuf[512];
    int intbuf[128];
}buff_u;

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

void
getblock(uint sec, void *buf, void* imagepointer)
{
    uint bitoffset = 512*sec; //TODO
    imagepointer += bitoffset;
    memcpy(buf, imagepointer, 512);
}

int main (int argc, char *argv[]){
    e_errno errorflag;
    char* error_message;
    buff_u blockbuf;
    buff_u bitmapbuf;
	buff_u dircheckbuf;
    struct dirent* directoryptr;
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0){
		errorflag = noimg;
		goto bad;
	}
	struct stat sbuf;
	int rc = fstat (fd, &sbuf);
	assert (rc == 0);

	void* img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(img_ptr != MAP_FAILED);
    struct superblock *sb;
	sb = (struct superblock *) (img_ptr + BSIZE);
	int blockcount = 29;
	

    uint inodeHistory = 0;
    // Parse through the inodes
	int i ;
	struct dinode *dip = (struct dinode*) (img_ptr + 2*BSIZE);
	struct dinode *iblockstart = dip;
    for (i = 0; i < sb->ninodes; i++)
    {
        if (dip->type < 0 || dip->type > 3)
        {
			errorflag = badinode;
			goto bad;
		}
					
		//it's a directory entry!
		if (dip->type == 1){
			getblock(dip->addrs[0], (void*)blockbuf.charbuf, img_ptr);
			directoryptr = (struct dirent*)blockbuf.charbuf;
			if (!(strcmp(directoryptr->name, ".") == 0  && (strcmp((directoryptr+1)->name, "..") == 0))){
				errorflag = baddirformat;
				goto bad;
			}
			
			if(i == ROOTINO){
				if (!(directoryptr->inum == ROOTINO && (directoryptr+1)->inum == ROOTINO)){
						errorflag = norootdir;
						goto bad;
				}			
			}

			int dirBlocks = 0;
			int foundInodeinDir = 0;
			struct dirent* parseThroughCurrentDir;
			while (dip->addrs[dirBlocks] != 0){
				getblock(dip->addrs[dirBlocks], (void*)dircheckbuf.charbuf, img_ptr);
				parseThroughCurrentDir = (struct dirent*)dircheckbuf.charbuf;
				int direntptr = 0;
				while (direntptr <= (512/sizeof(struct dirent)))
				{
					if (parseThroughCurrentDir->inum == i)
					{
						foundInodeinDir = 1;
						break;
					}
					direntptr++;
					parseThroughCurrentDir++;
				}
				if (foundInodeinDir)
					break;
				
				dirBlocks++;
			}
			
			dirBlocks++; //get to indirect pointers block
                if ((dirBlocks == NDIRECT) && (dip->addrs[dirBlocks]) != 0)
                {
                    getblock(dip->addrs[dirBlocks], (void*)dircheckbuf.charbuf, img_ptr); //block containing pointer to 128 pointers
					int indirectptr = 0;
					while (dircheckbuf.intbuf[indirectptr] != 0){
						
						parseThroughCurrentDir = (struct dirent*)(&dircheckbuf.charbuf[indirectptr]);
						int indirectDirentPtr = 0;
						while(indirectDirentPtr <= (512/sizeof(struct dirent))){
							if(parseThroughCurrentDir->inum == i){
								foundInodeinDir = 1;
								break;
							}
							indirectDirentPtr++;
							parseThroughCurrentDir++;
						}
						indirectptr++;
						if (foundInodeinDir)
							break;
					}
                }
			
			if (!foundInodeinDir)
			{
				errorflag = inodenotindir;
				goto bad;
			}
			
			//check parent dir mismatch
			int parentinode = (directoryptr+1)->inum;
            struct dinode *parseptr = iblockstart;
            parseptr += parentinode;
            int k = 0;
			int found = 0;
            while (parseptr->addrs[k] != 0){
                getblock(parseptr->addrs[k], (void*)blockbuf.charbuf, img_ptr);
                directoryptr = (struct dirent*)blockbuf.charbuf;
                while (directoryptr->inum != 0){
                    //printf("name: %s, directoryptr->inum: %d, i: %d\n", directoryptr->name, directoryptr->inum, i);
                    if (directoryptr->inum == i){
                        found = 1;
                        break;
                    }
                    directoryptr++;
                }
                k++;
                // If we can't find a match in the direct blocks and there is an indirect pointer, search in those blocks
                if ((k == NDIRECT) && (iblockstart->addrs[k]) != 0)
                {
                    getblock(iblockstart->addrs[k], (void*)blockbuf.charbuf, img_ptr);
                    directoryptr = (struct dirent*)blockbuf.charbuf;
                    while(directoryptr->inum!=0){
                        if(directoryptr->inum == i){
                            found = 1;
                            break;
                        }
                    }
                }
				
            }
            if (!found){
                errorflag = parentdirmismatch;
                goto bad;
            }
			
		}
		
		
		
        // Parse through direct pointers
		int j;
        for (j = 0; j < NDIRECT; j++)
        {
           printf("addrs: %d\n", xint(dip->addrs[j]));


            //For the first direct address of first direct pointer, initialize the inodeHistory
            if((i == 0)&&(j == 0))
            {
                inodeHistory = dip->addrs[j];
            }

            // Check for bad i node data block address
            if ((xint(dip->addrs[j]) >= xint(sb->size))||((xint(dip->addrs[j]) <= BBLOCK(sb->size,sb->ninodes)) && (xint(dip->addrs[j]) != 0))){
				errorflag = badinodeadd;
				goto bad;
			}

            // Check if these pointers are marked as used in the bitmap block.
            if (dip->addrs[j] != 0)
            {
                // Check if inode address has been used before
                if((dip->addrs[j] <= inodeHistory))
                {
                    // throw error
                    errorflag = addused;
                    goto bad;
                }
                else
                {
                    inodeHistory = dip->addrs[j];
                }
				++blockcount;
                char bitmapcheckbuf;
                //printf("BBLOCK: %d\n", BBLOCK(dip->addrs[j], sb->ninodes));
                getblock(BBLOCK(dip->addrs[j], sb->ninodes), (void*)bitmapbuf.charbuf, img_ptr);
                bitmapcheckbuf = bitmapbuf.charbuf[(dip->addrs[j])/8];
                bitmapcheckbuf >>= ((dip->addrs[j])%8);

                if(!(bitmapcheckbuf & 0b00000001))
                {
                    errorflag = addmarkedfree;
                    goto bad;
                }
            }

		}
		if (dip->addrs[NDIRECT] != 0)
		{
			++blockcount;
		}

       // printf("type: %d\n", xint(dip->type));

		//indirect pointers
        getblock(dip->addrs[NDIRECT], (void*)blockbuf.charbuf, img_ptr);
        int indirect;
        for(indirect = 0; indirect < INTS_PER_BLOCK; indirect++)
        {
            if ((xint(blockbuf.intbuf[indirect]) >= xint(sb->size))||((xint(blockbuf.intbuf[indirect]) <= BBLOCK(sb->size,sb->ninodes)) && (xint(blockbuf.intbuf[indirect]) != 0)))
            {
                errorflag = badinodeadd;
                goto bad;
            }
            //printf("blockbuf content [%d]: %d\n", indirect, blockbuf.intbuf[indirect]);
			
            if (blockbuf.intbuf[indirect] != 0)
            {
                // Check if inode address has been used before
                if((blockbuf.intbuf[indirect] <= inodeHistory))
                {
                    // throw error
                    errorflag = addused;
                    goto bad;
                }
                else
                {
                    inodeHistory = blockbuf.intbuf[indirect];
                }

				++blockcount;
                char bitmapcheckbuf;
                getblock(BBLOCK(blockbuf.intbuf[indirect], sb->ninodes), (void*)bitmapbuf.charbuf, img_ptr);
                bitmapcheckbuf = bitmapbuf.charbuf[blockbuf.intbuf[indirect]/8];
                bitmapcheckbuf >>= (blockbuf.intbuf[indirect]%8);

                if(!(bitmapcheckbuf & 0b00000001))
                {
                    errorflag = addmarkedfree;
                    goto bad;
                }
            }
			
        }
		dip++; //will increment by size of dip
	}
	
	//check bitmap
	//struct dinode *inodeptr = (struct dinode*) (img_ptr + 2*BSIZE);
	//++inodeptr;
	//struct dinode *inodeparser = inodeptr;
	uint totalbits = 0;
	getblock(BBLOCK(dip->addrs[0], sb->ninodes), (void*)bitmapbuf.charbuf, img_ptr);
	int m;
	int eighttimes;
	int bitcnt = 0;
	for (m = 0; m < BSIZE; m++)
	{
		//if (bitmapbuf.charbuf[m] != 0)
		//{
			char characterbuffer = bitmapbuf.charbuf[m];
			for (eighttimes = 0; eighttimes < 8; eighttimes++)
			{
				if (characterbuffer & 0b00000001)
				{
					++totalbits;
				}
				characterbuffer >>= 1;
					/*
					
					inodeparser = inodeptr;
					inodeparser += bitcnt;
					if (inodeparser->type == 0)
					{
						errorflag = blknotused;
						goto bad;
					}
				}
			++bitcnt;*/
			}
		//}
	}
	printf("totalbits: %d, blockcount: %d\n", totalbits, blockcount);
	if (totalbits != blockcount){
		errorflag = blknotused;
		goto bad;
	}
	
	return 0;

    bad:
    switch(errorflag){
		case noimg :
			error_message = "image not found\n"; break;
		case badinode : 
			error_message = "bad inode\n"; break;
		case badinodeadd : 
			error_message = "bad address in inode\n"; break;
		case norootdir : 
			error_message = "root directory does not exist\n"; break;
		case baddirformat : 
			error_message = "directory not properly formatted\n"; break;
		case parentdirmismatch : 
			error_message = "parent directory mismatch\n"; break;
		case addmarkedfree : 
			error_message = "address used by inode but marked free in bitmap\n"; break;
		case blknotused : 
			error_message = "bitmap marks block in use but it is not in use\n"; break;
		case addused : 
			error_message = "address used more than once\n"; break;
		case inodenotindir : 
			error_message = "inode marked use but not found in a directory\n"; break;
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
