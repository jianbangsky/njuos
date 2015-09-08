#ifndef __FM_H__
#define __FM_H__

#include "types.h"
#include "adt/list.h"

#define INODE_NUM 1024
#define BLOCK_NUM 4096
#define BLOCK_SIZE 256
#define KERNEL_INODE_NUM 128
#define KERNEL_FILE_NUM 128

#define OPEN 0
#define READ 1
#define WRITE 2
#define CLOSE 3
#define CREATE 4
#define DELETE 5
#define LSEEK 6
#define DUP 7
#define DUP2 8
#define MKDIR 9
#define RMDIR 10
#define LSDIR 11
#define CHDIR 12
#define EXIT_FILE 13
#define COPY_FD_TABLE 14
#define INIT_FD_TABLE 15

#define PLAIN 0
#define DIR 1

extern pid_t FILE;
struct dir_entry;
extern struct dir_entry* root_dir;

typedef uint32_t block_t;
typedef int size_t;
typedef int inode_t;

typedef struct iNode_entry {
   size_t size;
   int type;
   int dev_id;
   int link;
   block_t index[15];
}iNode_entry;

typedef struct vnode_entry{
   inode_t index;
   iNode_entry inode;
   int count;
   ListHead list;
} vnode_entry;

typedef struct dir_entry {
   char filename[32];
   inode_t index;
} dir_entry;

typedef struct Open_file_entry{
   int flag;
   uint32_t offset;
   int count;
   vnode_entry *vnode;
   ListHead list;
} Open_file_entry;

void read_bitmap_from_disk();

void init_vnode_array();

vnode_entry* get_free_vnode();

void release_kernel_vnode(vnode_entry *vnode);

vnode_entry *search_vnode_by_index(inode_t index);


uint32_t get_free_block();

void set_block_idle(uint32_t block_addr);

inode_t get_free_disk_inode();

void release_disk_inode(inode_t index);

void init_file_table();

Open_file_entry *get_free_file_entry();

void release_file_entry(Open_file_entry *file_entry);

void read_inode_from_disk(inode_t index, iNode_entry *inode);

void write_inode_to_disk(inode_t index, iNode_entry *inode);

#endif