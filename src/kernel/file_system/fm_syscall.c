#include "fm.h"
#include "kernel.h"
#include "x86/x86.h"
#include "hal.h"
#include "string.h"

pid_t FILE;

#define level_end0 (BLOCK_SIZE * 12)
#define level_end1 (level_end0 + (BLOCK_SIZE / 4) * BLOCK_SIZE)
#define level_end2 (level_end1 + (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4) * BLOCK_SIZE)
#define level_end3 (level_end2 + (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4) * BLOCK_SIZE)

#define index_zero_size BLOCK_SIZE
#define index_one_size (BLOCK_SIZE * BLOCK_SIZE / 4)
#define index_two_size (BLOCK_SIZE * (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4))

uint8_t index_block1[BLOCK_SIZE];
uint8_t index_block2[BLOCK_SIZE];
uint8_t index_block3[BLOCK_SIZE];

uint8_t block_buf_array[20][BLOCK_SIZE];
bool block_buf_flag[20];

dir_entry dir;
dir_entry* root_dir = &dir;

vnode_entry *stdin_vnode;
vnode_entry *stdout_vnode;
vnode_entry *stderr_vnode;

static const char *ttynames[4] = {"tty1", "tty2", "tty3", "tty4"};


void init_file(void);
static void file_thread(void);


void fm_read(Msg *m);
int read_data(pid_t req_pid, int file_offset, iNode_entry* inode, uint8_t* buf, int len);
int read_tty(pid_t req_pid, int dev_id, uint8_t *buf, int len);

void fm_write(Msg *m);
int write_data(int req_pid, int file_offset, iNode_entry *inode, uint8_t *buf, int len);
int write_tty(pid_t req_pid, int dev_id, uint8_t *buf, int len);
void release_block(iNode_entry* inode, int file_offset);

void fm_open(Msg *m);
vnode_entry* read_inode(const dir_entry *dir, char* path);
int read_dir_entry(dir_entry *dir, dir_entry *next_dir, char* name);

void fm_create(Msg *m);
int _create(PCB *pcb, char *path, vnode_entry *vnode);

void fm_close(Msg *m);
void fm_lseek(Msg *m);
void fm_dup(Msg *m);
void fm_dup2(Msg *m);

void fm_delete_file(Msg *m);
void _delete_file(PCB *pcb, dir_entry *dir, char *path, bool is_file);
void remove_inode_from_dir(inode_t parent_index, char *filename);
void _remove_inode_from_dir(iNode_entry *parent_inode, int position);
void _delete_file_by_inode(inode_t index, iNode_entry *inode);

void fm_mkdir(Msg *m);
int _create_dir(PCB *pcb, dir_entry *dir, char *path, dir_entry *new_dir);
void add_inode_into_dir(inode_t parent_index, const char *name, inode_t index);

void fm_rmdir(Msg *m);
void _delete_dir_by_inode(inode_t index, iNode_entry *inode);

void fm_lsdir(Msg *m);
void _lsdir(dir_entry *dir, PCB *pcb, char* buf);

void fm_chdir(Msg *m);

void fm_exit_file(Msg *m);

void init_fd_table(Msg *m);

void copy_fd_table(Msg *m);

void init_file_format() {
   root_dir->index = get_free_disk_inode();
   iNode_entry inode;
   inode.size = 0;
   inode.type = DIR;
   inode.link = 1;
   write_inode_to_disk(root_dir->index, &inode);
   root_dir->index = 0;
   strcpy(root_dir->filename, "/");

   char *s1 = ".";
   char *s2 = "..";
   add_inode_into_dir(0, s1, 0);
   add_inode_into_dir(0, s2, 0);

   stdin_vnode = get_free_vnode();
   stdout_vnode = get_free_vnode();
   stderr_vnode = get_free_vnode();
   stdin_vnode->count = 1;
   stdin_vnode->inode.dev_id = 1;
   stdout_vnode->count = 1;
   stdout_vnode->inode.dev_id = 1;
   stderr_vnode->count = 1;
   stderr_vnode->inode.dev_id = 1;
}

void init_file(void) {
   init_vnode_array();
   init_file_table();
   PCB *p = create_kthread(file_thread);
   FILE = p->pid;
   wakeup(p);
}

void file_thread(void) {
   read_bitmap_from_disk();
   init_file_format();
   Msg m;
   while(true) {
       receive(ANY, &m);
       switch(m.type) {
          case OPEN: fm_open(&m);
             break;
          case READ: fm_read(&m);
             break;
          case WRITE: fm_write(&m);
             break;
          case CREATE: fm_create(&m);
             break;
          case CLOSE: fm_close(&m);
             break;
          case DELETE: fm_delete_file(&m);
             break;
          case LSEEK: fm_lseek(&m);
             break;
          case DUP: fm_dup(&m);
             break;
          case DUP2: fm_dup2(&m);
             break;
          case MKDIR: fm_mkdir(&m);
             break;
          case RMDIR: fm_rmdir(&m);
             break;
          case LSDIR: fm_lsdir(&m);
             break;
          case CHDIR: fm_chdir(&m);
             break;
          case EXIT_FILE: fm_exit_file(&m);
             break;
          case INIT_FD_TABLE: init_fd_table(&m);
             break;
          case COPY_FD_TABLE: copy_fd_table(&m);
             break;
          default: assert(0);
       }
   }
}

uint8_t* get_free_block_buffer() {
   int i = 0;
   for(; i < 20; i++) {
      if(!block_buf_flag[i]) {
         block_buf_flag[i] = true;
         return block_buf_array[i];
      }
   }
   return NULL;
}

void free_block_buffer(uint8_t* addr) {
   int index = (int)(addr - block_buf_array[0]) / BLOCK_SIZE;
   block_buf_flag[index] = false;
}

