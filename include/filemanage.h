#ifndef __FILE_MANAGE_H__
#define __FILE_MANAGE_H__

void init_file();
void do_read(int file_name, uint8_t *buf, off_t offset, size_t len);

#endif