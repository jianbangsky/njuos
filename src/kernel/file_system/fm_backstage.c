#include "fm.h"
#include "kernel.h"
#include "x86/x86.h"
#include "hal.h"
#include "string.h"

#define inode_bitmap_base  0
#define block_bitmap_base (INODE_NUM)
#define inode_base (block_bitmap_base + BLOCK_NUM)
#define block_base (inode_base + INODE_NUM * sizeof(iNode_entry))

uint8_t inode_bitmap[INODE_NUM];
int inode_bit_index;
uint8_t block_bitmap[BLOCK_NUM];
int block_bit_index;

//uint8_t inode_visited_map[INODE_NUM/8];   
vnode_entry vnode_array[KERNEL_INODE_NUM];
ListHead free_vnode_list;
ListHead active_vnode_list;

Open_file_entry open_file_table[KERNEL_FILE_NUM];
ListHead free_open_file_list;

void read_bitmap_from_disk() {
   dev_read("ram", current->pid, (void*)inode_bitmap, inode_bitmap_base, INODE_NUM);
   dev_read("ram", current->pid, (void*)block_bitmap, block_bitmap_base, BLOCK_NUM);
}

void init_vnode_array(){
   list_init(&free_vnode_list);
   list_init(&active_vnode_list);
   int i;
   for(i = 0; i < KERNEL_INODE_NUM; i++) {
      list_add_after(&free_vnode_list, &(vnode_array[i].list));
   }
}

vnode_entry* get_free_vnode() {
   assert(!list_empty(&free_vnode_list));
   vnode_entry* vnode = list_entry(free_vnode_list.next, vnode_entry, list);
   list_del(free_vnode_list.next);
   list_add_before(&active_vnode_list, &(vnode->list));
   vnode->count = 1;
   return vnode;
}

void release_kernel_vnode(vnode_entry *vnode) {
   vnode->count--;
   if(vnode->count == 0) {
      write_inode_to_disk(vnode->index, &(vnode->inode));
      list_del(&(vnode->list));
      list_add_after(&free_vnode_list, &(vnode->list));
   }
}

vnode_entry *search_vnode_by_index(inode_t index) {
   ListHead *ptr;
   list_foreach(ptr, &active_vnode_list) {
      vnode_entry *vnode = list_entry(ptr, vnode_entry, list);
      if(vnode->index == index) {
         return vnode;
      }
   }
   return NULL;
}

uint32_t get_free_block(){
   while(block_bitmap[block_bit_index]) {
      block_bit_index++;
      block_bit_index %= BLOCK_NUM;
   }
   block_bitmap[block_bit_index] = 1;
   uint32_t block_addr = block_base + block_bit_index * BLOCK_SIZE;
   return block_addr;
}


void set_block_idle(uint32_t block_addr) {
   uint32_t index = (block_addr - block_base) / BLOCK_SIZE;
   block_bitmap[index] = 0;
}

inode_t get_free_disk_inode() {
   while(inode_bitmap[inode_bit_index]) {
      inode_bit_index++;
      inode_bit_index %= INODE_NUM;
   }
   inode_bitmap[inode_bit_index] = 1;
   return inode_bit_index;   
}

void release_disk_inode(inode_t index) {
   inode_bitmap[index] = 0;
}

void init_file_table() {
   list_init(&free_open_file_list);
   int i;
   for(i = 0; i < KERNEL_FILE_NUM; i++) {
      list_add_after(&free_open_file_list, &(open_file_table[i].list));
   }
}

Open_file_entry *get_free_file_entry() {
   assert(!list_empty(&free_open_file_list));
   Open_file_entry *file_entry = list_entry(free_open_file_list.next, Open_file_entry, list);
   file_entry->count = 1;
   list_del(free_open_file_list.next);
   return file_entry;
}

void release_file_entry(Open_file_entry *file_entry) {
   file_entry->count--;
   if(file_entry->count == 0) {
      release_kernel_vnode(file_entry->vnode);
      list_add_after(&free_open_file_list, &(file_entry->list));
   }
}


void read_inode_from_disk(inode_t index, iNode_entry *inode) {
   uint32_t inode_addr = inode_base + index * sizeof(iNode_entry);
   dev_read("ram", current->pid, (void*)inode, inode_addr, sizeof(iNode_entry));
}

void write_inode_to_disk(inode_t index, iNode_entry *inode) {
   uint32_t inode_addr = inode_base + index * sizeof(iNode_entry);
   dev_write("ram", current->pid, (void*)inode, inode_addr, sizeof(iNode_entry));
}