void fm_read(Msg *m) {     //user systerm call
   pid_t req_pid = m->i[0];
   int fd = m->i[1];
   uint8_t *buf = (uint8_t *)m->i[2];
   int len = m->i[3];

   PCB* pcb = fetch_pcb(m->src);
   Open_file_entry *file_entry = pcb->fd_table[fd];
   if(!file_entry) {
      m->dest = pcb->pid;
      m->src = FILE;
      m->ret = -1;
      send(pcb->pid, m);
      return;
   }
   int file_offset = file_entry->offset;
   iNode_entry* inode = &(file_entry->vnode->inode);
   int readed_len;
   if(inode->dev_id == 0) {
      readed_len = read_data(req_pid, file_offset, inode, buf, len);
      file_entry->offset += readed_len;
   } else {
      readed_len = read_tty(req_pid, inode->dev_id, buf, len);
   }

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = readed_len;
   send(pcb->pid, m);
}

int read_tty(pid_t req_pid, int dev_id, uint8_t *buf, int len) {
   return dev_read(ttynames[dev_id-1], req_pid, buf, 0, len);
}

inline int cur_read_len(int len, int file_offset, int size) {
   int block_rest = BLOCK_SIZE - file_offset % BLOCK_SIZE;
   int rest_size = size - file_offset;
   int cur_len = block_rest < len ? block_rest : len;
   cur_len = cur_len < rest_size ? cur_len : rest_size;
   return cur_len;
}

int read_data(pid_t req_pid, int file_offset, iNode_entry* inode, uint8_t* buf, int len) {     
   int readed_len = 0;
   if(file_offset < level_end0 && len > 0) {
      int min_end = inode->size < level_end0 ? inode->size : level_end0;
      while(file_offset < min_end && len > 0) {
         block_t ramdisk_offset = inode->index[file_offset/BLOCK_SIZE];
         ramdisk_offset += file_offset % BLOCK_SIZE;
         int cur_len = cur_read_len(len, file_offset, inode->size);
         dev_read("ram", req_pid, buf + readed_len, ramdisk_offset, cur_len);
         file_offset += cur_len;
         readed_len += cur_len;
         len -= cur_len;
      }
   }
   if(file_offset >= inode->size) {
      return readed_len;
   }
   if(file_offset < level_end1 && len > 0) {
      block_t index_offset1 = inode->index[12];
      dev_read("ram", current->pid, (void *)index_block1, index_offset1, BLOCK_SIZE);
      int min_end = inode->size < level_end1 ? inode->size : level_end1;
      int i = (file_offset - level_end0) / index_zero_size;
      while(file_offset < min_end && len > 0) {
         block_t ramdisk_offset = *((uint32_t*)index_block1 + i);
         ramdisk_offset += file_offset % BLOCK_SIZE;
         int cur_len = cur_read_len(len, file_offset, inode->size);
         dev_read("ram", req_pid, buf + readed_len, ramdisk_offset, cur_len);
         file_offset += cur_len;
         readed_len += cur_len;
         len -= cur_len;
         i++;
      }
   } 
   if(file_offset >= inode->size) {
      return readed_len;
   }
   if(file_offset < level_end2 && len > 0) {
      block_t index_offset1 = inode->index[13];
      dev_read("ram", current->pid, (void *)index_block1, index_offset1, BLOCK_SIZE);
      int min_end1 = inode->size < level_end2 ? inode->size : level_end2;
      int i = (file_offset - level_end1) / index_one_size;
      while(file_offset < min_end1 && len > 0) {
         block_t index_offset2 = *((uint32_t*)index_block1 + i);
         dev_read("ram", current->pid, (void *)index_block2, index_offset2, BLOCK_SIZE);
         int temp_end = level_end1 + (i+1) * index_one_size;
         int min_end2 = inode->size < temp_end ? inode->size : temp_end;
         temp_end -= index_one_size;
         int j = (file_offset - temp_end) / index_zero_size;
         while(file_offset < min_end2 && len > 0) {
            block_t ramdisk_offset = *((uint32_t*)index_block2 + j);
            ramdisk_offset += file_offset % BLOCK_SIZE;
            int cur_len = cur_read_len(len, file_offset, inode->size);
            dev_read("ram", req_pid, buf + readed_len, ramdisk_offset, cur_len);
            file_offset += cur_len;
            readed_len += cur_len;
            len -= cur_len;
            j++;
         }
         i++;
      }
   }
   if(file_offset > inode->size) {
      return readed_len;
   }
   if(len > 0){
      block_t index_offset1 = inode->index[14];
      dev_read("ram", current->pid, (void *)index_block1, index_offset1, BLOCK_SIZE);
      int min_end1 = inode->size < level_end3 ? inode->size : level_end3;
      int i = (file_offset - level_end2)/index_two_size;
      while(file_offset < min_end1 && len > 0) {
         block_t index_offset2 = *((uint32_t*)index_block1 + i);
         dev_read("ram", current->pid, (void *)index_block2, index_offset2, BLOCK_SIZE);
         int temp_end = level_end2 + (i+1) * index_two_size;   
         int min_end2 = inode->size < temp_end ? inode->size : temp_end;
         temp_end -= index_two_size;
         int j = (file_offset - temp_end)/((BLOCK_SIZE/4) * BLOCK_SIZE);
         while(file_offset < min_end2 && len > 0) {
            block_t index_offset3 = *((uint32_t*)index_block1 + j);
            dev_read("ram", current->pid, (void *)index_block3, index_offset3, BLOCK_SIZE);

            int temp_end1 = level_end2 + i * index_two_size + (j + 1) * index_one_size;
            int min_end3 = inode->size < temp_end1 ? inode->size : temp_end1;
            temp_end1 -= index_one_size;
            int k = (file_offset - temp_end1) / index_zero_size;
            while(file_offset < min_end3 && len > 0) {
               block_t ramdisk_offset = *((uint32_t*)index_block3 + k);
               ramdisk_offset += file_offset % BLOCK_SIZE;
               int cur_len = cur_read_len(len, file_offset, inode->size);
               dev_read("ram", req_pid, buf + readed_len, ramdisk_offset, cur_len);
               file_offset += cur_len;
               readed_len += cur_len;
               len -= cur_len;
               k++;
            }
            j++;
         }
         i++;
      }
   } 
   return readed_len;
}

