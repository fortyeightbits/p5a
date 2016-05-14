#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "types.h"

#define STDERR_FD 2
#define INTS_PER_BLOCK 128
#define NUMBER_OF_LOGENTRIES 1000

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

typedef struct fileusage
{
    uint inum;
    uint hits;
    uint file_type;
}fileusage_t;

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


// Takes in a pointer to single directory entry and pointer to array of fileusage_t structures.
int logUsage(struct dirent* directoryDataBlock, fileusage_t* usageLog, void* imagepointer)
{
    int logentry;
    struct dinode* inodeBase = (struct dinode*) (imagepointer + (2*BSIZE));
    if(((inodeBase[directoryDataBlock->inum].type) != 3) && strcmp(directoryDataBlock->name, ".."))
    {
        for(logentry = 0; logentry < NUMBER_OF_LOGENTRIES; logentry++)
        {
            //printf("directorydata->inum: %d\n", directoryDataBlock->inum);
            if ((directoryDataBlock->inum) == usageLog[logentry].inum)
            {
                usageLog[logentry].hits++;
                return 1;
            }
            //(usageLog[logentry].inum)!= 0
        }

        // If code gets here it means this inum was not added to the log entry yet.
        // Parse through fileusage log to find empty slot, then append to the array
        for(logentry = 0; logentry < NUMBER_OF_LOGENTRIES; logentry++)
        {
            if((usageLog[logentry].inum) == 0) //Not in use!
            {
                usageLog[logentry].inum = directoryDataBlock->inum;
                //usageLog[logentry].file_type =
                usageLog[logentry].hits++;
                break;
            }

        }
        return 1;
    }
    return 0;
}

// This function takes in a usage log and checks two tests. The first - if there are file references with a different number of hits compared to
// the actual nlinks listed in inode. Second - if there are any directory type inodes with nlink > 1. Returns -1 for the former and -2 for the latter. 0 for good.
int checkFileUsage(fileusage_t* usageLog, void* imagePointer)
{
    struct dinode* inodeBase = (struct dinode*) (imagePointer+(2*BSIZE));
    // parse through entire log
    int logparser;
    for (logparser = 0; logparser < NUMBER_OF_LOGENTRIES; logparser++)
    {
        if (usageLog[logparser].inum == 0)
        {
            // Break if you enter unpopulated part of log.
            break;
        }
        // Quick test to check if there are directories with more than 1 hit.
        if ((usageLog[logparser].hits > 2) && (usageLog[logparser].file_type = 1))
        {
            return -2;
        }

        // Test to check if there are files with number of hits in directories that do not equal refences in inode.
        if((usageLog[logparser].hits != (inodeBase[usageLog[logparser].inum].nlink)) && (usageLog[logparser].file_type == 2))
        {
            return -1;
        }
//        //Debugcode, remove later
//        //if (usageLog[logparser].hits > 1)
        if (logparser < 30)
        {
            //printf("usageLog[%d].inum: %d ; hits: %d\n", logparser, usageLog[logparser].inum, usageLog[logparser].hits);
        }
    }
    return 0;
}

