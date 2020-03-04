#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define UNUSED_SUPERBLOCK 4079
#define UNUSED_ROOTDIR 10
#define SIGNATURE "ECS150FS"
#define SIGNATURELENGTH 8
#define FAT_EOC 0xffff

/* Data structure for superblock */
struct __attribute__((packed)) Superblock {
  uint8_t signature[SIGNATURELENGTH];
  uint16_t total_blocks;
  uint16_t root_dir_blk_index;
  uint16_t data_blk_start_index;
  uint16_t num_data_blks;
  uint8_t num_blk_FAT;
  uint8_t unused[UNUSED_SUPERBLOCK];
};



/* Data structure for rootdir */
struct __attribute__((packed)) RootDir {
  uint8_t filename[FS_FILENAME_LEN];
  uint32_t size_of_file;
  uint16_t index_first_datablk;
  uint8_t unused[UNUSED_ROOTDIR];
};

/* Data structure for the file descriptor */
struct  __attribute__((packed)) FileDescriptor {
  int ifopened;
  uint8_t filename[FS_FILENAME_LEN];
  uint16_t fdnumber;
  uint16_t fdoffset;
  uint16_t indexinroot;

};

struct Superblock superblock;
uint16_t *fat;
struct RootDir rootdir[FS_FILE_MAX_COUNT];
struct FileDescriptor FD[FS_OPEN_MAX_COUNT];
/* For the sake of error management */
int mounted = 0;

int fs_mount(const char *diskname) {
  /* Open the virtual disk */
  if (block_disk_open(diskname) == -1) {
    return -1;
  }

  /* Read the block */
  if (block_read(0, (void*)&superblock) == -1) {
    return -1;
  }

  /* Check the signature */
  if (strncmp((char *)superblock.signature, SIGNATURE, SIGNATURELENGTH) != 0) { //jjjjjjjj
    return -1;
  }

  /* Check if total amount of block corresponds to block_disk_count */
  if (superblock.total_blocks != block_disk_count()) {
    return -1;
  }

  fat = (uint16_t *)malloc( BLOCK_SIZE * superblock.num_blk_FAT * sizeof(uint16_t));
  for (size_t i = 0; i < superblock.num_blk_FAT; i++) {
    if (block_read(i + 1, &fat[BLOCK_SIZE/2 * i]) == -1) {
      return -1;
    }
  }

  if (block_read(superblock.root_dir_blk_index, &rootdir) == -1) {
    return -1;
  }

  mounted = 1;
  return 0;
}

int fs_umount(void) {
  /* If no virtual disk is being opened */
  if (!mounted) {
    return -1;
  }

  if (block_write(0, (void*)&superblock) == -1) {
    return -1;
  }

  for (size_t i = 0; i < superblock.num_blk_FAT; i++) {
    if (block_write(i + 1, &fat[BLOCK_SIZE/2 * i]) == -1) {
      return -1;
    }
  }

  if (block_write(superblock.root_dir_blk_index, &rootdir) == -1) {
    return -1;
  }

  if(block_disk_close() == -1) {
    return -1;
  }

  free(fat);
  mounted = 0;

  return 0;

}

int fs_info(void) {
  if(!mounted) {
    return -1;
  }

  printf("FS Info:\n");
  printf("total_blk_count=%d\n", superblock.total_blocks);
  printf("fat_blk_count=%d\n", superblock.num_blk_FAT);
  printf("rdir_blk=%d\n", superblock.root_dir_blk_index);
  printf("data_blk=%d\n", superblock.data_blk_start_index);
  printf("data_blk_count=%d\n", superblock.num_data_blks);

  int free_fat_count = 0;
  for(size_t i = 0; i < superblock.num_data_blks; i++) {
    if(fat[i] == 0) {
      free_fat_count++;
    }
  }

  printf("fat_free_ratio=%d/%d\n", free_fat_count, superblock.num_data_blks);

  int free_root_count = 0;
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(*rootdir[i].filename == 0) {
      free_root_count++;
    }
  }

  printf("rdir_free_ratio=%d/%d\n", free_root_count, FS_FILE_MAX_COUNT);

  return 0;



}

