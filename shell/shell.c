#include "../include/sys_call.h"

#define true 1
#define false 0
#define NULL 0

typedef int size_t;

char current_dir[256];

int parse(char* cmd, char* cmd_type);
void convert_to_full_path(const char *dir_name, char *real_dir);
void cd(char *dir_name);
void ls(char *dir_name);
void mkfile(char *filename);
void rmfile(char *path_name);
void _mkdir(char *dir_name);
void _rmdir(char *dir_name);
void echo(char *s);
void cat(char *filename);
void _exec(char *filename, char *args);
void put_to_shell_tty(char *buf);


void strcpy(char *d, const char *s);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char *str);

int main(){
	current_dir[0] = '/';
	current_dir[1] = '\0';
	puts("/$ ");	
	while(1) {
		char cmd[256];
		read_line(cmd);
		char cmd_type[256];
		int pos = parse(cmd, cmd_type);
		char* args;
		if(pos != -1) {
			args = cmd + pos;
		} else {
			args = NULL;
		}
		if(!strcmp(cmd_type, "cd")) {
			cd(args);
		} else if(!strcmp(cmd_type, "ls")) {
			ls(args);
		} else if(!strcmp(cmd_type, "mkfile")) {
			mkfile(args);
		} else if(!strcmp(cmd_type, "rmfile")) {
			rmfile(args);
		} else if(!strcmp(cmd_type, "mkdir")) {
			_mkdir(args);
		} else if(!strcmp(cmd_type, "rmdir")) {
			_rmdir(args);
		} else if(!strcmp(cmd_type, "echo")) {
			echo(args);
		} else if(!strcmp(cmd_type, "cat")) {
			cat(args);
		} else {
			_exec(cmd_type, cmd);
		}
	}
}

int parse(char* cmd, char* cmd_type) {
	int i = 0;
	for(; cmd[i] != '\0'; i++) {
		if(cmd[i] == ' ') {
			break;
		}
		cmd_type[i] = cmd[i];
	}
	cmd_type[i] = '\0';
	if(cmd[i] == '\0') {
		return -1;
	}
	return i+1;
}

void convert_to_full_path(const char *dir_name, char *real_dir) {
	if(!dir_name || dir_name[0] != '/') {
		strcpy(real_dir, current_dir);
	} else {
		real_dir[0] = '/';
		real_dir[1] = '\0';
		dir_name++;
	}
	if(!dir_name) {
		return;
	}
	int i = strlen(real_dir);
	int j = 0;
	char temp[256];
	int k = 0;
	while(true) {
		if(dir_name[j] == '/' || dir_name[j] == '\0') {
			temp[k] = '\0';
			if(!strcmp(temp, "..")) {    //equal to ".."
				while(i != 0 && real_dir[i] != '/') {
					i--;
				}
				real_dir[i] = '\0';
				if(i == 0) {
					real_dir[0] = '/';
					real_dir[1] = '\0';
				}
			} else if(strcmp(temp, ".")){   //equal to "."
				if(i != 1) {		//i == 1 means root dir
					real_dir[i] = '/';
					i++;
				}
				strcpy(real_dir + i, temp);
				i += k;
			}
			k = -1;
		} else {
			temp[k] = dir_name[j];
		}
		if(dir_name[j] == '\0' || (dir_name[j] == '/' && dir_name[j+1] == '\0')) {
			break;
		}
		j++;
		k++;
	}
}

void cd(char *dir_name) {
	char real_dir[256];
	convert_to_full_path(dir_name, real_dir);
	if(chdir(real_dir) == 0) {
		strcpy(current_dir, real_dir);
		put_to_shell_tty(NULL);
	} else {
		char *err_msg = "no such dir";
		put_to_shell_tty(err_msg);
	}

}

void ls(char *dir_name) {
	char real_dir[256];
	convert_to_full_path(dir_name, real_dir);
	char buf[4096];
	if(lsdir(real_dir, buf) == 0) {
		put_to_shell_tty(buf);
	} else {
		char *err_msg = "no such dir";
		put_to_shell_tty(err_msg);
	}
}

void mkfile(char *filename) {
	char real_file[256];
	convert_to_full_path(filename, real_file);
	if(create(real_file, 0) == 0) {
		put_to_shell_tty(NULL);
	} else {
		char *err_msg = "can't create file";
		put_to_shell_tty(err_msg);
	}
}

void rmfile(char *path_name) {
	char real_file[256];
	convert_to_full_path(path_name, real_file);
	if(delete_file(real_file) == 0) {
		put_to_shell_tty(NULL);
	} else {
		char *err_msg = "can't delete file";
		put_to_shell_tty(err_msg);
	}
}

void _mkdir(char *dir_name) {
	char real_dir[256];
	convert_to_full_path(dir_name, real_dir);
	if(mkdir(real_dir) == 0) {
		put_to_shell_tty(NULL);
	} else {
		char *err_msg = "can't create dir";
		put_to_shell_tty(err_msg);
	}
}

void _rmdir(char *dir_name) {
	char real_dir[256];
	convert_to_full_path(dir_name, real_dir);
	if(rmdir(real_dir) == 0) {
		put_to_shell_tty(NULL);
	} else {
		char *err_msg = "can't delete dir";
		put_to_shell_tty(err_msg);
	}
}


void echo(char *s) {
	put_to_shell_tty(s);
}

void cat(char *filename) {
	char real_file[256];
	convert_to_full_path(filename, real_file);
	char buf[256];
	int fd = open(real_file, 1, 0);
	int readed_len;
	while((readed_len = read(fd, (uint8_t*)buf, 256)) != 0) {
		//put_to_shell_tty(buf, false);
		puts(buf);
	}
	puts("\n");
	put_to_shell_tty(NULL);
}


void _exec(char *filename, char *args) {
	char real_file[256];
	convert_to_full_path(filename, real_file);
	pid_t pid;
	if((pid = fork()) == 0) {
		if(exec(real_file, args) == -1) {
			char *err_msg = "no such elf file!\n";
			puts(err_msg);
			exit(0);
		}
	} else {
		waitpid(pid);
	}
	put_to_shell_tty(NULL);
}



void put_to_shell_tty(char *buf) {
	if(buf) {
		puts(buf);
	}
	char stdout_dir[256];
	int i = 0;
	if(buf) {
		stdout_dir[0] = '\n';
		i++;
	}
	strcpy(stdout_dir+i,  current_dir);
	int len = strlen(current_dir) + i;
	stdout_dir[len] = '$';
	stdout_dir[len+1] = ' ';
	stdout_dir[len+2] = '\0';
	puts(stdout_dir);
}


size_t strlen(const char *str) {
	int len = 0;
	while (*str ++) len ++;
	return len;
}

void strcpy(char *d, const char *s) {
	while(*s != '\0') {
		*d = *s;
		d++, s++;
	}
	*d = '\0';
}

int strcmp(const char* s1, const char* s2) {
	int i = 0;
	while(s1[i] != '\0' && s2[i] != '\0') {
		if(s1[i] > s2[i]){
			return 1;
		} else if(s1[i] < s2[i]) {
			return -1;
		}
		i++;
	}
	if(s1[i] == '\0' && s2[i] == '\0') {
		return 0;
	} else if(s1[i] == '\0') {
		return -1;
	} else {
		return 1;
	}
}