int main (int argc, char *argv[]){
    e_errno errorflag;
    char* error_message;
    fileusage_t* totalFsAndDs = (fileusage_t*)calloc(NUMBER_OF_LOGENTRIES, sizeof(fileusage_t));//[NUMBER_OF_LOGENTRIES];
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
	struct dinode * iblockstart = dip;
	struct dinode * iblocktest = dip;
	
	//int array [100];
	//int index = 0;
	//special snowflake loop to get inodes in use
	struct dinode *inodeinuse = dip;
	struct dinode *ptr = dip;
	int inodenum;
	int found = 0;
	 for (inodenum = 0; inodenum < sb->ninodes; inodenum++)
    {
		if (inodeinuse->type != 0){
			//array[index] = inodenum; //an inode in use
			//printf("Inum: %d, type: %d\n", array[index], inodeinuse->type);
			///////////LOOK THROUGH DIR FOR inodenum//////////////
			int x;
			int y;
			struct dirent* direntptr;
            ptr = dip;
			for (x = 0; x < sb->ninodes; x++){
                //printf("inodenum: %d ----- x: %d ---- ptr->type: %d\n", inodenum, x, ptr->type);
				if (ptr->type == 1){ //directory
					y = 0;
					while (ptr->addrs[y] > 0 && ptr->addrs[y] < 1024){
                        //printf("ptraddr: %d\n", ptr->addrs[y]);
						getblock(ptr->addrs[y], (void*)dircheckbuf.charbuf, img_ptr);
						direntptr = (struct dirent*)dircheckbuf.charbuf;
						int z = 0;
						while (z < (512/sizeof(struct dirent)))
						{
							if (direntptr->inum == inodenum){
								found = 1;
							}
							direntptr++;
							z++;
						}
					y++;
					}
				}
				ptr++;
			}
			if (!found){
				errorflag = inodenotindir;
				goto bad;
			}
			///////////////////////////////////////////////
		} 
		inodeinuse++;
	}
	//int usedinodecnt = index;
	
	//main inode loop
    for (i = 0; i < sb->ninodes; i++)
    {
        if (dip->type < 0 || dip->type > 3)
        {
			errorflag = badinode;
			goto bad;
		}
		
		if (i == ROOTINO)
		{
			if (dip->type != 1){
				errorflag = norootdir;
				goto bad;
			}
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
			
            // test for dirents referring to inodes which are free.
			int dirBlocks = 0;
			//int foundInodeinDir = 0;
			struct dirent* parseThroughCurrentDir;
			while (dip->addrs[dirBlocks] != 0){
				getblock(dip->addrs[dirBlocks], (void*)dircheckbuf.charbuf, img_ptr);
				parseThroughCurrentDir = (struct dirent*)dircheckbuf.charbuf;
				int direntptr = 0;
                while (direntptr < (512/sizeof(struct dirent)))
				{
					iblocktest = iblockstart;
					if (parseThroughCurrentDir->inum != 0){

						iblocktest = iblocktest + (parseThroughCurrentDir->inum);
						if (iblocktest->type == 0)
						{
							errorflag = inodefree;
							goto bad;
						}
                    }
					
					/*
					int meh;
					printf("cnt: %d\n", usedinodecnt);
					//printf("i: %d\n", i);
					
					//printf("parsethroughinum: %d\n", parseThroughCurrentDir->inum);
					for (meh = 0; meh < usedinodecnt; meh++){
						if (parseThroughCurrentDir->inum == array[meh] && array[meh] != 0)
						{
							printf("found direct! %d\n", array[meh]);
							usedinodecnt--;
							array[meh] = 0;
						}
					}*/

                    // Store entry types (file or directory, dev not stored) and number of usages
                    logUsage(parseThroughCurrentDir, totalFsAndDs, img_ptr);
					direntptr++;
					parseThroughCurrentDir++;
				}
				
				dirBlocks++;
			}
            // indirect pointers section of the previous test.
			dirBlocks++; //get to indirect pointers block
                if ((dirBlocks == NDIRECT) && (dip->addrs[dirBlocks]) != 0)
                {
                    getblock(dip->addrs[dirBlocks], (void*)dircheckbuf.charbuf, img_ptr); //block containing pointer to 128 pointers
					int indirectptr = 0;
					while (dircheckbuf.intbuf[indirectptr] != 0){
						
						parseThroughCurrentDir = (struct dirent*)(&dircheckbuf.charbuf[indirectptr]);
						int indirectDirentPtr = 0;
                        while(indirectDirentPtr < (512/sizeof(struct dirent))){
							

							iblocktest = iblockstart;
							if (parseThroughCurrentDir->inum != 0){
                                //printf("parseThroughCurrentDir->inum: %d\n", parseThroughCurrentDir->inum);
								iblocktest = iblocktest + (parseThroughCurrentDir->inum);
								if (iblocktest->type == 0)
								{
									errorflag = inodefree;
									goto bad;
								}
                            }
							
							/*
							int meh2;
							printf("cnt: %d\n", usedinodecnt);
							for (meh2 = 0; meh2 < usedinodecnt; meh2++){
								//printf("what: %d\n", array[meh]);
								if (parseThroughCurrentDir->inum == array[meh2] && array[meh2] != 0)
								{
									printf("found indirect! %d\n", array[meh2]);
									usedinodecnt--;
									array[meh2] = 0;						
								}		
							}*/

                            // Store entry types (file or directory, dev not stored) and number of usages
                            logUsage(parseThroughCurrentDir, totalFsAndDs, img_ptr);

							indirectDirentPtr++;
							parseThroughCurrentDir++;
						}
						indirectptr++;
						
					}
                }
			
			
			
			//check parent dir mismatch
			int parentinode = (directoryptr+1)->inum;
			
			if (i != ROOTINO){
				if (parentinode == directoryptr->inum){
					 errorflag = parentdirmismatch;
					goto bad;
				}
			}
			
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
           //printf("addrs: %d\n", xint(dip->addrs[j]));


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
	
	/*
	if (usedinodecnt != 0)
	{
		errorflag = inodenotindir;
		goto bad;
	}*/
	
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
	//printf("totalbits: %d, blockcount: %d\n", totalbits, blockcount);
	if (totalbits != blockcount){
		errorflag = blknotused;
		goto bad;
	}
	
    // Check through file usage logs and compare hits with usage
    int fileusagereturn = checkFileUsage(totalFsAndDs, img_ptr);

    if (fileusagereturn == -1)
    {
        errorflag = badrefcnt;
        goto bad;
    }
    else if (fileusagereturn == -2)
    {
        errorflag = dirused;
        goto bad;
    }


	return 0;

    bad:
    switch(errorflag){
		case noimg :
			error_message = "image not found.\n"; break;
		case badinode : 
			error_message = "ERROR: bad inode.\n"; break;
		case badinodeadd : 
			error_message = "ERROR: bad address in inode.\n"; break;
		case norootdir : 
			error_message = "ERROR: root directory does not exist.\n"; break;
		case baddirformat : 
			error_message = "ERROR: directory not properly formatted.\n"; break;
		case parentdirmismatch : 
			error_message = "ERROR: parent directory mismatch.\n"; break;
		case addmarkedfree : 
			error_message = "ERROR: address used by inode but marked free in bitmap.\n"; break;
		case blknotused : 
			error_message = "ERROR: bitmap marks block in use but it is not in use.\n"; break;
		case addused : 
			error_message = "ERROR: address used more than once.\n"; break;
		case inodenotindir : 
			error_message = "ERROR: inode marked use but not found in a directory.\n"; break;
		case inodefree : 
			error_message = "ERROR: inode referred to in directory but marked free.\n"; break;
		case badrefcnt : 
			error_message = "ERROR: bad reference count for file.\n"; break;
		case dirused : 
			error_message = "ERROR: directory appears more than once in file system.\n"; break;
	}
	
	write(STDERR_FILENO, error_message, strlen(error_message));
    return 1;
}