int fs_create(const char *filename) {
  /* If filename is null */
  if(!filename) {
    return -1;
  }

  /* If filename is too long */
  if(strlen(filename) > FS_FILENAME_LEN) {
    return -1;
  }

  /* If filename does not terminate with null */
  if(filename[strlen(filename)] != '\0') {
    return -1;
  }

  /* Check if filename already exist */
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strcmp((char*)rootdir[i].filename, filename) == 0) {
      return -1;
    }
  }

  /* Check if if the root directory already contains
    FS_FILE_MAX_COUNT files*/
  int count = 0;
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strlen((char*)rootdir[i].filename)) {
      count++;
    }
  }
  if(count == FS_FILE_MAX_COUNT){
    return -1;
  }

  size_t first_entry = 0;
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(!strlen((char*)rootdir[i].filename)) {
      first_entry = i;
      strcpy((char*)rootdir[first_entry].filename, filename);
      rootdir[first_entry].size_of_file = 0;
      rootdir[first_entry].index_first_datablk = FAT_EOC;
      return 0;
    }
  }

  return 0;

}

int fs_delete(const char *filename) {
  /* If filename is null */
  if(!filename) {
    return -1;
  }

  /* If filename is too long */
  if(strlen(filename) > FS_FILENAME_LEN) {
    return -1;
  }

  /* If filename does not terminate with null */
  if(filename[strlen(filename)] != '\0') {
    return -1;
  }

  /* Check if the file exist */
  int ifexist = 0;
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strcmp((char*)rootdir[i].filename, filename) == 0) {
      ifexist = 1;
    }
  }
  if(!ifexist) {
    return -1;
  }

  /* To check if the file is currently opened */
  for(size_t i = 0; i <FS_OPEN_MAX_COUNT; i++) {
    if(strcmp((char*)FD[i].filename, filename) == 0) {
      if(FD[i].ifopened) {
        return -1;
      } else{
        break;
      }
    }
  }

  /* Now we need to clear the content in the root and deal with the fat */
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strcmp((char*)rootdir[i].filename, filename) == 0) {
      memset(rootdir[i].filename, '\0', strlen(filename));
      rootdir[i].size_of_file = 0;
      size_t get_datablk = rootdir[i].index_first_datablk;
      rootdir[i].index_first_datablk = 0;

      if (get_datablk == FAT_EOC) {
        return 0;
      }
      /* Here we free the allocation in the fat */
      while(fat[get_datablk] != FAT_EOC) {
        size_t new_getblk = fat[get_datablk];
        fat[get_datablk] = 0;
        get_datablk = new_getblk;
      }
    }
  }

  return 0;

}

int fs_ls(void) {
  if(!mounted) {
    return -1;
  }
  printf("FS Ls:\n");
  for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if (strlen((char*)rootdir[i].filename)) {
      printf("file: %s, size: %d, ", rootdir[i].filename, rootdir[i].size_of_file);
      printf("data_blk: %d\n", rootdir[i].index_first_datablk);
    }
  }
  return 0;
}

int fs_open(const char *filename) {
  /* If filename is null */
  if(!filename) {
    return -1;
  }

  /* If filename is too long */
  if(strlen(filename) > FS_FILENAME_LEN) {
    return -1;
  }

  /* If filename does not terminate with null */
  if(filename[strlen(filename)] != '\0') {
    return -1;
  }

  /* Check if the file exist */
  int ifexist = 0;
  uint16_t correspondroot = 0;

  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strcmp((char*)rootdir[i].filename, filename) == 0) {
      ifexist = 1;
      correspondroot = (uint16_t )i;
      break;
    }
  }
  if(!ifexist) {
    return -1;
  }

  /* To check if already maximum amount */
  int openfilecount = 0;
  for(int i = 0; i < FS_OPEN_MAX_COUNT; i++)
  {
    if(strlen((char*)FD[i].filename) != 0){
      openfilecount++;
    }
  }
  if(openfilecount == FS_OPEN_MAX_COUNT) {
    return -1;
  }

  /* Now we try to initialize the file descrpitor */
  uint16_t i;
  for(i = 0; i < FS_OPEN_MAX_COUNT; i++)
  {
    if(FD[i].ifopened == 0){
      strcpy((char*)FD[i].filename, filename);
      FD[i].ifopened = 1;
      FD[i].fdnumber = i;
      FD[i].fdoffset = 0;
      FD[i].indexinroot = correspondroot;
      break;
    }
  }
  return i;

}

int fs_close(int fd) {
  if(fd > FS_OPEN_MAX_COUNT - 1 || fd < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 0) {
    return -1;
  }

  FD[fd].fdnumber = (uint16_t )-1;
  FD[fd].ifopened = 0;
  FD[fd].fdoffset = 0;
  FD[fd].indexinroot = (uint16_t )-1;
  strcpy((char*)FD[fd].filename, "");
  return 0;


}

int fs_stat(int fd) {
  if(fd > FS_OPEN_MAX_COUNT - 1 || fd < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 0) {
    return -1;
  }

  for(int i = 0; i < FS_OPEN_MAX_COUNT; i++)
  {
    if(FD[i].fdnumber == fd)
    {
      return rootdir[FD[i].indexinroot].size_of_file;
    }
  }

  return 0;
}

