#ifndef __KERN_FS_FILE_H__
#define __KERN_FS_FILE_H__

//#include <types.h>
#include <fs.h>
#include <proc.h>
#include <atomic.h>
#include <assert.h>

struct inode;
struct stat;
struct dirent;

struct file {
    enum {
        FD_NONE, FD_INIT, FD_OPENED, FD_CLOSED,
    } status;  //访问文件的执行状态
    bool readable;  // 文件是否可读
    bool writable;  // 文件是否可写
    int fd;         // 文件在filemap中的索引值
    off_t pos;      //访问文件的当前位置
    struct inode *node;  // 该文件对应的内存inode指针
    atomic_t open_count; // 打开此文件的次数
};

void filemap_init(struct file *filemap);
void filemap_open(struct file *file);
void filemap_close(struct file *file);
void filemap_dup(struct file *to, struct file *from);
bool file_testfd(int fd, bool readable, bool writable);

int file_open(char *path, uint32_t open_flags);
int file_close(int fd);
int file_read(int fd, void *base, size_t len, size_t *copied_store);
int file_write(int fd, void *base, size_t len, size_t *copied_store);
int file_seek(int fd, off_t pos, int whence);
int file_fstat(int fd, struct stat *stat);
int file_fsync(int fd);
int file_getdirentry(int fd, struct dirent *dirent);
int file_dup(int fd1, int fd2);
int file_pipe(int fd[]);
int file_mkfifo(const char *name, uint32_t open_flags);

static inline int
fopen_count(struct file *file) {
    return atomic_read(&(file->open_count));
}

static inline int
fopen_count_inc(struct file *file) {
    return atomic_add_return(&(file->open_count), 1);
}

static inline int
fopen_count_dec(struct file *file) {
    return atomic_sub_return(&(file->open_count), 1);
}

#endif /* !__KERN_FS_FILE_H__ */

