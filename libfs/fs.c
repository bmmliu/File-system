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

/* Data structure for FAT */
struct __attribute__((packed)) FAT {
  uint16_t content;
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
};

struct Superblock superblock;
struct FAT *fat;
struct RootDir rootdir[FS_FILE_MAX_COUNT];
struct FileDescriptor FD[FS_OPEN_MAX_COUNT];
/* For the sake of error management */
int mounted;

int fs_mount(const char *diskname) {
  /* Open the virtual disk */
  if (block_disk_open(diskname) == -1) {
    return -1;
  }

  /* Read the block */
  if (block_read(0, &superblock) == -1) {
    return -1;
  }

  /* Check the signature */
  if (strncmp((char *) superblock.signature, SIGNATURE, SIGNATURELENGTH) != 0) {
    return -1;
  }

  /* Check if total amount of block corresponds to block_disk_count */
  if (superblock.total_blocks != block_disk_count()) {
    return -1;
  }

  fat = malloc((size_t) BLOCK_SIZE * superblock.num_blk_FAT);
  for (size_t i = 0; i < superblock.num_blk_FAT; i++) {
    if (block_read(i + 1, fat + BLOCK_SIZE * i) == -1) {
      return -1;
    }
  }

  if (block_read(superblock.root_dir_blk_index, rootdir) == -1) {
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

  if (block_write(0, &superblock) == -1) {
    return -1;
  }

  for (size_t i = 0; i < superblock.num_blk_FAT; i++) {
    if (block_write(i + 1, fat + BLOCK_SIZE * i) == -1) {
      return -1;
    }
  }

  if (block_write(superblock.root_dir_blk_index, rootdir) == -1) {
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
    if(fat[i].content == 0) {
      free_fat_count++;
    }
  }

  printf("fat_free_ratio=%d/%d\n", free_fat_count, superblock.num_data_blks);

  int free_root_count = 0;
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if((char*)rootdir[i].filename == '\0') {
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
    %FS_FILE_MAX_COUNT files*/
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
    if(strlen((char*)rootdir[i].filename)) {
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

      /* Here we free the allocation in the fat */
      while(get_datablk != FAT_EOC) {
        size_t whatisnext = fat[get_datablk].content;
        fat[whatisnext].content = 0;
        get_datablk = whatisnext;
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
  for(size_t i = 0; i < FS_FILE_MAX_COUNT; i++) {
    if(strcmp((char*)rootdir[i].filename, filename) == 0) {
      ifexist = 1;
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
  for(uint16_t i = 0; i < FS_OPEN_MAX_COUNT; i++)
  {
    if(strlen((char*)FD[i].filename) == 0){
      strcpy((char*)FD[i].filename, filename);
      FD[i].ifopened = 1;
      FD[i].fdnumber = i;
      FD[i].fdoffset = 0;
      return FD[i].fdnumber;
    }
  }
  return 0;

}

int fs_close(int fd) {
  if(FD[fd].fdnumber > FS_OPEN_MAX_COUNT - 1 || FD[fd].fdnumber < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 1) {
    return -1;
  }

  FD[fd].fdnumber = -1;
  FD[fd].ifopened = 0;
  FD[fd].fdoffset = 0;
  return 0;


}

int fs_stat(int fd) {
  if(FD[fd].fdnumber > FS_OPEN_MAX_COUNT - 1 || FD[fd].fdnumber < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 1) {
    return -1;
  }

  for(int i = 0; i < FS_FILE_MAX_COUNT; i++)
  {
    if(strcmp((char*)rootdir[i].filename, (char*)FD[fd].filename) == 0)
    {
      return rootdir[i].size_of_file;
    }
  }

  return 0;
}

int fs_lseek(int fd, size_t offset) {
  if(fd > FS_OPEN_MAX_COUNT - 1 ||fd < 0 ) {
    return -1;
  }

  if(FD[fd].ifopened == 1) {
    return -1;
  }

  if(offset < 0) {
    return -1;
  }

  

}

int fs_write(int fd, void *buf, size_t count) {
  /* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count) {
  /* TODO: Phase 4 */
}