int fs_lseek(int fd, size_t offset) {
  if(fd > FS_OPEN_MAX_COUNT - 1 ||fd < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 0) {
    return -1;
  }

  if(offset > rootdir[FD[fd].indexinroot].size_of_file) {
    return -1;
  }

  FD[fd].fdoffset = (uint16_t)offset;

  return 0;
}


uint16_t fs_findfirstblock()
{
  /* data block will start at block 1 to have same index with FAT */
  uint16_t block = superblock.data_blk_start_index + 1;
  while(block < superblock.total_blocks && fat[block]!= 0)
    block++;

  if (block == superblock.total_blocks)
    return FAT_EOC;

  /* update in fat array */
  fat[block] = block;
  return block - superblock.data_blk_start_index;
}

uint16_t fs_get_block_from_offset(uint16_t firstblock, uint16_t offset)
{
  uint16_t block = firstblock + superblock.data_blk_start_index;
  while(offset >= BLOCK_SIZE)
  {
    uint16_t newblock = fat[block];
    if (newblock == 0 || newblock == FAT_EOC)
    {
      newblock = fs_findfirstblock();
      if (newblock == FAT_EOC)
        return FAT_EOC;
      newblock += superblock.data_blk_start_index;
      fat[block] = newblock;
    }
    block = newblock;
    offset -= BLOCK_SIZE;
  }
  return block;
}



int fs_write(int fd, void *buf, size_t count)
{
  /* check if fs is valid, fd is opened*/
  if (!mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT || FD[fd].ifopened == 0)
    return -1;

  /* get the starting index of block in root entries */
  uint16_t entry = FD[fd].indexinroot;
  uint16_t firstblock = rootdir[entry].index_first_datablk;


  /* when file is an empty file and needs to extend the size */
  if (firstblock == FAT_EOC && count > 0)
  {
    firstblock = rootdir[entry].index_first_datablk = fs_findfirstblock();
    if (firstblock == FAT_EOC)
      return 0;
  }

  int byteswritten = 0;
  uint16_t offset = FD[fd].fdoffset;

  /* 1. check the position of offset in block. and set the bytesleft
   * 2. convert offset position into the corresponding data block
   * 3. copy the content of block out to tempbuf
   * 4. copy the rest of the block content from @buf
   * 5. overwritten the whole block with tempbuf */
  uint8_t tmpbuffer[BLOCK_SIZE];
  while(count > 0)
  {
    uint16_t bytesleft = BLOCK_SIZE - (offset % BLOCK_SIZE);
    if (bytesleft > count)
      bytesleft = count;

    uint16_t block = fs_get_block_from_offset(firstblock, offset);
    if (block == FAT_EOC || block_read(block, tmpbuffer) < 0)
      break;

    memcpy(tmpbuffer + (offset % BLOCK_SIZE), buf + byteswritten, bytesleft);
    if (block_write(block, tmpbuffer) < 0)
      break;

    count -= bytesleft;
    byteswritten += bytesleft;
    offset += bytesleft;
  }

  if (rootdir[entry].size_of_file < offset)
    rootdir[entry].size_of_file = offset;

  FD[fd].fdoffset = offset;
  return byteswritten;

}

int fs_read(int fd, void *buf, size_t count)
{
  /* check if fs is valid, fd is opened*/
  if (!mounted || fd < 0 || fd >= FS_OPEN_MAX_COUNT || FD[fd].ifopened == 0)
    return -1;

  uint16_t entry = FD[fd].indexinroot;
  uint16_t firstblock = rootdir[entry].index_first_datablk;

  int bytesread = 0;
  uint16_t size = rootdir[entry].size_of_file;
  uint16_t offset = FD[fd].fdoffset;

  if (offset + count > size)
    count = size - offset;

  /* read fd block by block */
  uint8_t tmpbuffer[BLOCK_SIZE];
  while(count > 0)
  {
    uint16_t bytesleft = BLOCK_SIZE - (offset % BLOCK_SIZE);
    if (bytesleft > count)
      bytesleft = count;
    /* get the offset current block */
    uint16_t block = fs_get_block_from_offset(firstblock, offset);
    if (block == FAT_EOC || block_read(block, tmpbuffer) < 0)
      break;
    memcpy(buf + bytesread, tmpbuffer + (offset % BLOCK_SIZE), bytesleft);

    /* copy offset current block into the buffer,
     * and set offset points to next block */
    count -= bytesleft;
    bytesread += bytesleft;
    offset += bytesleft;
  }

  /* implicitly incremented offset */
  FD[fd].fdoffset = offset;
  return bytesread;
}