void fm_write(Msg *m) {
   pid_t req_pid = m->i[0];
   int fd = m->i[1];
   uint8_t *buf = (uint8_t*)m->i[2];
   int len = m->i[3];
   PCB* pcb = fetch_pcb(m->src);
   Open_file_entry *file_entry = pcb->fd_table[fd];
   if(!file_entry) {
      m->dest = pcb->pid;
      m->src = FILE;
      m->ret = -1;
      send(pcb->pid, m);
      return ;
   }
/*   if(file_entry->flag & O_APPEND) {
      file_entry->offset = file_entry->inode->size;
   }*/
   int file_offset = file_entry->offset;
   iNode_entry* inode = &(file_entry->vnode->inode);
   if(file_offset < inode->size) {         //we need trunc file
      release_block(inode, file_offset);
      inode->size = file_offset;
   }
   int writed_len;
   if(inode->dev_id == 0) {
      writed_len = write_data(req_pid, file_offset, inode, buf, len);
      file_entry->offset = inode->size;
   } else {
      writed_len = write_tty(req_pid, inode->dev_id, buf, len);
   }

   m->dest = pcb->pid;
   m->src = current->pid;
   m->ret = writed_len;
   send(pcb->pid, m);
}

int write_tty(pid_t req_pid, int dev_id, uint8_t *buf, int len) {
   return dev_write(ttynames[dev_id-1], req_pid, buf, 0, len);
}

int write_data(int req_pid, int file_offset, iNode_entry *inode, uint8_t *buf, int len) {
   int writed_len = 0;
   if(file_offset < level_end0) {
      while(file_offset < level_end0 && len > 0) { 
         if (file_offset % BLOCK_SIZE == 0) {
            inode->index[file_offset/BLOCK_SIZE] = get_free_block();
         }
         block_t ramdisk_offset = inode->index[file_offset/BLOCK_SIZE];
         ramdisk_offset += file_offset % BLOCK_SIZE;
         int cur_len = BLOCK_SIZE - file_offset % BLOCK_SIZE;
         cur_len = cur_len < len ? cur_len : len;
         dev_write("ram", req_pid, buf + writed_len, ramdisk_offset, cur_len);
         file_offset += cur_len;
         writed_len += cur_len;
         len -= cur_len;
         inode->size += cur_len;
      }
   } 
   if(file_offset < level_end1 && len > 0) {
      block_t index_offset1;
      if(file_offset == level_end0) {
         inode->index[12] = get_free_block();
         index_offset1 = inode->index[12];
      } else {
         index_offset1 = inode->index[12];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      }
      int i = (file_offset - level_end0) / index_zero_size;
      bool flag = false;
      while(file_offset < level_end1 && len > 0) {
         block_t ramdisk_offset;
         if(file_offset % BLOCK_SIZE == 0) {
            ramdisk_offset = get_free_block();
            *((uint32_t*)index_block1 + i) = ramdisk_offset;
            flag = true;
         } else {
            ramdisk_offset = *((uint32_t*)index_block1 + i);
            ramdisk_offset += file_offset % BLOCK_SIZE;
         }
         int cur_len = BLOCK_SIZE - file_offset % BLOCK_SIZE;
         cur_len = cur_len < len ? cur_len : len;
         dev_write("ram", req_pid, buf + writed_len, ramdisk_offset, cur_len);
         file_offset += cur_len;
         writed_len += cur_len;
         len -= cur_len;
         inode->size += cur_len;
         i++;
      }
      if(flag) {
         dev_write("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      }
   }
   if(file_offset < level_end2 && len > 0) {
      block_t index_offset1;
      if(file_offset == level_end1) {
         inode->index[13] = get_free_block();
         index_offset1 = inode->index[13];
      } else {
         index_offset1 = inode->index[13];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      }
      int i = (file_offset - level_end1) / index_one_size;
      bool flag1 = false;
      while(file_offset < level_end2 && len > 0) {
         block_t index_offset2;
         int min_end = level_end1 + (i+1) * index_one_size;
         int pre_end = min_end - index_one_size;
         if(file_offset == pre_end) {
            index_offset2 = get_free_block();
            *((uint32_t*)index_block1 + i) = index_offset2;
            flag1 = true;
         } else {
            index_offset2 = *((uint32_t*)index_block1 + i);
            dev_read("ram", current->pid, index_block2, index_offset2, BLOCK_SIZE);
         }
         int j = (file_offset - pre_end) / BLOCK_SIZE;
         bool flag2 = false;
         while(file_offset < min_end && len > 0) {
            block_t ramdisk_offset;
            if(file_offset % BLOCK_SIZE == 0) {
               ramdisk_offset = get_free_block();
               *((uint32_t*)index_block2 + j) = ramdisk_offset;
               flag2 = true;
            } else {
               ramdisk_offset = *((uint32_t*)index_block2 + j);
               ramdisk_offset += file_offset % BLOCK_SIZE;
            }
            int cur_len = BLOCK_SIZE - file_offset % BLOCK_SIZE;
            cur_len = cur_len < len ? cur_len : len;
            dev_write("ram", req_pid, buf + writed_len, ramdisk_offset, cur_len);
            file_offset += cur_len;
            writed_len += cur_len;
            len -= cur_len;
            inode->size += cur_len;
            j++;
         }
         if(flag2) {
            dev_write("ram", current->pid, index_block2, index_offset2, BLOCK_SIZE);
         }
         i++;
      }
      if(flag1) {
         dev_write("ram", current->pid, index_block1, index_offset1, BLOCK_SIZE);
      }
   }
   if(file_offset < level_end3 && len > 0) {
      block_t index_offset1;
      if(file_offset == level_end2) {
         index_offset1 = get_free_block();
         inode->index[14] = index_offset1;
      } else {
         index_offset1 = inode->index[13];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      }
      int i = (file_offset - level_end2) / index_two_size;
      bool flag1 = false;
      while(file_offset < level_end3 && len > 0) {
         block_t index_offset2;
         int min_end1 = level_end2 + (i+1) * index_two_size;
         int pre_end1 = min_end1 - index_two_size;
         if(file_offset == pre_end1) {
            index_offset2 = get_free_block();
            *((uint32_t*)index_block1 + i) = index_offset2;
            flag1 = true;
         } else {
            index_offset2 = *((uint32_t*)index_block1 + i);
            dev_read("ram", current->pid, (void*)index_block2, index_offset2, BLOCK_SIZE);
         }
         int j = (file_offset - pre_end1) / index_one_size;
         bool flag2 = false;
         while(file_offset < min_end1 && len > 0) {
            block_t index_offset3;
            int min_end2 = level_end2 + i * index_two_size + (j+1) * index_one_size;
            int pre_end2 = min_end2 - index_one_size;
            if(file_offset == pre_end2) {
               index_offset3 = get_free_block();
               *((uint32_t*)index_block2 + j) = index_offset3;
               flag2 = true;
            } else {
               index_offset3 = *((uint32_t*)index_block2 + j);
               dev_read("ram", current->pid, index_block3, index_offset3, BLOCK_SIZE);
            }
            int k = (file_offset - pre_end2) / BLOCK_SIZE;
            bool flag3 = false;
            while(file_offset < min_end2 && len > 0) {
               block_t ramdisk_offset;
               if(file_offset % BLOCK_SIZE == 0) {
                  ramdisk_offset = get_free_block();
                  *((uint32_t*)index_block3 + k) = ramdisk_offset;
                  flag3 = true;
               } else {
                  ramdisk_offset = *((uint32_t*)index_block3 + k);
                  ramdisk_offset += file_offset % BLOCK_SIZE;
               }
               int cur_len = BLOCK_SIZE - file_offset % BLOCK_SIZE;
               cur_len = cur_len < len ? cur_len : len;
               dev_write("ram", req_pid, buf + writed_len, ramdisk_offset, cur_len);
               file_offset += cur_len;
               writed_len += cur_len;
               len -= cur_len;
               inode->size += cur_len;
               k++;
            }
            if(flag3) {
               dev_write("ram", current->pid, (void*)index_block3, index_offset3, BLOCK_SIZE);
            }
            j++;
         }
         if(flag2) {
            dev_write("ram", current->pid, (void*)index_block2, index_offset2, BLOCK_SIZE);
         }
         i++;
      }
      if(flag1) {
         dev_write("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      }
   }
   return writed_len;
}

void release_block(iNode_entry* inode, int file_offset) {
   if(file_offset < inode->size && file_offset < level_end0) {
      file_offset = level_end0;
      int i = file_offset / BLOCK_SIZE;
      if(file_offset % BLOCK_SIZE == 0) {
         set_block_idle(inode->index[i]);
      }
      file_offset = file_offset - file_offset % BLOCK_SIZE + BLOCK_SIZE;
   } 
   if(file_offset < inode->size && file_offset < level_end1) {
      block_t index_offset1 = inode->index[12];
      dev_read("ram", current->pid, (void *)index_block1, index_offset1, BLOCK_SIZE);
      int min_end = inode->size < level_end1 ? inode->size : level_end1;
      int i = (file_offset - level_end0) / BLOCK_SIZE;
      if(file_offset == level_end0) {
         set_block_idle(index_offset1);
      }
      while(file_offset < min_end) {
         block_t ramdisk_offset = *((uint32_t*)index_block1 + i);
         if (file_offset % BLOCK_SIZE == 0) {
            set_block_idle(ramdisk_offset);
         } else {
            file_offset -= file_offset % BLOCK_SIZE;
         }
         file_offset += BLOCK_SIZE;
         i++;
      }
   }  
   if(file_offset < inode->size && file_offset < level_end2) {
      block_t index_offset1 = inode->index[13];
      dev_read("ram", current->pid, (void *)index_block1, index_offset1, BLOCK_SIZE);
      int min_end1 = inode->size < level_end2 ? inode->size : level_end2;
      if(file_offset == level_end1) {
         set_block_idle(index_offset1);
      }
      int i = (file_offset - level_end1) / index_one_size;
      while(file_offset < min_end1) {
         block_t index_offset2 = *((uint32_t*)index_block1 + i);
         dev_read("ram", current->pid, (void *)index_block2, index_offset2, BLOCK_SIZE);
         int temp_end = level_end1 + (i+1) * index_one_size;
         int min_end2 = inode->size < temp_end ? inode->size : temp_end;
         temp_end -= index_one_size;
         if(file_offset == temp_end) {
            set_block_idle(index_offset2);
         }
         int j = (file_offset - temp_end) / BLOCK_SIZE;
         while(file_offset < min_end2) {
            block_t ramdisk_offset = *((uint32_t*)index_block2 + j);
            if (file_offset % BLOCK_SIZE == 0) {
               set_block_idle(ramdisk_offset);
            } else {
               file_offset -= file_offset % BLOCK_SIZE;
            }
            file_offset += BLOCK_SIZE;
            j++;
         }
         i++;
      }
   }
   if(file_offset < inode->size) {
      block_t index_offset1 = inode->index[14];
      dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
      if(file_offset == level_end2) {
         set_block_idle(index_offset1);
      }
      int i = (file_offset - level_end2) / index_two_size;
      while(file_offset < inode->size) {
         block_t index_offset2 = *((uint32_t*)index_block1 + i);
         dev_read("ram", current->pid, (void *)index_block2, index_offset2, BLOCK_SIZE);
         int temp_end1 = level_end2 + (i+1) * index_two_size;
         int min_end1 = inode->size < temp_end1 ? inode->size : temp_end1;
         temp_end1 -= index_two_size;
         if(file_offset == temp_end1) {
            set_block_idle(index_offset2);
         }
         int j = (file_offset - temp_end1)/index_one_size;
         while(file_offset < min_end1) {
            block_t index_offset3 = *((uint32_t*)index_block2 + j);
            dev_read("ram", current->pid, (void*)index_block3, index_offset3, BLOCK_SIZE);
            int temp_end2 = level_end2 + i * index_two_size + (j + 1) * index_one_size;
            int min_end2 = inode->size < temp_end2 ? inode->size : temp_end2;
            temp_end2 -= index_one_size;
            if(file_offset == temp_end2) {
               set_block_idle(index_offset3);
            }
            int k =  (file_offset - temp_end1) / BLOCK_SIZE;
            while(file_offset < min_end2) {
               block_t ramdisk_offset = *((uint32_t*)index_block3 + k);
               if(file_offset % BLOCK_SIZE == 0) {
                  set_block_idle(ramdisk_offset);
               } else {
                  file_offset -= file_offset % BLOCK_SIZE;
               }
               file_offset += BLOCK_SIZE;
               k++;
            }
            j++;
         }
         i++;
      }
   }
}

void fm_open(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   char buf[256];
   copy_to_kernel(pcb, buf, (void*)m->i[0], 256);
   printk("%s, fm_open\n", buf);
   char* path = buf;
   int flag = m->i[1];    
   //int mode = m->i[2];
   dir_entry *dir;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   vnode_entry *vnode;
   vnode = read_inode(dir, path);
   if(vnode == NULL ) {    
      if(flag != 1) {     //flag == 1 means we can create file 
         m->dest = pcb->pid;
         m->src = FILE;
         m->ret = -1;       //mean success
         send(pcb->pid, m);
         return;
      }
      copy_to_kernel(pcb, path, (void*)m->i[0], 256);
      vnode = get_free_vnode();
      _create(pcb, path, vnode);
   }
   Open_file_entry *file_entry = get_free_file_entry();
   file_entry->vnode = vnode;
   file_entry->offset = 0;       //may need modify
   file_entry->count = 1;
   //file_entry->flag = flag;

   while(pcb->fd_table[pcb->min_fd] != NULL) {
      pcb->min_fd = (pcb->min_fd + 1) % 100;
   }
   int fd = pcb->min_fd;
   pcb->fd_table[pcb->min_fd] = file_entry;
   pcb->min_fd = (pcb->min_fd + 1) % 100;

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = fd;       //mean success
   send(pcb->pid, m);
}

vnode_entry* read_inode(const dir_entry *dir, char* path) {
   if(path[0] == '\0') {
      //open_error();
      return NULL;
   }
   bool flag = false;
   dir_entry cur_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   while(1) {
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      char* p = path + i + 1;
      dir_entry next_dir;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
         return NULL;
      }
      path = p;
      if(flag) {
         inode_t index = next_dir.index;
         vnode_entry* vnode = search_vnode_by_index(index);
         if(vnode == NULL) {
            vnode = get_free_vnode();
            vnode->index = index;
            read_inode_from_disk(index, &(vnode->inode));
         } else {
            vnode->count++;
         }
         return vnode;
      } else {
         cur_dir.index = next_dir.index;
         strcpy(cur_dir.filename, next_dir.filename);
      }
   }
   return NULL;
}

int read_dir_entry(dir_entry *dir, dir_entry *next_dir, char* name) {
   int len = (BLOCK_SIZE / sizeof(dir_entry)) * sizeof(dir_entry); 
   int offset = 0;  
   iNode_entry dir_inode;
   read_inode_from_disk(dir->index, &dir_inode);
   int readed_len;
   uint8_t *block_buf = get_free_block_buffer();
   while((readed_len = read_data(current->pid, offset, &dir_inode, block_buf, len)) > 0) {
      offset += readed_len;
      uint8_t *p = block_buf;
      while(readed_len > 0) {
         char *filename = ((dir_entry*)p)->filename;
         if(strcmp(filename, name) == 0) {
            next_dir->index = ((dir_entry*)p)->index;
            strcpy(next_dir->filename, filename);
            free_block_buffer(block_buf);
            return 0;
         }
         p += sizeof(dir_entry);
         readed_len -= sizeof(dir_entry);
      }
   }
   free_block_buffer(block_buf);
   return -1;
}

void fm_create(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   char path[256];
   copy_to_kernel(pcb, path, (char*)m->i[0], 256);
   //int mode = m->i[1];
   _create(pcb, path, NULL);
   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);
}

int _create(PCB *pcb, char *path, vnode_entry *vnode) {
   dir_entry *dir;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   dir_entry cur_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   bool flag = false;
   while(1) {
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      char* p = path + i + 1;
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      dir_entry next_dir;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
          if(flag == false) {     //no directory
            *(p-1) = '/';
            p = path;
            char *q = NULL;
            while(*p != '\0') {
               if(*p == '/') {     //q record filename's first one.
                  q = p + 1;
               }
               p++;
            }
            *(q-1) = '\0';   //we need delete filename, and only record dir name
            _create_dir(pcb, &cur_dir, path, &cur_dir);
            path = q;          //filename
         }
         inode_t index = get_free_disk_inode();
         iNode_entry inode;
         inode.size = 0;
         inode.type = PLAIN;
         inode.link = 1;
         inode.dev_id = 0;
         write_inode_to_disk(index, &inode);
         add_inode_into_dir(cur_dir.index, path, index);
         if(vnode) {
            vnode->index = index;
            vnode->inode = inode;                 //important
            vnode->count = 1;
         }
         return 0;
      }
      if(flag == true) {         //file already existed.
         return -1;
      }
      cur_dir.index = next_dir.index;
      strcpy(cur_dir.filename, next_dir.filename);
      path = p;
   }
   return -1;
}

void fm_mkdir(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   char buf[256];
   copy_to_kernel(pcb, buf, (char*)m->i[0], 256);
   char* path = buf;
   dir_entry *dir;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   _create_dir(pcb, dir, path, NULL);
   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);
}

int _create_dir(PCB *pcb, dir_entry *dir, char *path, dir_entry *new_dir) {
   dir_entry cur_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   bool flag = false;
   while(1) {
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      char* p = path + i + 1;
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      dir_entry next_dir;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
         inode_t index = get_free_disk_inode();
         iNode_entry inode;
         inode.size = 0;
         inode.type = DIR;
         inode.link = 1;
         write_inode_to_disk(index, &inode);

         add_inode_into_dir(cur_dir.index, path, index);

         char *s1 = ".";
         char *s2 = "..";
         add_inode_into_dir(index, s1, index);
         add_inode_into_dir(index, s2, cur_dir.index);

         strcpy(next_dir.filename, path);
         next_dir.index = index;
      }
      cur_dir.index = next_dir.index;
      strcpy(cur_dir.filename, next_dir.filename);
      if(flag) {
         if(new_dir) {
            new_dir->index = cur_dir.index;
            strcpy(new_dir->filename, cur_dir.filename);
         }
         return 0;
      }
      path = p;
   }
}

void add_inode_into_dir(inode_t parent_index, const char *name, inode_t index) {
   iNode_entry parent_inode;
   read_inode_from_disk(parent_index, &parent_inode);
   dir_entry new_entry;
   new_entry.index = index;
   strcpy(new_entry.filename, name);
   write_data(current->pid, parent_inode.size, &parent_inode, (uint8_t*)(&new_entry), sizeof(new_entry));
   write_inode_to_disk(parent_index, &parent_inode);
}

void fm_close(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   int fd = m->i[0];
   Open_file_entry* file_entry = pcb->fd_table[fd];
   release_file_entry(file_entry);
   pcb->fd_table[fd] = NULL;
   if(pcb->min_fd > fd) {
      pcb->min_fd = fd;
   }
   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);
}

void fm_lseek(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   int fd = m->i[0];
   off_t offset = m->i[1];
   int whence = m->i[2];
   if(pcb->fd_table[fd] == NULL) {
      return;
   }
   Open_file_entry *file_entry = pcb->fd_table[fd];
   if(whence == 0) {    //SEEK_SET
      file_entry->offset = offset;
   } else if(whence == 1) { //SEEK_CUR
      file_entry->offset += offset;
   } else {
      file_entry->offset = file_entry->vnode->inode.size + offset;
   }

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);
}

void fm_dup(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   int fd = m->i[0];
   Open_file_entry *file_entry = pcb->fd_table[fd];
   if(!file_entry) {
      return;
   }
   int new_fd = pcb->min_fd;
   pcb->fd_table[new_fd] = file_entry;
   file_entry->count++;
   while(pcb->fd_table[pcb->min_fd] != NULL)   {
      pcb->min_fd = (pcb->min_fd + 1) % 100;
   }
   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = new_fd;
   send(pcb->pid, m);
}

void fm_dup2(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   int fd = m->i[0];
   int new_fd = m->i[1];
   if(pcb->fd_table[new_fd] != NULL) {
      Open_file_entry* file_entry = pcb->fd_table[fd];
      release_file_entry(file_entry);
      pcb->fd_table[fd] = NULL;
   }
   pcb->fd_table[new_fd] = pcb->fd_table[fd];
   pcb->fd_table[fd]->count++;

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);
}

void fm_delete_file(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   char buf[256];
   char *path = buf;
   copy_to_kernel(pcb, buf, (char*)m->i[0], 256);
   dir_entry *dir;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   _delete_file(pcb, dir, path, true);

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);   
}

void fm_rmdir(Msg *m) {
   PCB* pcb = fetch_pcb(m->src);
   char buf[256];
   char *path = buf;
   copy_to_kernel(pcb, path, (char*)m->i[0], 256);
   dir_entry *dir;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   _delete_file(pcb, dir, path, false);

   m->dest = pcb->pid;
   m->src = FILE;
   m->ret = 0;
   send(pcb->pid, m);   
}

void _delete_file(PCB *pcb, dir_entry *dir, char *path, bool is_file) {
   dir_entry cur_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   bool flag = false;
   while(1) {
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      char* p = path + i + 1;
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      dir_entry next_dir;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
         return;       //no such file or directory
      }
      if(flag == true) {
         inode_t index = next_dir.index;
         iNode_entry inode; 
         read_inode_from_disk(index, &inode);
         if(is_file) {
            if(inode.type == DIR) {    //we want delete file, but the path is a dir
               return;
            }
         } else {
            if(inode.type == PLAIN) {
               return;
            }
         }
         _delete_dir_by_inode(index, &inode); //maybe dir or file

         remove_inode_from_dir(cur_dir.index, next_dir.filename);
      }
      cur_dir.index = next_dir.index;
      strcpy(cur_dir.filename, next_dir.filename);
      path = p;
   }
}

void _delete_dir_by_inode(inode_t index, iNode_entry *inode) {
   if(inode->type == PLAIN) {
      _delete_file_by_inode(index, inode);
      return;
   }
   uint8_t *buf = get_free_block_buffer();
   int len = (BLOCK_SIZE / sizeof(dir_entry)) * sizeof(dir_entry); 
   int offset = 0;
   int readed_len;
   while((readed_len = read_data(current->pid, offset, inode, buf, len)) > 0) {
      uint8_t *p = buf;
      offset += readed_len;
      while(readed_len > 0) {
         char *filename = ((dir_entry*)p)->filename;
         if(strcmp(filename, ".") && strcmp(filename, "..")) {  // shouldn't delete "." and ".."
            int cur_index = ((dir_entry*)p)->index;
            iNode_entry cur_inode;
            read_inode_from_disk(cur_index, &cur_inode);
            _delete_dir_by_inode(cur_index, &cur_inode);       //recursive delete
         }
         p += sizeof(dir_entry);
         readed_len -= sizeof(dir_entry);
      }
   }
   free_block_buffer(buf);
   _delete_file_by_inode(index, inode);
}

void _delete_file_by_inode(inode_t index, iNode_entry *inode) {
   release_block(inode, 0);   //return block
   release_disk_inode(index);
}

void remove_inode_from_dir(inode_t parent_index, char *filename) {
   int len = (BLOCK_SIZE / sizeof(dir_entry)) * sizeof(dir_entry); 
   int offset = 0;  
   iNode_entry dir_inode;
   read_inode_from_disk(parent_index, &dir_inode);
   int readed_len;
   int position = 0;
   uint8_t *block_buf = get_free_block_buffer();
   while((readed_len = read_data(current->pid, offset, &dir_inode, block_buf, len)) > 0) {
      uint8_t *p = block_buf;
      while(readed_len > 0) {
         char *name = ((dir_entry*)p)->filename;
         if(strcmp(filename, name) == 0) {
            _remove_inode_from_dir(&dir_inode, position);
            dir_inode.size -= sizeof(dir_entry);
            write_inode_to_disk(parent_index, &dir_inode);
            free_block_buffer(block_buf);
            return;
         }
         p += sizeof(dir_entry);
         readed_len -= sizeof(dir_entry);
         position ++;
      }
      offset += readed_len;
   }
   free_block_buffer(block_buf);
}

void _remove_inode_from_dir(iNode_entry *parent_inode, int position) {
   dir_entry last_dir;
   int dir_entry_size = sizeof(dir_entry);
   read_data(current->pid, parent_inode->size - dir_entry_size, parent_inode, (uint8_t*)&last_dir, dir_entry_size);
   release_block(parent_inode, parent_inode->size - sizeof(dir_entry));

   int offset = position * dir_entry_size;
   int len = dir_entry_size;
   uint8_t* p = (uint8_t*)(&last_dir);
   while(len > 0) {
      if(offset < level_end0) {
         block_t ramdisk_offset = parent_inode->index[offset/BLOCK_SIZE];
         ramdisk_offset += offset % BLOCK_SIZE;
         int cur_len = BLOCK_SIZE - offset % BLOCK_SIZE;
         cur_len = cur_len < len ? cur_len : len;
         dev_write("ram", current->pid, p, ramdisk_offset, cur_len);
         len -= cur_len;
         p += cur_len;
         offset += cur_len;
      } else if(offset < level_end1) {
         block_t index_offset1 = parent_inode->index[12];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
         int i = (offset - level_end0) / BLOCK_SIZE;
         block_t ramdisk_offset = *((uint32_t*)index_block1 + i);
         ramdisk_offset += offset % BLOCK_SIZE;
         int cur_len = BLOCK_SIZE - offset % BLOCK_SIZE;
         cur_len = cur_len < len ?   cur_len : len;
         dev_write("ram", current->pid, p, ramdisk_offset, cur_len);
         len -= cur_len;
         p += cur_len;
         offset += cur_len;
      } else if(offset < level_end2) {
         block_t index_offset1 = parent_inode->index[13];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
         int i = (offset - level_end1) / index_one_size;
         block_t index_offset2 = *((uint32_t*)index_block1 + i);
         dev_read("ram", current->pid, (void*)index_block2, index_offset2, BLOCK_SIZE);
         int j = ((offset - level_end1) % index_one_size) / BLOCK_SIZE;
         block_t ramdisk_offset = *((uint32_t*)index_block2 + j);
         ramdisk_offset += offset % BLOCK_SIZE;
         int cur_len = BLOCK_SIZE - offset % BLOCK_SIZE;
         cur_len = cur_len < len ?   cur_len : len;
         dev_write("ram", current->pid, p, ramdisk_offset, cur_len);
         len -= cur_len;
         p += cur_len;
         offset += cur_len;
      } else {
         block_t index_offset1 = parent_inode->index[14];
         dev_read("ram", current->pid, (void*)index_block1, index_offset1, BLOCK_SIZE);
         int i = (offset - level_end2) / index_two_size;
         block_t index_offset2 = *((uint32_t*)index_block2 + i);
         dev_read("ram", current->pid, (void*)index_block2, index_offset2, BLOCK_SIZE);
         int j = ((offset - level_end2) % index_two_size) / index_one_size;
         block_t index_offset3 = *((uint32_t*)index_block2 + j);
         dev_read("ram", current->pid, (void*)index_block3, index_offset3, BLOCK_SIZE);
         int k = (((offset - level_end2) % index_two_size) % index_one_size) / BLOCK_SIZE;
         block_t ramdisk_offset = *((uint32_t*)index_block3 + k);
         ramdisk_offset += offset % BLOCK_SIZE;
         int cur_len = BLOCK_SIZE - offset % BLOCK_SIZE;
         cur_len = cur_len < len ?   cur_len : len;
         dev_write("ram", current->pid, p, ramdisk_offset, cur_len);
         len -= cur_len;
         p += cur_len;
         offset += cur_len;
      }
   }
}

void fm_lsdir(Msg *m) {
   PCB *pcb = fetch_pcb(m->src);
   char* dir_name = (char*)(m->i[0]);
   char* buf = (char*)(m->i[1]);
   dir_entry *dir;
   char path_buf[256];
   copy_to_kernel(pcb, path_buf, dir_name, 256);
   char* path = path_buf;
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   dir_entry cur_dir, next_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   bool flag = false;
   if(path[0] == '\0') {
      flag = true;
   }
   while(1) {
      if(flag == true) {
         _lsdir(&cur_dir, pcb, buf);
         m->dest = pcb->pid;
         m->src = FILE;
         m->ret = 0;
         send(pcb->pid, m);
         return;
      }
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      char* p = path + i + 1;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
         m->src = FILE;
         m->ret = -1;
         send(pcb->pid, m);
         return;
      }
      cur_dir.index = next_dir.index;
      strcpy(cur_dir.filename, next_dir.filename);
      path = p;
   }
}

void _lsdir(dir_entry *dir, PCB *pcb, char* buf) {
   int len = (BLOCK_SIZE / sizeof(dir_entry)) * sizeof(dir_entry); 
   int offset = 0;  
   iNode_entry dir_inode;
   read_inode_from_disk(dir->index, &dir_inode);
   int readed_len = 0;
   uint8_t *block_buf = get_free_block_buffer();
   int position = 0;
   while((readed_len = read_data(current->pid, offset, &dir_inode, block_buf, len)) > 0) {
      uint8_t* r = block_buf;
      offset += readed_len;
      while(readed_len > 0) {
         char filename[40];
         strcpy(filename, ((dir_entry*)r)->filename);
         int filename_len = strlen(filename);
         filename[filename_len] = ' ';
         filename[filename_len+1] = ' ';
         filename[filename_len+2] = ' ';
         filename[filename_len+3] = '\0';
         copy_from_kernel(pcb, buf + position, filename, filename_len + 4);
         position += filename_len+3;
         r += sizeof(dir_entry);
         readed_len -= sizeof(dir_entry);
      }
   }
/*   char c = '\0';
   copy_from_kernel(pcb, buf + position - 1, &c, 1);*/
   free_block_buffer(block_buf);
}

void fm_chdir(Msg *m) {
   PCB *pcb = fetch_pcb(m->src);
   char* buf = (char*)m->i[0];
   dir_entry *dir;
   char path_buf[256];
   copy_to_kernel(pcb, path_buf, buf, 256);
   char* path = path_buf;
   if(path[0] == '\0') {
      m->dest = pcb->pid;
      m->src = FILE;
      m->ret = 0;
      send(pcb->pid, m);
      return;
   }
   if(path[0] == '/') {
      dir = root_dir;
      path += 1;
   } else {
      dir = &(pcb->current_dir);
   }
   dir_entry cur_dir, next_dir;
   cur_dir.index = dir->index;
   strcpy(cur_dir.filename, dir->filename);
   bool flag = false;
   if(path[0] == '\0') {
      flag = true;
   }
   while(1) {
      if(flag == true) {
         iNode_entry inode;
         read_inode_from_disk(cur_dir.index, &inode);
         if(inode.type == PLAIN) {
            m->dest = pcb->pid;
            m->src = FILE;
            m->ret = -1;
            send(pcb->pid, m);
            return;            
         }
         pcb->current_dir.index = cur_dir.index;
         strcpy(pcb->current_dir.filename, cur_dir.filename);
         m->dest = pcb->pid;
         m->src = FILE;
         m->ret = 0;
         send(pcb->pid, m);
         return;
      }
      int i = 0;
      while(path[i] != '\0' && path[i] != '/') {
         i++;
      }
      if(path[i] == '/') {
         path[i] = '\0';
      } else {
         flag = true;
      }
      char* p = path + i + 1;
      if(read_dir_entry(&cur_dir, &next_dir, path) != 0) {
         m->src = FILE;
         m->ret = -1;
         send(pcb->pid, m);
         return;
      }
      cur_dir.index = next_dir.index;
      strcpy(cur_dir.filename, next_dir.filename);
      path = p;
   }

}

void fm_exit_file(Msg *m) {
   PCB *pcb = fetch_pcb(m->i[0]);
   int i;
   for(i = 0; i < 100; i++) {
      Open_file_entry* file_entry = pcb->fd_table[i];
      if(file_entry) {
         release_file_entry(file_entry);
      }
   }
   m->dest = m->src;
   m->src = FILE;
   m->ret = 0;
   send(m->dest, m);
}

void init_fd_table(Msg *m) {
   PCB *pcb = fetch_pcb(m->i[0]);
   pcb->fd_table[0] = get_free_file_entry();
   pcb->fd_table[1] = get_free_file_entry();
   pcb->fd_table[2] = get_free_file_entry();
   pcb->fd_table[0]->vnode = stdin_vnode;
   stdin_vnode->count++;
   pcb->fd_table[1]->vnode = stdout_vnode;
   stdout_vnode->count++;
   pcb->fd_table[2]->vnode = stderr_vnode;
   stderr_vnode->count++;

   m->dest = m->src;
   m->src = FILE;
   m->ret = 0;
   send(m->dest, m);
}

void copy_fd_table(Msg *m) {
   PCB *cpcb = fetch_pcb(m->i[0]);
   PCB *ppcb = fetch_pcb(m->i[1]);
   int i;
   for(i = 0; i < 100; i++) {
      if(ppcb->fd_table[i] != NULL) {
         ppcb->fd_table[i]->count++;
         cpcb->fd_table[i] = ppcb->fd_table[i];
      }
   }
   m->dest = m->src;
   m->src = FILE;
   m->ret = 0;
   send(m->dest, m);   
}
